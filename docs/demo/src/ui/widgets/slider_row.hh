#pragma once

#include <memory>
#include <string_view>

#include "fltr/components/slider.hpp"
#include "fltr/widgets/framework.hpp"
#include "ui/theme.hh"
#include "ui/widgets/mapped_value.hh"

namespace demo {

class SliderTrack;

class SliderTrackState final : public fltr::State<SliderTrack> {
public:
  void dispose() override;
  fltr::WidgetRef build(fltr::BuildContext& context) override;

private:
  /// Two views of the same published fraction: how far the fill has grown and
  /// how far the thumb has moved. Both are paint-only, so dragging the slider
  /// repaints two render objects and rebuilds nothing.
  MappedValue<fltr::Transform2D> fill_;
  MappedValue<fltr::Transform2D> thumb_;
};

/// A fixed-width slider track. Fixed because the thumb's travel has to be known
/// when the mapping is set up, and fltr has no `LayoutBuilder` to ask the parent
/// how wide it ended up -- a real gap, recorded on the gaps page.
class SliderTrack final : public fltr::Configure<SliderTrack, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    float width = 180.0f;
    float thumbSize = 14.0f;
    fltr::Color fill;
    fltr::Color track;
    fltr::Color thumb;
  };

  explicit SliderTrack(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "SliderTrack"; }
  const Args& args() const noexcept { return args_; }

  std::unique_ptr<fltr::State<SliderTrack>> createState() const {
    return std::make_unique<SliderTrackState>();
  }

private:
  Args args_;
};

struct SliderRowSpec {
  fltr::Key key;
  std::string_view label;
  /// Already formatted by the caller, whose buffer outlives the build.
  std::string_view valueText;
  float value = 0.0f;
  float min = 0.0f;
  float max = 1.0f;
  int divisions = 0;
  fltr::Callback<void(float)> onChanged;
  bool enabled = true;
};

fltr::WidgetRef sliderRow(const Theme& theme, const SliderRowSpec& spec);

}  // namespace demo
