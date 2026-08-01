#pragma once

#include "fltr/components/states.hpp"
#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/focus.hpp"

namespace fltr {

/// The envelope every interactive component wraps its visuals in: one focus
/// node, one pointer region, one publication of what the component is doing, and
/// the guard that makes `enabled = false` mean something. Only the gestures in
/// the middle differ between components, so everything around them is described
/// once, here.
///
/// The guard is always present, whatever `enabled` says. Inserting it only when
/// disabled would change the shape of the tree at the moment a control is greyed
/// out, and everything below -- the consumer's visuals, and any State they hold
/// -- would be inflated afresh for a reason that has nothing to do with them.
struct ComponentShell {
  OwnedStates* states = nullptr;
  ValueListenable<float>* fraction = nullptr;
  bool enabled = true;

  FocusNode* focusNode = nullptr;
  bool canRequestFocus = true;
  bool skipTraversal = false;
  bool autofocus = false;
  Callback<bool(const KeyEvent&)> onKey;
  Callback<void(bool)> onFocusChange;

  /// The component's own region: its gestures, its cursor, its child. Hover is
  /// filled in here, because every component reports it the same way.
  Pointer::Args pointer;
};

WidgetRef buildComponentShell(const ComponentShell& shell);

}  // namespace fltr
