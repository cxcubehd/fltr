#include "ui/controls.hpp"

#include <algorithm>

#include "fltr/widgets/animated.hpp"
#include "fltr/widgets/basic.hpp"
#include "ui/button.hpp"
#include "ui/drag.hpp"
#include "ui/layout.hpp"
#include "ui/theme.hpp"

namespace fltrdemo {

using namespace fltr;

namespace {

constexpr AnimationDriver::Config kSwitch{.duration = 0.16f, .curve = Curves::easeOutCubic};

// ---------------------------------------------------------------------------
// Toggle
// ---------------------------------------------------------------------------

class ToggleState final : public State<Toggle> {
public:
  WidgetRef build(BuildContext& context) override {
    const Theme& theme = themeOf(context);
    const bool on = widget().value();
    const float trackWidth = theme.unit() * 5.5f;
    const float trackHeight = theme.unit() * 2.75f;
    const float inset = theme.unit() * 0.25f;

    WidgetRef knob = SizedBox::make({
        .size = {trackHeight - inset * 2.0f, trackHeight - inset * 2.0f},
        .child = DecoratedBox::make({
            .decoration = {.color = on ? theme.accent : theme.borderBright,
                           .radius = BorderRadius::all(theme.radius())},
        }),
    });

    WidgetRef track = AnimatedDecoration::make({
        .decoration = {.color = on ? theme.accentDim : theme.surfaceSunken,
                       .radius = BorderRadius::all(theme.radius() * 2.0f),
                       .borderColor = hovered_ ? theme.accent : theme.border,
                       .borderWidth = theme.hairline()},
        .animation = kSwitch,
        .child = SizedBox::make({
            .size = {trackWidth, trackHeight},
            .child = Padding::make({
                .padding = EdgeInsets::all(inset),
                .child = AnimatedAlign::make({
                    .alignment = on ? Alignment::centerRight() : Alignment::centerLeft(),
                    .animation = kSwitch,
                    .child = knob,
                }),
            }),
        }),
    });

    return RepaintBoundary::make({
        .child = Pointer::make({
            .behavior = HitTestBehavior::Opaque,
            .onEnter = Callback<void()>([this] { setHovered(true); }),
            .onExit = Callback<void()>([this] { setHovered(false); }),
            .onTap = Callback<void()>([this] { widget().onChanged()(!widget().value()); }),
            .child = track,
        }),
    });
  }

private:
  void setHovered(bool hovered) {
    if (hovered_ == hovered) return;
    setState([&] { hovered_ = hovered; });
  }

