#include "testing.hpp"

#include <cmath>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#include "app/app_state.hpp"
#include "app/root.hpp"
#include "fltr/widgets/binding.hpp"

using namespace fltr;
using namespace fltrtest;
using namespace fltrdemo;

namespace {

/// The demo, driven by a test instead of by a window.
///
/// Deliberately the real thing: the root is `DemoApp` over a real `AppState`,
/// so what these tests assert about is what the screen does. Only two things
/// are substituted -- the clock, which is an explicit delta per frame, and the
/// text service, which is the library's monospace one. The service is declared
/// before the binding and therefore destroyed after it, which is the same
/// ordering `main.cpp` relies on and the reason the teardown test can ask the
/// service what is still outstanding.
class DemoHarness {
public:
  explicit DemoHarness(Size surface = {1280.0f, 800.0f}) {
    app_.surface().set(surface);
    binding_.emplace(surface, text_);
    binding_->attachRoot([this] { return DemoApp::make({.state = &app_}); });
  }

  Scene frame(float seconds = 0.0f) {
    app_.beginFrame();
    return binding_->drawFrame(seconds);
  }

  /// Long enough for anything in flight to finish, so a test that cares about
  /// an idle frame is measuring one.
  void settle() {
    for (int i = 0; i < 40; ++i) frame(0.02f);
  }

  AppState& app() noexcept { return app_; }
  WidgetBinding& binding() noexcept { return *binding_; }
  const PipelineOwner::FrameStats& stats() noexcept { return binding_->pipeline().stats(); }
  int builds() noexcept { return binding_->buildOwner().buildCount(); }
  std::size_t tickers() noexcept { return binding_->tickers().activeTickerCount(); }
  Element& root() const noexcept { return *binding_->rootElement(); }
  MonospaceTextService& text() noexcept { return text_; }

  void teardown() { binding_.reset(); }

private:
  MonospaceTextService text_;
  AppState app_;
  std::optional<WidgetBinding> binding_;
};

PointerEvent mouse(PointerPhase phase, Offset position) {
  return {phase, 0, PointerDeviceKind::Mouse, position};
}

/// Where a box ended up on screen. Walks up asking each parent where its child
/// is, because parent data lives in the parent here.
Rect globalRect(const RenderBox& box) {
  Offset offset = Offset::zero();
  const RenderObject* node = &box;
  while (const RenderObject* parent = node->parent()) {
    Offset within = Offset::zero();
    parent->visitChildrenWithOffsets([&](RenderObject& child, Offset at) {
      if (&child == node) within = at;
    });
    offset = offset + within;
    node = parent;
  }
  return Rect::fromOriginSize(offset, box.size());
}

void visit(Element& element, FunctionRef<void(Element&)> fn) {
  fn(element);
  element.visitChildren([&fn](Element& child) { visit(child, fn); });
}

/// Elements by widget name, in tree order. A test seam that costs the demo
/// nothing: `name()` is what the debug dump already prints.
std::vector<Element*> elementsNamed(Element& root, const char* name) {
  std::vector<Element*> found;
  visit(root, [&](Element& element) {
    if (std::strcmp(element.widget()->name(), name) == 0) found.push_back(&element);
  });
  return found;
}

Element& firstNamed(Element& root, const char* name) {
  const std::vector<Element*> found = elementsNamed(root, name);
  FLTR_EXPECTS(!found.empty(), "no such element in this tree");
  return *found.front();
}

/// The rows, as (server id, element) in the order they are laid out.
std::vector<std::pair<std::int64_t, Element*>> rowsIn(Element& root) {
  std::vector<std::pair<std::int64_t, Element*>> rows;
  for (Element* element : elementsNamed(root, "ServerRow")) {
    rows.emplace_back(element->widget()->key().value, element);
  }
  return rows;
}

Element& firstKeyed(Element& root, Key key) {
  Element* found = nullptr;
  visit(root, [&](Element& element) {
    if (found == nullptr && element.widget()->key() == key) found = &element;
  });
  FLTR_EXPECTS(found != nullptr, "no element with that key in this tree");
  return *found;
}

const RenderDecoratedBox& decoratedIn(Element& element) {
  const RenderDecoratedBox* found = nullptr;
  const auto walk = [&](auto&& self, const RenderObject& node) -> void {
    if (found != nullptr) return;
    if (const auto* box = dynamic_cast<const RenderDecoratedBox*>(&node)) {
      found = box;
      return;
    }
    node.visitChildren([&](RenderObject& child) { self(self, child); });
  };
  walk(walk, *element.renderObject());
  FLTR_EXPECTS(found != nullptr, "no decorated box under this element");
  return *found;
}

/// A press and a release in the same place, which is what a click is once the
/// gesture arena has had its say.
void tap(DemoHarness& h, Offset at) {
  h.binding().dispatchPointer(mouse(PointerPhase::Down, at));
  h.binding().dispatchPointer(mouse(PointerPhase::Up, at));
}

}  // namespace

