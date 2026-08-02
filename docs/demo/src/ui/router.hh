#pragma once

#include <cstdint>
#include <memory>

#include "fltr/widgets/framework.hpp"
#include "ui/widgets/mapped_value.hh"

namespace demo {

/// fltr has no navigator, no routes and no screen stack, deliberately: a game
/// engine already has one. This is the whole router -- an enum in an
/// `Observable`, watched at the top of the tree.
enum class Screen : std::uint8_t { MainMenu, LevelSelect, Settings, Playing };

class PresenceSlide;

class PresenceSlideState final : public fltr::State<PresenceSlide> {
public:
  void dispose() override;
  fltr::WidgetRef build(fltr::BuildContext& context) override;

private:
  MappedValue<fltr::Transform2D> offset_;
};

/// The in and out halves of a page transition.
///
/// `AnimatedSwitcher` hands a subtree a 0..1 "how present am I" value and keeps
/// the outgoing subtree mounted, ticking and *never rebuilt* until it reaches
/// zero. Both properties here are render-attached, so an entire page slides and
/// fades for the cost of two repaints per frame and no builds at all.
class PresenceSlide final : public fltr::Configure<PresenceSlide, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    /// Owned by the switcher; outlives this subtree by construction, because the
    /// subtree is what the switcher is animating.
    fltr::ValueListenable<float>* presence = nullptr;
    float distance = 18.0f;
    fltr::WidgetRef child;
  };

  explicit PresenceSlide(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "PresenceSlide"; }
  const Args& args() const noexcept { return args_; }

  std::unique_ptr<fltr::State<PresenceSlide>> createState() const {
    return std::make_unique<PresenceSlideState>();
  }

private:
  Args args_;
};

/// The `PresenceTransition` the switcher is configured with. A plain function,
/// because that is what the callback slot holds; the state it needs lives in the
/// widget it returns.
fltr::WidgetRef slidePresence(fltr::WidgetRef child, fltr::ValueListenable<float>& presence);

}  // namespace demo
