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
has re-recorded. (The header used to describe it as a sum of per-list revisions,
which it never was; corrected in the M1–M4 review.) Unchanged means nothing was re-recorded anywhere in the tree and
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

This is the one observable abstraction, to be shared by reactivity (M6) and
animation (M7). **The rebuild-versus-repaint distinction deliberately does not
live here.** It lives in the subscriber: `observeForPaint` marks a render object
needing paint and nothing else, while a subscribing element will mark itself
needing rebuild. Same signal, different sink, chosen at the call site.

The layout-invalidating counterpart is deliberately absent until something
animates a layout property; it lands with the first widget that does.

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
the element order once the element list is final. Slots disappear from the design
entirely.

Reordering swaps `Slot` objects. Nothing is adopted or dropped, so a keyed child
that moves keeps its layout state, its paint state, and its per-child data, which
travels in the slot with it. The permutation is a selection pass with a linear
inner scan, so O(n²) in the worst case against sibling counts that are single
digits in a HUD; `markNeedsLayout` fires only if something actually moved.

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
A parent-data widget names the data it writes (`using ChildData = FlexChildData`)
rather than the container it expects, and `ParentDataSlot` carries that type's
tag, so `Flexible` inside a `Stack` names its own mistake. That is the same
no-RTTI mechanism `WidgetType` uses; the framework contains no `dynamic_cast`.

Each slot's data is gathered into a default-initialised value that the walk
fills in, and written to the container once. Removing a `Flexible` therefore
clears the flex it had set, and a rebuild that changes nothing writes nothing —
which is what makes `setChildData`'s change guard actually bite.

> **Corrected after M4.** This first reset every slot to default and *then*
> re-applied, so the guard fired on both writes and every rebuild of a container
> holding a `Flexible` or `Positioned` relayed out and repainted it. Since a HUD
> row essentially always holds one, that was the steady-state path. The test that
> claimed to cover "equivalent configuration mutates no render object" used a
> single-child `Padding`, which never reaches this code.

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

---

## Review of M1–M4

A pass over everything built so far, before the milestones that build on it.

### Three defects, each reproduced before it was fixed

**Rebuilding a container with parent data mutated the render tree.** Covered
above. Measured on a `Row` + `Flexible` rebuilt with identical configuration:
one layout, four paints, a bumped scene revision, against zero for the same tree
without the `Flexible`.

**`setState` after unmount dereferenced null.** `StatefulElement::unmount` clears
`State::element_`, and `setState` used it unguarded — a segfault exactly where a
game callback outlives the panel that registered it. `State::mounted()` existed
two lines above and was not consulted. It is now a precondition.

**`flushLayout`'s convergence cap was cumulative.** It used
`FrameStats::layoutPasses` as its own loop counter, and only `resetStats` — which
only `drawFrame` calls — cleared it. Driving `flushLayout` directly gave a silent
no-op after 32 calls and a spurious *"layout did not converge"* on the 33rd, on a
perfectly healthy tree. `flushBuild` already used a local; `flushLayout` now
matches.

The shape all three share: each was a claim the code made about itself that
nothing checked. Each now has a test that fails without the fix.

### Scaffolding removed

Hit testing was fully written in M2, three milestones before its own, with no
test touching it — and `PointerEvent` was forward-declared but never defined, so
`HitTestTarget::handleEvent`, a virtual on every `RenderBox`, could not be
called by anyone.

Split accordingly. Resolving *which boxes lie under a point, and in what space*
is the render tree's own business and stays, now in `render/hit_test.hpp` and
covered by tests: exclusive edges, deepest-first paths in local space, transform
inversion including a non-invertible one, and overlapping children resolving
topmost-first. Routing, `PointerEvent`, the arena and recognizers are M5, and
`gestures/` reappears when they do.

Also removed: `PaintBackend`, a consumer-side interface the framework never
referenced; `PipelineOwner::requestVisualUpdate` and its flag, which nothing ever
read; `RenderBoxContainer::moveChild`, superseded by `reorderChildren`, where
having two ways to move a child invites the wrong one.

`TextOverflow` went the other way. It was declared and carried in `ParagraphSpec`
with nothing setting it and nothing reading it. Since the brief's whole point
about text is that the interface must be honest *now*, it is plumbed through
`Text` and `RenderParagraph` to the service instead, which reports back through
`ParagraphMetrics::didEllipsize`.

Deliberately kept, though currently unused: the value-type API on `Offset`,
`Size`, `Rect`, `EdgeInsets`, `Color`, `BoxConstraints` and `Key`, and the
per-type `lerp` overloads. That is the vocabulary this port is meant to mirror,
not speculative machinery, and M7's interpolation layer is named in the brief.
`BoxConstraints::copyWith` was the exception worth fixing rather than keeping: it
replaced all four bounds unconditionally, which made it a slower spelling of
aggregate initialisation, and its doc comment described a function that did not
exist. It now takes optionals and preserves what it is not given, as Flutter's
does.

