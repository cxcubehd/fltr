#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "fltr/core/callback.hpp"
#include "fltr/widgets/framework.hpp"

namespace fltrdemo {

enum class ButtonKind : std::uint8_t {
  Menu,     ///< the title screen's stack of wide buttons
  Compact,  ///< Apply, Back, Connect: sized to its label
  Tab,      ///< flat, and selected rather than pressed
};

/// A button, and the demo's smallest complete example of authoring a component.
///
/// Everything it does is the framework's:
///
///   - `Pointer` supplies enter, exit, tap-down, tap and tap-cancel. Tap-down
///     fires when the region *wins* the pointer, which is the instant press
///     feedback is correct to show.
///   - hover and press are `State`, because they belong to the button and to
///     nothing else. What the button *does* is the caller's, passed in as a
///     `Callback` -- a controlled component, like every other one here.
///   - the two looks are interpolated by `AnimatedDecoration` and
///     `AnimatedTransform`, which hand the animated value to a render object
///     rather than rebuilding: a frame of hover is one repaint, no layout and
///     no build. Moving the cursor in and out faster than the animation settles
///     stays continuous, because re-targeting re-bases the interval from the
///     value currently on screen.
///
/// The `RepaintBoundary` around it is what makes "hovering one button repaints
/// one boundary" true rather than approximately true.
class Button final : public fltr::Configure<Button, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    std::string_view label;
    fltr::Callback<void()> onPressed;
    bool enabled = true;
    /// Held-on look, for a tab or a toggle that reuses this button.
    bool selected = false;
    ButtonKind kind = ButtonKind::Menu;
    /// 0 means "as wide as the label needs".
    float width = 0.0f;
  };

  explicit Button(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Button"; }

  std::string_view label() const noexcept { return args_.label; }
  const fltr::Callback<void()>& onPressed() const noexcept { return args_.onPressed; }
  bool enabled() const noexcept { return args_.enabled && static_cast<bool>(args_.onPressed); }
  bool selected() const noexcept { return args_.selected; }
  ButtonKind kind() const noexcept { return args_.kind; }
  float width() const noexcept { return args_.width; }

  std::unique_ptr<fltr::State<Button>> createState() const;

private:
  Args args_;
};

}  // namespace fltrdemo
