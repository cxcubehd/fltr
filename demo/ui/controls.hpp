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