// ---------------------------------------------------------------------------
// The central claim
// ---------------------------------------------------------------------------

TEST(demo_an_idle_frame_costs_nothing_on_every_page) {
  for (const Page page : {Page::Title, Page::Settings, Page::Servers}) {
    DemoHarness h;
    h.app().goTo(page);
    h.settle();

    const Scene before = h.frame(0.016f);
    const Scene after = h.frame(0.016f);

    CHECK_EQ(h.builds(), 0);
    CHECK_EQ(h.binding().buildOwner().dirtyElementCount(), std::size_t{0});
    CHECK_EQ(h.stats().layouts, 0);
    CHECK_EQ(h.stats().paints, 0);
    CHECK_EQ(h.stats().boundariesRepainted, 0);
    CHECK_EQ(h.tickers(), std::size_t{0});
    // Nothing was recorded, so the scene the renderer would submit is the one
    // it submitted last time.
    CHECK_EQ(after.revision, before.revision);
  }
}

TEST(demo_hovering_a_button_repaints_one_boundary_and_lays_out_nothing) {
  DemoHarness h;
  h.settle();

  const Rect button = globalRect(*firstNamed(h.root(), "Button").renderObject());
  h.binding().dispatchPointer(mouse(PointerPhase::Hover, button.center()));
  h.frame(0.016f);

  // One button rebuilt itself, and the consequence stopped at its own repaint
  // boundary: no layout at all, and the rest of the screen untouched.
  CHECK_EQ(h.stats().layouts, 0);
  CHECK_EQ(h.stats().boundariesRelaidOut, 0);
  CHECK_EQ(h.stats().boundariesRepainted, 1);

  // And the frames that follow are pure animation: the hover interpolation
  // repaints that one boundary and rebuilds nothing.
  h.frame(0.016f);
  CHECK_EQ(h.builds(), 0);
  CHECK_EQ(h.stats().layouts, 0);
  CHECK_EQ(h.stats().boundariesRepainted, 1);
}

