#include "fltr/focus/manager.hpp"

#include <algorithm>
#include <cmath>

namespace fltr {

namespace {

bool isVertical(TraversalDirection direction) noexcept {
  return direction == TraversalDirection::Up || direction == TraversalDirection::Down;
}

/// How far `delta` goes the way we were asked to go. Negative is backwards.
float advance(TraversalDirection direction, Offset delta) noexcept {
  switch (direction) {
    case TraversalDirection::Up: return -delta.dy;
    case TraversalDirection::Down: return delta.dy;
    case TraversalDirection::Left: return -delta.dx;
    case TraversalDirection::Right: return delta.dx;
  }
  return 0.0f;
}

float sideways(TraversalDirection direction, Offset delta) noexcept {
  return std::fabs(isVertical(direction) ? delta.dx : delta.dy);
}

/// Whether two rects overlap on the axis we are *not* travelling along, which is
/// what makes "the row below this one" beat "the row below and three columns
/// across".
bool sharesBand(TraversalDirection direction, Rect from, Rect to) noexcept {
  return isVertical(direction) ? from.left < to.right && to.left < from.right
                               : from.top < to.bottom && to.top < from.bottom;
}

bool contains(const std::vector<FocusNode*>& nodes, const FocusNode* node) noexcept {
  return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
}

}  // namespace

FocusManager::FocusManager(KeyboardBinding& keyboard, FocusScopeNode& root)
    : keyboard_(&keyboard), root_(&root) {
  root_->setManager(this);
  keyboard_->addHandler(*this);
}

FocusManager::~FocusManager() {
  keyboard_->removeHandler(*this);
  if (root_) root_->setManager(nullptr);
}

void FocusManager::forgetRoot(const FocusNode& node) noexcept {
  if (root_ != &node) return;
  root_ = nullptr;
  // The tree this manager was built around is going, so there is nowhere left
  // for the focus to be. Stated here rather than left to the children detaching
  // on the way down: those would land the focus on `node.enclosingScope()`, and
  // the only reason that is not this dying node is that `asScope()` is already
  // answering as the base class by the time a destructor orphans its children.
  // Depending on that is depending on the vptr, so the invariant is written
  // instead. Nothing is notified -- every listener that would hear it is being
  // destroyed alongside the tree.
  primary_ = nullptr;
}

// ---------------------------------------------------------------------------
// Where the focus is
// ---------------------------------------------------------------------------

bool FocusManager::requestFocus(FocusNode& node) {
  FocusNode* target = &node;
  // A scope hands the focus on to whatever it remembers, recursively, and takes
  // it itself only when it remembers nothing focusable.
  while (FocusScopeNode* scope = target->asScope()) {
    FocusNode* remembered = scope->focusedChild_;
    if (!remembered || !remembered->isFocusable()) break;
    target = remembered;
  }
  if (!target->isFocusable()) return false;
  setPrimary(target);
  return true;
}

void FocusManager::autofocus(FocusNode& node) {
  FocusScopeNode* scope = node.enclosingScope();
  if (!scope) return;
  if (scope->focusedChild_ == nullptr && requestFocus(node)) return;
  // Refused because something else in this scope holds the focus. The claim is
  // kept rather than dropped, and the first to ask wins -- so a screen built
  // behind the one it replaces is focused as soon as that one is gone, and does
  // not steal the focus in the meantime.
  if (scope->pendingAutofocus_ == nullptr) scope->pendingAutofocus_ = &node;
}

void FocusManager::grantPendingAutofocus(FocusScopeNode& scope) {
  FocusNode* pending = scope.pendingAutofocus_;
  if (pending == nullptr || scope.focusedChild_ != nullptr) return;
  // The claim survives a refusal: a node that is between parents this instant --
  // an element reconciled onto another one's slot -- asks again from `attach`.
  if (requestFocus(*pending)) scope.pendingAutofocus_ = nullptr;
}

void FocusManager::unfocus(FocusNode& node) {
  if (!primary_ || (primary_ != &node && !primary_->isDescendantOf(node))) return;
  FocusScopeNode* scope = node.enclosingScope();
  // Forgotten as well as left, or the scope would hand the focus straight back.
  if (scope) scope->focusedChild_ = nullptr;
  setPrimary(scope);
  if (scope) grantPendingAutofocus(*scope);
}

void FocusManager::setPrimary(FocusNode* node) {
  if (primary_ == node) return;

  before_.clear();
  for (FocusNode* walk = primary_; walk; walk = walk->parent()) before_.push_back(walk);

  primary_ = node;
  after_.clear();
  FocusNode* below = nullptr;
  for (FocusNode* walk = primary_; walk; walk = walk->parent()) {
    after_.push_back(walk);
    if (FocusScopeNode* scope = walk->asScope(); scope && below) scope->focusedChild_ = below;
    below = walk;
  }

  const std::uint32_t generation = ++notifyGeneration_;
  const auto notify = [&](FocusNode* changed) {
    if (notifyGeneration_ == generation) changed->notifyFocusChanged();
  };
  for (std::size_t i = 0; i < before_.size(); ++i) {
    if (!contains(after_, before_[i])) notify(before_[i]);
  }
  for (std::size_t i = 0; i < after_.size(); ++i) {
    if (!contains(before_, after_[i])) notify(after_[i]);
  }
  // Both ends of the move stay on their chain when one is an ancestor of the
  // other, so `hasFocus` did not change for them but `hasPrimaryFocus` did.
  if (!before_.empty() && contains(after_, before_.front())) notify(before_.front());
  if (primary_ && contains(before_, primary_)) notify(primary_);
}

void FocusManager::willDetach(FocusNode& node) {
  std::replace(dispatching_.begin(), dispatching_.end(), &node, static_cast<FocusNode*>(nullptr));
  const auto leaving = [&node](const FocusNode* other) {
    return other != nullptr && (other == &node || other->isDescendantOf(node));
  };
  for (FocusNode* walk = node.parent(); walk; walk = walk->parent()) {
    FocusScopeNode* scope = walk->asScope();
    if (!scope) continue;
    if (leaving(scope->focusedChild_)) scope->focusedChild_ = nullptr;
    if (leaving(scope->pendingAutofocus_)) scope->pendingAutofocus_ = nullptr;
  }

  FocusScopeNode* scope = node.enclosingScope();
  if (primary_ && (primary_ == &node || primary_->isDescendantOf(node))) setPrimary(scope);
  // The screen that held the focus is going away, which is the moment a screen
  // built behind it has been waiting for. Checked even when this node was not
  // the focused one: an excluded subtree hands the focus back to its scope
  // before it unmounts, and the claim is still outstanding when it does.
  if (scope) grantPendingAutofocus(*scope);
}

void FocusManager::didChangePolicy(FocusNode& node) {
  if (!primary_ || (primary_ != &node && !primary_->isDescendantOf(node))) return;
  if (primary_->isFocusable()) return;

  FocusScopeNode* scope = node.enclosingScope();
  if (node.isFocusable()) {
    setPrimary(&node);
    return;
  }
  // A subtree that has just excluded itself -- a page on its way out -- is
  // forgotten as well as left. Remembering it would keep the scope looking
  // occupied to anything else that wants the focus, which is what an incoming
  // page's autofocus is.
  if (scope) scope->focusedChild_ = nullptr;
  setPrimary(scope);
  if (scope) grantPendingAutofocus(*scope);
}

std::size_t FocusManager::nodeCount() const noexcept {
  const auto count = [](auto&& self, const FocusNode& node) -> std::size_t {
    std::size_t total = 0;
    for (const FocusNode* child : node.children()) total += 1 + self(self, *child);
    return total;
  };
  return root_ ? count(count, *root_) : 0;
}

// ---------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------

bool FocusManager::handleKey(const KeyEvent& event) {
  dispatching_.clear();
  for (FocusNode* walk = primary_ ? primary_ : root_; walk; walk = walk->parent()) {
    dispatching_.push_back(walk);
  }
  for (std::size_t i = 0; i < dispatching_.size(); ++i) {
    FocusNode* node = dispatching_[i];
    if (node && node->onKey && node->onKey(event)) {
      dispatching_.clear();
      return true;
    }
  }
  dispatching_.clear();
  return handleTraversalKey(event);
}

bool FocusManager::handleTraversalKey(const KeyEvent& event) {
  const FocusScopeNode* scope = scopeForTraversal();
  if (!scope || event.type == KeyEventType::Up) return false;
  switch (event.logical) {
    case LogicalKey::Tab:
      if (!scope->tabTraversal()) return false;
      return event.modifiers.has(KeyModifier::Shift) ? previousFocus() : nextFocus();
    case LogicalKey::ArrowUp:
      return scope->directionalTraversal() && moveFocusInDirection(TraversalDirection::Up);
    case LogicalKey::ArrowDown:
      return scope->directionalTraversal() && moveFocusInDirection(TraversalDirection::Down);
    case LogicalKey::ArrowLeft:
      return scope->directionalTraversal() && moveFocusInDirection(TraversalDirection::Left);
    case LogicalKey::ArrowRight:
      return scope->directionalTraversal() && moveFocusInDirection(TraversalDirection::Right);
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Traversal
// ---------------------------------------------------------------------------

FocusScopeNode* FocusManager::scopeForTraversal() const noexcept {
  if (primary_) {
    if (FocusScopeNode* scope = primary_->asScope()) return scope;
    if (FocusScopeNode* scope = primary_->enclosingScope()) return scope;
  }
  return root_;
}

void FocusManager::gatherTraversable(const FocusNode& node, std::vector<FocusNode*>& out) const {
  if (!node.descendantsAreFocusable()) return;
  for (FocusNode* child : node.children()) {
    if (child->canRequestFocus() && !child->skipTraversal()) out.push_back(child);
    gatherTraversable(*child, out);
  }
}

bool FocusManager::nextFocus() { return moveTab(1); }

bool FocusManager::previousFocus() { return moveTab(-1); }

bool FocusManager::moveTab(int step) {
  const FocusScopeNode* scope = scopeForTraversal();
  if (!scope) return false;
  candidates_.clear();
  gatherTraversable(*scope, candidates_);
  if (candidates_.empty()) return false;

  const auto found = std::find(candidates_.begin(), candidates_.end(), primary_);
  const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(candidates_.size());
  const std::ptrdiff_t from = found == candidates_.end() ? (step > 0 ? -1 : 0)
                                                         : std::distance(candidates_.begin(), found);
  const std::ptrdiff_t to = ((from + step) % count + count) % count;
  return requestFocus(*candidates_[static_cast<std::size_t>(to)]);
}

bool FocusManager::moveFocusInDirection(TraversalDirection direction) {
  const FocusScopeNode* scope = scopeForTraversal();
  if (!scope) return false;
  candidates_.clear();
  gatherTraversable(*scope, candidates_);
  if (candidates_.empty()) return false;

  const Rect from = primary_ ? primary_->rect() : Rect::zero();
  // Nothing focused, or focused on something that has never been laid out: the
  // direction says nothing, so this is just an entry into the scope.
  if (from.isEmpty()) return requestFocus(*candidates_.front());

  FocusNode* best = nullptr;
  bool bestInBand = false;
  float bestAhead = 0.0f;
  float bestAside = 0.0f;
  for (FocusNode* candidate : candidates_) {
    if (candidate == primary_) continue;
    const Rect rect = candidate->rect();
    if (rect.isEmpty()) continue;
    const Offset delta = rect.center() - from.center();
    const float ahead = advance(direction, delta);
    if (ahead <= 0.0f) continue;
    const float aside = sideways(direction, delta);
    const bool inBand = sharesBand(direction, from, rect);
    // In-band candidates are ranked by how far ahead they are; everything else
    // by plain distance, so a diagonal never beats something straight ahead.
    const bool better =
        best == nullptr || (inBand && !bestInBand) ||
        (inBand == bestInBand &&
         (inBand ? (ahead < bestAhead || (ahead == bestAhead && aside < bestAside))
                 : (ahead * ahead + aside * aside < bestAhead * bestAhead + bestAside * bestAside)));
    if (!better) continue;
    best = candidate;
    bestInBand = inBand;
    bestAhead = ahead;
    bestAside = aside;
  }
  return best != nullptr && requestFocus(*best);
}

}  // namespace fltr
