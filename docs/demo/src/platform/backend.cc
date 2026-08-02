#include "platform/backend.hh"

#include <algorithm>
#include <cmath>

#include "rlgl.h"

namespace demo {

using fltr::BorderRadius;
using fltr::DisplayList;
using fltr::Offset;
using fltr::PaintCmd;
using fltr::PaintOp;
using fltr::Rect;
using fltr::Scene;
using fltr::Transform2D;

namespace {

Rectangle toRectangle(Rect r) noexcept {
  return Rectangle{r.left, r.top, r.width(), r.height()};
}

/// The four corner radii actually drawable inside `bounds`.
///
/// fltr carries independent x and y radii per corner and this backend draws
/// circular arcs, so the x radius is the one that survives. Neighbouring corners
/// that would overlap are scaled down together, the way CSS does it, so an
/// oversized radius never turns an edge inside out.
struct Corners {
  float topLeft = 0.0f;
  float topRight = 0.0f;
  float bottomRight = 0.0f;
  float bottomLeft = 0.0f;
};

Corners cornersFor(const BorderRadius& radius, Rect bounds) noexcept {
  Corners corners{radius.topLeft.x, radius.topRight.x, radius.bottomRight.x, radius.bottomLeft.x};

  float scale = 1.0f;
  const auto fit = [&scale](float sum, float extent) {
    if (sum > extent) scale = std::min(scale, extent / sum);
  };
  fit(corners.topLeft + corners.topRight, bounds.width());
  fit(corners.bottomLeft + corners.bottomRight, bounds.width());
  fit(corners.topLeft + corners.bottomLeft, bounds.height());
  fit(corners.topRight + corners.bottomRight, bounds.height());

  return {std::max(0.0f, corners.topLeft * scale), std::max(0.0f, corners.topRight * scale),
          std::max(0.0f, corners.bottomRight * scale), std::max(0.0f, corners.bottomLeft * scale)};
}

Corners inset(Corners corners, float amount) noexcept {
  const auto shrink = [amount](float radius) { return std::max(0.0f, radius - amount); };
  return {shrink(corners.topLeft), shrink(corners.topRight), shrink(corners.bottomRight),
          shrink(corners.bottomLeft)};
}

float largest(Corners corners) noexcept {
  return std::max(std::max(corners.topLeft, corners.topRight),
                  std::max(corners.bottomRight, corners.bottomLeft));
}

/// Points along one 90-degree corner. The same count is used for every corner of
/// an outline so that an outer and an inner outline pair up vertex for vertex.
int arcSteps(float radius) noexcept {
  return std::clamp(static_cast<int>(std::ceil(radius * 0.9f)), 3, 20);
}

/// Wound the way raylib winds its own shapes -- down the left side, along the
/// bottom, up the right -- because backface culling is on and the batch does not
/// care which of the two triangles it is looking at.
void buildOutline(std::vector<Vector2>& out, Rect bounds, Corners corners, int steps) {
  struct Arc {
    float cx, cy, radius, startDegrees;
  };
  const Arc arcs[4] = {
      {bounds.left + corners.topLeft, bounds.top + corners.topLeft, corners.topLeft, 270.0f},
      {bounds.left + corners.bottomLeft, bounds.bottom - corners.bottomLeft, corners.bottomLeft,
       180.0f},
      {bounds.right - corners.bottomRight, bounds.bottom - corners.bottomRight,
       corners.bottomRight, 90.0f},
      {bounds.right - corners.topRight, bounds.top + corners.topRight, corners.topRight, 360.0f},
  };

  const float step = 90.0f / static_cast<float>(steps);

  out.clear();
  out.reserve(static_cast<std::size_t>(4 * (steps + 1)));
  for (const Arc& arc : arcs) {
    for (int i = 0; i <= steps; ++i) {
      const float radians = (arc.startDegrees - step * static_cast<float>(i)) * DEG2RAD;
      out.push_back(Vector2{arc.cx + std::cos(radians) * arc.radius,
                            arc.cy + std::sin(radians) * arc.radius});
    }
  }
}

}  // namespace

fltr::ImageHandle Backend::registerTexture(Texture2D texture) {
  textures_.push_back(texture);
  return static_cast<fltr::ImageHandle>(textures_.size());
}

::Color Backend::tinted(fltr::Color color) const noexcept {
  const float a = alpha_.back();
  return ::Color{color.r, color.g, color.b,
                 static_cast<unsigned char>(std::lround(static_cast<float>(color.a) * a))};
}

void Backend::applyClip() const {
  if (clips_.empty()) {
    EndScissorMode();
    return;
  }
  const Rect& clip = clips_.back();
  BeginScissorMode(static_cast<int>(std::floor(clip.left)), static_cast<int>(std::floor(clip.top)),
                   static_cast<int>(std::ceil(clip.width())),
                   static_cast<int>(std::ceil(clip.height())));
}

void Backend::pushTransform(const Transform2D& m) {
  const float matrix[16] = {m.a,  m.b,  0.0f, 0.0f, m.c,  m.d,  0.0f, 0.0f,
                            0.0f, 0.0f, 1.0f, 0.0f, m.tx, m.ty, 0.0f, 1.0f};
  rlPushMatrix();
  rlMultMatrixf(matrix);
  transforms_.push_back(transform_);
  transform_ = m.then(transform_);
}

void Backend::popTransform() {
  rlPopMatrix();
  transform_ = transforms_.back();
  transforms_.pop_back();
}

void Backend::pushClip(Rect rect) {
  // The scissor is applied to the framebuffer, not to the modelview matrix, so
  // the rectangle has to be mapped into screen space here.
  Rect mapped = transform_.applyToRect(rect);
  if (!clips_.empty()) mapped = mapped.intersect(clips_.back());
  if (mapped.isEmpty()) mapped = Rect::fromLTWH(mapped.left, mapped.top, 0.0f, 0.0f);
  clips_.push_back(mapped);
  applyClip();
}

void Backend::popClip() {
  clips_.pop_back();
  applyClip();
}

void Backend::fillRounded(Rect bounds, const BorderRadius& radius, ::Color color) {
  const Corners corners = cornersFor(radius, bounds);
  buildOutline(outline_, bounds, corners, arcSteps(largest(corners)));
  DrawTriangleFan(outline_.data(), static_cast<int>(outline_.size()), color);
}

void Backend::strokeRounded(Rect bounds, const BorderRadius& radius, float width, ::Color color) {
  const Corners corners = cornersFor(radius, bounds);
  const float thickness = std::min(width, std::min(bounds.width(), bounds.height()) * 0.5f);
  const int steps = arcSteps(largest(corners));

  buildOutline(outline_, bounds, corners, steps);
  buildOutline(innerOutline_,
               Rect::fromLTRB(bounds.left + thickness, bounds.top + thickness,
                              bounds.right - thickness, bounds.bottom - thickness),
               inset(corners, thickness), steps);

  // One closed strip rather than four arcs and four edges: the ring has no seam
  // to leave a gap or a double-blended overlap at.
  ring_.clear();
  ring_.reserve(outline_.size() * 2u + 2u);
  for (std::size_t i = 0; i < outline_.size(); ++i) {
    ring_.push_back(innerOutline_[i]);
    ring_.push_back(outline_[i]);
  }
  ring_.push_back(innerOutline_.front());
  ring_.push_back(outline_.front());
  DrawTriangleStrip(ring_.data(), static_cast<int>(ring_.size()), color);
}

void Backend::submit(const Scene& scene) {
  commands_ = 0;
  alpha_.resize(1);
  alpha_[0] = 1.0f;
  clips_.clear();
  transforms_.clear();
  groups_.clear();
  transform_ = Transform2D::identity();
  if (scene.root != nullptr) run(*scene.root, Offset::zero());
  EndScissorMode();
}

void Backend::run(const DisplayList& list, Offset origin) {
  // A list is embedded at an offset rather than re-recorded at its new position,
  // so translation is the caller's job -- this is the only place it happens.
  const bool shifted = origin.dx != 0.0f || origin.dy != 0.0f;
  if (shifted) pushTransform(Transform2D::translation(origin));

  for (const PaintCmd& cmd : list.commands()) {
    ++commands_;
    switch (cmd.op) {
      case PaintOp::PushTransform:
        pushTransform(cmd.pushTransform.transform);
        groups_.push_back(Group::Transform);
        break;

      case PaintOp::PushClipRect:
        // A rounded clip cannot be expressed as a scissor rectangle. The corners
        // are dropped, which is visible only where something actually overflows.
        pushClip(cmd.pushClipRect.rect);
        groups_.push_back(Group::Clip);
        break;

      case PaintOp::PushOpacity:
        alpha_.push_back(alpha_.back() * cmd.pushOpacity.alpha);
        groups_.push_back(Group::Opacity);
        break;

      case PaintOp::Pop:
        if (!groups_.empty()) {
          const Group group = groups_.back();
          groups_.pop_back();
          if (group == Group::Opacity) {
            alpha_.pop_back();
          } else if (group == Group::Clip) {
            popClip();
          } else {
            popTransform();
          }
        }
        break;

      case PaintOp::DrawRect:
        DrawRectangleRec(toRectangle(cmd.drawRect.rect), tinted(cmd.drawRect.color));
        break;

      case PaintOp::DrawRRect: {
        const Rect bounds = cmd.drawRRect.rect;
        if (bounds.isEmpty()) break;
        const bool rounded = !cmd.drawRRect.radius.isZero();

        if (cmd.drawRRect.fill.a > 0) {
          if (rounded) {
            fillRounded(bounds, cmd.drawRRect.radius, tinted(cmd.drawRRect.fill));
          } else {
            DrawRectangleRec(toRectangle(bounds), tinted(cmd.drawRRect.fill));
          }
        }
        if (cmd.drawRRect.borderWidth > 0.0f && cmd.drawRRect.border.a > 0) {
          if (rounded) {
            strokeRounded(bounds, cmd.drawRRect.radius, cmd.drawRRect.borderWidth,
                          tinted(cmd.drawRRect.border));
          } else {
            DrawRectangleLinesEx(toRectangle(bounds), cmd.drawRRect.borderWidth,
                                 tinted(cmd.drawRRect.border));
          }
        }
        break;
      }

      case PaintOp::DrawImage: {
        const fltr::ImageHandle handle = cmd.drawImage.image;
        if (handle == 0 || handle > textures_.size()) break;
        const Texture2D& texture = textures_[static_cast<std::size_t>(handle) - 1u];
        Rectangle source = toRectangle(cmd.drawImage.src);
        if (source.width <= 0.0f || source.height <= 0.0f) {
          source = Rectangle{0.0f, 0.0f, static_cast<float>(texture.width),
                             static_cast<float>(texture.height)};
        }
        DrawTexturePro(texture, source, toRectangle(cmd.drawImage.dst), Vector2{0.0f, 0.0f}, 0.0f,
                       tinted(cmd.drawImage.tint));
        break;
      }

      case PaintOp::DrawParagraph:
        // The text service measured it and the text service draws it: nothing
        // here knows what a glyph or a run is.
        text_.draw(cmd.drawParagraph.paragraph, cmd.drawParagraph.offset,
                   {cmd.drawParagraph.tint.r, cmd.drawParagraph.tint.g, cmd.drawParagraph.tint.b,
                    tinted(cmd.drawParagraph.tint).a});
        break;

      case PaintOp::DrawList:
        // The repaint-boundary seam. A boundary's list is re-recorded only when
        // that subtree repaints; an immediate-mode backend still walks it every
        // frame, but a retained one would cache on (pointer, revision()).
        if (cmd.drawList.list != nullptr) run(*cmd.drawList.list, cmd.drawList.offset);
        break;
    }
  }

  if (shifted) popTransform();
}

}  // namespace demo
