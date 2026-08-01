#pragma once

#include <memory>

#include "fltr/components/component.hpp"

namespace fltr {

class RawButton;

class RawButtonState final : public State<RawButton> {
public:
  void initState() override;
  void didUpdateWidget(const RawButton& previous) override;
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

private:
  void adoptConfiguration();
  bool handleKey(const KeyEvent& event);
  void handleFocusChange(bool focused);
  void setPressed(bool pressed) { states_.update(WidgetState::Pressed, pressed); }

  OwnedStates states_;
  /// A keyboard press is held between the key going down and its coming up, so
  /// losing the focus in between has to let go of it: the key-up will be
  /// delivered somewhere else entirely.
  bool keyHeld_ = false;
};

/// A press, and everything that has to be true around one.
///
/// Also the list item a menu needs: `selected` is the only thing a current entry
/// has that an ordinary button does not, and it is published rather than drawn.
class RawButton final : public Configure<RawButton, StatefulWidget> {
public:
  struct Args {
    Key key;
    Callback<void()> onPressed;
    /// Adding this creates a long-press recognizer; leaving it unset creates
    /// none, so a plain button costs one tap recognizer and nothing else.
    Callback<void()> onLongPress;
    /// Disabled: no gesture reaches it, it cannot take the focus, and it
    /// publishes `Disabled`. It still swallows the pointer, so a disabled
    /// control never leaks a click through to what it is drawn over -- wrap it
    /// in `IgnorePointer` to want the other thing.
    bool enabled = true;
    /// Published as `Selected`. What a menu's current entry has and a button
    /// does not.
    bool selected = false;

    /// Consumer-owned; null means this State owns one. Must outlive the widget,
    /// as every object a widget names by pointer must.
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

  explicit RawButton(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "RawButton"; }
  const Args& args() const noexcept { return args_; }

  std::unique_ptr<State<RawButton>> createState() const {
    return std::make_unique<RawButtonState>();
  }

private:
  Args args_;
};

}  // namespace fltr
