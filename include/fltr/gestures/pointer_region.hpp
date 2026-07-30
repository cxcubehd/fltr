#pragma once

#include <string>

#include "fltr/core/callback.hpp"
#include "fltr/gestures/binding.hpp"
#include "fltr/gestures/events.hpp"
#include "fltr/gestures/tap.hpp"
#include "fltr/render/box.hpp"

namespace fltr {

/// What a Pointer widget configures, in one piece so the render object has a
/// single place to notice a change.
struct PointerCallbacks {
  Callback<void()> onEnter;
  Callback<void()> onExit;
  Callback<void()> onTapDown;
  Callback<void()> onTap;
  Callback<void()> onTapCancel;
};

/// The one render object that consumes pointer events.
///
/// It lives with the gestures rather than in `render/` so the render tree keeps
/// the property it was built with in M2: no dependency on the gesture layer at
/// all, and testable without one.
class RenderPointerRegion final : public RenderProxyBox {
public:
  RenderPointerRegion(PointerBinding& binding, HitTestBehavior behavior);
  ~RenderPointerRegion() override;

  const char* typeName() const override { return "PointerRegion"; }
  std::string describe() const override;

  void setBehavior(HitTestBehavior behavior);
  void setCallbacks(const PointerCallbacks& callbacks);

  RenderPointerRegion* asPointerRegion() noexcept override { return this; }

  bool hitTest(HitTestResult& result, Offset position) override;

  bool wantsHover() const noexcept { return onEnter_ || onExit_; }
  bool hovered() const noexcept { return hovered_; }

private:
  friend class MouseTracker;
  friend class PointerBinding;

  void receiveDown(const PointerEvent& down);
  void enter();
  void exit();

  PointerBinding* binding_;
  TapGestureRecognizer tap_;
  Callback<void()> onEnter_;
  Callback<void()> onExit_;
  HitTestBehavior behavior_;
  bool hovered_ = false;
};

}  // namespace fltr
