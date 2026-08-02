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

OverlayEntry* OverlayEntryHostState::entry() const noexcept {
  return widget().overlay().entryById(widget().id());
}

void OverlayEntryHostState::initState() {
  if (OverlayEntry* live = entry()) {
    subscribeMember<OverlayEntryHostState, &OverlayEntryHostState::onNeedsBuild>(*live, rebuilds_,
                                                                                 this);
  }
}

WidgetRef OverlayEntryHostState::build(BuildContext& context) {
  OverlayEntry* live = entry();
  if (live) return live->build(context);
  // Gone since this host was emitted -- which the overlay has already asked to
  // rebuild for, so this lasts until later in the same frame. Nothing to show,
  // but a container's child still owes it a render object.
  return SizedBox::make({.size = Size::zero()});
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

OverlayEntry* OverlayState::entryById(std::int64_t id) const noexcept {
  for (OverlayEntry* entry : entries_) {
    if (entry->id_ == id) return entry;
  }
  return nullptr;
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
                                             const std::int64_t id = entries_[i - 1]->id_;
                                             return OverlayEntryHost::make({
                                                 .key = Key::of(id),
                                                 .overlay = this,
                                                 .id = id,
                                             });
                                           }),
      }),
  });
}

}  // namespace fltr
