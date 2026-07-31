#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include "fltr/core/arena.hpp"
#include "fltr/core/function_ref.hpp"
#include "fltr/core/key.hpp"
#include "fltr/core/listenable.hpp"
#include "fltr/core/type_tag.hpp"
#include "fltr/paint/text.hpp"
#include "fltr/render/box.hpp"

namespace fltr {

class BuildOwner;
class Element;
class InheritedElementBase;
class InheritedScope;
class PointerBinding;
class RenderObjectElement;
class Widget;

using ElementPtr = std::unique_ptr<Element>;

// ---------------------------------------------------------------------------
// Widget configuration
// ---------------------------------------------------------------------------

using WidgetType = TypeTag;

template <class T>
constexpr WidgetType widgetTypeOf() noexcept {
  return typeTagOf<T>();
}

/// The arena widgets are built into. Ambient rather than a parameter, because a
/// widget expression nests arbitrarily deep and threading an allocator through
/// it would put noise at exactly the call sites this framework optimises for.
Arena* currentBuildArena() noexcept;

/// Opens the window during which widgets may be created, and releases every one
/// of them on exit.
class BuildScope {
public:
  explicit BuildScope(Arena& arena) noexcept;
  ~BuildScope();

  BuildScope(const BuildScope&) = delete;
  BuildScope& operator=(const BuildScope&) = delete;

private:
  Arena* arena_;
};

/// True once the build that allocated `generation` has ended and its arena has
/// been released.
bool isStaleGeneration(std::uint32_t generation) noexcept;

/// A reference to arena-allocated widget configuration.
///
/// It carries type and key rather than reading them back through the pointer,
/// so a ref that outlived its arena can still be reconciled without being
/// dereferenced -- which is the ordinary case, not an exotic one: an element
/// that rebuilds alone re-emits the child ref it stored from an earlier build.
class WidgetRef {
public:
  WidgetRef() = default;
  WidgetRef(const Widget& widget, std::uint32_t generation) noexcept;

  explicit operator bool() const noexcept { return widget_ != nullptr; }
  WidgetType type() const noexcept { return type_; }
  Key key() const noexcept { return key_; }

  /// From a build whose arena has been released. The configuration behind it
  /// cannot have changed since, because the element holding it copied it
  /// verbatim, so the subtree it names needs no work.
  bool stale() const noexcept { return widget_ != nullptr && isStaleGeneration(generation_); }

  /// Matching type and key reconcile onto the same element, keeping its State
  /// and render object; anything else discards it and inflates afresh.
  bool canUpdate(const Widget& existing) const noexcept;

  const Widget* get() const;
  const Widget* operator->() const { return get(); }
  const Widget& operator*() const { return *get(); }

private:
  const Widget* widget_ = nullptr;
  WidgetType type_ = nullptr;
  Key key_;
  std::uint32_t generation_ = 0;
};

template <class W, class Args>
WidgetRef newWidget(const Args& args) {
  Arena* arena = currentBuildArena();
  FLTR_EXPECTS(arena != nullptr, "widgets may only be created during a build scope");
  return {*arena->create<W>(args), arena->generation()};
}

/// The children of a multi-child widget. The braced list at the call site is a
/// temporary, so entries are copied into the build arena. Null refs are dropped,
/// which makes `visible ? Badge::make({...}) : WidgetRef{}` work inline.
class WidgetList {
public:
  WidgetList() = default;
  WidgetList(std::initializer_list<WidgetRef> widgets);

  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }
  bool stale() const noexcept { return size_ != 0 && isStaleGeneration(generation_); }

  const WidgetRef& operator[](std::size_t i) const {
    FLTR_EXPECTS(i < size_, "widget list index out of range");
    FLTR_EXPECTS(!stale(),
                 "a widget list from an earlier build was read after its arena was released");
    return items_[i];
  }

private:
  const WidgetRef* items_ = nullptr;
  std::size_t size_ = 0;
  std::uint32_t generation_ = 0;
};

