#include "fltr/components/component.hpp"

namespace fltr {

WidgetRef buildComponentShell(const ComponentShell& shell) {
  OwnedStates* states = shell.states;
  FLTR_EXPECTS(states != nullptr, "a component shell needs the states its component owns");

  FLTR_EXPECTS(!shell.pointer.onEnter && !shell.pointer.onExit,
               "hover belongs to the shell; a component's own enter and exit would be dropped");
  Pointer::Args pointer = shell.pointer;
  pointer.onEnter = [states] { states->update(WidgetState::Hovered, true); };
  pointer.onExit = [states] { states->update(WidgetState::Hovered, false); };

  return ComponentScope::make({
      .states = states->get(),
      .fraction = shell.fraction,
      .child = Focus::make({
          .node = shell.focusNode,
          // Disabled is not a separate axis in the focus tree: a control that
          // cannot be used is a control the keyboard should walk straight past,
          // and `canRequestFocus` already means exactly that.
          .canRequestFocus = shell.enabled && shell.canRequestFocus,
          .skipTraversal = shell.skipTraversal,
          .autofocus = shell.autofocus,
          .onKey = shell.onKey,
          .onFocusChange = shell.onFocusChange,
          .child = AbsorbPointer::make({
              .absorbing = !shell.enabled,
              .child = Pointer::make(pointer),
          }),
      }),
  });
}

}  // namespace fltr
