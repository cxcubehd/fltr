#include "testing.hpp"

#include <memory>
#include <string>

#include "fltr/harness.hpp"
#include "fltr/widgets/binding.hpp"
#include "fltr/widgets/focus.hpp"
#include "fltr/widgets/scroll.hpp"
#include "widget_harness.hpp"

using namespace fltr;
using namespace fltrtest;

namespace {

KeyEvent press(LogicalKey key, KeyModifiers modifiers = {}) {
  return {.type = KeyEventType::Down, .logical = key, .modifiers = modifiers};
}

WidgetRef placed(Rect rect, WidgetRef child) {
  return Positioned::make({
      .left = rect.left,
      .top = rect.top,
      .width = rect.width(),
      .height = rect.height(),
      .child = child,
  });
}

WidgetRef screen(WidgetList children) {
  return Stack::make({.fit = StackFit::Expand, .children = children});
}

/// A focusable box of a known size at a known place, which is what directional
/// traversal needs and what tab order does not.
WidgetRef box(FocusNode* node, Rect rect, bool skipTraversal = false,
              bool canRequestFocus = true) {
  return placed(rect, Focus::make({
                          .node = node,
                          .canRequestFocus = canRequestFocus,
                          .skipTraversal = skipTraversal,
                          .child = SizedBox::make({.size = rect.size()}),
                      }));
}

/// Records every node a key visited, so a test asserts the whole walk rather
/// than one flag at a time.
struct KeyTally {
  std::string visited;
  int invoked = 0;
};

Callback<bool(const KeyEvent&)> record(KeyTally& tally, const char* name, bool consume) {
  return [&tally, name, consume](const KeyEvent&) {
    tally.visited += name;
    return consume;
  };
}

}  // namespace

// ---------------------------------------------------------------------------
// A tree with no scope in it
// ---------------------------------------------------------------------------

TEST(focus_a_tree_with_no_scope_registers_nothing_and_is_pointer_only) {
  FocusNode node;
  Harness h;
  ScriptedRoot root(h, [&] { return screen({box(&node, Rect::fromLTWH(0, 0, 40, 20))}); });
  h.frame();

  CHECK_EQ(h.binding().keyboard().handlerCount(), std::size_t{0});
  CHECK(!node.attached());
  CHECK(!node.hasFocus());
  CHECK(!node.requestFocus());
  CHECK(dumpElementTree(h.rootElement()).find("FocusMarker") == std::string::npos);
  CHECK(!h.binding().dispatchKey(press(LogicalKey::Tab)));
}

TEST(focus_one_scope_registers_exactly_one_handler_however_many_nodes_it_holds) {
  FocusScopeNode scope;
  FocusNode a, b;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20)),
                         box(&b, Rect::fromLTWH(0, 40, 40, 20))}),
    });
  });
  h.frame();

  CHECK_EQ(h.binding().keyboard().handlerCount(), std::size_t{1});
  CHECK_EQ(scope.manager()->nodeCount(), std::size_t{2});
  CHECK(a.attached());
  CHECK(b.attached());
}

// ---------------------------------------------------------------------------
// Tab order
// ---------------------------------------------------------------------------

TEST(focus_tab_moves_in_build_order_and_wraps_at_either_end) {
  FocusScopeNode scope;
  FocusNode a, b, c;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20)),
                         box(&b, Rect::fromLTWH(0, 30, 40, 20)),
                         box(&c, Rect::fromLTWH(0, 60, 40, 20))}),
    });
  });
  h.frame();

  CHECK(h.binding().dispatchKey(press(LogicalKey::Tab)));
  CHECK(a.hasPrimaryFocus());
  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(b.hasPrimaryFocus());
  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(c.hasPrimaryFocus());
  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(a.hasPrimaryFocus());

  h.binding().dispatchKey(press(LogicalKey::Tab, KeyModifier::Shift));
  CHECK(c.hasPrimaryFocus());
}

TEST(focus_a_node_that_skips_traversal_is_still_focusable_on_purpose) {
  FocusScopeNode scope;
  FocusNode a, hud, b;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20)),
                         box(&hud, Rect::fromLTWH(0, 30, 40, 20), /*skipTraversal=*/true),
                         box(&b, Rect::fromLTWH(0, 60, 40, 20))}),
    });
  });
  h.frame();

  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(a.hasPrimaryFocus());
  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(b.hasPrimaryFocus());

  // Reachable by a click, which is the whole point of the distinction.
  CHECK(hud.requestFocus());
  CHECK(hud.hasPrimaryFocus());
}

