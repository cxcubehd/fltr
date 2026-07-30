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

/// Painting may not dirty layout and layout may not paint; both are enforced
/// against this rather than merely documented.
enum class PipelinePhase : std::uint8_t { Idle, Layout, Paint };

// ---------------------------------------------------------------------------
// RenderObject
// ---------------------------------------------------------------------------

/// Protocol-agnostic base: tree structure, ownership, invalidation, painting.
/// The layout protocol itself lives in RenderBox, so a second protocol could be
/// added without disturbing the invalidation machinery -- though it would first
/// have to make Constraints polymorphic, which one protocol does not justify.
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

  /// Extra detail for the headless dump only.
  virtual std::string describe() const { return {}; }

  void attach(PipelineOwner* owner);
  /// Detaches this whole subtree from its pipeline and purges any of its nodes
  /// from the owner's dirty lists, so nothing left there can outlive its node.
  void detach();

  // --- invalidation -------------------------------------------------------

  /// Registers the nearest enclosing relayout boundary with the owner. The walk
  /// stops there rather than at the root, keeping update cost proportional to
  /// the dirty set.
  void markNeedsLayout();
  void markParentNeedsLayout();

  /// Walks up to the nearest repaint boundary, touching no layout state.
  void markNeedsPaint();

  /// Call when the value `sizedByParent()` would return has changed.
  void markNeedsLayoutForSizedByParentChange();

  bool needsLayout() const noexcept { return needsLayout_; }
  bool needsPaint() const noexcept { return needsPaint_; }
  bool isRelayoutBoundary() const noexcept { return relayoutBoundary_ == 1; }

  /// Size depends only on constraints, never on children -- which makes such an
  /// object a relayout boundary unconditionally.
  virtual bool sizedByParent() const { return false; }

  /// Owns its own DisplayList, so repainting it does not re-record its parent.
  virtual bool isRepaintBoundary() const { return false; }

  // --- painting -----------------------------------------------------------
  virtual void paint(PaintingContext& context, Offset offset) = 0;

  /// Wraps `paint` with the bookkeeping and invariant checks. Callers use this,
  /// never `paint` directly.
  void paintWithContext(PaintingContext& context, Offset offset);

  /// Only meaningful for repaint boundaries; created lazily.
  DisplayList& boundaryList();
  const DisplayList* boundaryListIfAny() const noexcept { return boundaryList_.get(); }

  /// Bounds painted into, in this object's own coordinate space.
  virtual Rect paintBounds() const { return Rect::zero(); }

  std::uint32_t layoutCount() const noexcept { return layoutCount_; }
  std::uint32_t paintCount() const noexcept { return paintCount_; }

  /// Implemented by the protocol layer; re-runs layout with stored constraints.
  virtual void layoutWithoutResize() = 0;

protected:
  RenderObject() = default;

  void adoptChild(RenderObject* child);
  void dropChild(RenderObject* child);

  /// The render-attached animation path: a change invalidates only painting, so
  /// no element rebuild and no per-frame widget allocation. `slot` is a member
  /// of the concrete render object, so the subscription's lifetime is exactly
  /// the render object's with no shared ownership.
  void observeForPaint(Subscription& slot, Listenable* source) {
    slot.detach();
    if (source) {
      source->subscribe(
          slot, [](void* p) { static_cast<RenderObject*>(p)->markNeedsPaint(); }, this);
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
/// A repaint boundary child is recorded into its *own* list, at its own origin,
/// and positioned by a DrawList command in the parent. Moving a boundary
/// therefore changes only that DrawList's offset and never re-records it.
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

/// Drives the render tree through its phases and holds the dirty sets, which
/// are separate so an animation that only affects painting never touches the
/// layout list.
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

  /// When false, `drawFrame` is a no-op and the scene's revision will not move.
  bool needsFrame() const noexcept {
    return !nodesNeedingLayout_.empty() || !nodesNeedingPaint_.empty();
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

  /// One frame, as the game loop calls it. Cheap enough to call unconditionally:
  /// with nothing dirty it does no layout, no paint and no recording, and
  /// returns a Scene whose revision is unchanged.
  Scene drawFrame();

  // --- per-frame accounting, used by tests --------------------------------
  struct FrameStats {
    int layouts = 0;   ///< performLayout / performResize invocations
    int paints = 0;    ///< paintWithContext invocations
    int boundariesRelaidOut = 0;
    int boundariesRepainted = 0;
    /// Times the layout dirty list had to be drained. A healthy frame is 0 or 1;
    /// more means laying a node out dirtied an unvisited ancestor, which is
    /// legal but rarely intended.
    int layoutPasses = 0;
  };
  const FrameStats& stats() const noexcept { return stats_; }
  void resetStats() noexcept { stats_ = {}; }

  std::size_t dirtyLayoutCount() const noexcept { return nodesNeedingLayout_.size(); }
  std::size_t dirtyPaintCount() const noexcept { return nodesNeedingPaint_.size(); }

  /// The node whose performLayout is currently running. RenderBox::size() reads
  /// it to enforce that a parent only measures a child it asked to measure.
  RenderObject* activeLayoutNode() const noexcept { return activeLayoutNode_; }

private:
  friend class RenderObject;
  friend class RenderBox;
  friend class PaintingContext;

  /// Returns the pipeline to Idle however the phase is left, including on a
  /// contract violation: in a checked build that throws, and a pipeline stuck in
  /// Paint would turn one reported failure into a confusing second one during
  /// teardown.
  class PhaseScope {
  public:
    PhaseScope(PipelineOwner& owner, PipelinePhase entering) noexcept : owner_(owner) {
      owner_.phase_ = entering;
    }
    ~PhaseScope() { owner_.phase_ = PipelinePhase::Idle; }

    PhaseScope(const PhaseScope&) = delete;
    PhaseScope& operator=(const PhaseScope&) = delete;

  private:
    PipelineOwner& owner_;
  };

  void setActiveLayoutNode(RenderObject* n) noexcept { activeLayoutNode_ = n; }
  FrameStats& mutableStats() noexcept { return stats_; }

  RenderObject* root_ = nullptr;
  std::vector<RenderObject*> nodesNeedingLayout_;
  std::vector<RenderObject*> nodesNeedingPaint_;
  /// Each flush drains its dirty list into one of these, so nodes dirtied
  /// mid-flush land in a fresh list. Members rather than locals, so a frame that
  /// does work still allocates nothing once the high-water mark is reached.
  std::vector<RenderObject*> layoutScratch_;
  std::vector<RenderObject*> paintScratch_;
  PipelinePhase phase_ = PipelinePhase::Idle;
  RenderObject* activeLayoutNode_ = nullptr;
  FrameStats stats_;
  /// Incremented every time any display list is re-recorded.
  std::uint64_t recordCounter_ = 0;
};

}  // namespace fltr
