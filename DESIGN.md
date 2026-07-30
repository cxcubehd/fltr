# fltr — design notes

A retained-mode UI framework for embedding in a game engine. The architecture is
a deliberate port of Flutter's Widget / Element / RenderObject split, with no
Dart, no GC, and no coupling to a graphics API.

This document records **decisions and their costs**, especially where they
diverge from Flutter. It is written as the implementation proceeds; each
milestone appends its section.

---

## Load-bearing invariants (expensive to reverse)

These are the reasons for choosing Flutter's architecture in the first place.
Nothing below is negotiable without a redesign.

1. **Immutable config / persistent element / render object.** Declarative
   authoring stays compatible with incremental update because the thing the game
   code writes (a widget) is not the thing that persists (an element) and not the
   thing that lays out and paints (a render object).
2. **Constraints down, sizes up, single pass.** A parent hands a child
   `BoxConstraints`; the child returns a `Size`. A child cannot see its parent's
   size. This is what makes layout linear rather than iterative.
3. **Relayout and repaint boundaries.** Update cost is proportional to the dirty
   set, not the tree. Layout invalidation and paint invalidation are *separate*
   paths with separate dirty lists.
4. **Gesture arena.** Overlapping interactive regions resolve deterministically.

---

## Milestone 1 — geometry, constraints, command representation, text boundary

### Memory model

Three lifetimes, deliberately kept distinct:

| Thing | Lifetime | Ownership |
|---|---|---|
| Widget config | one build phase | `Arena`, released wholesale |
| Element | until reconciled away | tree-owned, `unique_ptr` down, raw up |
| RenderObject | until its element dies | tree-owned, `unique_ptr` down, raw up |

`Arena` (`core/arena.hpp`) is a bump allocator with chunk reuse. `reset()`
rewinds to the first chunk and bumps a generation counter — a pointer store plus
an increment. Chunks are retained, so the steady state performs **no**
allocation. `arena_reuses_chunks_after_reset` asserts this directly.

`Arena::create<T>` **statically rejects** non-trivially-destructible types. A
widget that genuinely owns heap memory must use `createOwning<T>`, which
registers a destructor to run at reset. The verbosity is the point: owning
widgets are an exception and should read as one.

### DIVERGENCE: elements adopt their configuration by value

Flutter's `Element._widget` points at a Widget that the GC keeps alive for as
long as the element references it. With an arena that resets every build, that
pointer would dangle.

**Chosen:** the arena is *pure build scratch*. Each concrete `Element<W>` stores
its config as a by-value `W` member. Updating is one assignment — a memcpy for
trivially copyable configs. The element's storage is allocated once, when the
element is created, and reused for every subsequent update.

This works because an element is only ever updated with a widget of the *same
concrete type* (`canUpdate` requires matching runtime type and key); a type
change discards the element entirely. So the by-value member is always the right
type and never needs resizing.

**What it costs:**
- One small copy per *updated* element per rebuild. Configs are POD-ish and
  typically 16–128 bytes, so this is bytes moved, not allocations made.
- Child `Widget*` pointers stored inside an adopted config are only valid during
  the build phase that produced them. Elements never read them outside that
  window (they are consumed by `updateChild` immediately), but holding a stale
  pointer is a footgun, so `WidgetRef` carries the arena generation and checked
  builds trap a stale dereference.

> **Corrected in M4.** "Elements never read them outside that window" is false,
> and building the element layer is what showed it. Any element that rebuilds
> *without* being handed a new configuration — the whole point of `setState` —
> re-emits the child ref it adopted, and that ref belongs to a released arena by
> then. Since a wrapper widget that stores a `child` and passes it through is the
> most ordinary shape in the catalogue, this is the common path, not an edge
> case. See *A stale ref means unchanged* below for what it cost and what it
> turned out to buy.

**What it buys:** unambiguous lifetime, no refcounting anywhere in the tree, and
an arena reset that is genuinely a pointer operation rather than a mark phase.

*Considered and rejected:* generational arena with widgets outliving the build
(reintroduces the ownership ambiguity the memory model rules out); type-erased
heap buffer per element (same lifetime guarantees, but loses static typing at the
element boundary and adds an allocation per element).

### Command representation

`DisplayList` is a flat `std::vector<PaintCmd>` — fixed-size, trivially copyable
tagged commands in one contiguous array. Nothing in it assumes the consumer is
immediate-mode or retained-mode:

