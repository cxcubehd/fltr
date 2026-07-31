#pragma once

#include <cstddef>
#include <vector>

#include "fltr/paint/display_list.hpp"
#include "platform/raylib_text.hpp"

namespace fltrdemo {

/// Translates a recorded scene into raylib draw calls.
///
/// The list is a flat command array with a balanced push/pop state stack; the
/// backend maintains that stack itself, which is the whole of the contract.
/// Nothing here allocates once the stack has reached its high-water mark.
///
/// Two limits are deliberate rather than accidental, and both are checked
/// rather than assumed:
///
///   - **transforms are translation and scale only.** raylib's scissor is in
///     window space and knows nothing about any matrix, so a clip nested under
///     a transform has to be transformed on the CPU before it can become a
///     scissor rectangle -- which is exact for translate/scale and not for
///     rotation. A rotation is counted in `unsupportedTransforms()` and drawn
///     with its translation only, which is visibly wrong rather than subtly
///     wrong.
///   - **rounded clips are applied as their bounding rectangle.** raylib has no
///     rounded scissor. The demo only ever rounds a clip whose child already
///     paints a matching rounded rectangle, so nothing depends on the corners.
class RaylibRenderer {
public:
  explicit RaylibRenderer(const RaylibTextService& text) noexcept : text_(&text) {}

  /// Walks the scene. Call between BeginDrawing and EndDrawing.
  void submit(const fltr::Scene& scene);

  /// Draw commands issued by the last submit, which is what makes "hovering one
  /// button re-records one boundary" visible as a number rather than a claim.
  std::size_t commandCount() const noexcept { return commands_; }
  std::size_t unsupportedTransforms() const noexcept { return unsupported_; }

private:
  /// The resolved painting state at one depth: everything a draw needs, so a
  /// command is translated by reading the top of the stack and nothing else.
  struct State {
    fltr::Transform2D transform = fltr::Transform2D::identity();
    fltr::Rect clip;
    float opacity = 1.0f;
  };

  void walk(const fltr::DisplayList& list);
  const State& top() const noexcept { return stack_.back(); }
  void syncScissor(const fltr::Rect& clip);
  fltr::Color tint(fltr::Color color) const noexcept;
  fltr::Rect mapRect(fltr::Rect rect) const noexcept;

  std::vector<State> stack_;
  const RaylibTextService* text_;
  fltr::Rect applied_;
  bool scissorOn_ = false;
  std::size_t commands_ = 0;
  std::size_t unsupported_ = 0;
};

}  // namespace fltrdemo