TEST(focus_a_node_that_cannot_request_focus_is_never_focusable_at_all) {
  FocusScopeNode scope;
  FocusNode a, inert;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20)),
                         box(&inert, Rect::fromLTWH(0, 30, 40, 20), /*skipTraversal=*/false,
                             /*canRequestFocus=*/false)}),
    });
  });
  h.frame();

  CHECK(!inert.requestFocus());
  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(a.hasPrimaryFocus());
  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(a.hasPrimaryFocus());
}

TEST(focus_excluding_a_subtree_takes_everything_below_it_out_of_the_order) {
  FocusScopeNode scope;
  FocusNode outside, buried;
  Harness h;
  bool excluded = true;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({box(&outside, Rect::fromLTWH(0, 0, 40, 20)),
                         placed(Rect::fromLTWH(0, 30, 40, 20),
                                Focus::make({
                                    .canRequestFocus = false,
                                    .skipTraversal = true,
                                    .descendantsAreFocusable = !excluded,
                                    .child = Focus::make({
                                        .node = &buried,
                                        .child = SizedBox::make({.size = {40, 20}}),
                                    }),
                                }))}),
    });
  });
  h.frame();

  CHECK(!buried.isFocusable());
  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(outside.hasPrimaryFocus());
  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(outside.hasPrimaryFocus());

  excluded = false;
  root.rebuild();
  h.frame();
  CHECK(buried.isFocusable());
  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(buried.hasPrimaryFocus());
}

// ---------------------------------------------------------------------------
// Key routing
// ---------------------------------------------------------------------------

TEST(focus_a_key_walks_from_the_focused_node_up_and_the_first_to_take_it_ends_the_walk) {
  FocusScopeNode scope;
  FocusNode outer, inner;
  KeyTally tally;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = Focus::make({
            .node = &outer,
            .canRequestFocus = false,
            .skipTraversal = true,
            .onKey = record(tally, "outer", false),
            .child = Focus::make({
                .node = &inner,
                .onKey = record(tally, "inner", false),
                .child = SizedBox::make({.size = {40, 20}}),
            }),
        }),
    });
  });
  h.frame();
  inner.requestFocus();

  CHECK(!h.binding().dispatchKey(press(LogicalKey::KeyJ)));
  CHECK_EQ(tally.visited, std::string("innerouter"));

  tally.visited.clear();
  outer.onKey = record(tally, "outer", true);
  CHECK(h.binding().dispatchKey(press(LogicalKey::KeyJ)));
  CHECK_EQ(tally.visited, std::string("innerouter"));

  tally.visited.clear();
  inner.onKey = record(tally, "inner", true);
  CHECK(h.binding().dispatchKey(press(LogicalKey::KeyJ)));
  CHECK_EQ(tally.visited, std::string("inner"));
}

TEST(focus_an_unclaimed_key_is_reported_unconsumed_so_the_game_can_have_it) {
  FocusScopeNode scope;
  FocusNode a;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({.node = &scope,
                             .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20))})});
  });
  h.frame();
  a.requestFocus();

  CHECK(!h.binding().dispatchKey(press(LogicalKey::KeyW)));
}

TEST(focus_a_scope_that_leaves_the_arrows_alone_never_sees_them_move) {
  FocusScopeNode scope;
  FocusNode a, b;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .directionalTraversal = false,
        .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20)),
                         box(&b, Rect::fromLTWH(0, 40, 40, 20))}),
    });
  });
  h.frame();
  a.requestFocus();

  CHECK(!h.binding().dispatchKey(press(LogicalKey::ArrowDown)));
  CHECK(a.hasPrimaryFocus());
  // Tab still works: the two are separate policies, not one switch.
  CHECK(h.binding().dispatchKey(press(LogicalKey::Tab)));
  CHECK(b.hasPrimaryFocus());
}

// ---------------------------------------------------------------------------
// Directional traversal
// ---------------------------------------------------------------------------

TEST(focus_a_dpad_moves_to_the_nearest_node_that_way) {
  FocusScopeNode scope;
  FocusNode a, b, c, d;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20)),
                         box(&b, Rect::fromLTWH(60, 0, 40, 20)),
                         box(&c, Rect::fromLTWH(0, 40, 40, 20)),
                         box(&d, Rect::fromLTWH(60, 40, 40, 20))}),
    });
  });
  h.frame();
  a.requestFocus();

  CHECK(h.binding().dispatchKey(press(LogicalKey::ArrowRight)));
  CHECK(b.hasPrimaryFocus());
  h.binding().dispatchKey(press(LogicalKey::ArrowDown));
  CHECK(d.hasPrimaryFocus());
  h.binding().dispatchKey(press(LogicalKey::ArrowLeft));
  CHECK(c.hasPrimaryFocus());
  h.binding().dispatchKey(press(LogicalKey::ArrowUp));
  CHECK(a.hasPrimaryFocus());

  // Nothing that way: the key belongs to whoever else wants it.
  CHECK(!h.binding().dispatchKey(press(LogicalKey::ArrowUp)));
  CHECK(a.hasPrimaryFocus());
}

