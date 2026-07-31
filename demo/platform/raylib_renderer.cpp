#include "platform/raylib_renderer.hpp"

#include <algorithm>
#include <cmath>

namespace fltrdemo {
namespace {

Rectangle toRaylib(fltr::Rect r) noexcept {
  return Rectangle{r.left, r.top, r.width(), r.height()};
}

Color toRaylib(fltr::Color c) noexcept { return Color{c.r, c.g, c.b, c.a}; }

/// raylib expresses corner rounding as a fraction of the shorter side rather
/// than as a radius, so the same rounded rectangle needs converting.
float roundnessFor(const fltr::BorderRadius& radius, fltr::Size size) noexcept {
  const float largest = std::max({radius.topLeft.x, radius.topRight.x, radius.bottomRight.x,
                                  radius.bottomLeft.x});
  const float shorter = std::min(size.width, size.height);
  if (largest <= 0.0f || shorter <= 0.0f) return 0.0f;
  return std::clamp(2.0f * largest / shorter, 0.0f, 1.0f);
}

constexpr int kCornerSegments = 8;

}  // namespace

fltr::Color RaylibRenderer::tint(fltr::Color color) const noexcept {
  const float alpha = top().opacity;
  return alpha >= 1.0f ? color : color.scaleAlpha(alpha);
}

fltr::Rect RaylibRenderer::mapRect(fltr::Rect rect) const noexcept {
  return top().transform.applyToRect(rect);
}

void RaylibRenderer::syncScissor(const fltr::Rect& clip) {
  if (scissorOn_ && clip == applied_) return;
  applied_ = clip;
  scissorOn_ = true;
  BeginScissorMode(static_cast<int>(std::floor(clip.left)), static_cast<int>(std::floor(clip.top)),
                   static_cast<int>(std::ceil(std::max(0.0f, clip.width()))),
                   static_cast<int>(std::ceil(std::max(0.0f, clip.height()))));
}

void RaylibRenderer::submit(const fltr::Scene& scene) {
  commands_ = 0;
  unsupported_ = 0;
  stack_.clear();
  stack_.push_back(State{fltr::Transform2D::identity(),
                         fltr::Rect::fromOriginSize(fltr::Offset::zero(), scene.surface), 1.0f});
  scissorOn_ = false;
  if (scene.root) walk(*scene.root);
  if (scissorOn_) {
    EndScissorMode();
    scissorOn_ = false;
  }
}

void RaylibRenderer::walk(const fltr::DisplayList& list) {
  for (const fltr::PaintCmd& cmd : list.commands()) {
    ++commands_;
    switch (cmd.op) {
      case fltr::PaintOp::PushTransform: {
        const fltr::Transform2D& m = cmd.pushTransform.transform;
        if (m.b != 0.0f || m.c != 0.0f) ++unsupported_;
        State next = top();
        next.transform = m.then(next.transform);
        stack_.push_back(next);
        break;
      }
      case fltr::PaintOp::PushClipRect: {
        State next = top();
        // On the CPU, before it becomes a scissor: raylib's scissor is in window
        // space and takes no account of the transform this clip sits under.
        next.clip = next.clip.intersect(mapRect(cmd.pushClipRect.rect));
        stack_.push_back(next);
        break;
      }
      case fltr::PaintOp::PushOpacity: {
        State next = top();
        next.opacity *= cmd.pushOpacity.alpha;
        stack_.push_back(next);
        break;
      }
      case fltr::PaintOp::Pop:
        FLTR_EXPECTS(stack_.size() > 1, "a display list popped more state than it pushed");
        stack_.pop_back();
        break;

      case fltr::PaintOp::DrawRect: {
        syncScissor(top().clip);
        DrawRectangleRec(toRaylib(mapRect(cmd.drawRect.rect)), toRaylib(tint(cmd.drawRect.color)));
        break;
      }
      case fltr::PaintOp::DrawRRect: {
        syncScissor(top().clip);
        const fltr::Rect rect = mapRect(cmd.drawRRect.rect);
        const float roundness = roundnessFor(cmd.drawRRect.radius, rect.size());
        const fltr::Color fill = tint(cmd.drawRRect.fill);
        const fltr::Color border = tint(cmd.drawRRect.border);
        if (fill.a != 0) {
          if (roundness <= 0.0f) {
            DrawRectangleRec(toRaylib(rect), toRaylib(fill));
          } else {
            DrawRectangleRounded(toRaylib(rect), roundness, kCornerSegments, toRaylib(fill));
          }
        }
        if (cmd.drawRRect.borderWidth > 0.0f && border.a != 0) {
          const float width = cmd.drawRRect.borderWidth * std::fabs(top().transform.a);
          if (roundness <= 0.0f) {
            DrawRectangleLinesEx(toRaylib(rect), width, toRaylib(border));
          } else {
            DrawRectangleRoundedLinesEx(toRaylib(rect), roundness, kCornerSegments, width,
                                        toRaylib(border));
          }
        }
        break;
      }
      case fltr::PaintOp::DrawImage: {
        // The demo draws no images: every pixel of it comes from rectangles and
        // text, which is the point. The handle is consumer-owned, so a real
        // consumer would look its texture up here.
        break;
      }
      case fltr::PaintOp::DrawParagraph: {
        syncScissor(top().clip);
        const fltr::Offset at = top().transform.apply(cmd.drawParagraph.offset);
        text_->draw(cmd.drawParagraph.paragraph, at, tint(cmd.drawParagraph.tint),
                    std::fabs(top().transform.a));
        break;
      }
      case fltr::PaintOp::DrawList: {
        // The repaint-boundary seam. This backend re-walks the embedded list
        // every frame, which is free enough at these sizes; a backend that
        // translated into GPU buffers would cache on
        // (cmd.drawList.list, list->revision()) and re-translate only when the
        // revision moved, which is exactly why the representation carries one.
        State next = top();
        next.transform =
            fltr::Transform2D::translation(cmd.drawList.offset).then(next.transform);
        stack_.push_back(next);
        walk(*cmd.drawList.list);
        stack_.pop_back();
        break;
      }
    }
  }
}

}  // namespace fltrdemo