- an immediate-mode backend (raylib) walks the array once per frame;
- a retained-mode backend (Vulkan, D3D) translates once and re-uses the
  translation while `revision()` is unchanged.

`beginRecording()` clears but retains capacity, so re-recording allocates
nothing (`displaylist_rerecord_does_not_reallocate`).

**`PaintOp::DrawList` is the repaint-boundary seam.** A boundary owns its own
`DisplayList`; its parent embeds it by reference. Re-recording the child leaves
the parent's list — and its revision — untouched, which is exactly what makes a
partial repaint cheap. It is also the seam a real compositing layer tree slots
into later: give `DrawListCmd` a transform/opacity slot and let a compositor own
those lists. That is additive, not a rewrite.

`Scene::revision` is a monotonic count of how many display lists the pipeline
has re-recorded. Unchanged means nothing was re-recorded anywhere in the tree and
the consumer may resubmit last frame's translation verbatim. A per-list
`revision()` is also exposed, so a backend that caches translated state keys it
on `(list pointer, list revision)` and re-translates only the boundaries that
actually moved on.

Images and fonts are `std::uint64_t` opaque handles. The framework never loads,
allocates, or frees a GPU resource.

### Text: the boundary now, the implementation later

Text rendering is out of scope for this phase, but the *interface* is not,
because retrofitting text into a design that assumed it was simple is a rewrite.

`TextService` (`paint/text.hpp`) is consumed exactly the way a rendering backend
is consumed. `acquire(spec, maxWidth) -> ParagraphHandle` returns a measured
paragraph; `metrics()` exposes an overall `Size`, per-line `LineMetrics`, and
positioned `PositionedRun`s. Deliberately **not** assumed anywhere: single-line,
fixed-width, ASCII, or one glyph per character. A span may split across lines and
a line may hold runs from several spans, which the placeholder actually
exercises.

`byteOffsetAt` exists now so hit testing inside text does not need a second
mechanism bolted on later.

`MonospaceTextService` is the development placeholder: fixed advance width,
greedy word wrap, hard-break handling, alignment, and max-lines. It is not a text
renderer; it produces the *shape* of data a real shaper returns so that layout,
painting, and hit testing are written against the real contract.

Handles are owned by the caller and must be released. `liveParagraphs()` exists
so tests can assert paragraph handles are not leaked.

### Contracts

`FLTR_ASSERT` / `FLTR_EXPECTS` / `FLTR_ENSURES` lower to C++26 `contract_assert`
when available and to a checked call otherwise, gated on `__cpp_contracts`. The
default violation handler throws `ContractViolation` so tests can assert that
invariants actually trip; a shipping game installs a handler that logs and
aborts, or compiles with `FLTR_CHECKS=0` for zero cost.

Feature detection for deducing `this` and reflection lives in
`core/config.hpp`. Both have working C++23 fallbacks; the build requires neither.
Baseline is C++23 on Clang 17+ / GCC 13+.

### The shared observable core

`Listenable` + `Subscription` (`core/listenable.hpp`) is intrusive and
allocation-free: subscribing is two pointer stores, and a subscription unhooks
itself when its owner is destroyed. Notification is safe against listeners
subscribing or detaching mid-walk — including a listener detaching its
not-yet-visited successor — via a chain of cursors that `detach()` fixes up.

This is the one observable abstraction, shared by reactivity and animation. **The
rebuild-versus-repaint distinction deliberately does not live here.** It lives in
the subscriber: a `Watch` element subscribes and marks itself dirty for rebuild;
a render object subscribes via `observeForPaint` and only marks itself needing
paint, or via `observeForLayout` when the property genuinely affects layout. Same
signal, different sink, and which one you are on is visible at the call site.

---

## Milestone 2 — the render tree

Built and verified with no widget layer and no renderer at all: trees are
constructed by hand in `tests/test_render_layout.cpp` and `layout()` is called
directly. The render tree carries the invariants that cannot be fixed later, so
it has to be right before anything is built on top of it.

### The box protocol

`RenderBox::layout(constraints, parentUsesSize)` is the single entry point.
`performLayout()` is never called directly. Three things are enforced rather
than merely documented:

- **`setSize` may only be called from this object's own layout**, and the size
  must satisfy the incoming constraints (`layout_a_size_violating_constraints_is_a_contract_violation`).
