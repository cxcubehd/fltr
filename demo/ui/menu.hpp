#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

#include "fltr/core/callback.hpp"
#include "fltr/core/geometry.hpp"
#include "fltr/core/observable.hpp"
#include "fltr/render/box.hpp"
#include "fltr/widgets/framework.hpp"

namespace fltrdemo {

/// A handle on where something ended up.
///
/// A menu has to open *at* the control that owns it, and where that control is
/// is not knowable while building -- it is a layout result. So the control
/// marks itself with an `Anchor`, whose render object publishes itself here, and
/// the menu asks the link for the answer at the moment it opens. A link holds a
/// raw pointer it does not own; the render object clears it on the way out, so
/// a link to something no longer in the tree reads as unresolved rather than as
/// a dangling box.
class RenderAnchor;

class AnchorLink {
public:
  AnchorLink() = default;

  /// The link and its render object point at each other, and either may go
  /// first: a link owned by a `State` is destroyed when that State is, which
  /// happens while the render tree it belongs to is still standing. So both
  /// ends detach, and neither is left holding an address that has been freed.
  ~AnchorLink();

  AnchorLink(const AnchorLink&) = delete;
  AnchorLink& operator=(const AnchorLink&) = delete;

  RenderAnchor* box() const noexcept { return box_; }
  bool resolved() const noexcept { return box_ != nullptr; }

private:
  friend class RenderAnchor;
  RenderAnchor* box_ = nullptr;
};

class RenderAnchor final : public fltr::RenderProxyBox {
public:
  explicit RenderAnchor(AnchorLink& link) { setLink(link); }
  ~RenderAnchor() override;

  const char* typeName() const override { return "Anchor"; }
  void setLink(AnchorLink& link);

private:
  friend class AnchorLink;
  AnchorLink* link_ = nullptr;
};

/// Marks its child's box so something else can find it. Layout-transparent: it
/// is a proxy box, so it takes its child's size and paints it at its own origin.
class Anchor final : public fltr::Configure<Anchor, fltr::SingleChildRenderObjectWidget> {
public:
  struct Args {
    fltr::Key key;
    AnchorLink* link = nullptr;
    fltr::WidgetRef child;
  };
  using Render = RenderAnchor;

  explicit Anchor(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(args.link != nullptr, "an Anchor needs a link to publish to");
  }

  const char* name() const noexcept override { return "Anchor"; }
  fltr::WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderAnchor> createRenderObject(fltr::BuildContext&) const {
    return std::make_unique<RenderAnchor>(*args_.link);
  }
  void updateRenderObject(fltr::BuildContext&, RenderAnchor& render) const {
    render.setLink(*args_.link);
  }

private:
  Args args_;
};

/// Where a descendant's box sits inside an ancestor's, in the ancestor's
/// coordinates.
///
/// Walks up rather than down, and asks each *parent* where its child is: parent
/// data lives in the parent here, so a box cannot report its own offset. Only
/// offsets are accumulated, not transforms -- enough for a menu inside a page,
/// and not enough for one inside something rotated. The demo has no such thing,
/// and this asserts rather than pretending otherwise.
fltr::Rect boxWithin(const fltr::RenderBox& descendant, const fltr::RenderBox& ancestor);

/// Which menu is open, what is in it, and where it goes.
///
/// One controller serves a whole host, so at most one menu is open at a time --
/// which is the behaviour a settings screen wants, and it means "dismiss on
/// outside click" is a single barrier rather than a stack of them.
class MenuController {
public:
  struct Menu {
    const std::string_view* options = nullptr;
    std::size_t count = 0;
    int selected = -1;
    fltr::Callback<void(int)> onSelected;
  };

  /// Opening and closing is an observable change like any other, so a control
  /// that wants to look open subscribes and rebuilds only itself.
  fltr::ValueListenable<int>& openId() noexcept { return open_; }
  bool isOpen(int id) const noexcept { return id != 0 && open_.value() == id; }
  bool anyOpen() const noexcept { return open_.value() != 0; }

  /// Resolves the anchor against the host now, while both boxes are laid out.
  void open(int id, const AnchorLink& anchor, const Menu& menu);
  void close();
  /// Closes only if `id` is the menu currently open. What a control calls on
  /// its way out, so an open menu never outlives the control that opened it.
  void closeIf(int id);

  const Menu& menu() const noexcept { return menu_; }
  fltr::Rect anchorRect() const noexcept { return anchor_; }
  /// Filled in by the host, and the coordinate space every anchor resolves to.
  AnchorLink& hostLink() noexcept { return host_; }

private:
  fltr::Observable<int> open_{0};
  Menu menu_;
  fltr::Rect anchor_;
  AnchorLink host_;
};

/// The layer an open menu is drawn in.
///
/// A menu has to escape its parent's box, and nothing in this framework lets a
/// child paint outside its parent. So the menu is not a child of the control at
/// all: the host puts it in a `Stack` above the whole page, positioned at the
/// anchor's resolved rectangle. That is what a portal or an overlay entry is in
/// other frameworks, done with the pieces already here.
class MenuHost final : public fltr::Configure<MenuHost, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    MenuController* controller = nullptr;
    fltr::WidgetRef child;
  };

  explicit MenuHost(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(args.controller != nullptr, "a MenuHost needs a controller");
  }

  const char* name() const noexcept override { return "MenuHost"; }
  MenuController& controller() const noexcept { return *args_.controller; }
  fltr::WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<fltr::State<MenuHost>> createState() const;

private:
  Args args_;
};

/// A closed combo box: shows the chosen option, opens a list over everything.
///
/// Controlled, like every other control here -- it owns "am I open" and nothing
/// else, and even that lives in the shared controller because only one menu may
/// be open at a time.
class Dropdown final : public fltr::Configure<Dropdown, fltr::StatefulWidget> {
public:
  struct Args {
    fltr::Key key;
    /// Distinct and non-zero within one host. Zero means "no menu open".
    int id = 0;
    /// Storage the caller keeps alive: `Text` does not copy what it is given.
    const std::string_view* options = nullptr;
    std::size_t count = 0;
    int value = 0;
    fltr::Callback<void(int)> onChanged;
    MenuController* controller = nullptr;
    float width = 0.0f;
  };

  explicit Dropdown(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(args.id != 0, "a Dropdown needs a non-zero id");
    FLTR_EXPECTS(args.controller != nullptr, "a Dropdown needs a controller");
    FLTR_EXPECTS(args.count > 0, "a Dropdown needs options");
  }

  const char* name() const noexcept override { return "Dropdown"; }
  int id() const noexcept { return args_.id; }
  const std::string_view* options() const noexcept { return args_.options; }
  std::size_t count() const noexcept { return args_.count; }
  int value() const noexcept { return args_.value; }
  std::string_view selectedLabel() const noexcept;
  float width() const noexcept { return args_.width; }
  MenuController& controller() const noexcept { return *args_.controller; }
  const fltr::Callback<void(int)>& onChanged() const noexcept { return args_.onChanged; }

  std::unique_ptr<fltr::State<Dropdown>> createState() const;

private:
  Args args_;
};

}  // namespace fltrdemo
