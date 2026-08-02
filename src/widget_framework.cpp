#include "fltr/widgets/framework.hpp"

#include <algorithm>

namespace fltr {
namespace {

thread_local Arena* tBuildArena = nullptr;

/// Set for as long as an element is building, however that build is left --
/// including on a contract violation, which throws.
class BuildFlag {
public:
  explicit BuildFlag(bool& flag) noexcept : flag_(flag) { flag_ = true; }
  ~BuildFlag() { flag_ = false; }

  BuildFlag(const BuildFlag&) = delete;
  BuildFlag& operator=(const BuildFlag&) = delete;

private:
  bool& flag_;
};

}  // namespace

Arena* currentBuildArena() noexcept { return tBuildArena; }

BuildScope::BuildScope(Arena& arena) noexcept : arena_(&arena) {
  FLTR_EXPECTS(tBuildArena == nullptr, "build scopes do not nest");
  tBuildArena = arena_;
}

BuildScope::~BuildScope() {
  tBuildArena = nullptr;
  arena_->reset();
}

bool isStaleGeneration(std::uint32_t generation) noexcept {
  return tBuildArena == nullptr || generation != tBuildArena->generation();
}

WidgetRef::WidgetRef(const Widget& widget, std::uint32_t generation) noexcept
    : widget_(&widget), type_(widget.type()), key_(widget.key()), generation_(generation) {}

bool WidgetRef::canUpdate(const Widget& existing) const noexcept {
  return existing.type() == type_ && existing.key() == key_;
}

const Widget* WidgetRef::get() const {
  FLTR_EXPECTS(!stale(),
               "a widget from an earlier build was dereferenced after its arena was released");
  return widget_;
}

WidgetList::WidgetList(std::initializer_list<WidgetRef> widgets) {
  Arena* arena = currentBuildArena();
  FLTR_EXPECTS(arena != nullptr, "widget lists may only be created during a build scope");

  WidgetRef* items = static_cast<WidgetRef*>(
      arena->allocate(sizeof(WidgetRef) * widgets.size(), alignof(WidgetRef)));
  std::size_t kept = 0;
  for (const WidgetRef& widget : widgets) {
    if (widget) items[kept++] = widget;
  }
  items_ = items;
  size_ = kept;
  generation_ = arena->generation();
}

WidgetList WidgetList::generate(std::size_t count, FunctionRef<WidgetRef(std::size_t)> item) {
  Arena* arena = currentBuildArena();
  FLTR_EXPECTS(arena != nullptr, "widget lists may only be created during a build scope");

  // The array is reserved before the items are built, which a bump allocator
  // makes safe: building an item allocates after it and never moves it.
  WidgetRef* items =
      static_cast<WidgetRef*>(arena->allocate(sizeof(WidgetRef) * count, alignof(WidgetRef)));
  std::size_t kept = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (const WidgetRef widget = item(i)) items[kept++] = widget;
  }

  WidgetList list;
  list.items_ = items;
  list.size_ = kept;
  list.generation_ = arena->generation();
  return list;
}

// ---------------------------------------------------------------------------
// Element
// ---------------------------------------------------------------------------

void Element::mount(Element* parent, BuildOwner& owner) {
  FLTR_EXPECTS(!mounted(), "an element may only be mounted once");
  parent_ = parent;
  owner_ = &owner;
  depth_ = parent ? parent->depth_ + 1 : 0;
  inheritedScope_ = parent ? parent->inheritedScope_ : nullptr;
  extendInheritedScope();
  didMount();
  rebuild();
}

void Element::unmount() {
  visitChildren([](Element& child) { child.unmount(); });
  dependencies_.clear();
  if (inDirtyList_) owner_->stopTracking(*this);
  owner_ = nullptr;
  parent_ = nullptr;
  inheritedScope_ = nullptr;
}

void Element::markNeedsBuild() {
  if (dirty_) return;
  dirty_ = true;
  if (owner_) owner_->scheduleBuild(*this);
}

void Element::rebuild() {
  if (!dirty_ || !mounted()) return;
  dirty_ = false;
  // The build about to run re-registers whatever it reads, so an ambient value
  // this element stops reading stops reaching it.
  dependencies_.clear();
  const BuildFlag building(inBuild_);
  performRebuild();
}

RenderBox* Element::renderObject() const {
  RenderBox* found = nullptr;
  visitChildren([&found](Element& child) {
    if (!found) found = child.renderObject();
  });
  return found;
}

void Element::writeParentData(ParentDataSlot slot) const {
  visitChildren([&slot](Element& child) { child.writeParentData(slot); });
}

void Element::detachRenderObject() {
  visitChildren([](Element& child) { child.detachRenderObject(); });
}

RenderObjectElement* Element::ancestorRenderObjectElement() const {
  for (Element* e = parent_; e != nullptr; e = e->parent_) {
    if (RenderObjectElement* found = e->asRenderObjectElement()) return found;
  }
  return nullptr;
}

ElementPtr Element::inflate(WidgetRef widget) {
  FLTR_EXPECTS(!widget.stale(),
               "a widget from a released build reached inflation, so the element that held it "
               "is gone and its configuration cannot be recovered");
  ElementPtr child = widget->createElement();
  child->mount(this, *owner_);
  return child;
}

void Element::deactivate(ElementPtr child) {
  child->detachRenderObject();
  child->unmount();
}

ElementPtr Element::updateChild(ElementPtr child, WidgetRef widget) {
  if (!widget) {
    if (child) deactivate(std::move(child));
    return nullptr;
  }
  if (child) {
    if (widget.canUpdate(*child->widget())) {
      // A stale ref is the configuration this child already holds, so there is
      // nothing to adopt and the subtree can be skipped entirely.
      if (!widget.stale()) child->update(widget);
      return child;
    }
    deactivate(std::move(child));
  }
  return inflate(widget);
}