- **A parent may only read a child's size if it passed `parentUsesSize = true`.**
  Checked by comparing the pipeline's currently-active layout node against the
  child's parent. This is the load-bearing check: without it a parent could
  silently depend on a child that remained a relayout boundary, and would never
  be re-laid-out when that child changed.
- **Constraints must be normalised** on the way in.

`layoutChild` / `layoutChildForSize` are the two ways to lay out a child, named
so the call site says which one it is.

### Relayout boundaries

Computed exactly as Flutter does, and for the same reasons:

```
isRelayoutBoundary = !parentUsesSize || sizedByParent || constraints.isTight || parent == nullptr
```

`markNeedsLayout` walks up only to the nearest boundary and registers *that*
node with the owner. `markNeedsPaint` is a separate, cheaper walk that stops at
the nearest repaint boundary and never touches layout state.

The boundary flag is a tri-state (`-1` unknown / `0` no / `1` yes). Unknown means
"never laid out, or freshly reparented" and propagates like "no", which is the
safe direction.

DIVERGENCE: Flutter eagerly propagates a `_cleanRelayoutBoundary` pass when a
subtree is reparented. We reset the subtree's flags to unknown on adopt/drop and
otherwise rely on two guards in the flush loops — a node is skipped unless it is
still dirty *and* still owned by this pipeline.

> **Corrected in M3.** This section originally went on to say that detach could
> simply *leave* stale pointers in the dirty lists, relying on those guards. That
> is wrong, and driving the pipeline in M3 proved it: the guards have to
> dereference an entry to decide whether to skip it, so a detached node that is
> then destroyed leaves the flush reading freed memory. Detach now purges the
> lists — once per detach call, not once per detached node, so tearing down a
> subtree is O(subtree + dirty) rather than O(subtree x dirty). The guards stay,
> because they still cover a node that is alive but has moved to another
> pipeline.

### DIVERGENCE: per-child data lives in the parent

Flutter attaches a heap-allocated `ParentData` object to each child, typed by
whatever the parent happens to be. `RenderBoxContainer<ChildData>` stores that
data in the parent's own child list instead: statically typed, no allocation,
and a child cannot be asked for parent data belonging to a different parent.

The cost is that a child cannot read its own parent data without going through
the parent. Nothing in this framework needs that.

### Painting

`PaintingContext` records into a `DisplayList`. A repaint boundary is recorded
into its **own** list at its own origin, and the parent emits a `DrawList`
referencing it. Two consequences fall out for free:

- moving a boundary does not re-record it — only the parent's `DrawList` offset
  changes;
- re-recording a boundary does not touch the parent's list or its revision.

`pushOpacity` records nothing at all at alpha 0 and no push at alpha 1, so the
common cases cost nothing in the command stream.

### The widget catalogue's render half

`View` (root, always a repaint boundary), `Padding`, `Align`, `ConstrainedBox`,
`DecoratedBox`, `Opacity`, `Transform`, `ClipRect`, `RepaintBoundary`, `Row` /
`Column` (`RenderFlex`), `Stack`, `Paragraph`, `Sprite`.

Flex is two linear passes: inflexible children measure themselves, then flexible
children divide the remainder. The last flexible child receives
`freeSpace - allocatedFlexSpace` rather than its nominal share, matching Flutter
— which means a `FlexFit::Loose` sibling's unused space is handed to the last
flexible child instead of leaving a gap. That is tested explicitly, because it
is surprising.

`RenderStack` with `StackFit::Expand` is `sizedByParent`: its size comes from
its constraints alone, so it is always a relayout boundary and no child can ever
reach its parent. That is a genuine instance of the concept rather than a
decoration.

DIVERGENCE: Flutter's `RenderStack` is not `sizedByParent`; it computes its size
in `performLayout` via `_computeSize`. Making Expand `sizedByParent` here is
strictly stronger and gives the pipeline a real sizedByParent node to exercise.
The cost is that `StackFit::Expand` under unbounded constraints is now a
contract violation rather than a silent fallback — which is the right failure.

### Text stays an ordinary child

`RenderParagraph` resolves its size from constraints like any other box, by
asking the `TextService` to measure into `constraints.maxWidth`. Nothing in
layout special-cases it. A real shaper drops in behind `TextService` without
touching this class.

The render object owns its text as a `std::string`; the widget config carries
only a `std::string_view`. Render objects are long-lived and may own heap
memory, and the copy happens only when the text actually changes.