### Encapsulation and structure

`PipelineOwner` published `setPhase`, `mutableStats` and `setActiveLayoutNode`
under a comment calling them internal; `friend class RenderObject` was declared
but friendship does not inherit, so `RenderBox` could not use it. They are
private now, with `RenderBox` a friend, and `PhaseScope` is a private nested
class rather than a free struct that needed the setter public.

The contract machinery — `setViolationHandler`, `reportViolation`, the default
handler — was implemented in `src/geometry.cpp`. It lives in `src/contracts.cpp`.

Several headers relied on transitive includes for `std::optional`,
`std::unique_ptr`, `<type_traits>` and even `FLTR_EXPECTS`; they include what
they use.

`WidgetBinding` held its `RenderView` through an unchecked `static_cast` on every
call and asserted the one thing that could not fail. It now checks the root
widget's type at mount and caches the pointer. `attachRoot` forwards its callable
instead of taking a forwarding reference and ignoring it. `drawFrame` resets the
build count, so `BuildOwner::buildCount()` is per-frame like `PipelineOwner`'s
stats rather than cumulative.

### Comments

The narrative style has been cut back throughout. A comment that explains *why
the obvious thing is wrong here* earns its place; one that restates the code, or
argues for a decision this document already argues for, does not. Three pointed
at identifiers that do not exist (`FLTR_CONFIG_FIELDS`, `widgets/widget.hpp`,
`RenderAlignAnimated`), one described a `Watch` element as though it were
written, and one in `stack.cpp` asserted a child's size was readable while the
code beneath it hedged against exactly that.

### Known and deliberate

Every widget still repeats a constructor, `name()`, a `child()` accessor and an
`Args args_` member. `Configure` cannot absorb them: it is the CRTP base, so
`Derived::Args` is incomplete where the member would have to be declared. Every
way around it — a macro, namespace-scope `XArgs` types, friendship into the
element templates — costs more legibility than the four lines it saves, so the
repetition stays until reflection makes it unnecessary.

---

## Milestone 5 — pointer routing, the gesture arena, and hover

M2 left the render tree able to answer *which boxes lie under a point, and in
what space*. M5 is everything above that: what an event means, who gets to claim
it, and the entirely separate question of what the cursor is currently over.

Three mechanisms, deliberately not one:

| | resolved by | contested? | driven by |
|---|---|---|---|
| gesture routing | pointer id → recognizer | yes, in the arena | consumer events |
| hover | diffing hit-test results | never | events *and* the frame |
| hit testing | the render tree (M2) | — | both of the above |

`PointerBinding` owns all three and is where the consumer pushes events. Hover
never touches the arena — nothing competes for a hover, so there is nothing to
win — and the arena never hit tests.

### The event

`PointerEvent` is `{phase, pointer, kind, position}` and nothing else. No button
mask: the game decides what counts as a press, exactly as it decides what counts
as a frame. No timestamp: nothing in this layer reads a clock, and adding a field
that nothing sets and nothing reads is the mistake the M1–M4 review found in
`TextOverflow`. Time enters the framework with the ticker in M7, and the one
thing that wants it is named below.

`kind` *is* read — the mouse tracker ignores anything that is not a mouse — so it
earns its place.

### DIVERGENCE: routes, not a cached hit-test path

Flutter caches the hit-test path per pointer at down and dispatches every later
event of that gesture along it. We hit test only for the down event and route
everything after it by pointer id, to the recognizers that claimed it.

Two reasons, one of which is a use-after-free:

- a cached path is a list of raw render-object pointers held **across frames**,
  and a rebuild can destroy one mid-gesture. This is the M3 lesson again — a
  guard cannot skip an entry without dereferencing it. A route is instead owned
  by the recognizer that registered it and withdrawn in its destructor, so no
  container ever holds a pointer to a dead node.
- a recognizer must keep receiving events after the pointer leaves the region it
  started in — press a button, slide off, release — which routing by pointer
  gives directly and a path does not.

**What it costs:** a render object cannot receive raw move or up events without
going through a recognizer. Flutter's `Listener` has no equivalent here. The
catalogue's interaction widget is defined in terms of gestures and hover, so
nothing wants that today, and adding a raw-event recognizer later is additive.

### Hit-test behaviour

`HitTestBehavior` is Flutter's, unchanged, because the three cases are exactly
the ones a HUD needs: `DeferToChild` (hit only where a child was hit),
`Opaque` (hit anywhere within bounds, and nothing behind is reachable),
`Translucent` (take part without hiding what is behind). `RenderPointerRegion`
overrides `hitTest` rather than `hitTestSelf`, because translucency is the case
where a box adds itself to the path *and* returns false.

### The arena

