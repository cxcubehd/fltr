#pragma once

#include <concepts>
#include <utility>

#include "fltr/core/listenable.hpp"

namespace fltr {

/// A value the game pushes in and the UI reads.
///
/// Pushing an unchanged value is a no-op, which is what lets the consumer push
/// its whole state every frame and leave the framework to decide what that
/// implies for work.
///
/// The rebuild-versus-repaint distinction is not here, it is in the subscriber:
/// a `Watch` element rebuilds its subtree, while a render object observing the
/// same value through `observeForPaint` only repaints. One value can have both
/// kinds of listener at once.
template <class T>
  requires std::equality_comparable<T>
class Observable final : public Listenable {
public:
  Observable() = default;
  explicit Observable(T value) : value_(std::move(value)) {}

  const T& value() const noexcept { return value_; }

  void set(T next) {
    if (next == value_) return;
    value_ = std::move(next);
    notifyListeners();
  }

private:
  T value_{};
};

}  // namespace fltr
