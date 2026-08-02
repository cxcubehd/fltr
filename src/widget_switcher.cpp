#include "fltr/widgets/switcher.hpp"

#include <algorithm>

#include "fltr/widgets/animated.hpp"
#include "fltr/widgets/focus.hpp"

namespace fltr {

WidgetRef fadePresence(WidgetRef child, ValueListenable<float>& presence) {
  return Opacity::make({.animation = &presence, .child = child});
}

// ---------------------------------------------------------------------------
// Presence
// ---------------------------------------------------------------------------

AnimatedSwitcherState::Presence::Presence(AnimatedSwitcherState& state, WidgetRef shown,
                                          Key identity, AnimationDriver::Config config)
    : child(shown), key(identity), driver(config), owner(&state) {
  subscribeMember<Presence, &Presence::onStatus>(driver.statusChanges(), settled, this);
}

void AnimatedSwitcherState::Presence::onStatus() {
  if (!leaving || driver.status() != AnimationStatus::Dismissed) return;
  finished = true;
  // Let go of it from the build rather than from here: this runs while the
  // driver is notifying, and the subtree is still mounted around this value.
  if (owner->mounted()) owner->setState([] {});
}

// ---------------------------------------------------------------------------
// AnimatedSwitcherState
// ---------------------------------------------------------------------------

void AnimatedSwitcherState::initState() {
  // The first child is simply there. A panel does not fade in because the screen
  // it sits on appeared.
  if (widget().child()) add(widget().child(), false);
}

void AnimatedSwitcherState::didUpdateWidget(const AnimatedSwitcher&) {
  for (const auto& presence : live_) presence->driver.setConfig(widget().animation());

  const WidgetRef next = widget().child();
  Presence* current = showing();
  if (current && next && next.canUpdate(current->child)) {
    // The same subtree, newly configured: nothing to animate, and the fresh ref
    // is what lets reconciliation reach it.
    current->child = next;
    return;
  }
  if (current) {
    current->leaving = true;
    current->driver.reverse();
  }
  if (!next || revive(next)) return;
  add(next, true);
}

void AnimatedSwitcherState::dispose() {
  for (const auto& presence : live_) presence->driver.detach();
}

WidgetRef AnimatedSwitcherState::build(BuildContext& context) {
  // Safe here and only here: what a dropped presence leaves behind is a subtree
  // this same build is about to discard, and no paint or hit test runs between.
  dropFinished();

  const AnimatedSwitcher& config = widget();
  const bool muted = !TickerMode::of(context);
  for (const auto& presence : live_) presence->driver.setMuted(muted);

  return Stack::make({
      .alignment = config.alignment(),
      .children = WidgetList::generate(
          live_.size(),
          [this, &config](std::size_t i) {
            Presence& presence = *live_[i];
            return Focus::make({
                .key = presence.key,
                .canRequestFocus = false,
                .skipTraversal = true,
                .descendantsAreFocusable = !presence.leaving,
                .ensureVisible = false,
                .child = IgnorePointer::make({
                    .ignoring = presence.leaving,
                    .child = config.transition()(presence.child, presence.value),
                }),
            });
          }),
  });
}

AnimatedSwitcherState::Presence* AnimatedSwitcherState::showing() noexcept {
  if (live_.empty()) return nullptr;
  Presence& last = *live_.back();
  return last.leaving ? nullptr : &last;
}

bool AnimatedSwitcherState::revive(WidgetRef child) {
  const auto match = std::find_if(live_.begin(), live_.end(), [child](const auto& presence) {
    return presence->leaving && child.canUpdate(presence->child);
  });
  if (match == live_.end()) return false;

  // Coming back to something still on its way out re-enters that subtree from
  // wherever it had faded to, rather than building a second one beside it.
  (*match)->child = child;
  (*match)->leaving = false;
  (*match)->finished = false;
  (*match)->driver.forward();
  // Whatever is being shown is last, so that it is on top and `showing` finds it.
  std::rotate(match, match + 1, live_.end());
  return true;
}

void AnimatedSwitcherState::add(WidgetRef child, bool animate) {
  auto presence =
      std::make_unique<Presence>(*this, child, Key::of(++nextKey_), widget().animation());
  presence->driver.attach(context().tickers());
  if (animate) {
    presence->driver.forward();
  } else {
    presence->driver.jumpTo(1.0f);
  }
  live_.push_back(std::move(presence));
}

void AnimatedSwitcherState::dropFinished() {
  std::erase_if(live_, [](const auto& presence) { return presence->finished; });
}

}  // namespace fltr
