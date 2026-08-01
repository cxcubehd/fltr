#pragma once

#include <memory>

#include "fltr/animation/tween.hpp"
#include "fltr/components/component.hpp"

namespace fltr {

/// `Mixed` is Flutter's null third state: a parent checkbox whose children
/// disagree. Only a tristate toggle ever reports it.
enum class ToggleValue : std::uint8_t { Off, On, Mixed };

class RawToggle;

class RawToggleState final : public State<RawToggle> {
public:
  void initState() override;
  void didUpdateWidget(const RawToggle& previous) override;
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

private:
  void adoptConfiguration();
  bool handleKey(const KeyEvent& event);
  void handleFocusChange(bool focused);
  void toggle();
  void endDrag();
  /// Pins the position where it is, rather than animating to it: what a drag
  /// wants, since the thumb is following a finger and not a clock.
  void holdPosition(float fraction) { position_.setTween({fraction, fraction}); }

  ToggleValue nextValue() const noexcept;

  OwnedStates states_;
  AnimationDriver driver_;
  AnimatedValue<float> position_{driver_, Tween<float>{0.0f, 0.0f}};
  bool keyHeld_ = false;
  bool settleAfterDrag_ = false;
};

/// The one machine behind a checkbox, a switch and a radio.
///
/// Flutter's `ToggleableStateMixin`, which is private to `material/` and
/// entangled with a painter. Here it is the component, and the visual is the
/// consumer's: the 0..1 position that a checkbox draws a tick from and a switch
/// draws a thumb from is published through `ComponentScope`, so nothing about
/// the shape is decided here.
///
/// A radio is this with `canToggleOff = false`: a member of a group only its
/// siblings can clear, reporting its own value through `onChanged`.
class RawToggle final : public Configure<RawToggle, StatefulWidget> {
public:
  struct Args {
    Key key;
    ToggleValue value = ToggleValue::Off;
    /// Reports the value this toggle would move to. The consumer holds the
    /// value and decides: nothing here assumes the report was accepted.
    Callback<void(ToggleValue)> onChanged;
    /// Cycles Off, On, Mixed rather than Off, On.
    bool tristate = false;
    /// Whether pressing an On toggle turns it off. False is the radio shape.
    /// Describes the two-value cycle only: a tristate toggle always completes
    /// its own, since a group of them is not a thing radios form.
    bool canToggleOff = true;
    /// How far a thumb travels, which is what makes this a switch: a drag of
    /// that distance moves the position from end to end, and letting go past
    /// halfway reports the toggle. Zero is a checkbox -- no drag recognizer is
    /// created at all.
    float dragExtent = 0.0f;
    float positionDuration = 0.15f;

    bool enabled = true;
    WidgetStatesController* statesController = nullptr;
    FocusNode* focusNode = nullptr;
    bool canRequestFocus = true;
    bool skipTraversal = false;
    bool autofocus = false;

    MouseCursor cursor = MouseCursor::Click;
    HitTestBehavior behavior = HitTestBehavior::Opaque;
    PointerButtons buttons{PointerButton::Primary};
    WidgetRef child;
  };

  explicit RawToggle(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "RawToggle"; }
  const Args& args() const noexcept { return args_; }

  std::unique_ptr<State<RawToggle>> createState() const {
    return std::make_unique<RawToggleState>();
  }

private:
  Args args_;
};

}  // namespace fltr