Members join while the down event travels the path, which is deepest-first, so
join order is inner-to-outer. `close` — after the whole path has seen the down —
resolves at once if only one member joined. Otherwise the decision waits for
`sweep` on pointer up, which awards the first member still standing: the
innermost region under the point.

Two things the implementation has to get right, both re-encounters of hazards
from earlier milestones:

- **Storage.** A `std::vector` of members per pointer would be allocated and
  freed on every press, which is per-input heap churn in a game loop. Memberships
  live in one flat list shared by every arena, and resolving returns storage to
  the arena rather than to the allocator. `gestures_dispatching_a_pointer_in_the_steady_state_allocates_nothing`
  is what caught this; it failed with exactly one allocation per gesture.
- **Re-entrancy.** A loser's callback can destroy another contender, which
  withdraws it from the very list being walked. Withdrawal nulls a slot rather
  than erasing it and the walk re-reads the list each step — the same shape as
  the pipeline's dirty lists in M3 and the build scratch in M4. The arena is
  dropped *before* the winner is told, so neither winner nor loser can find
  anything still claiming the pointer.

**Decision: withdrawing a member never awards the gesture to anyone.** Flutter's
`GestureRecognizer.dispose` resolves as rejected, which can promote the remaining
contender. Ours is called from `~GestureRecognizer`, and firing a game callback
out of a destructor — during tree teardown, into state that is going away — is
worse than the alternative. The remaining contender is left to the sweep it was
already waiting for. The cost is that a two-contender arena that loses one member
mid-gesture resolves at up rather than immediately.

### Tap, and the one thing that needs a clock

`onTapDown` fires when the recognizer **wins** the pointer, not when the pointer
goes down, so a region that turns out to have lost never shows press feedback it
has to take back. With a single contender that is the same instant.

DIVERGENCE: Flutter also fires tap-down after a 100 ms press timeout, so a region
still competing can show feedback before the contest ends. That needs a clock and
a timer, and this layer has neither — the consumer's events are the only thing
that moves. Until the ticker lands, a *contested* tap shows its feedback on
release. Uncontested taps, which is what a HUD button is, are unaffected.

Travel beyond `kTouchSlop` cancels, before or after winning. After winning there
is no arena left to tell, so the recognizer runs the cancellation itself; that
asymmetry is the whole of `giveUp()`.

### Callbacks inside a widget configuration

A widget config is arena scratch and must stay trivially destructible, and the
lambda at a call site is a temporary that dies with the build expression — so
`FunctionRef` would dangle and `std::function` would allocate and would not be
trivially destructible.

`Callback<Sig>` copies the callable into inline storage (four pointers) and
static-asserts that it is trivially copyable and destructible. A lambda capturing
by reference or by trivial value passes; one capturing a `std::string` names the
rule it broke. This is the same constraint `Arena::create` already enforces on
widgets, reaching one level further in.

```cpp
Pointer::make({
  .behavior = HitTestBehavior::Opaque,
  .onEnter = [&] { state.hovered = true; },
  .onExit  = [&] { state.hovered = false; },
  .onTap   = [&] { fire(); },
  .child   = Panel(...),
})
```

### Hover: re-resolving, and knowing when to

Enter and exit are diffed out of hit-test results, never recognized. The tracker
holds the set of regions under the cursor and, on each new resolution, exits what
dropped out and enters what appeared. It publishes the new set *before* running
any callback, so a callback asking what is hovered gets the current answer.

The brief's harder half is re-resolving when the tree changes beneath a
**stationary** cursor. The trigger is precise rather than conservative:

> re-resolve after a frame in which **layout ran**.

Layout is what moves boxes. A paint-only change cannot alter what lies under the
cursor — which is exactly the invariant that makes `markNeedsPaint` the cheap
path, now doing a second job. So an opacity animation ticking every frame (M7's
render-attached path) costs zero hit tests, while a panel appearing costs one.
`setBehavior` invalidates directly, since it changes hit testing without touching
either phase.

The re-resolution runs at the **start** of `drawFrame`, not the end. The tree it
tests against is the one the previous frame left laid out, and anything an enter
or exit callback dirties is built by *that same frame* rather than the next. The
observable contract is: a tree change that moves a region under a stationary
cursor produces enter/exit on the following frame.

A frame where nothing moved does no hit test at all, which
`gestures_a_frame_that_changes_nothing_runs_no_hit_test` asserts directly.

**A region destroyed while hovered is dropped without an exit callback.** It
deregisters in its destructor, which is what keeps the tracker free of dangling
pointers. Firing exit there would call into state that is being destroyed
alongside it; there is nothing left to un-highlight.

The tracker follows **one cursor**. A game has one mouse, and a touch pointer
never hovers.

### The seam

`RenderBox::asPointerRegion()` returns null for every box but one. It is the
render tree's only mention of the gesture layer — a forward declaration, no
include — and it is what both routing and hover use to pick their targets out of
a path without RTTI, the same no-`dynamic_cast` approach as `WidgetType` and
`ParentDataSlot`.