TEST(demo_interrupting_a_hover_animation_continues_from_what_is_on_screen) {
  DemoHarness h;
  h.settle();

  Element& button = firstNamed(h.root(), "Button");
  const RenderDecoratedBox& decorated = decoratedIn(button);
  const auto distance = [](Color a, Color b) {
    return std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)) +
           std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)) +
           std::abs(static_cast<int>(a.b) - static_cast<int>(b.b));
  };

  const Rect rect = globalRect(*button.renderObject());
  const Offset over = rect.center();
  const Offset away{2.0f, 2.0f};
  const Color resting = decorated.decoration().color;

  // Both ends of the interval, measured rather than assumed, so the rest of
  // this test can talk about fractions of the distance between them.
  h.binding().dispatchPointer(mouse(PointerPhase::Hover, over));
  h.settle();
  const Color hovered = decorated.decoration().color;
  h.binding().dispatchPointer(mouse(PointerPhase::Hover, away));
  h.settle();
  const int range = distance(resting, hovered);
  CHECK(range > 0);

  // Interrupted a third of the way in.
  h.binding().dispatchPointer(mouse(PointerPhase::Hover, over));
  h.frame(0.016f);
  h.frame(0.04f);
  const Color midway = decorated.decoration().color;
  CHECK(distance(midway, resting) > 0);
  CHECK(distance(midway, hovered) > 0);

  h.binding().dispatchPointer(mouse(PointerPhase::Hover, away));
  h.frame(0.016f);
  const Color afterInterruption = decorated.decoration().color;
  // Continuous: the reversal is re-based on the value on screen, so the frame
  // that turns the animation around differs from the one before it by a tick
  // of animation and not by a jump to either end.
  CHECK(distance(afterInterruption, midway) < range / 4);

  h.frame(0.02f);
  CHECK(distance(decorated.decoration().color, resting) < distance(midway, resting));

  h.settle();
  CHECK_EQ(decoratedIn(button).decoration().color, resting);
}

TEST(demo_a_hidden_page_holds_no_active_tickers) {
  DemoHarness h;
  h.settle();

  // Every row on the server browser fades in when it arrives, so this page is
  // the strongest version of the claim: two hundred tickers, all of them mid
  // flight when the page is left.
  h.app().goTo(Page::Servers);
  h.frame(0.016f);
  const std::size_t whileVisible = h.tickers();
  CHECK(whileVisible > 100);

  h.app().goTo(Page::Title);
  h.frame(0.0f);
  // Muted, not merely idle: a hidden page's tickers are not subscribed at all.
  // What is left is the page transition's own driver, which is above the
  // hidden subtree rather than inside it.
  CHECK_EQ(h.tickers(), std::size_t{1});

  h.settle();
  CHECK_EQ(h.tickers(), std::size_t{0});
}

// ---------------------------------------------------------------------------
// Keyed reconciliation, on the real list
// ---------------------------------------------------------------------------

TEST(demo_re_sorting_the_server_list_permutes_rows_rather_than_rebuilding_them) {
  DemoHarness h;
  h.app().goTo(Page::Servers);
  h.settle();

  const std::vector<std::pair<std::int64_t, Element*>> before = rowsIn(h.root());
  CHECK(before.size() > 100);

  h.app().servers().sortBy(ServerColumn::Name);
  h.frame();
  const std::vector<std::pair<std::int64_t, Element*>> after = rowsIn(h.root());

  CHECK_EQ(after.size(), before.size());

  // The same rows, in a different order, in the same elements. Not one row was
  // discarded and rebuilt, so not one row lost its State.
  bool orderChanged = false;
  int matched = 0;
  for (const auto& [id, element] : after) {
    for (const auto& [wasId, wasElement] : before) {
      if (wasId != id) continue;
      CHECK_EQ(element, wasElement);
      ++matched;
      break;
    }
  }
  for (std::size_t i = 0; i < after.size(); ++i) {
    if (after[i].first != before[i].first) orderChanged = true;
  }
  CHECK_EQ(matched, static_cast<int>(before.size()));
  CHECK(orderChanged);
}

