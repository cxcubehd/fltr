#pragma once

namespace fltr {

/// An easing function over 0..1.
///
/// A plain function pointer: a curve costs one pointer to copy into a widget
/// configuration, stays trivially destructible like everything else that goes
/// into the build arena, and calls without an object in the way.
class Curve {
public:
  using Fn = float (*)(float t);

  constexpr Curve() = default;
  constexpr Curve(Fn fn) noexcept : fn_(fn) {}

  constexpr float operator()(float t) const noexcept { return fn_ ? fn_(t) : t; }
  constexpr explicit operator bool() const noexcept { return fn_ != nullptr; }

private:
  Fn fn_ = nullptr;
};

namespace detail {

constexpr float easeInQuad(float t) noexcept { return t * t; }
constexpr float easeOutQuad(float t) noexcept { return t * (2.0f - t); }
constexpr float easeInOutQuad(float t) noexcept {
  return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
}
constexpr float easeInCubic(float t) noexcept { return t * t * t; }
constexpr float easeOutCubic(float t) noexcept {
  const float u = 1.0f - t;
  return 1.0f - u * u * u;
}
constexpr float easeInOutCubic(float t) noexcept {
  const float u = 1.0f - t;
  return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * u * u * u;
}

}  // namespace detail

/// The set a HUD needs: ease-out for something arriving, ease-in for something
/// leaving, ease-in-out for something moving between two settled states. The
/// cubics are the same shapes with more of their travel at the ends.
namespace Curves {

inline constexpr Curve linear{};
inline constexpr Curve easeIn{&detail::easeInQuad};
inline constexpr Curve easeOut{&detail::easeOutQuad};
inline constexpr Curve easeInOut{&detail::easeInOutQuad};
inline constexpr Curve easeInCubic{&detail::easeInCubic};
inline constexpr Curve easeOutCubic{&detail::easeOutCubic};
inline constexpr Curve easeInOutCubic{&detail::easeInOutCubic};

}  // namespace Curves

}  // namespace fltr
