#pragma once

#include "fltr/widgets/binding.hpp"
#include "ui/drag.hpp"

namespace fltrdemo {

/// raylib's polled input, turned into the events the framework expects.
///
/// Pointer events go in as they are discovered rather than being batched into
/// the frame: `dispatchPointer` is deliberately unrelated to `drawFrame`, and a
/// press that arrives between two frames is delivered between two frames.
///
/// The one rule that matters for the idle-frame claim: a pointer that did not
/// move produces no event. Re-sending the cursor's position every frame would
/// hit test every frame, and the debug overlay would never read zero.
class InputPump {
public:
  /// Everything that happened since the last call.
  void pump(fltr::WidgetBinding& binding, PointerRouter& router, float scrollLineHeight);

  /// Pushes the window size in when raylib reports it changed.
  void syncSurface(fltr::WidgetBinding& binding);

private:
  fltr::Offset last_;
  bool hasPosition_ = false;
  bool pressed_ = false;
  bool focused_ = true;
};

}  // namespace fltrdemo
