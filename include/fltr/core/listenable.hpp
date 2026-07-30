#pragma once

#include <cstddef>
#include <utility>

#include "fltr/core/config.hpp"

namespace fltr {

class Listenable;

/// One edge in an observer graph, owned by the observer.
///
/// Intrusive and allocation-free: subscribing costs two pointer stores, and the
/// subscription unhooks itself when the observer is destroyed. This is what lets
/// a render object hold an animation subscription with correct lifetime without
/// any shared ownership.
class Subscription {
  friend class Listenable;

public:
  using Fn = void (*)(void* ctx);

  Subscription() = default;
  ~Subscription() { detach(); }

  Subscription(const Subscription&) = delete;
  Subscription& operator=(const Subscription&) = delete;

  Subscription(Subscription&& o) noexcept { adopt(std::move(o)); }
  Subscription& operator=(Subscription&& o) noexcept {
    if (this != &o) {
      detach();
      adopt(std::move(o));
    }
    return *this;
  }

  void detach() noexcept;
  bool attached() const noexcept { return owner_ != nullptr; }
  Listenable* source() const noexcept { return owner_; }

private:
  void adopt(Subscription&& o) noexcept;

  Listenable* owner_ = nullptr;
  Subscription* prev_ = nullptr;
  Subscription* next_ = nullptr;
  Fn fn_ = nullptr;
  void* ctx_ = nullptr;
};

/// The single observable core shared by reactivity (Observable<T>) and
/// animation (Animation<T>).
///
/// The rebuild-versus-repaint distinction deliberately does *not* live here. It
/// lives in the subscriber: a Watch element subscribes and marks itself dirty
/// for rebuild; a render object subscribes via watchForPaint and only marks
/// itself needing paint. Same signal, different sink, and which one you are on
/// is visible at the call site.
class Listenable {
  friend class Subscription;

public:
  Listenable() = default;
  virtual ~Listenable() { detachAll(); }

  Listenable(const Listenable&) = delete;
  Listenable& operator=(const Listenable&) = delete;

  void subscribe(Subscription& s, Subscription::Fn fn, void* ctx) {
    FLTR_EXPECTS(fn != nullptr, "subscribe requires a callback");
    s.detach();
    s.owner_ = this;
    s.fn_ = fn;
    s.ctx_ = ctx;
    s.prev_ = nullptr;
    s.next_ = head_;
    if (head_) head_->prev_ = &s;
    head_ = &s;
  }

  bool hasListeners() const noexcept { return head_ != nullptr; }

  std::size_t listenerCount() const noexcept {
    std::size_t n = 0;
    for (const Subscription* s = head_; s; s = s->next_) ++n;
    return n;
  }

protected:
  /// Safe against listeners subscribing or detaching (including detaching the
  /// not-yet-visited successor, or themselves) during the walk.
  void notifyListeners() {
    Subscription* next = head_;
    Cursor cursor{&next, cursors_};
    cursors_ = &cursor;
    struct Restore {
      Listenable* self;
      Cursor* prev;
      ~Restore() { self->cursors_ = prev; }
    } restore{this, cursor.prev};

    while (next) {
      Subscription* cur = next;
      next = cur->next_;
      cur->fn_(cur->ctx_);
    }
  }

private:
  struct Cursor {
    Subscription** slot;
    Cursor* prev;
  };

  void detachAll() noexcept {
    while (head_) head_->detach();
  }

  Subscription* head_ = nullptr;
  Cursor* cursors_ = nullptr;
};

inline void Subscription::detach() noexcept {
  if (!owner_) return;
  Listenable* o = owner_;
  // If an in-flight notification is about to visit this node, step it past.
  for (Listenable::Cursor* c = o->cursors_; c; c = c->prev) {
    if (*c->slot == this) *c->slot = next_;
  }
  if (prev_) {
    prev_->next_ = next_;
  } else {
    o->head_ = next_;
  }
  if (next_) next_->prev_ = prev_;
  owner_ = nullptr;
  prev_ = next_ = nullptr;
  fn_ = nullptr;
  ctx_ = nullptr;
}

inline void Subscription::adopt(Subscription&& o) noexcept {
  owner_ = o.owner_;
  prev_ = o.prev_;
  next_ = o.next_;
  fn_ = o.fn_;
  ctx_ = o.ctx_;
  if (owner_) {
    if (prev_) {
      prev_->next_ = this;
    } else {
      owner_->head_ = this;
    }
    if (next_) next_->prev_ = this;
    for (Listenable::Cursor* c = owner_->cursors_; c; c = c->prev) {
      if (*c->slot == &o) *c->slot = this;
    }
  }
  o.owner_ = nullptr;
  o.prev_ = o.next_ = nullptr;
  o.fn_ = nullptr;
  o.ctx_ = nullptr;
}

/// Subscribe `obj->*M` to `l`. Keeps call sites free of lambda plumbing.
template <class T, void (T::*M)()>
inline void subscribeMember(Listenable& l, Subscription& s, T* obj) {
  l.subscribe(
      s, [](void* p) { (static_cast<T*>(p)->*M)(); }, obj);
}

}  // namespace fltr