`RenderPointerRegion` itself lives in `gestures/`, not `render/`, so the render
tree keeps the property it was built with in M2: no dependency on gestures, and
testable without them.

`PointerBinding` is supplied to widgets through `BuildOwner`, exactly as
`TextService` is, and is declared before it in `WidgetBinding` so it outlives the
render tree whose regions withdraw from it.

### What is verified

- The three hit-test behaviours, including a translucent region letting the one
  behind it into the path.
- The arena directly: an uncontested arena resolves at close; a contested one
  waits and awards the first member; rejecting all but one awards the survivor;
  cancel awards nobody; withdrawal awards nobody and leaves the sweep to decide.
- Press then release fires tap-down then tap; a press outside every region
  reaches nobody.
- Travel beyond the slop cancels; travel within it does not; a release *outside*
  the region it started in still taps, which is the routing divergence doing its
  job.
- A cancelled pointer cancels a press already won, and the pointer is finished:
  a later release is nobody's.
- Overlapping regions resolve to the innermost recognizer, and neither shows
  press feedback while the contest is open. The loser has nothing to take back.
- A region destroyed mid-press withdraws its routes and its arena entry, and
  nothing fires out of a destructor.
- Hover enters and exits across regions; moving within one region is not a
  change; a cancelled pointer leaves nothing hovered; a touch pointer never
  hovers.
- Hover is re-resolved when the tree moves beneath a stationary cursor, in
  exactly one hit test.
- A region destroyed while hovered leaves no dangling reference and no exit.
- A frame that changes nothing runs no hit test and reports no work to do.
- A hover, press, move and release in the steady state allocate nothing.
- A callback that pushes another event re-enters dispatch, and is trapped.
  Dispatching and settling both record into one hit-test list, so a nested walk
  would reallocate the list the outer one is holding; `DispatchScope` names it
  the way `PipelineOwner::PhaseScope` names a phase violation.

The widget-test harness — `Harness`, `Scripted`, `ScriptedRoot`, `elementFor` —
moved to `tests/widget_harness.hpp`, since the gesture tests drive the binding
exactly the way the widget tests do.

Verified on GCC 13.3 and Clang 18.1, and under ASan + UBSan: 153 tests, 647
checks. The library also compiles clean with `FLTR_ENABLE_CHECKS=OFF`.

---

## Milestone 6 — reactivity and ambient propagation

Game state changes at arbitrary times and is pushed in from the game loop. The
API for that is deliberately blunt: the consumer pushes its whole state every
frame and the framework decides what that implies for work.

```cpp
shield.set(player.shield);   // unchanged: returns, notifies nobody
ammo.set(weapon.ammo);       // changed: dirties the subtrees that read it
Scene scene = binding.drawFrame();
```

Two mechanisms, and they are not the same shape:

| | reaches | invalidates | keyed by |
|---|---|---|---|
| `Observable<T>` + `Watch<T>` | whoever subscribed | that element's subtree | the value's identity |
| `Ambient<T>` | descendants that read it | those elements only | the widget's type |

### One observable, two sinks

`Observable<T>` is `Listenable` plus a value and an equality check. It adds
nothing to the observer core built in M1 — which is the point. The brief asks
whether animated values and reactivity should share an abstraction; they share
this one, and the rebuild-versus-repaint distinction stays where M1 put it, in
the subscriber:

- a `Watch` element subscribes and marks *itself* needing build;
- a render object subscribes through `observeForPaint` and marks *itself*
  needing paint — no element, no reconciliation, no widget allocation.

Both are exercised against the same `Observable` in M6's tests, because the
claim is worth nothing if only one sink exists. The second is the path M7's
animations take, and it already costs zero builds and zero layouts per push.

`set` compares before it stores, so an unchanged push is a load, a compare and a
return. That is what makes the per-frame-push API affordable: the consumer does
not have to track what changed, and the framework does not have to diff a tree
to find out.

### Watch is a StatefulWidget, not a new element kind

`Watch<T>` needed nothing new: `initState` subscribes, `didUpdateWidget`
re-subscribes when it is pointed at a different value, `dispose` detaches, and
the notification calls `setState` with an empty change — the value it builds
from lives outside it, so there is nothing of its own to mutate.

`dispose` detaching matters, and is not merely tidy. Removing a subtree unmounts
every element in it before destroying any of them, so a sibling's `dispose` —
a panel reporting its own closing by pushing a value, which is ordinary game
code — runs while an already-unmounted `Watch` is still alive. Left to the
`Subscription` destructor, that push would reach a `State` whose element has no
tree, and `setState`'s precondition would trap.
`reactivity_a_watch_stops_listening_when_it_unmounts_not_when_it_is_destroyed`
fails without the explicit detach.

### DIVERGENCE: the ambient scope is a snapshot, not a persistent map

