#include "ui/menu.hpp"

#include "fltr/widgets/basic.hpp"
#include "ui/button.hpp"
#include "ui/layout.hpp"
#include "ui/theme.hpp"

namespace fltrdemo {

using namespace fltr;

// ---------------------------------------------------------------------------
// Anchors
// ---------------------------------------------------------------------------

AnchorLink::~AnchorLink() {
  if (box_ != nullptr) box_->link_ = nullptr;
}

RenderAnchor::~RenderAnchor() {
  if (link_ != nullptr && link_->box_ == this) link_->box_ = nullptr;
}

void RenderAnchor::setLink(AnchorLink& link) {
  if (link_ == &link) return;
  if (link_ != nullptr && link_->box_ == this) link_->box_ = nullptr;
  link_ = &link;
  link_->box_ = this;
}

Rect boxWithin(const RenderBox& descendant, const RenderBox& ancestor) {
  Offset offset = Offset::zero();
  const RenderObject* node = &descendant;
  while (node != &ancestor) {
    const RenderObject* parent = node->parent();
    FLTR_EXPECTS(parent != nullptr, "the anchor is not below the box it was resolved against");
    Offset within = Offset::zero();
    parent->visitChildrenWithOffsets([&](RenderObject& child, Offset at) {
      if (&child == node) within = at;
    });
    offset = offset + within;
    node = parent;
  }
  return Rect::fromOriginSize(offset, descendant.size());
}

// ---------------------------------------------------------------------------
// MenuController
// ---------------------------------------------------------------------------

void MenuController::open(int id, const AnchorLink& anchor, const Menu& menu) {
  FLTR_EXPECTS(id != 0, "zero is not a menu id");
  FLTR_EXPECTS(anchor.resolved() && host_.resolved(),
               "a menu can only open once its anchor and its host have been laid out");
  menu_ = menu;
  anchor_ = boxWithin(*anchor.box(), *host_.box());
  open_.set(id);
}

void MenuController::close() {
  menu_ = {};
  open_.set(0);
}

void MenuController::closeIf(int id) {
  if (isOpen(id)) close();
}

namespace {

// ---------------------------------------------------------------------------
// MenuHost
// ---------------------------------------------------------------------------

class MenuHostState final : public State<MenuHost> {
public:
  void initState() override {
    subscribeMember<MenuHostState, &MenuHostState::onChanged>(menus().openId(), subscription_,
                                                              this);
  }
  void dispose() override { subscription_.detach(); }

  WidgetRef build(BuildContext& context) override {
    const Theme& theme = themeOf(context);
    const bool open = menus().anyOpen();

    return Stack::make({
        .fit = StackFit::Expand,
        .children =
            {
                Anchor::make({.link = &menus().hostLink(), .child = widget().child()}),
                // The barrier is why an outside click dismisses: it is above the
                // page and opaque, so the press lands here and never reaches
                // whatever was under it. Present only while a menu is open, so
                // it costs nothing the rest of the time.
                open ? Pointer::make({
                           .behavior = HitTestBehavior::Opaque,
                           .onTap = Callback<void()>([this] { menus().close(); }),
                       })
                     : WidgetRef{},
                open ? buildMenu(theme) : WidgetRef{},
            },
    });
  }

private:
  MenuController& menus() const noexcept { return widget().controller(); }
  void onChanged() {
    setState([] {});
  }

  WidgetRef buildMenu(const Theme& theme) {
    const MenuController::Menu& menu = menus().menu();
    const Rect anchor = menus().anchorRect();

    return Positioned::make({
        .left = anchor.left,
        .top = anchor.bottom + theme.hairline(),
        .width = anchor.width(),
        .child = DecoratedBox::make({
            .decoration = {.color = theme.surfaceRaised,
                           .radius = BorderRadius::all(theme.radius()),
                           .borderColor = theme.accent,
                           .borderWidth = theme.hairline()},
            .child = Padding::make({
                .padding = EdgeInsets::all(theme.hairline() * 2.0f),
                .child = Column::make({
                    .crossAxisAlignment = CrossAxisAlignment::Stretch,
                    .mainAxisSize = MainAxisSize::Min,
                    // The list length comes from the data, so it is generated
                    // rather than written out.
                    .children = WidgetList::generate(
                        menu.count,
                        [this, &menu](std::size_t i) {
                          return Button::make({
                              .key = Key::of(static_cast<int>(i)),
                              .label = menu.options[i],
                              .onPressed = Callback<void()>(
                                  [this, i] { choose(static_cast<int>(i)); }),
                              .selected = static_cast<int>(i) == menu.selected,
                              .kind = ButtonKind::Tab,
                          });
                        }),
                }),
            }),
        }),
    });
  }

