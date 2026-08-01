#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "fltr/animation/tween.hpp"
#include "fltr/core/callback.hpp"
#include "fltr/widgets/framework.hpp"

namespace fltr {

class AnimatedSwitcher;

/// How a subtree is animated in and out, given the subtree and how present it
/// is: 0 gone, 1 fully there.
using PresenceTransition = Callback<WidgetRef(WidgetRef, ValueListenable<float>&)>;

/// Fades a subtree in and out, which is what a switcher does unless told
/// otherwise. The value reaches a render object, so a frame of it repaints one
/// object and rebuilds nothing.
WidgetRef fadePresence(WidgetRef child, ValueListenable<float>& presence);

class AnimatedSwitcherState final : public State<AnimatedSwitcher> {
public:
  void initState() override;
  void didUpdateWidget(const AnimatedSwitcher& previous) override;
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

  /// Subtrees currently mounted: the one being shown, plus every one still on
  /// its way out.
  std::size_t liveCount() const noexcept { return live_.size(); }

private:
  /// One mounted subtree and the animation deciding how present it is.
  ///
  /// Its address is stable and it is never moved: the value is handed to a
  /// render object, and the status subscription names this object.
  struct Presence {
    Presence(AnimatedSwitcherState& owner, WidgetRef child, Key key,
             AnimationDriver::Config config);

    void onStatus();

    /// Re-emitted every build. After the build that produced it ends this ref is
    /// stale, and re-emitting it is exactly how the subtree is retained:
    /// reconciliation matches it by type and key and leaves the subtree alone.
    WidgetRef child;
    Key key;
    AnimationDriver driver;
    AnimatedValue<float> value{driver, {0.0f, 1.0f}};
    Subscription settled;
    AnimatedSwitcherState* owner;
    bool leaving = false;
    bool finished = false;
  };

  Presence* showing() noexcept;
  bool revive(WidgetRef child);
  void add(WidgetRef child, bool animate);
  void dropFinished();

  std::vector<std::unique_ptr<Presence>> live_;
  std::int64_t nextKey_ = 0;
};

/// Swaps its child for another, animating the old one out and the new one in.
///
/// The outgoing subtree is *retained*: it stays mounted, keeps its State and its
/// render objects, keeps ticking, and is discarded only once its animation has
/// finished. This is the one thing `INSTRUCTIONS.md` warned about by name, and
/// it turned out to need no change to reconciliation -- a widget ref outlives
/// the arena that made it, and re-emitting a stale one already means "this
/// subtree is unchanged, skip it".
///
/// Two children are the same child when their type and key match, which is the
/// same rule reconciliation uses everywhere else. Two panels of the same type
/// therefore need keys to swap rather than update in place.
///
/// A subtree on its way out is inert: it takes no pointer input and nothing in
/// it can hold the focus, so a control cannot be pressed or tabbed into while it
/// disappears. Both guards are always present and only their flag moves, which
/// is what keeps the subtree from being inflated afresh at the moment it leaves.
class AnimatedSwitcher final : public Configure<AnimatedSwitcher, StatefulWidget> {
public:
  struct Args {
    Key key;
    /// Null is a child too: it animates whatever is there out and leaves
    /// nothing, which is how a panel is dismissed.
    WidgetRef child;
    AnimationDriver::Config animation;
    PresenceTransition transition = &fadePresence;
    /// How the outgoing and incoming subtrees are aligned while both exist.
    Alignment alignment = Alignment::center();
  };

  explicit AnimatedSwitcher(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(static_cast<bool>(args.transition), "a switcher needs a transition");
  }

  const char* name() const noexcept override { return "AnimatedSwitcher"; }
  WidgetRef child() const noexcept { return args_.child; }
  const AnimationDriver::Config& animation() const noexcept { return args_.animation; }
  const PresenceTransition& transition() const noexcept { return args_.transition; }
  Alignment alignment() const noexcept { return args_.alignment; }

  std::unique_ptr<State<AnimatedSwitcher>> createState() const {
    return std::make_unique<AnimatedSwitcherState>();
  }

private:
  Args args_;
};

}  // namespace fltr