Flutter gives each element an `_inheritedElements` map, structurally shared with
its parent through a `PersistentHashMap` and copied on write by inherited
elements. Structural sharing needs a GC: the shared interior nodes have no
single owner.

**Chosen:** an `InheritedScope` is a flat open-addressed table owned by the
element that introduced it. An inherited element copies its parent's entries and
adds its own, once, at mount; every other element stores a pointer to its
parent's scope — one pointer store in `mount`, which is where the cost of the
whole mechanism lives for the elements that never provide anything.

A scope holds **no link to the one it extends**. That is the property worth
having, and it makes the O(1) claim structural rather than a benchmark: a lookup
*cannot* degrade into an ancestor walk, because there is no chain to walk.
`reactivity_an_ambient_scope_is_a_snapshot_not_a_chain` asserts it directly on
three scopes with no tree around them.

**What it costs:** copying k entries per inherited element mount, where k is the
number of *distinct ambient types in scope* — 2 or 3 in a HUD, not the tree
depth. Flutter's copy-on-write path copies less; ours copies a handful of
pointers once per provider, not per frame.

**What it buys:** unambiguous ownership, no shared interior nodes, no
refcounting — the same constraint that produced every other divergence here.

### Dependents are subscriptions, which decides when they are dropped

An inherited element *is* a `Listenable`, and a reader's dependency is an
ordinary `Subscription` stored on the reading element. There is no dependent set
and no reverse index: the intrusive list is both.

That changes what is affordable. Flutter clears an element's dependencies before
each rebuild and lets the build re-register what it actually reads; with a
dependent *set* on the provider, that is a removal from a container holding
every reader in the subtree. Here `clear()` is k detaches of two pointer stores
each, and re-registering is two more. So `Element::rebuild` clears
unconditionally, and an element that stops reading a value stops being woken by
it — precision Flutter pays for and we get for free.

`markNeedsBuild` is the notification target directly, so a change touches only
the dirty list. Readers are marked while the provider adopts its new
configuration, which is *before* the subtree beneath it is reconciled. That
ordering is what makes the interesting case work: a wrapper that rebuilds alone
re-emits the child ref it adopted, that ref is stale, and reconciliation skips
the entire subtree — while the readers inside it, reached by subscription rather
than by cascade, are rebuilt in the same frame.
`reactivity_an_ambient_change_rebuilds_its_readers_and_nothing_else` measures
exactly that: two elements built, the provider and the one reader, with a
`Column` and a `Padding` between them untouched.

### Ambient values are typed, not subclassed

Flutter has you subclass `InheritedWidget` per ambient value. `Ambient<T>` is
one class template instead, keyed by `widgetTypeOf<Ambient<T>>()`, so a theme
costs a struct rather than a widget:

```cpp
Ambient<Theme>::make({.value = theme, .child = hud()});
...
const Theme& theme = Ambient<Theme>::of(context);   // depends, and rebuilds
```

`T` is copied into the build arena, so it must be trivially destructible — the
rule every widget field already follows — and equality-comparable, since an
unchanged value must notify nobody. `updateShouldNotify` is that comparison; a
`Theme` with a defaulted `operator==` gets it for nothing.

Reading from anywhere other than a build — from `initState`, from a callback —
is a contract violation naming that, rather than the misleading "no provider
above this widget". The reason is the clearing above: a dependency registered
outside a build is dropped by the next one, so the read would keep working and
silently stop updating. (M7 tightened this check from "the element is mounted",
which stopped being the right question once `initState` began running on a
mounted element.)

### What is verified

- The whole state pushed every frame with nothing changed: no build, no layout,
  no paint, an unchanged revision, and `needsFrame()` false.
- A changed value rebuilds exactly one element and its subtree; the sibling
  watching a different value is not built.
- Several pushes between frames coalesce into one build.
- A `Watch` removed from the tree holds no subscription, and a later push does
  nothing at all; retargeted at another value, it follows it and drops the first.
- A value consumed by a render object repaints one boundary with zero builds and
  zero layouts — the other consumption path, on the same abstraction.
- A value pushed from a hover callback is built by that same frame, which is
  what M5's start-of-frame re-resolution was for.
- Pushing a value and pushing an ambient value both allocate nothing in the
  steady state, including the dependency re-registration a rebuild performs.
- An ambient value is found at any depth, the nearest one shadows an outer one,
  and an equal one notifies nobody.
- An ambient change rebuilds its readers and nothing else, through a subtree
  reconciliation skips entirely.
- An element that stops reading an ambient value stops being rebuilt by it, and
  one that leaves the tree leaves no dependency behind.
- Reading an ambient value that is not there is trapped.

Verified on GCC 13.3 and Clang 18.1, and under ASan + UBSan: 169 tests, 712
checks. The library also compiles clean with `FLTR_ENABLE_CHECKS=OFF`.

---

## Milestone 7 — animation

