#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

#include "fltr/core/config.hpp"

namespace fltr {

/// A callable small enough to live inside a widget configuration.
///
/// A widget config is arena scratch, so it must stay trivially destructible, and
/// the lambda written at a call site is a temporary that dies with the build
/// expression -- so the callable is *copied* into inline storage rather than
/// referenced, as FunctionRef would. Capturing anything that owns memory is
/// refused at compile time, which is the rule the arena already enforces on
/// widgets themselves.
///
/// A capture by reference must outlive the render object that ends up holding
/// the callback, which game state pushed in from the frame loop does.
template <class Sig>
class Callback;

template <class R, class... Args>
class Callback<R(Args...)> {
public:
  static constexpr std::size_t kCapacity = 4 * sizeof(void*);

  Callback() = default;

  template <class F>
    requires(!std::is_same_v<std::decay_t<F>, Callback> &&
             std::is_invocable_r_v<R, const std::decay_t<F>&, Args...>)
  Callback(F&& f) noexcept {
    using Fn = std::decay_t<F>;
    static_assert(std::is_trivially_copyable_v<Fn> && std::is_trivially_destructible_v<Fn>,
                  "a callback stored in a widget must capture only trivially copyable state: "
                  "capture by reference, or by value of a trivial type");
    static_assert(sizeof(Fn) <= kCapacity && alignof(Fn) <= alignof(void*),
                  "a callback stored in a widget captures too much: capture a pointer to the "
                  "state instead");
    ::new (storage_) Fn(std::forward<F>(f));
    invoke_ = [](const void* s, Args... args) -> R {
      return (*static_cast<const Fn*>(s))(std::forward<Args>(args)...);
    };
  }

  explicit operator bool() const noexcept { return invoke_ != nullptr; }

  R operator()(Args... args) const {
    FLTR_EXPECTS(invoke_ != nullptr, "an unset callback was invoked");
    return invoke_(storage_, std::forward<Args>(args)...);
  }

private:
  alignas(void*) std::byte storage_[kCapacity]{};
  R (*invoke_)(const void*, Args...) = nullptr;
};

static_assert(std::is_trivially_destructible_v<Callback<void()>>);
static_assert(std::is_trivially_copyable_v<Callback<void()>>);

}  // namespace fltr