static_assert(std::is_trivially_destructible_v<WidgetList>);

/// Immutable configuration, alive for one build phase and released wholesale.
/// A widget is never deleted and never destroyed polymorphically, hence the
/// protected non-virtual destructor -- which also keeps concrete widgets
/// trivially destructible, and so arena-safe.
class Widget {
public:
  Key key() const noexcept { return key_; }

  virtual WidgetType type() const noexcept = 0;
  virtual const char* name() const noexcept = 0;
  virtual ElementPtr createElement() const = 0;

protected:
  explicit Widget(Key key) noexcept : key_(key) {}
  ~Widget() = default;

  /// Protected, not public: a widget is immutable to everything except the
  /// element adopting it, which overwrites its whole configuration slot at once.
  Widget(const Widget&) = default;
  Widget& operator=(const Widget&) = default;

private:
  Key key_;
};

// ---------------------------------------------------------------------------
// BuildContext
// ---------------------------------------------------------------------------

/// What a build method is allowed to see. A handle rather than the element
/// itself, so building cannot reach the tree-mutating half of Element.
class BuildContext {
public:
  explicit BuildContext(Element& element) noexcept : element_(&element) {}

  Element& element() const noexcept { return *element_; }
  BuildOwner& owner() const noexcept;
  TextService& textService() const noexcept;
  PointerBinding& pointerBinding() const noexcept;
  bool mounted() const noexcept;

private:
  Element* element_;
};

// ---------------------------------------------------------------------------
// Element
// ---------------------------------------------------------------------------

class ChildList;

/// A mutable reference to one container's per-child layout data, tagged with
/// that data's type so a parent-data widget aimed at the wrong kind of container
/// is refused rather than reinterpreting memory.
class ParentDataSlot {
public:
  template <class Data>
  explicit ParentDataSlot(Data& data) noexcept : data_(&data), tag_(typeTagOf<Data>()) {}

  template <class Data>
  Data* as() const noexcept {
    return tag_ == typeTagOf<Data>() ? static_cast<Data*>(data_) : nullptr;
  }

private:
  void* data_;
  TypeTag tag_;
};

/// The persistent tree. An element outlives the widgets that configure it and
/// owns whatever state and render object that configuration implies.
class Element {
public:
  virtual ~Element() = default;

  Element(const Element&) = delete;
  Element& operator=(const Element&) = delete;

  Element* parent() const noexcept { return parent_; }
  BuildOwner* owner() const noexcept { return owner_; }
  int depth() const noexcept { return depth_; }
  bool mounted() const noexcept { return owner_ != nullptr; }
  bool dirty() const noexcept { return dirty_; }

  virtual const Widget* widget() const noexcept = 0;
  virtual void visitChildren(FunctionRef<void(Element&)>) const {}

  /// The render object this subtree contributes to its render parent. Elements
  /// that create no render object of their own forward to their child.
  virtual RenderBox* renderObject() const;

  void markNeedsBuild();

  /// Reads the ambient value introduced by the nearest enclosing widget of
  /// `type`, and subscribes this element to it: a change rebuilds this element
  /// and nothing else. The scope is a snapshot rather than a path, so the lookup
  /// costs the same at any depth.
  ///
  /// The subscription lasts until this element next rebuilds, which re-registers
  /// whatever the new build actually read.
  InheritedElementBase* dependOnInherited(WidgetType type);

  virtual void mount(Element* parent, BuildOwner& owner);
  virtual void unmount();
  virtual void update(WidgetRef widget) = 0;

  void rebuild();

  /// Lets a parent-data widget fill in the slot its subtree occupies in an
  /// ancestor container. The walk stops at the first render object in each
  /// branch, so it reaches through intervening component elements.
  virtual void writeParentData(ParentDataSlot slot) const;

  /// Destroys this subtree's render objects, called once at the top of a subtree
  /// being discarded. `unmount` runs after it, and must therefore never touch a
  /// render object.
  virtual void detachRenderObject();

protected:
  Element() = default;

