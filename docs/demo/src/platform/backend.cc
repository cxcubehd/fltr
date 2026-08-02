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

/// raylib takes one roundness for the whole rectangle, as a fraction of half its
/// shorter side, while fltr carries four corners with independent x and y radii.
/// The top-left x radius is used for all four; the demo never asks for more, and
/// the mismatch is recorded rather than hidden.
float roundnessFor(const BorderRadius& radius, Rect bounds) noexcept {
  const float half = std::min(bounds.width(), bounds.height()) * 0.5f;
  if (half <= 0.0f) return 0.0f;
  return std::clamp(radius.topLeft.x / half, 0.0f, 1.0f);
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
        const Rectangle rec = toRectangle(bounds);
        const float roundness = roundnessFor(cmd.drawRRect.radius, bounds);
        if (cmd.drawRRect.fill.a > 0) {
          if (roundness <= 0.0f) {
            DrawRectangleRec(rec, tinted(cmd.drawRRect.fill));
          } else {
            DrawRectangleRounded(rec, roundness, 8, tinted(cmd.drawRRect.fill));
          }
        }
        if (cmd.drawRRect.borderWidth > 0.0f && cmd.drawRRect.border.a > 0) {
          if (roundness <= 0.0f) {
            DrawRectangleLinesEx(rec, cmd.drawRRect.borderWidth, tinted(cmd.drawRRect.border));
          } else {
            DrawRectangleRoundedLinesEx(rec, roundness, 8, cmd.drawRRect.borderWidth,
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
