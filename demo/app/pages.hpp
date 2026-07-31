#pragma once

#include "app/app_state.hpp"
#include "fltr/widgets/framework.hpp"

namespace fltrdemo {

/// The three screens. Free functions rather than widgets, because each one is a
/// composition and not a thing with state or identity of its own -- what state
/// they have lives in AppState, and what state belongs to a control lives in
/// that control.
fltr::WidgetRef buildTitlePage(fltr::BuildContext& context, AppState& app);
fltr::WidgetRef buildSettingsPage(fltr::BuildContext& context, AppState& app);
fltr::WidgetRef buildServersPage(fltr::BuildContext& context, AppState& app);

}  // namespace fltrdemo