  virtual void performRebuild() = 0;
  virtual RenderObjectElement* asRenderObjectElement() noexcept { return nullptr; }

  /// Runs once the element is linked to its parent and before it builds, so an
  /// element that introduces an ambient value can extend the scope its subtree
  /// will see. The default inherits the parent's scope unchanged.
  virtual void extendInheritedScope() {}

  RenderObjectElement* ancestorRenderObjectElement() const;

  ElementPtr updateChild(ElementPtr child, WidgetRef widget);
  ElementPtr inflate(WidgetRef widget);
  void deactivate(ElementPtr child);

  bool dirty_ = true;
  const InheritedScope* inheritedScope_ = nullptr;

private:
  friend class BuildOwner;
  friend class ChildList;

  Element* parent_ = nullptr;
  BuildOwner* owner_ = nullptr;
  int depth_ = 0;
  bool inDirtyList_ = false;
  /// One per ambient value this element's last build read. Intrusive, so
  /// dropping them all and re-registering costs no allocation.
  std::vector<Subscription> dependencies_;
};

/// Holds the configuration by value, so the element keeps a stable copy once
/// the arena that produced the widget is gone. An element is only ever updated
/// with a widget of its own concrete type, so the member is never the wrong one.
template <class W, class Base>
class ConfiguredElement : public Base {
public:
  explicit ConfiguredElement(const W& config) : config_(config) {}

  const Widget* widget() const noexcept final { return &config_; }

  void update(WidgetRef widget) final {
    FLTR_EXPECTS(widget.type() == widgetTypeOf<W>(),
                 "an element may only be updated with a widget of its own type");
    adopt(static_cast<const W&>(*widget));
    this->dirty_ = true;
    this->rebuild();
  }

protected:
  virtual void adopt(const W& next) { config_ = next; }

  W config_;
};

// ---------------------------------------------------------------------------
// Keyed reconciliation
// ---------------------------------------------------------------------------

/// An element's children and the algorithm that reconciles them against a new
/// widget list. Unkeyed children reconcile by position; keyed children reconcile
/// by key and so survive reordering with State and render object intact.
///
/// The scratch buffers are members, so a rebuild allocates nothing once the
/// high-water mark is reached.
class ChildList {
public:
  void update(Element& parent, WidgetList widgets);

  std::size_t size() const noexcept { return children_.size(); }
  Element& operator[](std::size_t i) const noexcept { return *children_[i]; }
  void visit(FunctionRef<void(Element&)> visitor) const {
    for (const ElementPtr& child : children_) visitor(*child);
  }

private:
  struct KeyedChild {
    Key key;
    ElementPtr element;
  };

  std::vector<ElementPtr> children_;
  std::vector<ElementPtr> next_;
  std::vector<KeyedChild> keyed_;
};

// ---------------------------------------------------------------------------
// Component elements
// ---------------------------------------------------------------------------

/// An element that produces a child by building rather than by creating a
/// render object.
template <class W>
class ComponentElement : public ConfiguredElement<W, Element> {
public:
  using ConfiguredElement<W, Element>::ConfiguredElement;

  void visitChildren(FunctionRef<void(Element&)> visitor) const final {
    if (child_) visitor(*child_);
  }

protected:
  virtual WidgetRef build() = 0;

  void performRebuild() final { child_ = this->updateChild(std::move(child_), build()); }

  ElementPtr child_;
};

template <class W>
class StatelessElement final : public ComponentElement<W> {
public:
  using ComponentElement<W>::ComponentElement;

protected:
  WidgetRef build() override {
    BuildContext context(*this);
    return this->config_.build(context);
  }
};

template <class W>
class StatefulElement;

/// Mutable state whose lifetime is its element's, not any widget's. `widget()`
/// always reflects the newest configuration; `didUpdateWidget` receives the
/// previous one, and is where derived values are re-targeted.
template <class W>
class State {
public:
  virtual ~State() = default;

  State(const State&) = delete;
  State& operator=(const State&) = delete;

