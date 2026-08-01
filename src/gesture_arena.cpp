#include "fltr/gestures/arena.hpp"

#include <algorithm>

#include "fltr/core/config.hpp"

namespace fltr {

GestureArena::Entry* GestureArena::find(PointerId pointer) noexcept {
  for (Entry& entry : arenas_) {
    if (entry.pointer == pointer) return &entry;
  }
  return nullptr;
}

const GestureArena::Entry* GestureArena::find(PointerId pointer) const noexcept {
  return const_cast<GestureArena*>(this)->find(pointer);
}

GestureArenaMember* GestureArena::firstMember(PointerId pointer) const noexcept {
  for (const Slot& slot : members_) {
    if (slot.pointer == pointer && slot.member) return slot.member;
  }
  return nullptr;
}

std::size_t GestureArena::memberCount(PointerId pointer) const {
  std::size_t n = 0;
  for (const Slot& slot : members_) {
    if (slot.pointer == pointer && slot.member) ++n;
  }
  return n;
}

bool GestureArena::isOpen(PointerId pointer) const {
  const Entry* entry = find(pointer);
  return entry != nullptr && !entry->closed;
}

void GestureArena::drop(PointerId pointer) {
  std::erase_if(arenas_, [pointer](const Entry& entry) { return entry.pointer == pointer; });
  std::erase_if(members_, [pointer](const Slot& slot) { return slot.pointer == pointer; });
}

void GestureArena::add(PointerId pointer, GestureArenaMember& member) {
  Entry* entry = find(pointer);
  if (!entry) {
    arenas_.push_back({pointer});
    entry = &arenas_.back();
  }
  FLTR_EXPECTS(!entry->closed, "a gesture arena may not be joined after it has closed");
  members_.push_back({pointer, &member});
}

void GestureArena::close(PointerId pointer) {
  Entry* entry = find(pointer);
  if (!entry) return;
  entry->closed = true;
  const std::size_t live = memberCount(pointer);
  if (live == 0) {
    drop(pointer);
  } else if (live == 1) {
    award(pointer, firstMember(pointer));
  }
}

void GestureArena::sweep(PointerId pointer) {
  Entry* entry = find(pointer);
  if (!entry) return;
  if (entry->held) {
    entry->pendingSweep = true;
    return;
  }
  award(pointer, firstMember(pointer));
}

void GestureArena::hold(PointerId pointer) {
  if (Entry* entry = find(pointer)) entry->held = true;
}

void GestureArena::release(PointerId pointer) {
  Entry* entry = find(pointer);
  if (!entry || !entry->held) return;
  entry->held = false;
  if (entry->pendingSweep) sweep(pointer);
}

void GestureArena::cancel(PointerId pointer) {
  if (find(pointer)) award(pointer, nullptr);
}

void GestureArena::resolve(PointerId pointer, GestureArenaMember& member,
                           GestureDisposition disposition) {
  if (!find(pointer)) return;

  if (disposition == GestureDisposition::Accepted) {
    award(pointer, &member);
    return;
  }

  const auto slot = std::find_if(members_.begin(), members_.end(), [&](const Slot& s) {
    return s.pointer == pointer && s.member == &member;
  });
  if (slot == members_.end()) return;
  slot->member = nullptr;
  member.rejectGesture(pointer);

  // The callback may have withdrawn other members, or dropped this arena.
  const Entry* entry = find(pointer);
  if (!entry || !entry->closed) return;
  const std::size_t live = memberCount(pointer);
  if (live == 0) {
    drop(pointer);
  } else if (live == 1) {
    award(pointer, firstMember(pointer));
  }
}

/// Losers are told first, and the arena is gone before the winner is told, so
/// neither can find anything still claiming the pointer.
///
/// The walk is by index and re-reads the list every step: a losing member's
/// callback may destroy another contender, which withdraws it from this list.
void GestureArena::award(PointerId pointer, GestureArenaMember* winner) {
  for (std::size_t i = 0; i < members_.size(); ++i) {
    Slot& slot = members_[i];
    if (slot.pointer != pointer || slot.member == nullptr || slot.member == winner) continue;
    GestureArenaMember* loser = slot.member;
    slot.member = nullptr;
    loser->rejectGesture(pointer);
  }
  drop(pointer);
  if (winner) winner->acceptGesture(pointer);
}

void GestureArena::remove(GestureArenaMember& member) {
  for (Slot& slot : members_) {
    if (slot.member == &member) slot.member = nullptr;
  }
}

}  // namespace fltr
