#pragma once

#include <memory>

#include "fltr/components/component.hpp"

namespace fltr {

class RawSlider;

class RawSliderState final : public State<RawSlider> {
public:
  void initState() override;
  void didUpdateWidget(const RawSlider& previous) override;
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

private:
  void adoptConfiguration();
  bool handleKey(const KeyEvent& event);
  void handleFocusChange(bool focused);
  void moveTo(Offset local);
  void nudge(float byFraction);
  void report(float value);

  /// The distance the thumb's centre can actually travel: the box less the thumb
  /// it has to keep inside. Zero before the first layout.
  float trackExtent() const;
  float fractionOf(float value) const noexcept;
  float valueOf(float fraction) const noexcept;
  float keyStep() const noexcept;

  OwnedStates states_;
  Observable<float> fraction_;
};

/// A value dragged along an axis.
///
/// DIVERGENCE from Flutter's `Slider`, which tracks a thumb by accumulating drag
/// deltas: the value here is mapped absolutely from where the pointer is, so the
/// thumb goes to the finger on the first frame and cannot drift away from it
/// over a long drag. The consequence, stated plainly: there is no separate thumb
/// hit region, because pressing anywhere on the track is already a press on the
/// thumb.
///
/// The value belongs to the consumer. This reports where a gesture or a key
/// would put it and assumes nothing about the answer, which is what lets the
/// same slider be clamped, snapped or ignored by the code that owns the value.
class RawSlider final : public Configure<RawSlider, StatefulWidget> {
public:
  struct Args {
    Key key;
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    /// Snap to this many equal intervals -- `divisions = 4` gives five stops.
    /// Zero is continuous.
    int divisions = 0;
    Callback<void(float)> onChanged;
    Callback<void(float)> onChangeStart;
    Callback<void(float)> onChangeEnd;

    /// Which way the value runs -- and it runs the way the coordinate space
    /// does, so a vertical slider's minimum is at the *top*: pressing lower sets
    /// a higher value, and ArrowUp lowers it. A consumer wanting a volume
    /// column, with its maximum at the top, inverts in their own value mapping,
    /// which is one subtraction and keeps this from growing a flag whose whole
    /// job is to negate a number.
    Axis axis = Axis::Horizontal;
    /// How wide the thumb is, so the ends of the range stay reachable: the value
    /// maps across the box less this. Flutter's `_trackRect`, as one number.
    float thumbExtent = 0.0f;
    /// What one arrow key moves, as a fraction of the range. Ignored when there
    /// are divisions, which move by exactly one.
    float keyStep = 0.1f;
    float pageStep = 0.2f;

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

  explicit RawSlider(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "RawSlider"; }
  const Args& args() const noexcept { return args_; }

  std::unique_ptr<State<RawSlider>> createState() const {
    return std::make_unique<RawSliderState>();
  }

private:
  Args args_;
};

}  // namespace fltr