Animation is assumed to be running constantly and on many elements at once, so
the layering exists to make a frame of it cost as little as a frame of nothing.

```cpp
driver.forward();                  // or reverse, retarget, repeat
Scene scene = binding.drawFrame(dt);
```

### Time is a delta the consumer measured

`drawFrame(seconds)` takes the elapsed time the game loop measured, and a frame
in which no time passed does no ticking at all — which is why the 169 tests
written before this milestone still call `frame()` and still do nothing.

DIVERGENCE: Flutter's `Ticker` receives an absolute elapsed duration and carries
a start offset, which it has to re-base whenever the ticker is muted and
unmuted. **Ours receives a per-frame delta.** That makes muting exact rather
than approximately right: a muted ticker is simply not subscribed, so it resumes
where it stopped with no elapsed time to reconcile and no offset to keep. The
cost is that a driver cannot answer "how long have you been running", which
nothing here asks.

### DIVERGENCE: the frame clock is a Listenable, not a list of tickers

`TickerRegistry` holds the current frame's delta and a `Notifier`; a `Ticker` is
a `Subscription` on it plus three flags — started, muted, attached — that one
`sync()` turns into subscribed or not.

Flutter's `SchedulerBinding` keeps a map of ticker callbacks and re-entrancy is
handled per call site. Reusing the observer core from M1 gets three things for
nothing: starting or stopping a ticker from inside a tick is already safe (the
cursor chain), a stopped ticker is *provably* holding nothing, and the
"an animation in a hidden panel costs nothing" requirement becomes
`activeTickerCount()` — a number a test reads rather than a claim a comment
makes.

### The driver: duration is the whole range

`AnimationDriver` runs 0..1 and `duration` is the time for the *whole* range, so
a shorter journey takes proportionally less time. That single decision is what
makes interruption behave: reversing from 0.3 takes 30% of the duration rather
than all of it, so the pointer entering and leaving repeatedly neither drifts
nor slows down. `animation_repeated_interruption_accumulates_no_error` runs 40
interrupted round trips at irregular frame times and then asserts that one clean
run still lands exactly on 1.0.

Value and status are separate channels — `Listenable` for the value,
`statusChanges()` for the status — because a status listener wants the two
transitions, not the two hundred frames between them.

`Dismissed` and `Completed` mean settled at the bottom and the top of the range.
Settling in between reports the end it was travelling toward, which is Flutter's
behaviour and the only wart in the enum.

### Curves are function pointers, and reversal is where they bite

A `Curve` is one function pointer, so it copies into a widget configuration for
free and stays trivially destructible like everything else in the arena. Seven
of them, analytic: linear, quadratic and cubic ease-in/out/in-out. A
cubic-bezier curve needs per-instance state and is not here, because nothing has
asked for one.

A driver has a forward curve and an optional reverse curve. **Leaving the
reverse curve unset is the continuous choice**: with one curve in both
directions, reversing mid-flight is exactly continuous, because the value is the
same function of the same progress. A distinct reverse curve changes the value
at the instant of the reversal — a discontinuity Flutter has too.
`animation_a_reverse_curve_is_the_one_case_reversing_is_not_continuous` pins
that down rather than leaving it to be discovered.

### Interpolation is a concept, not a hierarchy

```cpp
template <class T>
concept Interpolatable = requires(const T& a, const T& b, float t) {
  { lerp(a, b, t) } -> std::convertible_to<T>;
};
```

M1 already defined `lerp` next to every geometry and paint type, so every type
the widget layer uses satisfies this without adding anything, and a consumer's
own type joins by defining one beside itself. Flutter's `Tween<T>` is a class to
subclass per type; ours is a `{from, to}` aggregate with an `at(t)`.

Composing interpolation with easing is `tween.at(driver.value())` — the driver
applies the curve, so there is no `CurvedAnimation` between them.

The one sharp edge: a scalar has no associated namespace, so `lerp(float, ...)`
has to be visible where the concept is *defined* rather than where it is used.
`static_assert(Interpolatable<float>)` sits next to the concept so that a
missing include fails there instead of somewhere confusing.

### One observable interface, two sources, two sinks

M6 asked whether animated values and reactivity should share an abstraction.
They share `ValueListenable<T>` — value plus notification — and nothing more:

|  | changes because | consumed by rebuilding | consumed by repainting |
|---|---|---|---|
| `Observable<T>` | the game pushed | `Watch<T>` | `observeForPaint` |
| `AnimatedValue<T>` | time passed | `Watch<T>` | `observeForPaint` |

`Watch<T>` needed one word changed — `Observable<T>*` became
`ValueListenable<T>*` — to become the general animation path as well as the
reactivity one. The rebuild-versus-repaint distinction stays where M1 put it, in
the subscriber, and is now measured on both rows:
`animation_advancing_a_paint_only_animation_rebuilds_nothing_and_relayouts_nothing`
against `animation_the_same_value_drives_a_rebuild_through_watch`, on the same
`AnimatedValue`.