  virtual void initState() {}
  virtual void didUpdateWidget(const W& previous) { (void)previous; }
  virtual void dispose() {}
  virtual WidgetRef build(BuildContext& context) = 0;

  const W& widget() const noexcept { return *widget_; }
  BuildContext context() const noexcept { return BuildContext(*element_); }
  bool mounted() const noexcept { return element_ != nullptr && element_->mounted(); }

  void setState(FunctionRef<void()> change) {
    FLTR_EXPECTS(mounted(), "setState on a State whose element is no longer mounted");
    change();
    if (mounted()) element_->markNeedsBuild();
  }

protected:
  State() = default;

private:
  friend class StatefulElement<W>;

  const W* widget_ = nullptr;
  Element* element_ = nullptr;
};

template <class W>
class StatefulElement final : public ComponentElement<W> {
public:
  explicit StatefulElement(const W& config)
      : ComponentElement<W>(config), state_(config.createState()) {
    FLTR_EXPECTS(state_ != nullptr, "createState must return a state object");
    state_->widget_ = &this->config_;
    state_->element_ = this;
  }

  void mount(Element* parent, BuildOwner& owner) override {
    state_->initState();
    ComponentElement<W>::mount(parent, owner);
  }

  void unmount() override {
    ComponentElement<W>::unmount();
    state_->dispose();
    state_->element_ = nullptr;
  }

  State<W>& state() const noexcept { return *state_; }

protected:
  void adopt(const W& next) override {
    const W previous = this->config_;
    this->config_ = next;
    state_->didUpdateWidget(previous);
  }

  WidgetRef build() override {
    BuildContext context(*this);
    return state_->build(context);
  }

private:
  std::unique_ptr<State<W>> state_;
};

/// Configures its child's slot in an ancestor container rather than creating a
/// render object of its own.
template <class W>
class ParentDataElement final : public ConfiguredElement<W, Element> {
public:
  using ConfiguredElement<W, Element>::ConfiguredElement;

  void visitChildren(FunctionRef<void(Element&)> visitor) const override {
    if (child_) visitor(*child_);
  }

  void writeParentData(ParentDataSlot slot) const override {
    if (auto* data = slot.as<typename W::ChildData>()) {
      *data = this->config_.childData();
      return;
    }
    FLTR_EXPECTS(false, "a parent-data widget must be a child of the container it configures");
  }

protected:
  void performRebuild() override {
    child_ = this->updateChild(std::move(child_), this->config_.child());
  }

private:
  ElementPtr child_;
};

// ---------------------------------------------------------------------------
// Render object elements
// ---------------------------------------------------------------------------

/// The seam between the element tree and the render tree. The render tree owns
/// its nodes; an element holds a raw pointer to the one it created.
class RenderObjectElement : public Element {
public:
  RenderBox* renderObject() const noexcept final { return renderObject_; }
  void writeParentData(ParentDataSlot) const final {}
  void detachRenderObject() final;

protected:
  RenderObjectElement* asRenderObjectElement() noexcept final { return this; }

  virtual std::unique_ptr<RenderBox> createRenderObject() = 0;
  virtual void updateRenderObject() = 0;
  virtual void rebuildChildren() {}

  virtual void insertRenderObjectChild(std::unique_ptr<RenderBox> child) = 0;
  virtual void removeRenderObjectChild(RenderBox& child) = 0;

  void performRebuild() final;

  RenderBox* renderObject_ = nullptr;
};

template <class W>
class LeafRenderElement final : public ConfiguredElement<W, RenderObjectElement> {
public:
  using ConfiguredElement<W, RenderObjectElement>::ConfiguredElement;

protected:
  std::unique_ptr<RenderBox> createRenderObject() override {
    BuildContext context(*this);
    return this->config_.createRenderObject(context);
  }
  void updateRenderObject() override {
    BuildContext context(*this);
    this->config_.updateRenderObject(context, render());
  }
  void insertRenderObjectChild(std::unique_ptr<RenderBox>) override {
    FLTR_EXPECTS(false, "a leaf render object cannot take a child");
  }
  void removeRenderObjectChild(RenderBox&) override {
    FLTR_EXPECTS(false, "a leaf render object has no children to remove");
  }

private:
  typename W::Render& render() const noexcept {
    return static_cast<typename W::Render&>(*this->renderObject_);
  }
};

