#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "fltr/render/overlay.hpp"
#include "fltr/widgets/basic.hpp"
#include "fltr/widgets/inherited.hpp"

namespace fltr {

class Overlay;
class OverlayState;

// ---------------------------------------------------------------------------
// OverlayEntry
// ---------------------------------------------------------------------------

/// One thing shown above the tree: a tooltip, a dropdown, a context menu, the
/// ghost a drag carries.
///
/// Consumer-owned and named by pointer, like a scroll controller or a focus
/// node. It is held by whatever decides to show something -- usually a State --
/// and must outlive its insertion; destroying an inserted entry removes it, so
/// neither teardown order can leave the overlay holding a dangling pointer.
///
/// The builder runs when the overlay rebuilds and when `markNeedsBuild` asks for
/// it. Whatever it captures by reference must outlive the entry, which is the
/// rule every callback stored in a widget already follows.
class OverlayEntry final : public Listenable {
public:
  using Builder = Callback<WidgetRef(BuildContext&)>;

  explicit OverlayEntry(Builder builder) : builder_(builder) {
    FLTR_EXPECTS(static_cast<bool>(builder), "an overlay entry needs a builder");
  }
  ~OverlayEntry() override { remove(); }

  /// Rebuilds this entry's subtree and nothing else in the overlay.
  void markNeedsBuild() { notifyListeners(); }

  bool inserted() const noexcept { return overlay_ != nullptr; }
  void remove();

  WidgetRef build(BuildContext& context) const { return builder_(context); }

private:
  friend class OverlayState;

  Builder builder_;
  OverlayState* overlay_ = nullptr;
  std::int64_t id_ = 0;
};

// ---------------------------------------------------------------------------
// The pieces the overlay's State emits
// ---------------------------------------------------------------------------

class OverlayEntryHost;

class OverlayEntryHostState final : public State<OverlayEntryHost> {
public:
  void initState() override;
  void didUpdateWidget(const OverlayEntryHost& previous) override;
  void dispose() override { rebuilds_.detach(); }
  WidgetRef build(BuildContext& context) override;

private:
  void listen();
  void onNeedsBuild() { setState([] {}); }

  Subscription rebuilds_;
};

/// Where one entry builds. It exists so that an entry asking to rebuild rebuilds
/// itself alone rather than every entry in the overlay.
class OverlayEntryHost final : public Configure<OverlayEntryHost, StatefulWidget> {
public:
  struct Args {
    Key key;
    OverlayEntry* entry = nullptr;
  };

  explicit OverlayEntryHost(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(args.entry != nullptr, "an overlay entry host needs an entry");
  }

  const char* name() const noexcept override { return "OverlayEntryHost"; }
  OverlayEntry& entry() const noexcept { return *args_.entry; }

  std::unique_ptr<State<OverlayEntryHost>> createState() const {
    return std::make_unique<OverlayEntryHostState>();
  }

private:
  Args args_;
};

/// The base subtree and the entries above it. Emitted by the overlay's State
/// rather than written at a call site.
class OverlayStack final : public Configure<OverlayStack, MultiChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    WidgetList children;
  };
  using Render = RenderOverlay;

  explicit OverlayStack(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "OverlayStack"; }
  WidgetList children() const noexcept { return args_.children; }

  std::unique_ptr<RenderOverlay> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderOverlay>();
  }
  void updateRenderObject(BuildContext&, RenderOverlay&) const {}

private:
  Args args_;
};

/// Publishes the enclosing overlay to its subtree, so anything below can show
/// something above everything.
class OverlayScope final : public Configure<OverlayScope, InheritedWidget> {
public:
  struct Args {
    Key key;
    OverlayState* overlay = nullptr;
    WidgetRef child;
  };

  explicit OverlayScope(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "OverlayScope"; }
  WidgetRef child() const noexcept { return args_.child; }
  OverlayState* overlay() const noexcept { return args_.overlay; }

