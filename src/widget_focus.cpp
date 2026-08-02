#include "fltr/widgets/focus.hpp"

#include "fltr/widgets/scroll.hpp"

namespace fltr {

// ---------------------------------------------------------------------------
// FocusScope
// ---------------------------------------------------------------------------

void FocusScopeState::dispose() {
  host_.release();
  manager_.reset();
}

WidgetRef FocusScopeState::build(BuildContext& context) {
  host_.bind<FocusScopeState, &FocusScopeState::didChangeFocus>(widget().node(), this);
  FocusScopeNode* node = host_.get();
  node->setElement(&context.element());
  node->setTabTraversal(widget().tabTraversal());
  node->setDirectionalTraversal(widget().directionalTraversal());

  if (manager_ && manager_->rootScope() != node) manager_.reset();
  if (FocusNode* parent = FocusMarker::of(context)) {
    // A scope that turns out to be nested draws on the tree it found rather
    // than starting one of its own.
    manager_.reset();
    node->attach(*parent);
  } else if (!manager_) {
    manager_ = std::make_unique<FocusManager>(context.keyboard(), *node);
  }
  return FocusMarker::make({.node = node, .child = widget().child()});
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------

void FocusState::dispose() { host_.release(); }

WidgetRef FocusState::build(BuildContext& context) {
  host_.bind<FocusState, &FocusState::didChangeFocus>(widget().args().node, this);
  FocusNode* node = host_.get();
  const Focus::Args& args = widget().args();
  node->setElement(&context.element());
  node->setCanRequestFocus(args.canRequestFocus);
  node->setSkipTraversal(args.skipTraversal);
  node->setDescendantsAreFocusable(args.descendantsAreFocusable);
  node->onKey = args.onKey;
  scroll_ = args.ensureVisible ? ScrollScope::of(context) : nullptr;

  FocusNode* parent = FocusMarker::of(context);
  if (!parent) {
    node->detach();
    return widget().child();
  }
  node->attach(*parent);

  if (args.autofocus && !autofocused_) {
    autofocused_ = true;
    node->autofocus();
  }
  return FocusMarker::make({.node = node, .child = widget().child()});
}

void FocusState::didChangeFocus() {
  if (!mounted()) return;
  FocusNode* node = host_.get();
  const bool focused = node != nullptr && node->hasFocus();
  if (focused == focused_) return;
  focused_ = focused;
  if (widget().args().onFocusChange) widget().args().onFocusChange(focused);

  if (!focused || !scroll_) return;
  if (const RenderBox* box = context().element().renderObject()) {
    scroll_->revealMinimally(*box, widget().args().ensureVisibleDuration);
  }
}

// ---------------------------------------------------------------------------
// Shortcuts
// ---------------------------------------------------------------------------

ShortcutList::ShortcutList(std::initializer_list<Shortcut> shortcuts) {
  Arena* arena = currentBuildArena();
  FLTR_EXPECTS(arena != nullptr, "shortcut lists may only be created during a build scope");
  Shortcut* items = static_cast<Shortcut*>(
      arena->allocate(sizeof(Shortcut) * shortcuts.size(), alignof(Shortcut)));
  std::size_t kept = 0;
  for (const Shortcut& shortcut : shortcuts) items[kept++] = shortcut;
  items_ = items;
  size_ = kept;
}

void ShortcutsState::initState() { adoptShortcuts(); }

void ShortcutsState::didUpdateWidget(const Shortcuts&) { adoptShortcuts(); }

void ShortcutsState::adoptShortcuts() {
  shortcuts_.assign(widget().shortcuts().begin(), widget().shortcuts().end());
}

WidgetRef ShortcutsState::build(BuildContext&) {
  return Focus::make({
      .canRequestFocus = false,
      .skipTraversal = true,
      .ensureVisible = false,
      .onKey = [this](const KeyEvent& event) { return invoke(event); },
      .child = widget().child(),
  });
}

bool ShortcutsState::invoke(const KeyEvent& event) const {
  if (event.type == KeyEventType::Up) return false;
  for (const Shortcut& shortcut : shortcuts_) {
    if (shortcut.stroke.key != event.logical || shortcut.stroke.modifiers != event.modifiers) {
      continue;
    }
    if (shortcut.onInvoke) shortcut.onInvoke();
    return true;
  }
  return false;
}

}  // namespace fltr
