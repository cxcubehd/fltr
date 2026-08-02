#pragma once

#include "fltr/widgets/framework.hpp"
#include "ui/theme.hh"

namespace demo {

class App;

fltr::WidgetRef hudScreen(App& app, const Theme& theme);

}  // namespace demo
