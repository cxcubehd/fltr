#include "fltr/widgets/overlay.hpp"

#include <algorithm>

namespace fltr {

// ---------------------------------------------------------------------------
// OverlayEntry
// ---------------------------------------------------------------------------

void OverlayEntry::remove() {
  if (overlay_) overlay_->remove(*this);
}

// ---------------------------------------------------------------------------
// OverlayEntryHost
// ---------------------------------------------------------------------------

void OverlayEntryHostState::initState() { listen(); }

void OverlayEntryHostState::didUpdateWidget(const OverlayEntryHost& previous) {
  if (&previous.entry() != &widget().entry()) listen();
}

void OverlayEntryHostState::listen() {
  subscribeMember<OverlayEntryHostState, &OverlayEntryHostState::onNeedsBuild>(
      widget().entry(), rebuilds_, this);
}

WidgetRef OverlayEntryHostState::build(BuildContext& context) {
  return widget().entry().build(context);
}

// ---------------------------------------------------------------------------
// OverlayState
// ---------------------------------------------------------------------------

void OverlayState::dispose() {
  for (OverlayEntry* entry : entries_) entry->overlay_ = nullptr;
  entries_.clear();
}

void OverlayState::insert(OverlayEntry& entry) {
  FLTR_EXPECTS(!entry.inserted(), "an overlay entry belongs to one overlay at a time");
  entry.overlay_ = this;
  // A fresh identity per insertion, so an entry taken out and put back builds a
  // new subtree rather than resuming the one it had.
  entry.id_ = ++nextId_;
  entries_.push_back(&entry);
  setState([] {});
}

void OverlayState::remove(OverlayEntry& entry) {
  if (entry.overlay_ != this) return;
  entry.overlay_ = nullptr;
  std::erase(entries_, &entry);
  setState([] {});
}

WidgetRef OverlayState::build(BuildContext&) {
  return OverlayScope::make({
      .overlay = this,
      .child = OverlayStack::make({
          // The base is child zero. Inserting an entry re-emits the same child
          // ref, which by then is stale -- so the tree being overlaid is not
          // rebuilt, not reconciled, and not disturbed at all.
          .children = WidgetList::generate(entries_.size() + 1,
                                           [this](std::size_t i) {
                                             if (i == 0) return widget().child();
                                             OverlayEntry* entry = entries_[i - 1];
                                             return OverlayEntryHost::make({
                                                 .key = Key::of(entry->id_),
                                                 .entry = entry,
                                             });
                                           }),
      }),
  });
}

}  // namespace fltr
