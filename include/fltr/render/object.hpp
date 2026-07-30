#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fltr/core/config.hpp"
#include "fltr/core/function_ref.hpp"
#include "fltr/core/geometry.hpp"
#include "fltr/core/listenable.hpp"
#include "fltr/paint/display_list.hpp"

namespace fltr {

class PipelineOwner;
class PaintingContext;
class RenderObject;

/// Which phase the pipeline is in. Phase separation is enforced, not merely
/// documented: painting may not dirty layout, and layout may not paint.
enum class PipelinePhase : std::uint8_t { Idle, Layout, Paint };

// ---------------------------------------------------------------------------
// RenderObject
// ---------------------------------------------------------------------------

/// Protocol-agnostic base: tree structure, ownership, invalidation, painting.
///
/// The layout protocol itself lives in RenderBox. Keeping the split means a
/// second protocol (slivers, say) could be added without disturbing the
/// invalidation machinery -- but note that today `layout` is only declared on
/// RenderBox, so a second protocol would first have to introduce a polymorphic
/// Constraints. That is a deliberate simplification: this framework has exactly
/// one protocol and paying for the abstraction now would buy nothing.
class RenderObject {
public:
  virtual ~RenderObject();

  RenderObject(const RenderObject&) = delete;
  RenderObject& operator=(const RenderObject&) = delete;

  // --- tree ---------------------------------------------------------------
  RenderObject* parent() const noexcept { return parent_; }
  PipelineOwner* owner() const noexcept { return owner_; }
  int depth() const noexcept { return depth_; }
  bool attached() const noexcept { return owner_ != nullptr; }

  virtual void visitChildren(FunctionRef<void(RenderObject&)> visitor) const = 0;

  /// Same walk, but also yields where each child is painted within this object.
  /// Overridden by anything that shifts its children.
  virtual void visitChildrenWithOffsets(
      FunctionRef<void(RenderObject&, Offset)> visitor) const {
    visitChildren([&visitor](RenderObject& c) { visitor(c, Offset::zero()); });
  }

  virtual const char* typeName() const = 0;

  /// Extra detail for the headless dump only. Nothing depends on this.
  virtual std::string describe() const { return {}; }

  void attach(PipelineOwner* owner);
  /// Detaches this whole subtree from its pipeline and purges any of its nodes
  /// from the owner's dirty lists, so nothing left there can outlive its node.
  void detach();

  // --- invalidation -------------------------------------------------------

  /// Marks this node as needing layout and registers the nearest enclosing
  /// relayout boundary with the owner. This is why update cost is proportional
  /// to the dirty set: the walk stops at the boundary rather than the root.
  void markNeedsLayout();
  void markParentNeedsLayout();

  /// The cheap path. Paint invalidation never touches layout state; it walks up
  /// to the nearest repaint boundary and stops. This is the path a render
  /// object observing an animation takes on every tick.
  void markNeedsPaint();

  /// Call when the value `sizedByParent()` would return has changed.
  void markNeedsLayoutForSizedByParentChange();

  bool needsLayout() const noexcept { return needsLayout_; }
  bool needsPaint() const noexcept { return needsPaint_; }
  bool isRelayoutBoundary() const noexcept { return relayoutBoundary_ == 1; }

  /// True when this object's size depends only on its constraints, never on its
  /// children. Such an object is always a relayout boundary.
  virtual bool sizedByParent() const { return false; }

  /// True when this object owns its own DisplayList, so repainting it does not
  /// re-record its parent.
  virtual bool isRepaintBoundary() const { return false; }

  // --- painting -----------------------------------------------------------
  virtual void paint(PaintingContext& context, Offset offset) = 0;

  /// Wraps `paint` with the bookkeeping and invariant checks. Callers use this,
  /// never `paint` directly.
  void paintWithContext(PaintingContext& context, Offset offset);

  /// Only meaningful for repaint boundaries; created lazily.
  DisplayList& boundaryList();
  const DisplayList* boundaryListIfAny() const noexcept { return boundaryList_.get(); }

  /// The bounds this object paints into, in its own coordinate space. Used for
  /// the harness dump and, later, for a compositing layer's bounds.
  virtual Rect paintBounds() const { return Rect::zero(); }