  bool hovered_ = false;
};

// ---------------------------------------------------------------------------
// TabStrip
// ---------------------------------------------------------------------------

class TabStripState final : public State<TabStrip> {
public:
  WidgetRef build(BuildContext& context) override {
    const Theme& theme = themeOf(context);
    const int selected = widget().selected();

    return Column::make({
        .crossAxisAlignment = CrossAxisAlignment::Stretch,
        .mainAxisSize = MainAxisSize::Min,
        .children =
            {
                Row::make({
                    .mainAxisSize = MainAxisSize::Min,
                    // One tab per entry in the caller's array: the length is
                    // data, so the list is generated.
                    .children = WidgetList::generate(
                        widget().count(),
                        [this, &theme, selected](std::size_t i) {
                          return Button::make({
                              .key = Key::of(static_cast<int>(i)),
                              .label = widget().labels()[i],
                              .onPressed =
                                  Callback<void()>([this, i] { choose(static_cast<int>(i)); }),
                              .selected = static_cast<int>(i) == selected,
                              .kind = ButtonKind::Tab,
                              .width = theme.tabWidth(),
                          });
                        }),
                }),
                divider(theme),
            },
    });
  }

private:
  void choose(int index) {
    if (index == widget().selected()) return;
    widget().onSelected()(index);
  }
};

// ---------------------------------------------------------------------------
// Slider
// ---------------------------------------------------------------------------

class SliderState final : public State<Slider> {
public:
  WidgetRef build(BuildContext& context) override {
    const Theme& theme = themeOf(context);
    const float width = theme.sliderWidth();
    const float knob = theme.knobWidth();
    const float travel = std::max(1.0f, width - knob);
    const float fraction = fractionOf(widget().value());
    const float barHeight = theme.unit() * 0.5f;
    const bool lit = hovered_ || dragging_;

    // The knob's alignment and the value the pointer maps to are the same
    // formula read in opposite directions, which is what keeps the knob exactly
    // under the cursor at both ends of the track.
    WidgetRef body = Stack::make({
        .fit = StackFit::Expand,
        .children =
            {
                Align::make({
                    .alignment = Alignment::centerLeft(),
                    .child = SizedBox::make({
                        .size = {width, barHeight},
                        .child = DecoratedBox::make({
                            .decoration = {.color = theme.surfaceSunken,
                                           .radius = BorderRadius::all(barHeight * 0.5f),
                                           .borderColor = theme.border,
                                           .borderWidth = theme.hairline()},
                        }),
                    }),
                }),
                Align::make({
                    .alignment = Alignment::centerLeft(),
                    .child = SizedBox::make({
                        .size = {knob * 0.5f + fraction * travel, barHeight},
                        .child = DecoratedBox::make({
                            .decoration = {.color = lit ? theme.accent : theme.accentDim,
                                           .radius = BorderRadius::all(barHeight * 0.5f)},
                        }),
                    }),
                }),
                Align::make({
                    .alignment = Alignment{-1.0f + 2.0f * fraction, 0.0f},
                    .child = SizedBox::make({
                        .size = {knob, theme.unit() * 2.25f},
                        .child = DecoratedBox::make({
                            .decoration = {.color = lit ? theme.textBright : theme.text,
                                           .radius = BorderRadius::all(theme.radius()),
                                           .borderColor = theme.background,
                                           .borderWidth = theme.hairline()},
                        }),
                    }),
                }),
            },
    });

    return RepaintBoundary::make({
        .child = DragTarget::make({
            .router = &widget().router(),
            .onDragStart = Callback<void(const DragDetails&)>(
                [this](const DragDetails& drag) { begin(drag); }),
            .onDragUpdate = Callback<void(const DragDetails&)>(
                [this](const DragDetails& drag) { moveTo(drag); }),
            .onDragEnd = Callback<void()>([this] { end(); }),
            .child = Pointer::make({
                .behavior = HitTestBehavior::Opaque,
                .onEnter = Callback<void()>([this] { setHovered(true); }),
                .onExit = Callback<void()>([this] { setHovered(false); }),
                .child = SizedBox::make({.size = {width, theme.unit() * 3.0f}, .child = body}),
            }),
        }),
    });
  }

private:
  float fractionOf(float value) const noexcept {
    return std::clamp((value - widget().min()) / (widget().max() - widget().min()), 0.0f, 1.0f);
  }

  void begin(const DragDetails& drag) {
    setState([&] { dragging_ = true; });
    moveTo(drag);
  }
  void end() {
    if (!dragging_) return;
    setState([&] { dragging_ = false; });
  }

  void moveTo(const DragDetails& drag) {
    // The knob's centre follows the pointer, so the usable travel is the track
    // minus the knob -- the same travel the alignment above resolves over.
    const float knob = drag.regionSize.height * 0.5f;
    const float travel = std::max(1.0f, drag.regionSize.width - knob);
    const float fraction = std::clamp((drag.localPosition.dx - knob * 0.5f) / travel, 0.0f, 1.0f);
    widget().onChanged()(widget().min() + fraction * (widget().max() - widget().min()));
  }

  void setHovered(bool hovered) {
    if (hovered_ == hovered) return;
    setState([&] { hovered_ = hovered; });
  }

  bool hovered_ = false;
  bool dragging_ = false;
};

}  // namespace

std::unique_ptr<State<Toggle>> Toggle::createState() const {
  return std::make_unique<ToggleState>();
}

std::unique_ptr<State<TabStrip>> TabStrip::createState() const {
  return std::make_unique<TabStripState>();
}

std::unique_ptr<State<Slider>> Slider::createState() const {
  return std::make_unique<SliderState>();
}

}  // namespace fltrdemo