  void choose(int index) {
    // Copied before closing: closing clears the menu, and the callback about to
    // run is one of the things it clears.
    const Callback<void(int)> selected = menus().menu().onSelected;
    menus().close();
    if (selected) selected(index);
  }

  Subscription subscription_;
};

// ---------------------------------------------------------------------------
// Dropdown
// ---------------------------------------------------------------------------

class DropdownState final : public State<Dropdown> {
public:
  void initState() override {
    subscribeMember<DropdownState, &DropdownState::onMenuChanged>(widget().controller().openId(),
                                                                  subscription_, this);
  }

  void dispose() override {
    subscription_.detach();
    // An open menu must not outlive the control it belongs to: it holds a
    // callback into this State.
    widget().controller().closeIf(widget().id());
  }

  WidgetRef build(BuildContext& context) override {
    const Theme& theme = themeOf(context);
    const bool open = widget().controller().isOpen(widget().id());
    const bool lit = open || hovered_;
    const float width = widget().width() > 0.0f ? widget().width() : theme.sliderWidth();

    TextStyle style = theme.body();
    style.color = lit ? theme.textBright : theme.text;
    TextStyle caret = theme.body();
    caret.color = lit ? theme.accent : theme.textDim;

    return RepaintBoundary::make({
        .child = Anchor::make({
            .link = &link_,
            .child = Pointer::make({
                .behavior = HitTestBehavior::Opaque,
                .onEnter = Callback<void()>([this] { setHovered(true); }),
                .onExit = Callback<void()>([this] { setHovered(false); }),
                .onTap = Callback<void()>([this] { toggle(); }),
                .child = SizedBox::make({
                    .size = {width, theme.controlHeight()},
                    .child = DecoratedBox::make({
                        .decoration = {.color = theme.surfaceSunken,
                                       .radius = BorderRadius::all(theme.radius()),
                                       .borderColor = lit ? theme.accent : theme.border,
                                       .borderWidth = theme.hairline()},
                        .child = Padding::make({
                            .padding = EdgeInsets::symmetric(theme.unit(), 0.0f),
                            .child = Row::make({
                                .crossAxisAlignment = CrossAxisAlignment::Center,
                                .children =
                                    {
                                        Flexible::make({
                                            .child = text(widget().selectedLabel(), style),
                                        }),
                                        // ASCII on purpose: the demo loads a
                                        // font's default charset, and a glyph
                                        // that is not there is a box.
                                        text("v", caret),
                                    },
                            }),
                        }),
                    }),
                }),
            }),
        }),
    });
  }

private:
  void toggle() {
    MenuController& menus = widget().controller();
    if (menus.isOpen(widget().id())) {
      menus.close();
      return;
    }
    menus.open(widget().id(), link_,
               {
                   .options = widget().options(),
                   .count = widget().count(),
                   .selected = widget().value(),
                   // Reads the *current* configuration when it runs, rather
                   // than capturing this one: a widget lives for one build.
                   .onSelected = Callback<void(int)>(
                       [this](int index) { widget().onChanged()(index); }),
               });
  }

  void onMenuChanged() {
    setState([] {});
  }

  void setHovered(bool hovered) {
    if (hovered_ == hovered) return;
    setState([&] { hovered_ = hovered; });
  }

  AnchorLink link_;
  Subscription subscription_;
  bool hovered_ = false;
};

}  // namespace

std::string_view Dropdown::selectedLabel() const noexcept {
  const auto index = static_cast<std::size_t>(args_.value);
  return index < args_.count ? args_.options[index] : std::string_view{};
}

std::unique_ptr<State<MenuHost>> MenuHost::createState() const {
  return std::make_unique<MenuHostState>();
}

std::unique_ptr<State<Dropdown>> Dropdown::createState() const {
  return std::make_unique<DropdownState>();
}

}  // namespace fltrdemo
