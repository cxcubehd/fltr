#include "testing.hpp"

#include "fltr/core/arena.hpp"
#include "fltr/core/constraints.hpp"
#include "fltr/core/geometry.hpp"
#include "fltr/core/key.hpp"
#include "fltr/core/listenable.hpp"

using namespace fltr;

TEST(geometry_alignment_inscribe) {
  const Size child{20, 10};
  const Size box{100, 50};
  CHECK_EQ(Alignment::topLeft().inscribe(child, box), (Offset{0, 0}));
  CHECK_EQ(Alignment::center().inscribe(child, box), (Offset{40, 20}));
  CHECK_EQ(Alignment::bottomRight().inscribe(child, box), (Offset{80, 40}));
  CHECK_EQ(Alignment::centerRight().inscribe(child, box), (Offset{80, 20}));
}

TEST(geometry_rect_ops) {
  const Rect a = Rect::fromLTWH(0, 0, 10, 10);
  const Rect b = Rect::fromLTWH(5, 5, 10, 10);
  CHECK(a.overlaps(b));
  CHECK_EQ(a.intersect(b), Rect::fromLTWH(5, 5, 5, 5));
  CHECK_EQ(a.expandToInclude(b), Rect::fromLTWH(0, 0, 15, 15));
  CHECK(a.contains({0, 0}));
  CHECK(!a.contains({10, 5}));  // right/bottom edges are exclusive
  CHECK_EQ(a.shift({2, 3}), Rect::fromLTWH(2, 3, 10, 10));
}

TEST(geometry_edge_insets) {
  const EdgeInsets e = EdgeInsets::symmetric(8, 4);
  CHECK_EQ(e.horizontal(), 16.0f);
  CHECK_EQ(e.vertical(), 8.0f);
  CHECK_EQ(e.deflateSize({100, 50}), (Size{84, 42}));
  CHECK_EQ(e.deflateSize({4, 2}), (Size{0, 0}));  // clamps at zero
}

TEST(geometry_transform_invert_roundtrip) {
  const Transform2D m =
      Transform2D::translation({10, 20}).then(Transform2D::scaling(2, 3));
  const Offset p{4, 5};
  const Offset q = m.apply(p);
  Transform2D inv;
  CHECK(m.invert(inv));
  const Offset back = inv.apply(q);
  CHECK_NEAR(back.dx, p.dx, 1e-4);
  CHECK_NEAR(back.dy, p.dy, 1e-4);

  // A degenerate transform reports failure rather than producing garbage.
  Transform2D bad{0, 0, 0, 0, 0, 0};
  Transform2D unused;
  CHECK(!bad.invert(unused));
}

TEST(geometry_transform_rect_bounds_under_rotation) {
  const Transform2D r = Transform2D::rotation(3.14159265f / 2.0f);
  const Rect out = r.applyToRect(Rect::fromLTWH(0, 0, 10, 4));
  CHECK_NEAR(out.width(), 4.0f, 1e-3);
  CHECK_NEAR(out.height(), 10.0f, 1e-3);
}

TEST(geometry_color_lerp) {
  const Color a = Color::argb(0xFF000000);
  const Color b = Color::argb(0xFFFFFFFF);
  CHECK_EQ(lerp(a, b, 0.0f), a);
  CHECK_EQ(lerp(a, b, 1.0f), b);
  CHECK_EQ(lerp(a, b, 0.5f).r, 128);
  CHECK_EQ(Color::argb(0x80FF8000).a, 128);
  CHECK_EQ(Color::argb(0x80FF8000).r, 255);
  CHECK_EQ(Color::argb(0x80FF8000).g, 128);
}

TEST(constraints_basics) {
  const BoxConstraints c{10, 100, 20, 200};
  CHECK_EQ(c.constrain({5, 5}), (Size{10, 20}));
  CHECK_EQ(c.constrain({500, 500}), (Size{100, 200}));
  CHECK(!c.isTight());
  CHECK(BoxConstraints::tight({10, 10}).isTight());
  CHECK_EQ(c.loosen(), (BoxConstraints{0, 100, 0, 200}));
  CHECK_EQ(c.smallest(), (Size{10, 20}));
  CHECK_EQ(c.biggest(), (Size{100, 200}));
  CHECK(c.isNormalized());
}

TEST(constraints_deflate_clamps_at_zero) {
  const BoxConstraints c = BoxConstraints::tight({10, 10});
  const BoxConstraints d = c.deflate(EdgeInsets::all(20));
  CHECK_EQ(d.maxWidth, 0.0f);
  CHECK_EQ(d.minWidth, 0.0f);
  CHECK(d.isNormalized());
}

TEST(constraints_enforce) {
  const BoxConstraints inner{0, 500, 0, 500};
  const BoxConstraints outer{0, 100, 0, 100};
  CHECK_EQ(inner.enforce(outer), (BoxConstraints{0, 100, 0, 100}));
}

// ---------------------------------------------------------------------------
// Arena
// ---------------------------------------------------------------------------