TEST(demo_a_refresh_keeps_the_servers_that_stayed_and_discards_the_ones_that_left) {
  DemoHarness h;
  h.app().goTo(Page::Servers);
  h.settle();

  const std::vector<std::pair<std::int64_t, Element*>> before = rowsIn(h.root());
  h.app().servers().refresh();
  h.frame();
  const std::vector<std::pair<std::int64_t, Element*>> after = rowsIn(h.root());

  const auto find = [](const std::vector<std::pair<std::int64_t, Element*>>& rows,
                       std::int64_t id) -> Element* {
    for (const auto& [rowId, element] : rows) {
      if (rowId == id) return element;
    }
    return nullptr;
  };

  int survived = 0;
  int arrived = 0;
  for (const auto& [id, element] : after) {
    Element* was = find(before, id);
    if (was == nullptr) {
      ++arrived;
      continue;
    }
    // A server that came back is the same row it was: same element, same
    // State, same render object.
    CHECK_EQ(element, was);
    ++survived;
  }
  int left = 0;
  for (const auto& [id, element] : before) {
    if (find(after, id) == nullptr) ++left;
  }

  CHECK(survived > 100);
  CHECK(left > 0);
  CHECK(arrived > 0);
  // The rows that left really are gone rather than reused for someone else.
  CHECK_EQ(survived + arrived, static_cast<int>(after.size()));
}

// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

TEST(demo_no_paragraph_handles_are_outstanding_once_the_tree_is_gone) {
  DemoHarness h;
  // Every page, so every kind of text the demo builds has been through the
  // service: static labels, model-owned strings and per-frame formatted ones.
  for (const Page page : {Page::Settings, Page::Servers, Page::Title}) {
    h.app().goTo(page);
    h.settle();
  }
  CHECK(h.text().liveParagraphs() > 0);

  h.teardown();
  CHECK_EQ(h.text().liveParagraphs(), std::size_t{0});
}

// ---------------------------------------------------------------------------
// The settings screen
// ---------------------------------------------------------------------------

TEST(demo_a_dropdown_opens_over_the_page_and_an_outside_click_dismisses_it) {
  DemoHarness h;
  h.app().goTo(Page::Settings);
  h.settle();

  const std::size_t closed = elementsNamed(h.root(), "Button").size();
  const Rect control = globalRect(*firstNamed(h.root(), "Dropdown").renderObject());

  tap(h, control.center());
  h.frame();
  CHECK(h.app().menus().anyOpen());

  // The options are on screen and below the control, which is over whatever the
  // page had there: the menu is not a child of the control, it is a layer above
  // the page positioned at the control's resolved rectangle.
  const std::vector<Element*> buttons = elementsNamed(h.root(), "Button");
  CHECK_EQ(buttons.size(), closed + 4);
  for (std::size_t i = closed; i < buttons.size(); ++i) {
    const Rect option = globalRect(*buttons[i]->renderObject());
    CHECK(option.top >= control.bottom);
    CHECK(option.left >= control.left);
  }

  // Anywhere else dismisses it, including places the page has nothing at all.
  tap(h, {control.left, control.top - 80.0f});
  h.frame();
  CHECK(!h.app().menus().anyOpen());
  CHECK_EQ(elementsNamed(h.root(), "Button").size(), closed);
}

TEST(demo_a_setting_reaches_outside_the_panel_that_changed_it) {
  DemoHarness h;
  h.app().goTo(Page::Settings);
  h.settle();

  Element& scrim = firstKeyed(h.root(), Key::of("scrim"));
  const Color before = decoratedIn(scrim).decoration().color;

  // What the brightness slider writes. The settings page cannot see the scrim
  // and the scrim has never heard of the settings page: the only thing between
  // them is an observable value and a Watch at the root.
  h.app().settings.brightness.set(0.0f);
  h.frame();

  CHECK(decoratedIn(scrim).decoration().color != before);
  // Two subtrees rebuilt: the scrim, and the row whose readout shows the
  // number. The scrim itself is a paint-only change; the layouts are the
  // slider's own -- its readout is new text and its knob is placed by an
  // alignment, and both of those resolve during layout. Either way the work is
  // a dozen nodes on a page that has hundreds, and it stops at three relayout
  // boundaries: the row's readout, its knob, and the fixed-width box the
  // readout sits in. The panel around them is never asked to lay out again.
  CHECK(h.builds() < 20);
  CHECK(h.stats().layouts < 20);
  CHECK_EQ(h.stats().boundariesRelaidOut, 3);
}
