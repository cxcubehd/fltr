#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

#include "fltr/core/callback.hpp"
#include "fltr/widgets/framework.hpp"

namespace fltrdemo {

class PointerRouter;

/// A controlled boolean, animated between its two looks.
///
/// The knob is placed by `AnimatedAlign`, which is the one animated property
/// that is not free: alignment resolves during layout, so every frame of the
/// knob's travel lays out the track's subtree. That subtree is two boxes inside
/// a fixed-size track, so the layout stops at the track's tight constraints --
/// which is the point. Choosing the expensive path deliberately, in a place
/// where it is bounded, is different from choosing it by accident.
class Toggle final : public fltr::Configure<Toggle, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    bool value = false;
    fltr::Callback<void(bool)> onChanged;
  };

  explicit Toggle(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Toggle"; }
  bool value() const noexcept { return args_.value; }
  const fltr::Callback<void(bool)>& onChanged() const noexcept { return args_.onChanged; }

  std::unique_ptr<fltr::State<Toggle>> createState() const;

private:
  Args args_;
};

/// A row of tabs, one selected.
///
/// Stateful for one reason, and it is the rule every control here follows: a
/// callback stored in a render object outlives the build that made it, so it
/// may only capture something that outlives a build. A `State` does; a widget,
/// which lives in the arena for one build phase, does not. So the per-tab
/// callback captures this `State` and an index, and reads the current
/// configuration when it fires.
class TabStrip final : public fltr::Configure<TabStrip, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    /// Storage the caller keeps alive: `Text` does not copy what it is given.
    const std::string_view* labels = nullptr;
    std::size_t count = 0;
    int selected = 0;
    fltr::Callback<void(int)> onSelected;
  };

  explicit TabStrip(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(args.count > 0, "a TabStrip needs tabs");
  }

  const char* name() const noexcept override { return "TabStrip"; }
  const std::string_view* labels() const noexcept { return args_.labels; }
  std::size_t count() const noexcept { return args_.count; }
  int selected() const noexcept { return args_.selected; }
  const fltr::Callback<void(int)>& onSelected() const noexcept { return args_.onSelected; }

  std::unique_ptr<fltr::State<TabStrip>> createState() const;

private:
  Args args_;
};

/// A continuous value from a drag.
///
/// Controlled: the parent owns the value and is told when the pointer moves it.
/// The drag itself comes from the demo's own router rather than from the
/// gesture arena -- see `ui/drag.hpp` for what that costs.
class Slider final : public fltr::Configure<Slider, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    fltr::Callback<void(float)> onChanged;
    PointerRouter* router = nullptr;
  };

  explicit Slider(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(args.router != nullptr, "a Slider needs a router to drag with");
    FLTR_EXPECTS(args.max > args.min, "a Slider needs a non-empty range");
  }

  const char* name() const noexcept override { return "Slider"; }
  float value() const noexcept { return args_.value; }
  float min() const noexcept { return args_.min; }
  float max() const noexcept { return args_.max; }
  PointerRouter& router() const noexcept { return *args_.router; }
  const fltr::Callback<void(float)>& onChanged() const noexcept { return args_.onChanged; }

  std::unique_ptr<fltr::State<Slider>> createState() const;

private:
  Args args_;
};

}  // namespace fltrdemo
