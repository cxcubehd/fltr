#pragma once

#include "fltr/widgets/framework.hpp"

namespace demo {

class App;

/// What the overlay entry builds. It is inserted for the whole of the gameplay
/// screen and decides for itself whether anything is on screen, which is what
/// lets the panel animate *out* instead of vanishing when it is dismissed.
fltr::WidgetRef pauseOverlay(App& app, fltr::BuildContext& context);

}  // namespace demo