TEST(focus_a_dpad_prefers_the_node_it_lines_up_with_over_a_nearer_one_off_to_the_side) {
  FocusScopeNode scope;
  FocusNode a, below, aside;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20)),
                         // Further down than `aside`, but in the same column.
                         box(&below, Rect::fromLTWH(0, 40, 40, 20)),
                         box(&aside, Rect::fromLTWH(120, 30, 40, 20))}),
    });
  });
  h.frame();
  a.requestFocus();

  h.binding().dispatchKey(press(LogicalKey::ArrowDown));
  CHECK(below.hasPrimaryFocus());
}

TEST(focus_a_dpad_falls_back_to_plain_distance_when_nothing_lines_up) {
  FocusScopeNode scope;
  FocusNode a, near, far;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20)),
                         box(&near, Rect::fromLTWH(60, 30, 40, 20)),
                         box(&far, Rect::fromLTWH(140, 60, 40, 20))}),
    });
  });
  h.frame();
  a.requestFocus();

  h.binding().dispatchKey(press(LogicalKey::ArrowDown));
  CHECK(near.hasPrimaryFocus());
}

// ---------------------------------------------------------------------------
// Scopes
// ---------------------------------------------------------------------------

TEST(focus_a_nested_scope_keeps_traversal_to_itself_once_the_focus_is_in_it) {
  FocusScopeNode outer, dialog;
  FocusNode behind, first, second;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &outer,
        .child = screen({box(&behind, Rect::fromLTWH(0, 0, 40, 20)),
                         placed(Rect::fromLTWH(0, 30, 100, 60),
                                FocusScope::make({
                                    .node = &dialog,
                                    .child = screen({box(&first, Rect::fromLTWH(0, 0, 40, 20)),
                                                     box(&second, Rect::fromLTWH(0, 30, 40, 20))}),
                                }))}),
    });
  });
  h.frame();
  first.requestFocus();

  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(second.hasPrimaryFocus());
  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(first.hasPrimaryFocus());
  CHECK(!behind.hasFocus());
}

TEST(focus_a_scope_remembers_where_it_was_and_gives_the_focus_back_there) {
  FocusScopeNode outer, dialog;
  FocusNode behind, first, second;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &outer,
        .child = screen({box(&behind, Rect::fromLTWH(0, 0, 40, 20)),
                         placed(Rect::fromLTWH(0, 30, 100, 60),
                                FocusScope::make({
                                    .node = &dialog,
                                    .child = screen({box(&first, Rect::fromLTWH(0, 0, 40, 20)),
                                                     box(&second, Rect::fromLTWH(0, 30, 40, 20))}),
                                }))}),
    });
  });
  h.frame();

  second.requestFocus();
  CHECK_EQ(dialog.focusedChild(), &second);
  behind.requestFocus();
  CHECK(!dialog.hasFocus());

  dialog.requestFocus();
  CHECK(second.hasPrimaryFocus());
}

TEST(focus_unfocusing_hands_the_focus_back_to_the_scope_and_forgets_where_it_was) {
  FocusScopeNode scope;
  FocusNode a;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({.node = &scope,
                             .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20))})});
  });
  h.frame();

  a.requestFocus();
  CHECK(a.hasPrimaryFocus());
  a.unfocus();
  CHECK(!a.hasFocus());
  CHECK(scope.hasPrimaryFocus());
  CHECK_EQ(scope.focusedChild(), static_cast<FocusNode*>(nullptr));
}

TEST(focus_removing_the_focused_node_leaves_the_focus_on_its_scope) {
  FocusScopeNode scope;
  FocusNode a, b;
  Harness h;
  bool showB = true;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20)),
                         showB ? box(&b, Rect::fromLTWH(0, 40, 40, 20)) : WidgetRef{}}),
    });
  });
  h.frame();

  b.requestFocus();
  CHECK(b.hasPrimaryFocus());

  showB = false;
  root.rebuild();
  h.frame();

  CHECK(!b.attached());
  CHECK(scope.hasPrimaryFocus());
  // And traversal starts over from the top rather than from a node that is gone.
  h.binding().dispatchKey(press(LogicalKey::Tab));
  CHECK(a.hasPrimaryFocus());
}

