#pragma once

#include <memory>
#include <string_view>

#include "fltr/components/toggle.hpp"
#include "fltr/widgets/framework.hpp"
#include "ui/theme.hh"
#include "ui/widgets/mapped_value.hh"

namespace demo {

class SwitchThumb;

class SwitchThumbState final : public fltr::State<SwitchThumb> {
public:
  void dispose() override;
  fltr::WidgetRef build(fltr::BuildContext& context) override;

private:
  /// The component publishes how far along it is as a 0..1 fraction; this turns
  /// that into a translation the `Transform` render object observes directly.
  /// Dragging the thumb therefore repaints one node and rebuilds nothing.
  MappedValue<fltr::Transform2D> travel_;
};

/// The moving part of a switch. Reads the enclosing component's fraction, so it
/// works inside a `RawToggle` and shows an off switch anywhere else.
class SwitchThumb final : public fltr::Configure<SwitchThumb, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    float travel = 16.0f;
    float diameter = 16.0f;
    fltr::Color color;
  };

  explicit SwitchThumb(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "SwitchThumb"; }
  const Args& args() const noexcept { return args_; }

  std::unique_ptr<fltr::State<SwitchThumb>> createState() const {
    return std::make_unique<SwitchThumbState>();
  }

private:
  Args args_;
};

struct ToggleRowSpec {
  fltr::Key key;
  std::string_view label;
  std::string_view hint;
  bool value = false;
  /// fltr's own signature, passed straight through: wrapping it in a
  /// `Callback<void(bool)>` would nest one 32-byte inline closure inside
  /// another, which `Callback` static-asserts against.
  fltr::Callback<void(fltr::ToggleValue)> onChanged;
  bool enabled = true;
  bool autofocus = false;
};

/// A labelled switch: `RawToggle` for the behaviour, a track and a thumb for the
/// look, and nothing shared between the two but the ambient component scope.
fltr::WidgetRef toggleRow(const Theme& theme, const ToggleRowSpec& spec);

}  // namespace demo
