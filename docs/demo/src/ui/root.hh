#pragma once

#include "fltr/widgets/framework.hpp"

namespace demo {

class App;

/// The whole widget tree, built once. Everything that changes afterwards changes
/// through an observable, not by re-running this.
fltr::WidgetRef buildRoot(App& app);

}  // namespace demo