TEST(arena_bump_and_reset) {
  Arena a(256);
  const std::uint32_t g0 = a.generation();
  int* x = a.create<int>(7);
  CHECK_EQ(*x, 7);
  CHECK(a.bytesUsed() >= sizeof(int));
  a.reset();
  CHECK_EQ(a.generation(), g0 + 1);
  CHECK_EQ(a.bytesUsed(), std::size_t{0});
}

TEST(arena_reuses_chunks_after_reset) {
  Arena a(1024);
  for (int i = 0; i < 200; ++i) (void)a.create<double>(1.0);
  const std::size_t reserved = a.reservedBytes();
  const std::size_t chunks = a.chunkCount();
  for (int frame = 0; frame < 50; ++frame) {
    a.reset();
    for (int i = 0; i < 200; ++i) (void)a.create<double>(1.0);
  }
  // Steady state: no further growth once the working set is reached.
  CHECK_EQ(a.reservedBytes(), reserved);
  CHECK_EQ(a.chunkCount(), chunks);
}

TEST(arena_owning_runs_destructors_at_reset) {
  static int live = 0;
  struct Owning {
    Owning() { ++live; }
    ~Owning() { --live; }
    std::vector<int> heap{1, 2, 3};
  };
  Arena a(256);
  (void)a.createOwning<Owning>();
  (void)a.createOwning<Owning>();
  CHECK_EQ(live, 2);
  a.reset();
  CHECK_EQ(live, 0);
}

TEST(arena_alignment_is_respected) {
  Arena a(256);
  (void)a.create<char>('x');
  struct alignas(32) Wide { double d[4]; };
  Wide* w = a.create<Wide>();
  CHECK_EQ(reinterpret_cast<std::uintptr_t>(w) % 32u, std::uintptr_t{0});
}

// ---------------------------------------------------------------------------
// Listenable
// ---------------------------------------------------------------------------

namespace {
struct Counter : fltr::Listenable {
  using Listenable::notifyListeners;
};
struct Observer {
  int hits = 0;
  Subscription sub;
  void onChanged() { ++hits; }
};
}  // namespace

TEST(listenable_notifies_and_unsubscribes_on_destruction) {
  Counter c;
  {
    Observer o;
    subscribeMember<Observer, &Observer::onChanged>(c, o.sub, &o);
    CHECK_EQ(c.listenerCount(), std::size_t{1});
    c.notifyListeners();
    CHECK_EQ(o.hits, 1);
  }
  // The observer went out of scope; the edge unhooked itself.
  CHECK_EQ(c.listenerCount(), std::size_t{0});
  c.notifyListeners();  // must not touch freed memory
}

TEST(listenable_survives_detach_during_notification) {
  Counter c;
  Observer a, b, d;
  subscribeMember<Observer, &Observer::onChanged>(c, d.sub, &d);
  subscribeMember<Observer, &Observer::onChanged>(c, b.sub, &b);
  subscribeMember<Observer, &Observer::onChanged>(c, a.sub, &a);
  // Head order is a, b, d. `a` detaches `b` (its not-yet-visited successor).
  struct Detacher {
    Subscription sub;
    Subscription* victim;
    int hits = 0;
    void onChanged() {
      ++hits;
      victim->detach();
    }
  } det{{}, &b.sub, 0};
  det.sub.detach();
  c.subscribe(
      det.sub, [](void* p) { static_cast<Detacher*>(p)->onChanged(); }, &det);

  c.notifyListeners();
  CHECK_EQ(det.hits, 1);
  CHECK_EQ(b.hits, 0);  // detached before it was reached
  CHECK_EQ(a.hits, 1);
  CHECK_EQ(d.hits, 1);
}

TEST(listenable_dies_before_subscriber) {
  Observer o;
  {
    Counter c;
    subscribeMember<Observer, &Observer::onChanged>(c, o.sub, &o);
    CHECK(o.sub.attached());
  }
  CHECK(!o.sub.attached());
}

TEST(listenable_subscription_is_movable) {
  Counter c;
  Observer o;
  subscribeMember<Observer, &Observer::onChanged>(c, o.sub, &o);
  Subscription moved = std::move(o.sub);
  CHECK(!o.sub.attached());
  CHECK(moved.attached());
  c.notifyListeners();
  CHECK_EQ(o.hits, 1);  // still routed to the same context
  CHECK_EQ(c.listenerCount(), std::size_t{1});
}

// ---------------------------------------------------------------------------
// Key
// ---------------------------------------------------------------------------

TEST(key_equality) {
  CHECK(Key::none() == Key::none());
  CHECK(Key::of(3) == Key::of(3));
  CHECK(!(Key::of(3) == Key::of(4)));
  CHECK(!(Key::of(3) == Key::none()));
  CHECK(Key::of("hp") == Key::of("hp"));
  CHECK(!(Key::of("hp") == Key::of("mp")));
  CHECK(!(Key::of("3") == Key::of(3)));
  CHECK(!Key::none().isSet());
  CHECK(Key::of(0).isSet());
}