  bool updateShouldNotify(const OverlayScope& previous) const noexcept {
    return args_.overlay != previous.args_.overlay;
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Overlay
// ---------------------------------------------------------------------------

class OverlayState final : public State<Overlay> {
public:
  void dispose() override;
  WidgetRef build(BuildContext& context) override;

  /// Puts `entry` above everything the overlay currently holds. Not from a
  /// build: showing something is what a callback does.
  void insert(OverlayEntry& entry);
  void remove(OverlayEntry& entry);

  std::size_t entryCount() const noexcept { return entries_.size(); }

private:
  std::vector<OverlayEntry*> entries_;
  std::int64_t nextId_ = 0;
};

/// A layer above `child` that anything below can put something into.
///
/// Nothing creates one for you: a tree with no overlay in it holds no entries,
/// no scope and no extra render object -- the same bargain the focus subsystem
/// makes. Put one high in the tree, usually directly under the root.
///
/// Entries are painted after the base and hit tested before it, and the base
/// alone decides the layout: however large an entry is, the tree underneath it
/// is laid out exactly as it would have been without the overlay.
class Overlay final : public Configure<Overlay, StatefulWidget> {
public:
  struct Args {
    Key key;
    WidgetRef child;
  };

  explicit Overlay(const Args& args) : Configure(args.key), args_(args) {
    FLTR_EXPECTS(static_cast<bool>(args.child), "an overlay needs a child to overlay");
  }

  const char* name() const noexcept override { return "Overlay"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<State<Overlay>> createState() const { return std::make_unique<OverlayState>(); }

  /// The nearest overlay above `context`, or null where there is none.
  ///
  /// Only from a build, as every ambient read is: a widget that shows things
  /// reads it while building and keeps the pointer for its callbacks.
  static OverlayState* of(BuildContext& context) {
    InheritedElementBase* node = context.element().dependOnInherited(widgetTypeOf<OverlayScope>());
    return node == nullptr ? nullptr
                           : static_cast<InheritedElement<OverlayScope>*>(node)->config().overlay();
  }

private:
  Args args_;
};

// ---------------------------------------------------------------------------
// Anchoring
// ---------------------------------------------------------------------------

/// Names the rectangle its child occupies, so that an overlay entry can be
/// placed against it.
class Anchor final : public Configure<Anchor, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    /// Consumer-owned; must outlive the widget.
    AnchorLink* link = nullptr;
    WidgetRef child;
  };
  using Render = RenderAnchor;

  explicit Anchor(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Anchor"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderAnchor> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderAnchor>(args_.link);
  }
  void updateRenderObject(BuildContext&, RenderAnchor& render) const { render.setLink(args_.link); }

private:
  Args args_;
};

/// Places its child against the rectangle an anchor named, rather than within
/// its own parent. What a dropdown, a tooltip and a context menu all are.
///
/// It fills the overlay it is in, so an entry is `Anchored` at the top and
/// whatever it is showing below that.
class Anchored final : public Configure<Anchored, SingleChildRenderObjectWidget> {
public:
  struct Args {
    Key key;
    AnchorLink* link = nullptr;
    /// The point on the anchor the child hangs from, and the point on the child
    /// that lands there. A dropdown is bottomLeft to topLeft.
    Alignment anchorSide = Alignment::bottomLeft();
    Alignment childSide = Alignment::topLeft();
    Offset offset;
    /// Keeps the child inside the overlay, which is what stops a menu opened
    /// near an edge from hanging off it.
    bool keepOnScreen = true;
    WidgetRef child;
  };
  using Render = RenderAnchored;

  explicit Anchored(const Args& args) : Configure(args.key), args_(args) {}

  const char* name() const noexcept override { return "Anchored"; }
  WidgetRef child() const noexcept { return args_.child; }

  std::unique_ptr<RenderAnchored> createRenderObject(BuildContext&) const {
    return std::make_unique<RenderAnchored>(args_.link, placement());
  }
  void updateRenderObject(BuildContext&, RenderAnchored& render) const {
    render.setLink(args_.link);
    render.setPlacement(placement());
  }

private:
  RenderAnchored::Placement placement() const noexcept {
    return {args_.anchorSide, args_.childSide, args_.offset, args_.keepOnScreen};
  }

  Args args_;
};

}  // namespace fltr