  // --- counters used by tests to prove work is bounded --------------------
  std::uint32_t layoutCount() const noexcept { return layoutCount_; }
  std::uint32_t paintCount() const noexcept { return paintCount_; }

  /// Implemented by the protocol layer; re-runs layout with stored constraints.
  virtual void layoutWithoutResize() = 0;

protected:
  RenderObject() = default;

  void adoptChild(RenderObject* child);
  void dropChild(RenderObject* child);

  /// Subscribe this render object to a listenable so that a change invalidates
  /// only painting. This is the render-attached animation path: no element
  /// rebuild, no reconciliation, no per-frame widget allocation.
  ///
  /// `slot` is a member of the concrete render object, so the subscription's
  /// lifetime is exactly the render object's lifetime with no shared ownership.
  void observeForPaint(Subscription& slot, Listenable* source) {
    slot.detach();
    if (source) {
      source->subscribe(
          slot, [](void* p) { static_cast<RenderObject*>(p)->markNeedsPaint(); }, this);
    }
  }

  /// The expensive counterpart, for animated properties that affect layout.
  /// Named differently on purpose: the call site should say which it is.
  void observeForLayout(Subscription& slot, Listenable* source) {
    slot.detach();
    if (source) {
      source->subscribe(
          slot, [](void* p) { static_cast<RenderObject*>(p)->markNeedsLayout(); }, this);
    }
  }

  std::uint32_t layoutCount_ = 0;
  std::uint32_t paintCount_ = 0;
  bool needsLayout_ = true;
  bool needsPaint_ = true;

  /// Tri-state: -1 unknown (never laid out, or freshly reparented), 0 no, 1 yes.
  /// Unknown behaves as "no" for propagation, which is the safe direction.
  signed char relayoutBoundary_ = -1;

private:
  friend class PipelineOwner;
  friend class PaintingContext;

  void redepthChild(RenderObject* child);
  void resetRelayoutBoundarySubtree();
  void detachSubtree();

  RenderObject* parent_ = nullptr;
  PipelineOwner* owner_ = nullptr;
  int depth_ = 0;
  bool wasRepaintBoundary_ = false;
  std::unique_ptr<DisplayList> boundaryList_;
};

// ---------------------------------------------------------------------------
// PaintingContext
// ---------------------------------------------------------------------------

/// Records painting into a DisplayList.
///
/// A repaint boundary child is recorded into its *own* list, positioned by a
/// DrawList command in the parent. The child's list is recorded relative to its
/// own origin, so moving a boundary does not require re-recording it -- only the
/// parent's DrawList offset changes.
class PaintingContext {
public:
  explicit PaintingContext(DisplayList& list) : list_(list) {}

  DisplayList& list() noexcept { return list_; }

  void paintChild(RenderObject& child, Offset offset);

  void pushOpacity(float alpha, FunctionRef<void(PaintingContext&)> painter) {
    if (alpha >= 1.0f) {
      painter(*this);
      return;
    }
    if (alpha <= 0.0f) return;  // fully transparent: record nothing at all
    list_.pushOpacity(alpha);
    painter(*this);
    list_.pop();
  }

  /// `transform` is applied about `pivot` in the current painting space, which
  /// is exactly the point hit testing inverts about.
  void pushTransform(const Transform2D& transform, Offset pivot,
                     FunctionRef<void(PaintingContext&)> painter) {
    if (transform.isIdentity()) {
      painter(*this);
      return;
    }
    list_.pushTransform(Transform2D::aroundOrigin(transform, pivot));
    painter(*this);
    list_.pop();
  }

  void pushClipRect(Rect clip, BorderRadius radius, FunctionRef<void(PaintingContext&)> painter) {
    list_.pushClipRect(clip, radius);
    painter(*this);
    list_.pop();
  }

private:
  DisplayList& list_;
};

// ---------------------------------------------------------------------------
// PipelineOwner
// ---------------------------------------------------------------------------

/// Drives the render tree through its phases and holds the dirty sets.
///
/// The two dirty lists are separate on purpose. Layout invalidation and paint
/// invalidation are different costs, and an animation that only affects painting
/// must never touch the layout list.
class PipelineOwner {
public:
  PipelineOwner() = default;
  ~PipelineOwner();

  PipelineOwner(const PipelineOwner&) = delete;
  PipelineOwner& operator=(const PipelineOwner&) = delete;