template <class W>
class SingleChildRenderElement final : public ConfiguredElement<W, RenderObjectElement> {
public:
  using ConfiguredElement<W, RenderObjectElement>::ConfiguredElement;

  void visitChildren(FunctionRef<void(Element&)> visitor) const override {
    if (child_) visitor(*child_);
  }

protected:
  std::unique_ptr<RenderBox> createRenderObject() override {
    BuildContext context(*this);
    return this->config_.createRenderObject(context);
  }
  void updateRenderObject() override {
    BuildContext context(*this);
    this->config_.updateRenderObject(context, render());
  }
  void rebuildChildren() override {
    child_ = this->updateChild(std::move(child_), this->config_.child());
  }
  void insertRenderObjectChild(std::unique_ptr<RenderBox> child) override {
    render().setChild(std::move(child));
  }
  void removeRenderObjectChild(RenderBox&) override { render().setChild(nullptr); }

private:
  typename W::Render& render() const noexcept {
    return static_cast<typename W::Render&>(*this->renderObject_);
  }

  ElementPtr child_;
};

template <class W>
class MultiChildRenderElement final : public ConfiguredElement<W, RenderObjectElement> {
public:
  using ConfiguredElement<W, RenderObjectElement>::ConfiguredElement;

  void visitChildren(FunctionRef<void(Element&)> visitor) const override {
    children_.visit(visitor);
  }

protected:
  std::unique_ptr<RenderBox> createRenderObject() override {
    BuildContext context(*this);
    return this->config_.createRenderObject(context);
  }
  void updateRenderObject() override {
    BuildContext context(*this);
    this->config_.updateRenderObject(context, render());
  }
  void rebuildChildren() override {
    const WidgetList children = this->config_.children();
    if (children.stale()) return;
    children_.update(*this, children);
    syncRenderChildren();
  }
  void insertRenderObjectChild(std::unique_ptr<RenderBox> child) override {
    render().addChild(std::move(child));
  }
  void removeRenderObjectChild(RenderBox& child) override {
    render().removeChildAt(render().indexOfChild(child));
  }

private:
  using Render = typename W::Render;

  Render& render() const noexcept { return static_cast<Render&>(*this->renderObject_); }

  /// Children are appended as they are inflated, so the container's order is
  /// only correct once the element list is final.
  ///
  /// Each slot's data is gathered into a default-initialised value and written
  /// once, so an unchanged rebuild leaves the container untouched and a removed
  /// parent-data widget clears what it had set.
  void syncRenderChildren() {
    order_.clear();
    order_.reserve(children_.size());
    for (std::size_t i = 0; i < children_.size(); ++i) {
      RenderBox* child = children_[i].renderObject();
      FLTR_EXPECTS(child != nullptr, "every child of a container must produce a render object");
      order_.push_back(child);
    }
    render().reorderChildren(order_);
    for (std::size_t i = 0; i < children_.size(); ++i) {
      typename Render::ChildData data;
      children_[i].writeParentData(ParentDataSlot(data));
      render().setChildData(i, data);
    }
  }

  ChildList children_;
  std::vector<RenderBox*> order_;
};

// ---------------------------------------------------------------------------
// Widget categories
// ---------------------------------------------------------------------------

class StatelessWidget : public Widget {
public:
  template <class W>
  using ElementFor = StatelessElement<W>;

protected:
  using Widget::Widget;
  ~StatelessWidget() = default;
};

class StatefulWidget : public Widget {
public:
  template <class W>
  using ElementFor = StatefulElement<W>;

protected:
  using Widget::Widget;
  ~StatefulWidget() = default;
};

