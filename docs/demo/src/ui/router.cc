#include "ui/router.hh"

#include "fltr/widgets/basic.hpp"

namespace demo {

using fltr::BuildContext;
using fltr::Opacity;
using fltr::Transform2D;
using fltr::WidgetRef;

namespace {

/// Fully present sits at rest; on its way in or out it is pushed down. The same
/// curve therefore reads as "rises into place" and "sinks away".
Transform2D rise(float presence, float distance) noexcept {
  return Transform2D::translation({0.0f, (1.0f - presence) * distance});
}

}  // namespace

void PresenceSlideState::dispose() { offset_.release(); }

WidgetRef PresenceSlideState::build(BuildContext&) {
  offset_.bind(widget().args().presence, &rise, widget().args().distance);

  return Opacity::make({
      // The presence value goes straight to the render object: no rebuild, and
      // no snapshot of it stored in a widget configuration.
      .animation = widget().args().presence,
      .child = fltr::Transform::make({
          .animation = &offset_,
          .child = widget().args().child,
      }),
  });
}

WidgetRef slidePresence(WidgetRef child, fltr::ValueListenable<float>& presence) {
  return PresenceSlide::make({.presence = &presence, .child = child});
}

}  // namespace demo