TEST(focus_a_scope_node_destroyed_under_a_live_tree_leaves_the_focus_nowhere) {
  FocusNode a;
  Harness h;
  auto owned = std::make_unique<FocusScopeNode>();
  FocusScopeNode* scope = owned.get();
  ScriptedRoot root(h, [&] {
    return FocusScope::make({.node = scope,
                             .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20))})});
  });
  h.frame();
  a.requestFocus();
  CHECK(a.hasPrimaryFocus());

  owned.reset();

  // The manager outlived the node it was built around and is still registered
  // for keys, so it must walk nothing rather than what is gone.
  CHECK(!a.attached());
  CHECK(!a.hasFocus());
  CHECK(a.rect().isEmpty());
  CHECK(!h.binding().dispatchKey(press(LogicalKey::Tab)));
}

TEST(focus_a_node_that_outlived_its_widget_answers_nothing_rather_than_reading_it) {
  FocusNode a;
  {
    Harness h;
    ScriptedRoot root(h, [&] {
      return FocusScope::make({.child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20))})});
    });
    h.frame();
    a.requestFocus();
    CHECK(a.hasPrimaryFocus());
    CHECK_EQ(a.rect(), Rect::fromLTWH(0, 0, 40, 20));
  }

  // The element this node was told about is gone with the tree, so the node has
  // nothing left to measure and must not go looking.
  CHECK(!a.attached());
  CHECK(a.rect().isEmpty());
}

TEST(focus_a_widget_that_goes_on_naming_a_destroyed_node_is_trapped) {
  Harness h;
  auto owned = std::make_unique<FocusNode>();
  FocusNode* named = owned.get();
  ScriptedRoot root(h, [&] {
    return FocusScope::make({.child = screen({box(named, Rect::fromLTWH(0, 0, 40, 20))})});
  });
  h.frame();

  // A build that still names the dead node is the consumer breaking the outlive
  // rule. Re-subscribing to it would be a read of freed memory rather than a
  // diagnosis of one.
  owned.reset();
  root.rebuild();
  CHECK_THROWS(h.frame());
}

TEST(focus_a_node_notifies_only_when_its_own_state_moved) {
  FocusScopeNode scope;
  FocusNode a, b;
  int changesA = 0;
  int changesB = 0;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({placed(Rect::fromLTWH(0, 0, 40, 20),
                                Focus::make({
                                    .node = &a,
                                    .onFocusChange = [&changesA](bool) { ++changesA; },
                                    .child = SizedBox::make({.size = {40, 20}}),
                                })),
                         placed(Rect::fromLTWH(0, 40, 40, 20),
                                Focus::make({
                                    .node = &b,
                                    .onFocusChange = [&changesB](bool) { ++changesB; },
                                    .child = SizedBox::make({.size = {40, 20}}),
                                }))}),
    });
  });
  h.frame();

  a.requestFocus();
  CHECK_EQ(changesA, 1);
  CHECK_EQ(changesB, 0);

  b.requestFocus();
  CHECK_EQ(changesA, 2);
  CHECK_EQ(changesB, 1);

  // Focusing what is already focused is not a change.
  b.requestFocus();
  CHECK_EQ(changesB, 1);
}

TEST(focus_autofocus_claims_an_empty_scope_and_never_steals_from_a_full_one) {
  FocusScopeNode scope;
  FocusNode first, second;
  Harness h;
  bool showSecond = false;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({placed(Rect::fromLTWH(0, 0, 40, 20),
                                Focus::make({
                                    .node = &first,
                                    .autofocus = true,
                                    .child = SizedBox::make({.size = {40, 20}}),
                                })),
                         showSecond ? placed(Rect::fromLTWH(0, 40, 40, 20),
                                             Focus::make({
                                                 .node = &second,
                                                 .autofocus = true,
                                                 .child = SizedBox::make({.size = {40, 20}}),
                                             }))
                                    : WidgetRef{}}),
    });
  });
  h.frame();
  CHECK(first.hasPrimaryFocus());

  showSecond = true;
  root.rebuild();
  h.frame();
  CHECK(first.hasPrimaryFocus());
}

// ---------------------------------------------------------------------------
// Shortcuts
// ---------------------------------------------------------------------------

