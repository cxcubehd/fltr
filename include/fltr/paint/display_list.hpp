#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "fltr/core/geometry.hpp"

namespace fltr {

/// GPU resources are consumer-owned. The framework only ever refers to them by
/// opaque handle and never loads, allocates, or frees one.
using ImageHandle = std::uint64_t;
using ParagraphHandle = std::uint64_t;
inline constexpr ParagraphHandle kNullParagraph = 0;

class DisplayList;

enum class PaintOp : std::uint8_t {
  // State stack. Every Push must be balanced by a Pop.
  PushTransform,
  PushClipRect,
  PushOpacity,
  Pop,
  // Draws.
  DrawRect,
  DrawRRect,
  DrawImage,
  DrawParagraph,
  /// Embeds another list by reference. This is the repaint-boundary seam: the
  /// referenced list is owned by a boundary render object and is only
  /// re-recorded when that subtree repaints. A backend may cache translated
  /// state keyed on (list pointer, list revision) and skip re-translation.
  DrawList,
};

const char* toString(PaintOp op) noexcept;

struct PushTransformCmd {
  Transform2D transform;
};
struct PushClipRectCmd {
  Rect rect;
  BorderRadius radius;
};
struct PushOpacityCmd {
  float alpha;
};
struct DrawRectCmd {
  Rect rect;
  Color color;
};
struct DrawRRectCmd {
  Rect rect;
  BorderRadius radius;
  Color fill;
  Color border;
  float borderWidth;
};
struct DrawImageCmd {
  Rect dst;
  Rect src;  // source rectangle in texel space; the consumer knows the texture
  ImageHandle image;
  Color tint;
};
struct DrawParagraphCmd {
  ParagraphHandle paragraph;
  Offset offset;
  Color tint;  // multiplied with the per-run colours the text service reported
};
struct DrawListCmd {
  const DisplayList* list;
  Offset offset;
};

/// One recorded command. Fixed size and trivially copyable, so a list is a flat
/// contiguous array a backend can walk without indirection.
struct PaintCmd {
  PaintOp op;
  union {
    PushTransformCmd pushTransform;
    PushClipRectCmd pushClipRect;
    PushOpacityCmd pushOpacity;
    DrawRectCmd drawRect;
    DrawRRectCmd drawRRect;
    DrawImageCmd drawImage;
    DrawParagraphCmd drawParagraph;
    DrawListCmd drawList;
  };

  PaintCmd() noexcept : op(PaintOp::Pop), pushOpacity{1.0f} {}
  explicit PaintCmd(PaintOp o) noexcept : op(o), pushOpacity{1.0f} {}
};

static_assert(std::is_trivially_destructible_v<PaintCmd>);
static_assert(std::is_trivially_copyable_v<PaintCmd>);

/// An ordered, backend-agnostic recording.
///
/// Nothing here assumes the consumer is immediate-mode or retained-mode: an
/// immediate-mode backend walks the list once per frame, a retained-mode
/// backend translates it once and re-uses the translation while `revision()`
/// is unchanged.
class DisplayList {
public:
  DisplayList() = default;

  /// Begins a new recording. Retains the command buffer's capacity, so a
  /// re-record in the steady state performs no allocation.
  void beginRecording() {
    cmds_.clear();
    ++revision_;
    depth_ = 0;
  }

  void pushTransform(const Transform2D& m) {
    PaintCmd c(PaintOp::PushTransform);
    c.pushTransform = {m};
    cmds_.push_back(c);
    ++depth_;
  }
  void pushClipRect(Rect r, BorderRadius radius = BorderRadius::zero()) {
    PaintCmd c(PaintOp::PushClipRect);
    c.pushClipRect = {r, radius};
    cmds_.push_back(c);
    ++depth_;
  }
  void pushOpacity(float alpha) {
    PaintCmd c(PaintOp::PushOpacity);
    c.pushOpacity = {alpha};
    cmds_.push_back(c);
    ++depth_;
  }
  void pop() {
    FLTR_EXPECTS(depth_ > 0, "DisplayList::pop without a matching push");
    PaintCmd c(PaintOp::Pop);
    cmds_.push_back(c);
    --depth_;
  }

  void drawRect(Rect r, Color color) {
    PaintCmd c(PaintOp::DrawRect);
    c.drawRect = {r, color};
    cmds_.push_back(c);
  }
  void drawRRect(Rect r, BorderRadius radius, Color fill, Color border = Color::transparent(),
                 float borderWidth = 0.0f) {
    PaintCmd c(PaintOp::DrawRRect);
    c.drawRRect = {r, radius, fill, border, borderWidth};
    cmds_.push_back(c);
  }
  void drawImage(Rect dst, Rect src, ImageHandle image, Color tint = Color{255, 255, 255, 255}) {
    PaintCmd c(PaintOp::DrawImage);
    c.drawImage = {dst, src, image, tint};
    cmds_.push_back(c);
  }
  void drawParagraph(ParagraphHandle p, Offset at, Color tint = Color{255, 255, 255, 255}) {
    PaintCmd c(PaintOp::DrawParagraph);
    c.drawParagraph = {p, at, tint};
    cmds_.push_back(c);
  }
  void drawList(const DisplayList* list, Offset at) {
    FLTR_EXPECTS(list != nullptr, "drawList requires a list");
    PaintCmd c(PaintOp::DrawList);
    c.drawList = {list, at};
    cmds_.push_back(c);
  }

  void endRecording() {
    FLTR_ENSURES(depth_ == 0, "DisplayList recording ended with unbalanced push/pop");
  }

  std::span<const PaintCmd> commands() const noexcept { return cmds_; }
  bool empty() const noexcept { return cmds_.empty(); }
  std::size_t size() const noexcept { return cmds_.size(); }

  /// Bumped by every beginRecording. A backend caches on (this, revision()).
  std::uint64_t revision() const noexcept { return revision_; }

private:
  std::vector<PaintCmd> cmds_;
  std::uint64_t revision_ = 0;
  int depth_ = 0;
};

/// What the framework hands the consumer each frame.
///
/// `revision` is the sum of the revisions of every list reachable from the
/// root; if it is unchanged since the last frame, absolutely nothing was
/// re-recorded and the consumer may resubmit whatever it did last time.
struct Scene {
  const DisplayList* root = nullptr;
  Size surface;
  std::uint64_t revision = 0;
};

/// Consumer-side translation target. The framework never implements a real one.
class PaintBackend {
public:
  virtual ~PaintBackend() = default;
  virtual void submit(const Scene& scene) = 0;
};

}  // namespace fltr
