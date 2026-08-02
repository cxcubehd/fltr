#pragma once

#include <vector>

#include "fltr/paint/display_list.hpp"
#include "platform/text.hh"
#include "raylib.h"

namespace demo {

/// Translates a recorded `DisplayList` into raylib calls.
///
/// fltr has no backend interface to implement: painting produces an ordered,
/// trivially copyable command array, and a consumer walks it. That is the whole
/// contract, so this is a switch statement and a state stack -- nothing is
/// registered, subclassed, or handed back to the framework.
class Backend {
public:
  explicit Backend(RaylibTextService& text) noexcept : text_(text) {}

  /// One frame. Safe to call with a scene whose root is null.
  void submit(const fltr::Scene& scene);

  /// GPU resources are consumer-owned; the framework only ever sees the handle.
  fltr::ImageHandle registerTexture(Texture2D texture);

  /// Commands translated during the last `submit`, for the debug readout.
  std::size_t lastCommandCount() const noexcept { return commands_; }

private:
  /// Which stack a `Pop` unwinds. The display list guarantees pushes and pops
  /// nest, but not that they alternate, so the only way to close the right group
  /// is to remember the order they were opened in.
  enum class Group : unsigned char { Transform, Clip, Opacity };

  void run(const fltr::DisplayList& list, fltr::Offset origin);
  void pushTransform(const fltr::Transform2D& transform);
  void popTransform();
  void pushClip(fltr::Rect rect);
  void popClip();
  void applyClip() const;

  /// raylib's own rounded rectangles take a single roundness for all four
  /// corners and leave a seam where the border ring meets itself, so the
  /// geometry is built here instead: one outline, four independent radii.
  void fillRounded(fltr::Rect bounds, const fltr::BorderRadius& radius, ::Color color);
  void strokeRounded(fltr::Rect bounds, const fltr::BorderRadius& radius, float width,
                     ::Color color);

  ::Color tinted(fltr::Color color) const noexcept;

  RaylibTextService& text_;
  std::vector<Texture2D> textures_;

  /// Alpha is a stack because `PushOpacity` nests, and raylib has no global
  /// alpha: every colour emitted inside the group is multiplied by the product.
  std::vector<float> alpha_{1.0f};
  /// Scissor rectangles, already intersected with their parent and already in
  /// screen space. raylib has one scissor, so a pop has to restore the previous.
  std::vector<fltr::Rect> clips_;
  /// The same transform the GPU matrix stack holds, kept on the CPU because a
  /// scissor rectangle is not affected by the matrix and has to be mapped here.
  fltr::Transform2D transform_;
  std::vector<fltr::Transform2D> transforms_;
  std::vector<Group> groups_;

  /// Reused every frame so that a rounded rectangle costs no allocation.
  std::vector<Vector2> outline_;
  std::vector<Vector2> innerOutline_;
  std::vector<Vector2> ring_;

  std::size_t commands_ = 0;
};

}  // namespace demo