A render object's animated property is an `Animatable<T>`: a constant and an
optional source, where a source shadows the constant. That is what lets one
`Opacity` widget cover `.opacity = 0.5f` and `.animation = &fade` instead of
needing two, and setting the shadowed constant invalidates nothing.

### Where a frame ticks

`drawFrame` is: tick, settle hover, build, layout, paint.

Ticking has to precede the build, because the general consumption path turns a
tick into `setState`; ticking after `flushBuild` would show every `Watch` over an
animation one frame stale. The visible consequence is that a state change and
the animation it starts land in the frame they happened, showing the value the
animation starts *from*, and advance from the next frame — the elapsed time this
frame reports belongs to the interval before the animation existed.

`needsFrame()` includes "any ticker is active", so a consumer that only draws
when the framework asks still animates.

### What is verified

- A driver arrives exactly on its target however coarse the last step, reports
  that it settled, and then does nothing however much more time passes.
- Frame durations are irregular and the total is what matters.
- Value and status are notified independently, and neither fires once settled.
- Reversing mid-flight does not move the value at the instant of the reversal,
  and takes time proportional to how far it has to come back.
- Forty interrupted round trips stay in range and leave no residue.
- Retargeting to an interior value proceeds from the current one.
- A repeating driver wraps; a ping-pong one reflects, and its status flips.
- A stopped, settled or muted driver holds no subscription at all, and a muted
  one resumes exactly where it stopped.
- A driver started before it has a clock begins when it gets one.
- Every curve is pinned at both ends, stays in range, and never doubles back.
- Interpolation is checked against hand-computed midpoints for the scalar,
  colour, offset, size, rect, insets, alignment, radius, transform and
  decoration types.
- A tick that does not move the interpolated value notifies nobody — thirty
  frames of a slow colour animation cause four rebuilds, not thirty.
- The render-attached path: zero builds, zero layouts, one boundary repainted.
- The rebuild path: the same value through `Watch`, which does relayout,
  because that is what it is for.
- An animation in an unmounted subtree consumes no time and holds no ticker, and
  a State releases its ticker on unmount rather than on destruction.
- A frame of animation allocates nothing.

---

## Milestone 8 — implicit animations and the render-attached widgets

This is the layer most UI code is written against, so it is the one whose
ergonomics were designed first:

```cpp
AnimatedOpacity::make({
  .opacity = hovered ? 1.0f : 0.35f,
  .animation = {.duration = 0.12f, .curve = Curves::easeOut},
  .child = badge(),
})
```

Nothing else. No controller to own, no ticker to dispose, no tween to declare.

### DIVERGENCE: no `forEachTween`; the animated value goes down the tree

Flutter's `ImplicitlyAnimatedWidgetState` visits a set of tweens on every
configuration change, rebuilding the subtree on every tick of the resulting
animation because what it passes down is a *sample* of the value.

**Chosen:** the State passes down the `AnimatedValue<T>` itself, and the render
object observes it. So the configuration change is the last build; every frame
after it invalidates exactly one render object.
`implicit_ticking_rebuilds_nothing_and_relayouts_nothing` asserts zero builds,
zero layouts and one repainted boundary on every one of eight frames.

That collapses the machinery too. `ImplicitlyAnimatedState<W>` is one template of
about twenty lines, and a widget joins by naming its value type, its target, its
timing, and a static `compose` that assembles the widget it wraps:

```cpp
static WidgetRef compose(const AnimatedOpacity& self, ValueListenable<float>& value) {
  return Opacity::make({.animation = &value, .child = self.args_.child});
}
```

### Re-basing, not retargeting

On a configuration change the interval is rebuilt as `{value on screen, new
target}` and the driver restarts from zero. The *value* therefore proceeds from
where it was — which is what the brief asks for — while the driver's normalized
progress does restart, which is what makes the curve apply to the whole of the
new journey rather than to a fragment of an old one.

Two consequences worth stating. An interrupted animation takes the full duration
to cover the remaining distance, so it is slower than the velocity-preserving
`animateTo` the driver offers for explicit use; that is Flutter's behaviour and
it reads better than a fast twitch. And an interval from a value to itself is
not an animation:
`implicit_a_target_that_changes_and_changes_back_leaves_nothing_running` changes
a target and changes it back before a frame passes, and no ticker survives it.

### Four properties, and the one that is not free

`AnimatedOpacity`, `AnimatedTransform` and `AnimatedDecoration` observe through
`observeForPaint`. `AnimatedAlign` observes through `observeForLayout` — the
layout-invalidating counterpart M1 deliberately left absent until something
animated a layout property. Alignment resolves during layout in this framework,
so every frame of it lays out the subtree below.

