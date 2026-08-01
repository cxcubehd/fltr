#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fltr/gestures/events.hpp"

namespace fltr {

enum class GestureDisposition : std::uint8_t { Accepted, Rejected };

/// A contender for one pointer. Exactly one member of an arena wins; every other
/// member is told it lost, so it can undo whatever provisional feedback it gave.
class GestureArenaMember {
public:
  virtual void acceptGesture(PointerId pointer) = 0;
  virtual void rejectGesture(PointerId pointer) = 0;

protected:
  ~GestureArenaMember() = default;
};

/// Decides which recognizer owns a pointer when several want it.
///
/// Members join while the down event travels the hit-test path, so they are
/// ordered deepest-first. `close` resolves immediately when only one joined;
/// otherwise the decision waits for `sweep` on pointer up, which awards the
/// first member still standing -- the innermost region under the point.
class GestureArena {
public:
  void add(PointerId pointer, GestureArenaMember& member);

  /// No further members may join. Resolves at once if only one did.
  void close(PointerId pointer);

  /// Forces a decision on pointer up: the first member still standing wins.
  void sweep(PointerId pointer);

  /// Defers the sweep. A recognizer whose gesture is not over when the pointer
  /// comes up -- a double tap waiting for the second one -- holds the arena so
  /// the sweep cannot award it to a contender in the meantime.
  void hold(PointerId pointer);
  /// Ends a hold, performing the sweep that arrived while it was in effect.
  void release(PointerId pointer);

  /// Drops the arena without awarding it, telling anyone still contending that
  /// they lost. A cancelled pointer leaves nothing behind whether or not every
  /// member withdrew itself on the way through.
  void cancel(PointerId pointer);

  void resolve(PointerId pointer, GestureArenaMember& member, GestureDisposition disposition);

  /// Withdraws a member from every arena it is in. Deliberately awards nothing:
  /// this is what a recognizer being destroyed calls, and firing a game callback
  /// out of a destructor is worse than leaving the remaining contender to be
  /// resolved by the sweep it was already waiting for.
  void remove(GestureArenaMember& member);

  std::size_t memberCount(PointerId pointer) const;
  bool isOpen(PointerId pointer) const;

private:
  struct Entry {
    PointerId pointer = 0;
    bool closed = false;
    bool held = false;
    bool pendingSweep = false;
  };
  /// One membership. Held in a flat list shared by every arena rather than a
  /// vector per pointer, so resolving a gesture returns storage to the arena
  /// instead of to the allocator.
  struct Slot {
    PointerId pointer = 0;
    /// Nulled rather than erased when a member withdraws, so a resolution in
    /// progress keeps its indices.
    GestureArenaMember* member = nullptr;
  };

  Entry* find(PointerId pointer) noexcept;
  const Entry* find(PointerId pointer) const noexcept;
  void drop(PointerId pointer);
  void award(PointerId pointer, GestureArenaMember* winner);
  GestureArenaMember* firstMember(PointerId pointer) const noexcept;

  std::vector<Entry> arenas_;
  std::vector<Slot> members_;
};

}  // namespace fltr
