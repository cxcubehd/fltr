#pragma once

#include "fltr/core/listenable.hpp"
#include "fltr/core/observable.hpp"

namespace demo {

/// Derives one animated value from another, without a rebuild.
///
/// fltr's render-attached properties take a `ValueListenable<T>` of exactly the
/// type they need -- `Transform2D` for `Transform`, `float` for `Opacity`. What
/// a component or a switcher publishes is a 0..1 fraction. This is the adapter
/// between them, and it is the whole reason the demo can animate a switch thumb,
/// a progress fill and a page transition without rebuilding anything.
///
/// It is deliberately the same shape as `AnimatedValue<T>`: a `ValueListenable`
/// that subscribes to a source and republishes. Owning one is a `State`'s job,
/// because a render object will hold a pointer to it.
template <class Out>
class MappedValue final : public fltr::ValueListenable<Out> {
public:
  /// A plain function pointer, and one scalar to close over -- how far a thumb
  /// travels, how far a page slides. Enough for every use here, and it keeps the
  /// adapter trivially copyable-ish and free of allocation.
  using Fn = Out (*)(float fraction, float parameter);

  MappedValue() = default;

  MappedValue(const MappedValue&) = delete;
  MappedValue& operator=(const MappedValue&) = delete;

  void bind(fltr::ValueListenable<float>* source, Fn fn, float parameter) {
    fn_ = fn;
    parameter_ = parameter;
    if (source == source_) {
      recompute();
      return;
    }
    subscription_.detach();
    source_ = source;
    if (source_ != nullptr) {
      fltr::subscribeMember<MappedValue, &MappedValue::onSourceChanged>(*source_, subscription_,
                                                                       this);
    }
    recompute();
  }

  void release() {
    subscription_.detach();
    source_ = nullptr;
  }

  const Out& value() const noexcept override { return value_; }

private:
  void onSourceChanged() { recompute(); }

  void recompute() {
    const float fraction = source_ != nullptr ? source_->value() : 0.0f;
    const Out next = fn_ != nullptr ? fn_(fraction, parameter_) : Out{};
    if (next == value_) return;
    value_ = next;
    this->notifyListeners();
  }

  Out value_{};
  Fn fn_ = nullptr;
  float parameter_ = 0.0f;
  fltr::ValueListenable<float>* source_ = nullptr;
  fltr::Subscription subscription_;
};

}  // namespace demo