TEST(focus_a_shortcut_fires_while_the_focus_is_below_it_and_not_when_it_is_elsewhere) {
  FocusScopeNode scope;
  FocusNode inside, outside;
  KeyTally tally;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({placed(Rect::fromLTWH(0, 0, 40, 20),
                                Shortcuts::make({
                                    .shortcuts = {{{LogicalKey::Escape}, [&tally] { ++tally.invoked; }}},
                                    .child = box(&inside, Rect::fromLTWH(0, 0, 40, 20)),
                                })),
                         box(&outside, Rect::fromLTWH(0, 40, 40, 20))}),
    });
  });
  h.frame();

  inside.requestFocus();
  CHECK(h.binding().dispatchKey(press(LogicalKey::Escape)));
  CHECK_EQ(tally.invoked, 1);

  outside.requestFocus();
  CHECK(!h.binding().dispatchKey(press(LogicalKey::Escape)));
  CHECK_EQ(tally.invoked, 1);
}

TEST(focus_a_shortcut_matches_its_modifiers_exactly) {
  FocusScopeNode scope;
  FocusNode node;
  KeyTally tally;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = Shortcuts::make({
            .shortcuts = {{{LogicalKey::KeyS, KeyModifier::Control},
                           [&tally] { ++tally.invoked; }}},
            .child = screen({box(&node, Rect::fromLTWH(0, 0, 40, 20))}),
        }),
    });
  });
  h.frame();
  node.requestFocus();

  CHECK(!h.binding().dispatchKey(press(LogicalKey::KeyS)));
  CHECK(!h.binding().dispatchKey(press(LogicalKey::KeyS,
                                       KeyModifier::Control | KeyModifier::Shift)));
  CHECK_EQ(tally.invoked, 0);

  CHECK(h.binding().dispatchKey(press(LogicalKey::KeyS, KeyModifier::Control)));
  CHECK_EQ(tally.invoked, 1);
}

TEST(focus_a_shortcut_survives_the_arena_that_declared_it) {
  FocusScopeNode scope;
  FocusNode node;
  KeyTally tally;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = Shortcuts::make({
            .shortcuts = {{{LogicalKey::Escape}, [&tally] { ++tally.invoked; }}},
            .child = screen({box(&node, Rect::fromLTWH(0, 0, 40, 20))}),
        }),
    });
  });
  h.frame();
  node.requestFocus();

  // Several builds later, with every arena that held the list long released.
  for (int i = 0; i < 4; ++i) {
    root.rebuild();
    h.frame();
  }
  CHECK(h.binding().dispatchKey(press(LogicalKey::Escape)));
  CHECK_EQ(tally.invoked, 1);
}

// ---------------------------------------------------------------------------
// Focus inside a scroll view
// ---------------------------------------------------------------------------

TEST(focus_moving_into_a_scrolled_row_brings_it_into_view_by_the_least_it_can) {
  FocusScopeNode scope;
  ScrollController controller;
  FocusNode first, last;
  Harness h({100, 60});
  const auto item = [](FocusNode* node, float height) {
    return Focus::make({
        .node = node,
        .ensureVisibleDuration = 0.0f,
        .child = SizedBox::make({.size = {100, height}}),
    });
  };
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = Scrollable::make({
            .controller = &controller,
            .child = Column::make({.children = {item(&first, 40),
                                                SizedBox::make({.size = {100, 100}}),
                                                item(&last, 40)}}),
        }),
    });
  });
  h.frame();
  CHECK_EQ(controller.offset(), 0.0f);

  last.requestFocus();
  CHECK_EQ(controller.offset(), 120.0f);

  // Already visible: nothing to do, and nothing done.
  last.requestFocus();
  CHECK_EQ(controller.offset(), 120.0f);

  first.requestFocus();
  CHECK_EQ(controller.offset(), 0.0f);
}

// ---------------------------------------------------------------------------
// Cost
// ---------------------------------------------------------------------------

TEST(focus_moving_the_focus_around_allocates_nothing) {
  FocusScopeNode scope;
  FocusNode a, b, c;
  Harness h;
  ScriptedRoot root(h, [&] {
    return FocusScope::make({
        .node = &scope,
        .child = screen({box(&a, Rect::fromLTWH(0, 0, 40, 20)),
                         box(&b, Rect::fromLTWH(0, 30, 40, 20)),
                         box(&c, Rect::fromLTWH(0, 60, 40, 20))}),
    });
  });
  h.frame();

  // Two laps, so every scratch list has reached the high-water mark a moving
  // focus needs before anything is counted.
  for (int warmup = 0; warmup < 6; ++warmup) h.binding().dispatchKey(press(LogicalKey::Tab));

  const std::size_t before = allocationCount();
  for (int i = 0; i < 12; ++i) {
    h.binding().dispatchKey(press(LogicalKey::Tab));
    h.binding().dispatchKey(press(LogicalKey::ArrowDown));
  }
  CHECK_EQ(allocationCount(), before);
}