---

## Milestone 3 — the pipeline

`PipelineOwner` was written in M2, because `RenderObject`'s invalidation cannot
compile without it, but nothing drove it: the M2 tests construct trees by hand
and call `layout()` directly. M3 is where it is actually driven, and driving it
is what found the bugs below. That is the argument for the milestone order —
none of these are visible by reading the code.

### The frame

`PipelineOwner::drawFrame()` is what a game loop calls. It resets the frame
stats, flushes layout, flushes paint, and returns the `Scene` to submit. It is
meant to be called unconditionally rather than guarded by `needsFrame()`,
because a frame with nothing dirty already costs nothing.

Layout flushes shallowest-first, so a parent's layout subsumes any dirty
descendant. Paint flushes deepest-first, so a boundary that repaints on its own
is already clean by the time an ancestor embeds it by reference.

### Phase separation, both directions

M2 enforced half of it: `markNeedsLayout` refuses to run during the paint phase.
The other half was missing — nothing stopped a node being painted outside the
paint phase. `paintWithContext` now requires it. A detached tree has no owner
and therefore no phase, which is how the M2 tests still paint by hand.

Phase restoration is exception-safe. In a shipping build a contract violation
aborts and this is moot; in a checked build it throws, and a pipeline left stuck
in `Paint` turns one reported failure into a confusing second one during
teardown, when a destructor drops a child and marks layout dirty.

### Non-convergence is capped, not asserted away

A node cannot spin on its own: it is still marked dirty while its own
`performLayout` runs, so marking itself short-circuits. Two nodes dirtying each
other can spin, and that is a real bug class in this design. The drain is capped
at 32 passes in *every* build, so the failure mode is one stale frame rather
than a frozen game, and a checked build then names it. `FrameStats::layoutPasses`
exposes the count; a healthy frame is 0 or 1.

### What driving the pipeline surfaced

**A use-after-free in the dirty lists.** Documented above, in the M2 section it
contradicts. The purge lives in `detach()` and in `~RenderObject` — the latter
covers a root destroyed while still installed, which also clears the owner's
root pointer.

