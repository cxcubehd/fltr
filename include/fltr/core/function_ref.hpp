#pragma once

#include <type_traits>
#include <utility>

namespace fltr {

/// A non-owning view of a callable, for parameters that are invoked before the
/// call returns. Used pervasively for tree visitors and paint callbacks, where
/// std::function's allocation would show up in every frame.
template <class Sig>
class FunctionRef;

template <class R, class... Args>
class FunctionRef<R(Args...)> {
public:
  FunctionRef() = delete;

  template <class F>
    requires(!std::is_same_v<std::decay_t<F>, FunctionRef> &&
             std::is_invocable_r_v<R, F&, Args...>)
  FunctionRef(F&& f) noexcept
      : obj_(const_cast<void*>(static_cast<const void*>(std::addressof(f)))),
        call_([](void* o, Args... args) -> R {
          return (*static_cast<std::remove_reference_t<F>*>(o))(std::forward<Args>(args)...);
        }) {}

  R operator()(Args... args) const { return call_(obj_, std::forward<Args>(args)...); }

private:
  void* obj_;
  R (*call_)(void*, Args...);
};

}  // namespace fltr