  /// Installs the render tree. The root must outlive the owner, or be removed
  /// with `setRootNode(nullptr)` first.
  void setRootNode(RenderObject* root);
  RenderObject* rootNode() const noexcept { return root_; }

  /// Drops every dirty-list entry whose node no longer belongs to this pipeline.
  /// Called once per detach rather than once per detached node, so tearing down
  /// a subtree stays O(subtree + dirty) instead of O(subtree x dirty).
  void purgeDetachedDirtyNodes();

  PipelinePhase phase() const noexcept { return phase_; }

  /// True when some node is dirty and a frame would do work. When this is false
  /// `drawFrame` is a no-op and the scene's revision will not change.
  bool needsFrame() const noexcept {
    return !nodesNeedingLayout_.empty() || !nodesNeedingPaint_.empty();
  }

  void requestVisualUpdate() noexcept { visualUpdateRequested_ = true; }
  bool consumeVisualUpdateRequest() noexcept {
    const bool v = visualUpdateRequested_;
    visualUpdateRequested_ = false;
    return v;
  }

  /// Lays out every dirty relayout boundary, shallowest first, so a parent's
  /// layout subsumes any of its descendants that were also dirty.
  void flushLayout();

  /// Repaints every dirty repaint boundary, deepest first, so a boundary that
  /// repaints on its own is already clean by the time an ancestor embeds it.
  void flushPaint();

  /// What the consumer submits. `Scene::revision` is unchanged exactly when no
  /// display list was re-recorded, so the consumer may resubmit last frame's
  /// translated state verbatim.
  Scene scene() const;

  /// One frame, as the game loop calls it. Resets the frame stats, flushes both
  /// phases in order, and hands back what to submit.
  ///
  /// Calling this when nothing is dirty does no work at all: no layout, no
  /// paint, no recording, and a Scene whose revision is unchanged from last
  /// frame. That is the steady state, and it is meant to be cheap enough to call
  /// unconditionally every frame rather than guarding it with `needsFrame()`.
  Scene drawFrame();

  // --- per-frame accounting, used by tests --------------------------------
  struct FrameStats {
    int layouts = 0;   ///< performLayout / performResize invocations
    int paints = 0;    ///< paintWithContext invocations
    int boundariesRelaidOut = 0;
    int boundariesRepainted = 0;
    /// Times the layout dirty list had to be drained. A healthy frame is 0 (no
    /// work) or 1; more means laying a node out dirtied an ancestor that had not
    /// been visited yet, which is legal but worth noticing.
    int layoutPasses = 0;
  };
  const FrameStats& stats() const noexcept { return stats_; }
  void resetStats() noexcept { stats_ = {}; }

  std::size_t dirtyLayoutCount() const noexcept { return nodesNeedingLayout_.size(); }
  std::size_t dirtyPaintCount() const noexcept { return nodesNeedingPaint_.size(); }

  // --- internal, used by the protocol layer -------------------------------
  /// The node whose performLayout is currently running. Used to enforce that a
  /// parent only reads a child's size when it asked for it.
  RenderObject* activeLayoutNode() const noexcept { return activeLayoutNode_; }
  void setActiveLayoutNode(RenderObject* n) noexcept { activeLayoutNode_ = n; }
  FrameStats& mutableStats() noexcept { return stats_; }
  void setPhase(PipelinePhase p) noexcept { phase_ = p; }

private:
  friend class RenderObject;
  friend class PaintingContext;

  RenderObject* root_ = nullptr;
  std::vector<RenderObject*> nodesNeedingLayout_;
  std::vector<RenderObject*> nodesNeedingPaint_;
  /// Each flush drains its dirty list into one of these so that nodes dirtied
  /// mid-flush land in a fresh list. They are members rather than locals so a
  /// frame that does work still allocates nothing once the high-water mark is
  /// reached -- which matters because an animating HUD does work every frame.
  std::vector<RenderObject*> layoutScratch_;
  std::vector<RenderObject*> paintScratch_;
  PipelinePhase phase_ = PipelinePhase::Idle;
  RenderObject* activeLayoutNode_ = nullptr;
  bool visualUpdateRequested_ = false;
  FrameStats stats_;
  /// Incremented every time any display list is re-recorded.
  std::uint64_t recordCounter_ = 0;
};

}  // namespace fltr