The brief asks that the call site make the cheap and expensive obvious. It is
one class comment and one structural difference — the expensive one is the only
widget whose render object calls `observeForLayout` — and it is measured:
`implicit_an_animated_alignment_lays_out_every_frame_and_says_so` asserts
`layouts > 0` and hand-checks where the child lands, next to three tests
asserting `layouts == 0` for the others.

### Muting a hidden subtree

`TickerMode` is an inherited widget, so it costs an O(1) scope lookup and the
implicit state reads it in `build` — where a change wakes exactly the readers,
by M6's machinery. A tree with no `TickerMode` in it registers no dependency and
pays nothing.

Muting detaches the ticker subscription, so a hidden panel keeps its state, its
subscriptions and its position but does not appear in `activeTickerCount()`, does
not make `needsFrame()` true, and resumes at the value it was frozen at.

### What `initState` needed, and what that cost

A State creates its driver in `initState`, which needs the tree's services — and
`initState` ran *before* the element was linked, so it had no owner and no way to
reach them.

`Element::mount` now links the element, resolves its ambient scope, calls
`didMount`, and then builds; `StatefulElement` runs `initState` from that hook.
This is Flutter's order, and it makes `State::mounted()` true in `initState`,
where it was previously false.

The one thing that ordering paid for: `dependOnInherited` used to be guarded by
"the element is mounted", which after the change no longer excluded `initState`.
It is now guarded by "an element is building", which is both the real reason and
a stronger check — it catches a read from a callback too. The guard is a flag set
by `Element::rebuild` for the length of `performRebuild`, released however that
build is left, the way `PipelineOwner::PhaseScope` already does for the pipeline.

### What is verified

- A configuration change animates to the new value, and the first configuration
  animates from nothing.
- An interruption continues from the value on screen, and thirty interruptions
  in a row still settle exactly on the target.
- Ticking rebuilds nothing and relayouts nothing, for opacity, transform and
  decoration; animating alignment does relayout, and moves the child where hand
  computation says.
- A hidden subtree holds no ticker, asks for no frames, and resumes where it
  stopped.
- A State unmounted but not yet destroyed has already released its ticker —
  which is the window a subtree teardown actually spends, since every element in
  it unmounts before any of them is destroyed.
- A steady frame of implicit animation allocates nothing.

Verified on GCC 13.3 and Clang 18.1, and under ASan + UBSan: 207 tests, 1245
checks. The library and the animation headers also compile clean with
`FLTR_ENABLE_CHECKS=OFF` on both compilers.

---

## The demo, and what it asked of the library

The interactive demo (`demo/`, built with `-DFLTR_BUILD_DEMO=ON`) is the first
consumer of this framework that was not written by the framework's own tests. It
exists to be evidence — that layout, reactivity, animation and hit-testing hold
up on a real screen, and that an idle frame costs nothing — and to be a worked
example of how UI is authored here. What it needed and did not find is recorded
below, because a library's gaps are most visible from outside it.

### A child list whose length comes from data

`WidgetList` had exactly one authoring entry point, `std::initializer_list`,
which fixes the number of children at the call site. That is right for the shape
almost every widget has, and wrong for the one the demo's server browser is: a
few hundred rows that come from a vector, get re-sorted, and must keep their
per-row `State` across the sort.

The workaround — chunking rows into nested fixed-size `Column`s — does not work,
and the reason is worth stating because it is a real constraint rather than an
inconvenience: keys reconcile only within one parent's child list, so a sort that
moves a row from one chunk to another destroys its element and its `State`. That
is the correct behaviour (cross-parent reparenting is what global keys are for,
and those are deferred), but it means a data-driven list has to be one list.

The gap was in the authoring surface only. `WidgetList`'s representation is
already `{const WidgetRef* items_; std::size_t size_; std::uint32_t generation_}`
filled from a runtime-sized arena allocation, `ChildList::update` already
reconciles arbitrary lengths by key, and `FunctionRef` already exists. So the
change is one static factory:

```cpp
static WidgetList generate(std::size_t count, FunctionRef<WidgetRef(std::size_t)> build);
```

It allocates the array up front, calls `build` once per index, drops null refs
exactly as the braced form does, and stamps the same arena generation. No
invariant, ownership rule or reconciliation behaviour changes; `stale()` still
means what it meant.

Two deliberate choices inside that one line. It is a named factory rather than a
second constructor: `.children = {...}` is the shape every call site has, and an
overload that also matched two braced arguments would make which one runs depend
on what `WidgetRef` happens to be constructible from — a bad thing to have to
reason about at a call site. And the builder runs *now*, inside the same arena
scope, which is what allows the array to be sized once and never grown; every
widget the builder creates lands after it.

What this is not is lazy building. `count` children means `count` elements,
built eagerly, every rebuild of the list's owner. Slivers, viewports that build
only what is visible, and scroll physics remain out of scope, and the demo's
browser is deliberately sized so that the cost of *not* having them is visible in
its debug overlay rather than hidden by it.
