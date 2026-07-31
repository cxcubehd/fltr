#pragma once

#include <concepts>
#include <utility>

#include "fltr/animation/driver.hpp"
#include "fltr/core/geometry.hpp"
#include "fltr/core/observable.hpp"

namespace fltr {

/// Anything two of which have a value in between.
///
/// `lerp` is found by ordinary overload resolution, so every geometry and paint
/// type already satisfies this and a consumer's own type joins by defining one
/// next to itself.
template <class T>
concept Interpolatable = requires(const T& a, const T& b, float t) {
  { lerp(a, b, t) } -> std::convertible_to<T>;
};

/// A scalar has no namespace to look in, so its overload has to be visible here
/// rather than at the point of use. This is the assertion that says so.
static_assert(Interpolatable<float>);

/// The interval an animation travels. Composing interpolation with easing is
/// `tween.at(driver.value())`: the driver applies the curve, so nothing sits
/// between the two.
template <class T>
  requires Interpolatable<T>
struct Tween {
  T from{};
  T to{};

  T at(float t) const { return lerp(from, to, t); }
};

/// A T that moves with a driver.
///
/// Same interface as `Observable<T>`, different reason for changing -- which is
/// the whole point of sharing `ValueListenable`: a render object observing this
/// repaints itself and nothing else, and a `Watch` over it rebuilds a subtree,
/// with no animation-specific machinery on either path.
template <class T>
  requires Interpolatable<T> && std::equality_comparable<T>
class AnimatedValue final : public ValueListenable<T> {
public:
  AnimatedValue(AnimationDriver& driver, Tween<T> tween)
      : driver_(&driver), tween_(std::move(tween)), value_(tween_.at(driver.value())) {
    subscribeMember<AnimatedValue, &AnimatedValue::sample>(driver, subscription_, this);
  }

  const T& value() const noexcept override { return value_; }

  void setTween(Tween<T> tween) {
    tween_ = std::move(tween);
    sample();
  }

  /// Runs a fresh interval from the value currently shown to `to`.
  ///
  /// Re-basing the interval here rather than retargeting the driver is what
  /// makes an interruption continuous: the new interval begins where the last
  /// one had actually reached, so the value never jumps and repeated
  /// interruption accumulates no error. The driver is restarted, so one shared
  /// by several values retargets all of them.
  void retarget(T to) {
    if (to == value_) {
      driver_->stop();
      return;
    }
    tween_ = {value_, std::move(to)};
    driver_->jumpTo(0.0f);
    driver_->forward();
  }

private:
  void sample() {
    T next = tween_.at(driver_->value());
    if (next == value_) return;
    value_ = std::move(next);
    this->notifyListeners();
  }

  AnimationDriver* driver_;
  Tween<T> tween_;
  T value_;
  Subscription subscription_;
};

}  // namespace fltr