class ParentDataWidget : public Widget {
public:
  template <class W>
  using ElementFor = ParentDataElement<W>;

protected:
  using Widget::Widget;
  ~ParentDataWidget() = default;
};

class LeafRenderObjectWidget : public Widget {
public:
  template <class W>
  using ElementFor = LeafRenderElement<W>;

protected:
  using Widget::Widget;
  ~LeafRenderObjectWidget() = default;
};

class SingleChildRenderObjectWidget : public Widget {
public:
  template <class W>
  using ElementFor = SingleChildRenderElement<W>;

protected:
  using Widget::Widget;
  ~SingleChildRenderObjectWidget() = default;
};

class MultiChildRenderObjectWidget : public Widget {
public:
  template <class W>
  using ElementFor = MultiChildRenderElement<W>;

protected:
  using Widget::Widget;
  ~MultiChildRenderObjectWidget() = default;
};

/// Supplies what every concrete widget would otherwise repeat: its runtime
/// type, the element that carries it, and the arena-allocating factory that call
/// sites use.
template <class Derived, class Base>
class Configure : public Base {
public:
  using Base::Base;

  WidgetType type() const noexcept final { return widgetTypeOf<Derived>(); }

  ElementPtr createElement() const final {
    return std::make_unique<typename Base::template ElementFor<Derived>>(
        static_cast<const Derived&>(*this));
  }

  /// `Self` defers naming `Derived::Args` until the call, by which point the
  /// derived widget is complete. It never deduces: a braced initialiser is a
  /// non-deduced context, so the default always wins.
  template <class Self = Derived>
  static WidgetRef make(const typename Self::Args& args) {
    return newWidget<Self>(args);
  }

protected:
  ~Configure() = default;
};

// ---------------------------------------------------------------------------
// BuildOwner
// ---------------------------------------------------------------------------

/// Drives the build phase and holds the dirty element set, mirroring what
/// PipelineOwner does for layout and paint.
class BuildOwner {
public:
  BuildOwner(TextService& textService, PointerBinding& pointerBinding) noexcept
      : textService_(&textService), pointerBinding_(&pointerBinding) {}
  ~BuildOwner();

  BuildOwner(const BuildOwner&) = delete;
  BuildOwner& operator=(const BuildOwner&) = delete;

  Arena& arena() noexcept { return arena_; }

  /// The ambient services a widget builds against. Both are supplied by the
  /// consumer and neither is owned here.
  TextService& textService() const noexcept { return *textService_; }
  PointerBinding& pointerBinding() const noexcept { return *pointerBinding_; }

  bool needsBuild() const noexcept { return !dirty_.empty(); }
  std::size_t dirtyElementCount() const noexcept { return dirty_.size(); }

  /// Rebuilds every dirty element shallowest first, so a parent's rebuild
  /// subsumes dirty descendants.
  void flushBuild();

  int buildCount() const noexcept { return buildCount_; }
  void resetBuildCount() noexcept { buildCount_ = 0; }

  void adoptRootRenderObject(std::unique_ptr<RenderBox> root);
  std::unique_ptr<RenderBox> releaseRootRenderObject() noexcept;
  RenderBox* rootRenderObject() const noexcept { return rootRenderObject_.get(); }

private:
  friend class Element;

  void scheduleBuild(Element& element);
  void stopTracking(Element& element);

  std::vector<Element*> dirty_;
  std::vector<Element*> buildScratch_;
  Arena arena_;
  TextService* textService_;
  PointerBinding* pointerBinding_;
  std::unique_ptr<RenderBox> rootRenderObject_;
  int buildCount_ = 0;
};

inline BuildOwner& BuildContext::owner() const noexcept { return *element_->owner(); }
inline TextService& BuildContext::textService() const noexcept {
  return element_->owner()->textService();
}
inline PointerBinding& BuildContext::pointerBinding() const noexcept {
  return element_->owner()->pointerBinding();
}
inline bool BuildContext::mounted() const noexcept { return element_->mounted(); }

}  // namespace fltr