**`RenderStack` read a child's size it had not asked for.** Under
`StackFit::Expand` it laid children out with `parentUsesSize = false` and then
read `child.size()` in the offset pass. The M2 code even carried a comment
explaining why that was supposed to be fine ("layoutChild gave it tight
constraints"), which is exactly the kind of reasoning the check exists to
refuse. The fix is to say `parentUsesSize = true`, which is honest and costs
nothing: tight constraints already make those children relayout boundaries, so
the boundary decision is unchanged. This is the check from M2 doing its job the
first time anything ran through it.

**A test expectation that taught something.** `EdgeInsets`-deflated constraints
stay tight, so `View > Padding > Row` makes the *Row* a relayout boundary, not
just the padding. Getting a non-boundary chain to test propagation with needs
something that genuinely loosens — an `Align`. Worth knowing when reasoning
about where invalidation actually stops in a real tree.

### No per-frame heap churn, measured

Both flush phases drain their dirty list into a scratch buffer so that nodes
dirtied mid-flush land in a fresh list. Those buffers are members, not locals,
so a frame that does work allocates nothing once the high-water mark is reached.
That matters because an animating HUD does work every frame — treating "steady
state" as "nothing changed" would miss the case the requirement is actually
about.

`pipeline_steady_state_frames_allocate_nothing` replaces the global `operator
new` with a counter and asserts zero allocations across three frame shapes: one
with nothing dirty, one repaint-only (what a render-attached animation produces
every tick), and one relayout.

Hoisting the buffers introduced a hazard worth naming: a node detached from
inside a flush — by a parent that restructures its children during layout —
would sit in the buffer that flush is walking. Purging blanks those entries
rather than erasing them, so the walk's iterators stay valid, and both loops
skip nulls.

### What is verified

- A first frame lays out and paints the whole tree; a second does nothing at
  all, and the scene's revision and commands are unchanged and resubmittable.
- Dirtying a leaf registers exactly one node — the nearest enclosing boundary —
  and re-lays-out that boundary and below, never above.
- The three ways to be a boundary each stop propagation: tight constraints,
  `sizedByParent`, and an unmeasured child. The negative case is tested too,
  since otherwise the positive ones prove nothing.
- Reparenting resets the boundary decision, and the node picks up its new
  parent's answer.
- A paint-only change performs no layout, repaints one boundary, and leaves the
  root's display list untouched when that boundary is nested.
- Repainting the root does not re-record a clean nested boundary.
- Moving a repaint boundary does not re-record it — only the parent's `DrawList`
  offset changes. This is the payoff of recording boundaries at their own origin.
- A render object observing a `Listenable` invalidates only its painting, and
  its subscription unhooks with it. That is the M7 render-attached path, already
  load-bearing.
- Both phase-separation violations, phase re-entry, painting a node that still
  needs layout, and non-convergence all trip contracts.
- Removing a subtree purges its dirty entries; a detached subtree is skipped and
  stays dirty for reuse; re-attaching re-registers it.

Verified on GCC 13.3 and Clang 18.1, and under ASan + UBSan. The library also
compiles clean with `FLTR_ENABLE_CHECKS=OFF`.

---

## Milestone 4 — the widget and element layer

Three trees now: immutable `Widget` configuration in an arena, persistent
`Element`s that reconcile it, and the render tree from M2/M3 underneath. The
build phase joins layout and paint as the first of three per-frame phases, and
`WidgetBinding::drawFrame()` runs all three in order.

### Authoring

Each widget declares its fields once, in a nested `Args` aggregate that callers
fill with designated initializers:

```cpp
Column::make({
  .crossAxisAlignment = CrossAxisAlignment::Start,
  .children = {
    Text::make({.text = "SHIELD", .style = label}),
    Flexible::make({.flex = 1, .child = Bar::make({.value = shield})}),
  },
})
```

Nesting allocates nothing at the call site: `make` bump-allocates into an
*ambient* build arena. Ambient rather than threaded through, because a widget
expression nests arbitrarily deep and an explicit allocator parameter would put
noise at exactly the call sites this is optimising.

`Configure<Derived, Base>` supplies the three things every widget would otherwise
repeat — its runtime type, its element, and `make`. Runtime type is the address
of a per-class object rather than `typeid`, so `canUpdate` is one pointer
compare. `make` defers naming `Derived::Args` through a defaulted template
parameter, because a class template's member declarations are instantiated while
the derived widget is still incomplete; a braced initialiser is a non-deduced
context, so the default always wins and the call site stays clean.

Widgets have a **protected non-virtual destructor**. They are arena scratch and
are never deleted, which is what keeps every concrete widget trivially
destructible and therefore accepted by `Arena::create` — the static assert that
enforces the memory model is doing real work here.

### DIVERGENCE: a stale ref means unchanged

The arena resets at the end of every build scope, so a `WidgetRef` an element
adopted is dangling by the next build. That collides head-on with the most
ordinary widget shape there is:

```cpp
WidgetRef build(BuildContext&) const {
  return Padding::make({.padding = args_.padding, .child = args_.child});
}
```

When that element rebuilds on its own, `args_.child` points into a released
arena. Flutter has no such problem: the child `Widget` object is kept alive by
the GC, and `updateChild` short-circuits on `identical(child.widget, newWidget)`.

**Chosen:** `WidgetRef` carries the identity it needs — type and key — beside the
pointer, and a ref from a released generation is treated as *unchanged*. It is
never dereferenced; reconciliation answers `canUpdate` from the ref alone, and a
match with a stale ref returns the existing child untouched.

This is sound because a stale ref can only have come from an element's adopted
config, and that element copied the configuration verbatim when the ref was
fresh. Nothing could have changed one without replacing the other.

**What it costs:** `WidgetRef` grows to a pointer, a type tag, a `Key`, and a
generation. It is arena scratch, so this is bytes bumped, not allocations made.
`WidgetList` carries a generation for the same reason — its backing array is
arena memory too, and a stale list is skipped whole rather than indexed.

**What it buys:** more than it costs, as it turns out. The generation counter
existed only as a debugging aid, and it has become the mechanism that makes an
independent rebuild *stop* at the branches it actually changed. A `setState` deep
in a HUD re-emits its untouched siblings as stale refs and the reconciler skips
those subtrees entirely — the same short-circuit Flutter gets from pointer
identity, reached from the opposite direction.

Inflating a stale ref would mean the element that held it is gone and its
configuration is unrecoverable. That is a contract violation, not a fallback.

### DIVERGENCE: no slots; order is fixed by a permutation pass

Flutter threads an `IndexedSlot` (index plus previous sibling) through
`insertRenderObjectChild` / `moveRenderObjectChild`, because sibling-anchored
moves compose correctly when several children move at once and intermediate
indices shift.

Since per-child data already lives in the parent here (the M2 divergence), our
containers are index-addressed and a simpler shape works: new children are
**appended**, and `RenderBoxContainer::reorderChildren` permutes the slots into
the element order in one linear selection pass once the element list is final.
Slots disappear from the design entirely.

Reordering swaps `Slot` objects. Nothing is adopted or dropped, so a keyed child
that moves keeps its layout state, its paint state, and its per-child data, which
travels in the slot with it. The cost is one extra O(n) pointer-compare pass per
multi-child rebuild that changed structure, and `markNeedsLayout` fires only if
something actually moved.

### Reconciliation

`ChildList::update` is Flutter's algorithm: match a leading run by position,
match a trailing run, then reconcile the middle by key. Unkeyed middle children
are discarded; keyed ones are held aside and reclaimed wherever their key
reappears. Scratch buffers are members, so a rebuild allocates nothing once the
high-water mark is reached.

A **null entry in a children list is dropped** at list construction, which is
what makes `visible ? Badge::make({...}) : WidgetRef{}` work inline. Single-child
slots keep null as a real value meaning "no child".

`deactivate` is the single choke point through which a discarded subtree passes.
Global keys and animating a subtree *out* are both deferred, and both would be
implemented by making that one function retain the subtree instead of destroying
it — so the design does not preclude them, which is what the brief asked for.

### Parent data without a second mechanism

`Flexible` and `Positioned` create no render object. They are `ParentDataWidget`s
whose element writes into the container's slot for its index, and the walk that
finds them stops at the first render object in each branch — so they still work
through an intervening stateless widget, which is where a real HUD puts them.
The container is named statically (`using Container = RenderFlex`) and checked at
the point of application, so `Flexible` inside a `Stack` names its own mistake.

Every index is reset to default data before the walk, so removing a `Flexible`
clears the flex it had set. `setChildData` is change-guarded, which makes both
writes free when nothing moved.

### Build ordering, and the same two hazards as M3

Dirty elements are rebuilt **shallowest first**, so a parent's rebuild subsumes
any descendant that was also dirty: by the time the list reaches the child, its
`update` has already run and cleared its dirty flag.

Both hazards M3 found in the pipeline recur exactly here, and are solved the same
way:

- **Non-convergence.** An element that dirties itself from its own build would
  spin. The drain is capped at 32 passes in *every* build, so it degrades to a
  stale frame rather than a freeze, and a checked build names it afterwards.
- **Unmounting mid-flush.** An element dropped by a parent's rebuild can be
  sitting in the buffer that build is walking. `stopTracking` erases it from the
  dirty list and *blanks* it in the scratch buffer, so the walk's iterators stay
  valid — the guard cannot skip an entry without dereferencing it first.

`~WidgetBinding` unmounts the root before anything is destroyed, so every `State`
sees `dispose()`.

### What is verified

- A first frame builds, lays out and paints; a second does no build, no layout,
  no paint, and returns an unchanged revision.
- A steady-state frame allocates nothing — and so does a *rebuilding* frame,
  which is the one that matters, since an animating HUD rebuilds every frame.
- A rebuild producing equivalent configuration mutates no render object: the
  element tree rebuilds, the render tree does not move, the revision holds.
- Keyed children survive reordering with the same `State` and the same render
  object, in the new order. Unkeyed children reconcile by position, and the test
  shows the State that used to back one config now backs another.
- Insert, remove, type-change, null child, and whole-subtree discard each dispose
  exactly the states they should and leave the container's child count right.
- `setState` rebuilds one element; an ancestor rebuild subsumes a dirtied
  descendant; a wrapper rebuilding alone leaves the child it stored untouched.
- Flex distribution, positioned placement, padding arithmetic and surface resize
  all produce hand-computed geometry through the widget layer.
- Text measures through `TextService` and releases its paragraph on teardown.
- The arena is reused rather than regrown across 32 rebuilds; a widget from a
  released build traps; creating one outside a build scope traps; a build that
  never settles is capped.
- A `RepaintBoundary` widget confines a paint-only change to its own list.

Verified on GCC 13.3 and Clang 18.1, and under ASan + UBSan: 123 tests, 436
checks. The library also compiles clean with `FLTR_ENABLE_CHECKS=OFF`.
