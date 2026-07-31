#pragma once

#include <concepts>
#include <utility>

#include "fltr/core/listenable.hpp"

namespace fltr {

/// A value that says when it changed.
///
/// This is the one interface reactivity and animation share. An `Observable<T>`
/// changes because the game pushed a new value and an `AnimatedValue<T>` because
/// time passed, and nothing consuming one can tell which it holds -- so a `Watch`
/// rebuilds from either, and a render object observes either.
///
/// The rebuild-versus-repaint distinction is deliberately not here; it is in the
/// subscriber. An element that subscribes marks itself needing build, while a
/// render object subscribing through `observeForPaint` marks only itself needing
/// paint. Same signal, different sink, chosen at the call site.
template <class T>
class ValueListenable : public Listenable {
public:
  virtual const T& value() const noexcept = 0;
};

/// A value the game pushes in and the UI reads.
///
/// Pushing an unchanged value is a no-op, which is what lets the consumer push
/// its whole state every frame and leave the framework to decide what that
/// implies for work.
template <class T>
  requires std::equality_comparable<T>
class Observable final : public ValueListenable<T> {
public:
  Observable() = default;
  explicit Observable(T value) : value_(std::move(value)) {}

  const T& value() const noexcept override { return value_; }

  void set(T next) {
    if (next == value_) return;
    value_ = std::move(next);
    this->notifyListeners();
  }

private:
  T value_{};
};

/// A render-object property that is either a constant or driven by a value
/// source. Holding both in one place is what lets one widget cover `.opacity =
/// 0.5f` and `.animation = &fade` without the render object growing two modes
/// that can disagree: a source, once set, shadows the constant.
template <class T>
  requires std::equality_comparable<T>
class Animatable {
public:
  Animatable() = default;
  explicit Animatable(T constant) : constant_(std::move(constant)) {}

  const T& value() const noexcept { return source_ ? source_->value() : constant_; }

  /// Both report whether this property needs invalidating, which is what the
  /// caller turns into a `markNeeds` call. Setting a shadowed constant does not.
  bool set(T next) {
    if (next == constant_) return false;
    constant_ = std::move(next);
    return source_ == nullptr;
  }
  bool setSource(ValueListenable<T>* next) {
    if (next == source_) return false;
    source_ = next;
    return true;
  }

private:
  T constant_{};
  ValueListenable<T>* source_ = nullptr;
};

}  // namespace fltr