// ---------------------------------------------------------------------------
// ChildList
// ---------------------------------------------------------------------------

void ChildList::update(Element& parent, WidgetList widgets) {
  const std::size_t oldCount = children_.size();
  const std::size_t newCount = widgets.size();

  next_.clear();
  next_.resize(newCount);

  // Leading run matched by position -- the whole list when nothing structural
  // changed.
  std::size_t oldTop = 0;
  std::size_t newTop = 0;
  while (oldTop < oldCount && newTop < newCount &&
         widgets[newTop].canUpdate(*children_[oldTop]->widget())) {
    next_[newTop] = parent.updateChild(std::move(children_[oldTop]), widgets[newTop]);
    ++oldTop;
    ++newTop;
  }

  // Trailing run: matched here, but placed last because the middle decides how
  // many of them there are.
  std::size_t oldEnd = oldCount;
  std::size_t newEnd = newCount;
  while (oldTop < oldEnd && newTop < newEnd &&
         widgets[newEnd - 1].canUpdate(*children_[oldEnd - 1]->widget())) {
    --oldEnd;
    --newEnd;
  }

  // In the middle an unkeyed child is discarded, while a keyed one is held
  // aside and reclaimed wherever its key reappears.
  keyed_.clear();
  for (std::size_t i = oldTop; i < oldEnd; ++i) {
    const Key key = children_[i]->widget()->key();
    if (key.isSet()) {
      keyed_.push_back({key, std::move(children_[i])});
    } else {
      parent.deactivate(std::move(children_[i]));
    }
  }

  for (; newTop < newEnd; ++newTop) {
    const WidgetRef widget = widgets[newTop];
    ElementPtr reused;
    if (widget.key().isSet()) {
      for (KeyedChild& candidate : keyed_) {
        if (candidate.element && candidate.key == widget.key() &&
            widget.canUpdate(*candidate.element->widget())) {
          reused = std::move(candidate.element);
          break;
        }
      }
    }
    next_[newTop] = parent.updateChild(std::move(reused), widget);
  }

  for (KeyedChild& leftover : keyed_) {
    if (leftover.element) parent.deactivate(std::move(leftover.element));
  }
  keyed_.clear();

  for (; newTop < newCount; ++newTop, ++oldEnd) {
    next_[newTop] = parent.updateChild(std::move(children_[oldEnd]), widgets[newTop]);
  }

  children_.swap(next_);
  next_.clear();
}

// ---------------------------------------------------------------------------
// RenderObjectElement
// ---------------------------------------------------------------------------

void RenderObjectElement::performRebuild() {
  if (!renderObject_) {
    std::unique_ptr<RenderBox> created = createRenderObject();
    FLTR_EXPECTS(created != nullptr, "createRenderObject must return a render object");
    renderObject_ = created.get();
    if (RenderObjectElement* host = ancestorRenderObjectElement()) {
      host->insertRenderObjectChild(std::move(created));
    } else {
      owner()->adoptRootRenderObject(std::move(created));
    }
  }
  updateRenderObject();
  rebuildChildren();
}

void RenderObjectElement::detachRenderObject() {
  if (!renderObject_) return;
  if (RenderObjectElement* host = ancestorRenderObjectElement()) {
    host->removeRenderObjectChild(*renderObject_);
  } else {
    owner()->releaseRootRenderObject();
  }
  renderObject_ = nullptr;
}

// ---------------------------------------------------------------------------
// BuildOwner
// ---------------------------------------------------------------------------

BuildOwner::~BuildOwner() = default;

void BuildOwner::scheduleBuild(Element& element) {
  element.inDirtyList_ = true;
  dirty_.push_back(&element);
}

void BuildOwner::stopTracking(Element& element) {
  element.inDirtyList_ = false;
  std::erase(dirty_, &element);
  // An element dropped by a parent's rebuild can be sitting in the buffer that
  // build is walking. Blank it rather than erase it, so the iterators stay valid.
  std::replace(buildScratch_.begin(), buildScratch_.end(), &element,
               static_cast<Element*>(nullptr));
}

void BuildOwner::flushBuild() {
  if (dirty_.empty()) return;
  BuildScope scope(arena_);

  // Building an element can dirty one not yet visited, so the list is drained
  // rather than swept once. The cap applies in every build, so an element that
  // dirties itself degrades to a stale frame rather than a freeze.
  static constexpr int kMaxBuildPasses = 32;
  int passes = 0;
  while (!dirty_.empty() && passes < kMaxBuildPasses) {
    ++passes;
    buildScratch_.clear();
    buildScratch_.swap(dirty_);
    std::sort(buildScratch_.begin(), buildScratch_.end(),
              [](const Element* a, const Element* b) { return a->depth() < b->depth(); });
    for (Element* element : buildScratch_) {
      if (element == nullptr || !element->mounted()) continue;
      element->inDirtyList_ = false;
      if (!element->dirty()) continue;
      ++buildCount_;
      element->rebuild();
    }
  }
  buildScratch_.clear();

  FLTR_ENSURES(dirty_.empty(),
               "build did not converge: an element keeps dirtying itself while building");
}

void BuildOwner::adoptRootRenderObject(std::unique_ptr<RenderBox> root) {
  FLTR_EXPECTS(rootRenderObject_ == nullptr, "the element tree already has a root render object");
  rootRenderObject_ = std::move(root);
}

std::unique_ptr<RenderBox> BuildOwner::releaseRootRenderObject() noexcept {
  return std::move(rootRenderObject_);
}

}  // namespace fltr
