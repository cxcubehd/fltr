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

## Milestone 9 — input foundations

Everything M10 through M12 needs from the pointer and the keyboard, and nothing
that reads from either. Scrolling wants drag, velocity and the wheel; the
component layer wants long press, double tap, cursors and buttons; focus wants a
key surface; and all three want to ask a render object where it is.

### Time enters the gesture layer through the frame

M5 recorded that a contested tap could not show press feedback early "because
that needs a clock, and nothing in this layer has one". A long press has the same
problem in a sharper form: no pointer event arrives while a finger rests, so
nothing but a clock can ever fire it.

DIVERGENCE: Flutter stamps every `PointerEvent` with a platform timestamp and
schedules recognizer deadlines on `Timer`. `PointerBinding` instead owns a single
monotonic value that `WidgetBinding::drawFrame` advances by the frame's elapsed
seconds, and recognizers both measure durations and set deadlines against it.
Three things follow:

- there is one clock, so a deadline and a velocity sample cannot disagree about
  when something happened;
- a test makes a long press fire by passing `0.6f` to a frame, with no wall time
  and no flake — the same bargain the ticker already makes;
- a game pumps its input queue at a frame boundary anyway, so a per-event
  timestamp would usually be the frame's timestamp with extra steps.

The cost is resolution: a deadline lands on the next frame boundary rather than
exactly, and several events between two frames share one instant. The second is
the one that matters, because it is what the velocity tracker sees, so
`addSample` *replaces* a sample whose time is unchanged rather than adding a
second one — otherwise one instant would be weighted by however many events the
consumer happened to pump into it. If sub-frame timestamps are ever wanted,
adding one to `PointerEvent` is additive and nothing above has to change.

`needsFrame()` is true while any deadline is pending, so a consumer that draws
only when asked still fires long presses.

### Velocity is fitted, not differenced

`VelocityTracker` keeps twenty samples in a ring, discards anything more than
100ms older than the newest, and fits a degree-2 polynomial per axis by solving
the 3x3 normal equations, taking the slope at the moment of release. This is
Flutter's approach and it is worth the arithmetic: a finger that decelerates into
a release produces a large *final* delta, and a two-sample difference turns that
into a fling the hand did not throw. The test for it holds the pointer still for
the last three samples and asserts the estimate falls under the fling threshold.

The fit also reports confidence — how much of the variance it explains — so a
gesture that reversed mid-flight is distinguishable from one that did not. Below
three distinct samples, or with a singular system, it falls back to the secant
over the window, which is all the evidence supports.

### A render object can finally say where it is

The gap surgery item 1 named: `visitChildrenWithOffsets` walks down, hit testing
resolves positions on the way down, and nothing walked up. `RenderObject` now has
Flutter's pair — `applyPaintTransform(child, transform)` and
`getTransformTo(ancestor)` — plus `localToGlobal`, `globalToLocal` and a rect
form, with a null ancestor meaning the root.

The default `applyPaintTransform` reads the child's offset out of
`visitChildrenWithOffsets`, so every shifting object in the framework — padding,
alignment, flex, stack — got it right without being touched. It also traps a node
that is not a child, because the failure that would otherwise follow is the
quiet kind: an object that shifts its children and forgets to report one answers
the walk upward with a zero offset, and every space derived from it is wrong
with nothing at all saying so. Only
`RenderTransform` overrides it, and it now derives painting, hit testing and the
ancestor walk from one `childTransform()`, so the three cannot disagree about
where the pivot is. The cost, recorded rather than hidden: the default is a
linear scan of the parent's children per level, where Flutter's parent-data
pointer is O(1). `getTransformTo` is therefore O(depth x siblings), which is
fine for the handful of calls per gesture that use it and would not be fine per
frame per node.

**This lifts the M5 limitation.** `HitTestResult` still records resolved local
positions rather than transforms, and still should — but a recognizer no longer
needs the path to answer "where is this in my own space". It asks the region that
owns it, which walks up. A drag inside a scaled or rotated subtree now reports
correct local positions, and there is a test that drives one through a 2x scale.

### Signals do not enter the arena

A wheel notch is not contested: the pointer is not pressed and no gesture is
forming. `dispatchSignal` hit tests, then offers the signal to each region
innermost-first until one returns true, and reports whether any did — so a
consumer whose UI declined the wheel can scroll its own world with it. This is
what a browser does when a list scrolled to its end lets the page move, and it is
what M10 will hang smooth wheel scrolling on. It is the second hit-testing entry
point `PointerBinding` grew; the per-frame hover resolution was the first.

### The arena learned to be held

A double tap has to survive the first tap's *up*, and the sweep that up triggers
would award the pointer to whoever else is contending. `GestureArena::hold` and
`release` defer the sweep, exactly as Flutter's do. The visible consequence,
which is Flutter's too: a region with both `onTap` and `onDoubleTap` fires its
single tap one double-tap timeout late, because until that timeout elapses the
first tap may still turn out to be half of one. The test says so in its name.

### Recognizers are created on demand

`RenderPointerRegion` previously held a `TapGestureRecognizer` by value. With
four recognizer kinds that would make every region pay for all of them, so each
now lives behind a `unique_ptr` created when a callback wants it and destroyed
when none does. `recognizerCount()` makes "a region that only asks for a cursor
holds nothing" a number a test reads rather than a claim.

The same region carries the cursor. `MouseTracker` resolves the innermost hovered
region with an opinion, `Defer` being the absence of one, and `Basic` when
nothing under the pointer has any. The consumer reads `WidgetBinding::cursor()`
once a frame and applies it, since it owns the window.

### Two behaviours worth pinning down because they surprise people

Both are Flutter's, and both are tests rather than comments:

- **An uncontested drag begins at the down.** An arena with one member resolves
  the moment it closes, so a region whose only gesture is a drag starts dragging
  before anything has moved. That is what a scroll view wants — content follows
  the finger from the first pixel — and the slop only exists once something
  competes.
- **`DragStartBehavior::Start` swallows the travel before the accepting event,
  not the accepting event's own delta.** With input at 60Hz or better the
  residual is a few pixels. The two settings are tested with the same event
  sequence so the difference between them is exactly the ten pixels one of them
  swallows.

### DIVERGENCE: one drag recognizer with an axis

Flutter has `VerticalDragGestureRecognizer`, `HorizontalDragGestureRecognizer`
and `PanGestureRecognizer` as three classes. What differs between them is which
distance is compared against which slop and which component is reported, so this
is one recognizer with a `DragAxis`. The arena behaviour is identical, and a
widget that has to choose at runtime instantiates one type instead of three.

A constrained drag also reports only its own axis, so a consumer never has to
remember to discard the other one.

### The keyboard is a surface, not routing

`KeyboardBinding` holds what is physically held, the current modifiers, and an
ordered list of handlers, each of which may consume an event. There is no notion
of focus in it: M11's focus manager will register as one more handler, which is
what keeps a tree with no focus scope in it holding nothing.

The held-key set earns its place on its own — a game reads it directly for
movement, where an event stream is the wrong shape — and `clearPressed()` covers
losing the window, where the ups will never arrive.

Physical and logical keys are separate enums, as in Flutter and for the same
reason: movement binds to the position so WASD survives AZERTY, while a shortcut
binds to the meaning. The produced code point travels on the event rather than
being derived from the key, because which one it is depends on the layout and the
IME, both of which live on the consumer's side.

### What is verified

- A nested box maps its own space into an ancestor's, and stopping at an
  intermediate ancestor answers in that ancestor's space.
- The ancestor walk and hit testing agree through a scale, having derived the
  matrix from the same place; a degenerate transform reports the origin rather
  than a wrong point.
- Velocity is fitted over a window: a pointer that stopped before release is not
  a fling, samples sharing an instant are merged rather than weighted twice, and
  samples outside the horizon are not evidence.
- A contested drag waits for the slop and an uncontested one does not; a tap
  inside a draggable region is still a tap; dragging out of a tappable one
  cancels the tap and keeps the drag; nested drags are decided by which axis
  cleared its slop first.
- A drag inside a scaled subtree reports its own space.
- A long press fires from the clock while the pointer holds still, lifting early
  is a tap instead, drifting abandons it, and the deadline is withdrawn with the
  gesture.
- Two taps in the same place are a double tap; one too late is two single taps,
  the first of them delayed by exactly the timeout; one too far away is neither.
- A signal is offered innermost-first, chains outward when declined, is reported
  unconsumed when nothing wants it, and never enters the arena.
- The innermost region with an opinion decides the cursor; recognizers appear and
  disappear with the callbacks that want them; a cursor-only region holds none.
- A gesture answers only to the buttons it was given, so a secondary-button menu
  and a primary-button tap coexist on overlapping regions.
- A handler that consumes a key ends the walk; modifiers and the produced
  character travel with the event; losing the window releases everything held.
- A steady frame with a drag in flight allocates nothing, and neither does a
  frame that fires a deadline — which is why the deadline list is partitioned and
  insertion-sorted by hand rather than with `std::stable_partition` and
  `std::stable_sort`, both of which take a temporary buffer from the heap.
- A tree with nothing pending asks for no frames, and one with a press pending
  asks for them until it fires.

Verified on GCC 13.3 and Clang 18.1, and under ASan + UBSan: 246 tests, 1361
checks. The library also compiles clean with `FLTR_ENABLE_CHECKS=OFF`.

---

## Milestone 10 — scrolling

The centrepiece of this phase, and the first subsystem that spends M9 rather
than building on it: the velocity tracker feeds the fling, `dispatchSignal`
returning a bool is the wheel path, and `getTransformTo` is what makes
`ensureVisible` answerable.

### Scrolling is a paint operation, and that is measured

`RenderViewport` lays its child out once with the scroll axis released, then
*paints* it at a negative offset inside a clip. The offset reaches painting and
hit testing and nothing else, so moving it runs no layout at all. The viewport
observes the position through `observeForPaint` — the render-attached path M7
built — and is itself a repaint boundary with the scrolled content wrapped in
another, so a frame of scrolling re-records exactly three commands: the clip,
one `DrawList` referring to the content, and the pop. There is a test that
counts them, and one that asserts `layouts == 0` and `buildCount == 0` for a
frame that moved the offset.

Hit testing needs no clipping of its own: `RenderBox::hitTest` has already
rejected anything outside the viewport's own bounds before the child is asked.

### Physics is a policy object, and owns no state

`ScrollMetrics` is what the rules are allowed to see — four numbers — and
`ScrollPhysics` answers three questions about them: how much of a user's push
becomes offset, how much of a proposed offset the boundary refuses, and what
simulation a release implies. Because an instance holds nothing, the framework
ships them as shared constants and a consumer that wants its own writes one and
hands out a pointer. Nothing allocates a physics object per scrollable, and
`defaultScrollPhysics()` is a function-local static.

DIVERGENCE: Flutter composes physics by chaining (`AlwaysScrollableScrollPhysics
().applyTo(BouncingScrollPhysics())`), with each override delegating to a
parent. We do not. The cost is that mixing two behaviours means writing a class
that does both rather than composing two that each do one; the benefit is that
`physics->applyBoundaryConditions(...)` is one virtual call rather than a chain
whose length is a runtime property.

### The four simulations, and where `Simulation` lives

`AnimationDriver` is normalized 0..1 over a duration. A simulation is unbounded
in value and ends by tolerance, so `Simulation` stands *alongside* the driver in
`animation/` rather than changing it — which is what workstream G asked for, and
what a spring-based implicit animation would reuse later without touching
scrolling.

All four are Flutter's, in closed form rather than integrated, so a frame is a
handful of transcendentals with no accumulated error:

- **friction** — exponential decay, the iOS fling and the first half of the
  bouncing one. It can also answer `finalX` and `timeAtX`, which is how the
  bouncing simulation knows when to hand over without integrating to find out.
- **spring** — the three damping cases solved separately, with
  `withDampingRatio` naming the useful parameter rather than the raw one.
- **clamping** — Android's power-law curve over a finite duration, kept with its
  original constants so it is recognisably the platform's rather than an
  approximation of it. Unlike the others it genuinely *stops*.
- **bouncing** — friction until it reaches the edge, then a spring that inherits
  the friction's velocity (capped, or an unclamped transfer throws the content
  most of a screen past the end) and pulls it back.

A fling allocates one simulation. That is once per gesture, not once per frame,
and the test that a frame of fling allocates nothing measures the part that
matters.

### DIVERGENCE: activities are a value with a tag, not a hierarchy

Flutter models idle, drag, ballistic and driven scrolling as `ScrollActivity`
subclasses a consumer can extend. This is one `Activity` value with a
`ScrollActivityKind` and the fields each mode needs, and `tick` is a switch.

Two reasons. The set is closed — there is no extension point for activities
anywhere in this framework, so the polymorphism would buy nothing. And a
hierarchy allocates on every transition, where this allocates only for the
ballistic simulation: beginning a drag, taking a wheel notch, or going idle
costs nothing at all, which is what the steady-state rule wants from the two
that happen during a gesture.

The cost, stated: adding a sixth mode edits a switch rather than adding a class,
and a consumer cannot supply an activity of its own.

### Smooth wheel, against a target that keeps moving

Flutter applies a wheel delta to `pixels` immediately — a jump per notch. The
wheel activity holds a *target* instead: each notch adds to it, clamped by the
range, and each frame closes on it by `1 - exp(-dt/tau)`.

The exponential is not decoration. Our clock is a consumer-supplied variable
delta, so a fixed-duration tween would be wrong at any other frame rate, and
notches keep arriving mid-flight — which is the same "retarget from the current
value rather than restarting" requirement M7 met for `AnimationDriver`, applied
to a different quantity. A test spins the wheel twice with a frame in between
and asserts the two notches land on one target rather than the second one
restarting from where the first had got to.

Trackpad pans are not notches: they are already physical displacements, so
`PointerSignalKind::Pan` is applied directly with no easing.

**Not done, with the reason.** The brief asked for a fling from a trackpad pan's
own velocity. `PointerSignalEvent` has no phase, so nothing in the surface says
when a pan *ended*, and a fling has no moment to start at. The fix is one field
— Flutter has `PointerPanZoomStart/Update/End` — and it is additive; nothing
above would change. Pan therefore scrolls but does not throw.

### `ensureVisible`, and what surgery item 1 bought

`RenderViewport::offsetToReveal` maps the target's paint bounds into the
viewport's space with `localToGlobalRect(bounds, viewport)` — the M9 ancestor
walk — and adds the current offset to convert back into content space. That last
step is what makes the answer independent of where the view already is, which a
test pins by asking from two different offsets and getting the same number.

`alignment` is 0 for the leading edge and 1 for the trailing one. Flutter's
`ScrollPositionAlignmentPolicy` (keep-visible-at-start / at-end) is not here;
M11's directional traversal is what will want it, and it is a policy on top of
this, not a change to it.

### DIVERGENCE: no `ScrollNotification`

The principle stated at the top of this phase, now paid for. Flutter bubbles
scroll and overscroll notifications up the element tree as a second dispatch
path. We do not add one, and everything *inside* the scrollable reads the
position from `ScrollScope` in O(1) with subscription lifetimes that are already
correct.

The consequence is real and lands squarely on the two indicators: a scrollbar
and an overscroll stretch have to be drawn *outside* the scrollable, and an
ancestor cannot passively observe a descendant. So they are handed a
`ScrollController` — the object that already exists for reaching a scroll view
from outside — and they watch it, not just the position it holds, because it has
none until the scrollable below has built. `NestedScrollView`-style coordination
stays off the table until something wants it.

### Two objects that know each other, and clear the link

`ScrollController` and `ScrollPosition` hold each other, and each nulls the
other's pointer in its destructor. So does `RenderViewport` with the position.
This is not defensive coding: a consumer's controller is a local whose scope
ends in whatever order it happens to end in relative to the tree that used it,
and the first version of this got a use-after-free at teardown that only the
UBSan build caught. `RenderScrollbarThumb` uses the subscription itself as the
liveness token, which is the same idea spelled with the mechanism M6 already
provides.

A position remembers exactly one controller, so *swapping* one for another has
to be a hand-off — detach the old, then attach the new — and not an attach over
the top. Attaching over the top leaves the controller that was let go still
holding a link this side no longer knows about: it reads as a live client, and
its destructor writes into a position that may already be gone. That was a hole
in this invariant rather than an exception to it, and it is now a test.

### Overscroll is what the boundary refused

Under clamping physics the offset never leaves its range, so there is nothing in
`pixels` for a stretch to read. `ScrollPosition` therefore accumulates what
`applyBoundaryConditions` refused into `overscroll()` — saturating, so leaning on
an edge approaches a limit rather than winding up — and lets it decay once no
finger is holding it.

That makes the indicator a pure function with no state of its own: it scales the
view by `1 + fraction * maxStretch` along the scroll axis, anchored at the edge
*opposite* the one being pushed, as Flutter's `StretchingOverscrollIndicator`
does. The scale is an `Observable<Transform2D>` the `Transform` render object
subscribes to, so a frame of stretching repaints one object and rebuilds
nothing; only the pivot lives in the widget, and it changes once per overscroll
episode rather than once per frame.

Physics that let the offset leave its range produce no refusal and therefore no
stretch. That is deliberate: bouncing and stretching are two treatments of the
same event, and showing both at once would double-count. There is a test for
each half.

### The scrollbar

Behaviour and geometry, no style. `ScrollbarGeometry::resolve` is the whole of
the mathematics — thumb extent from the visible fraction with a floor a pointer
can actually hold, thumb position from how far through the range the offset is —
and both the render object that paints it and the state that interprets a drag
read the same function, so they cannot disagree about where the thumb is.

`RenderScrollbarThumb` observes the position for paint, so scrolling moves the
thumb with no rebuild. The track strip is an opaque pointer region rather than
the thumb itself: an auto-hiding bar that only answered to the pointer while
visible could never be revealed. A press on the track away from the thumb is not
a grab, so the offset does not leap to wherever the pointer landed.

The fade-out needs a delay, and neither the driver nor the gesture clock has
one, so the state counts idle seconds on a `Ticker` of its own and reverses the
fade when the count passes. It stops that ticker when the fade completes, which
is what lets `needsFrame()` go quiet again.

### The lifecycle wrinkle worth writing down

An indicator wrapping a scrollable is built *first*, so its controller has no
position yet, and the attach that happens while its own subtree is mounting
arrives in a later build generation — by which point the `WidgetRef` it holds
for its child belongs to a released arena.

The framework already handles that (`updateChild` skips a stale ref whose
element is unchanged), but only if the child stays in the same slot. So
`Scrollbar` builds its `Stack` unconditionally and puts a null in the track's
place until there is something to draw, rather than switching between "just the
child" and "a stack". Any widget whose shape depends on state that arrives after
its first build has this constraint; it is cheap to satisfy and expensive to
discover.

### Non-lazy, and what that costs

One child, laid out once. A scroll view builds and lays out every child whether
or not it is visible, so a few hundred are fine and a few thousand are not.
Nothing about the position, physics, activities, controller, wheel, overscroll
or scrollbar would change if lazy children arrived: workstream F replaces only
the child-management half, which is the same bet Flutter made when
`SingleChildScrollView` and `Viewport` came out differently.

### What is verified

- Friction decays toward a rest it only reaches in the limit and can say when it
  passes a point; a critically damped spring arrives without overshooting and an
  underdamped one rings; the clamping fling stops at a finite time and stays
  stopped; a bouncing fling carries past the edge and is pulled back.
- A viewport reports the extents its content implies, and content that fits
  reports nothing to scroll and declines the wheel.
- Moving the offset paints, never lays out and never rebuilds; the frame
  re-records a clip, one reference and a pop.
- Content that shrank under the offset springs back into range rather than
  jumping.
- Dragging moves the content with the pointer; a release with speed keeps going
  and settles inside the range; a slow release stops where it was let go.
- The boundary refuses what is pushed past it and reports how much, and lets it
  go when released; bouncing physics leaves its range instead, resisting more
  the further out it is; physics that refuse everything keep the view still.
- A wheel notch eases rather than jumping, notches in flight accumulate into one
  target, a wheel at the end declines so the signal chains outward, and a
  trackpad pan is applied directly.
- A controller holds its offset before it has a scrollable and is inert after it
  loses one; either object may be destroyed first.
- `ensureVisible` brings a descendant to the edge it was asked for and says the
  same thing wherever the view already is; `animateTo` arrives over the time it
  was given.
- The thumb is sized by the visible fraction with a floor, drags the content
  further than itself, ignores a press on the track away from it, appears when
  something moves and fades when nothing does, and stays up and widens under a
  resting pointer.
- A refused push stretches the view and lays nothing out; a view that bounces
  never stretches as well.
- A frame of fling allocates nothing, and a settled view asks for no frames.

Verified on GCC 13.3 and Clang 18.1 with zero warnings under `-Wall -Wextra
-Wpedantic -Wshadow -Wnon-virtual-dtor`, and under ASan + UBSan: 283 tests,
1676 checks. The library also compiles clean with `FLTR_ENABLE_CHECKS=OFF`.

---

## Milestone 11 — focus and keyboard

Before the components, deliberately: every component in M12 is then written
once, with keyboard activation, focus state and gamepad navigation designed in
rather than retrofitted. The load-bearing half of this milestone is not the
focus tree — it is what a tree *without* one costs.

### Pointer-only is a property of the tree, not a setting

M12 depends on this milestone to compile, not to run. That is a claim about
allocation, so it is spelled as one:

- `FocusScope` owns the `FocusManager`, and the manager is what registers the
  single `KeyHandler` on `KeyboardBinding`. No scope, no manager, no handler.
- `Focus` publishes its `FocusMarker` — the ambient nearest-enclosing-node —
  only when it found one to attach to. In a tree with no scope, `Focus` returns
  its child unchanged, so there is no inherited element, no scope table copy and
  no dependency edge.
- A `FocusNode` a State holds but never attaches has a null manager, and every
  operation on it is a no-op rather than an error: `requestFocus()` returns
  false, `hasFocus()` is false, and no key can reach it.

The test asserts all four: zero handlers, an unattached node, no `FocusMarker`
in the element dump, and Tab reported unconsumed so the game receives it.

The accepted cost is the mirror image of the property. Because the marker's
*presence* depends on there being a focus tree, introducing a scope above an
already-mounted subtree changes that subtree's shape and re-inflates it. This is
the same class of wrinkle M10 recorded for the scrollbar, and the same fix
applies where it matters: declare the scope at the root of the screen, which is
where one goes anyway.

The per-component opt-outs are Flutter's own three axes rather than invented
ones — `canRequestFocus` (never focusable, pointer only), `skipTraversal`
(focusable by a click or programmatically, never by Tab or a D-pad), and
`descendantsAreFocusable` (Flutter's `ExcludeFocus`). A HUD element that is
clickable but has no business in a menu's tab order is the second of those, and
it is the common case.

### DIVERGENCE: one walk, not four layers

Flutter routes a key through `HardwareKeyboard`, then the focus chain, then
`Shortcuts`, then `Actions`, with `Intent` in between so an ancestor can
re-target what a descendant means by "activate".

There is one walk here. Every node from the focused one up to the root scope is
offered the event and the first to return true ends it; traversal is what
happens to the keys nobody took. `Shortcuts` is a node that declines to be
focused, so it sits in the chain above its subtree and sees keys on the way up —
which is exactly what Flutter's `Shortcuts` is, minus the indirection.

The cost, stated: a shortcut is bound to its callback where it is declared, so
an ancestor cannot re-bind what a descendant means. Nothing in this framework
wants that today; the layer to add if it ever does is `Actions`, and it would
sit on top of this rather than replace it.

`ShortcutList` copies into the build arena as `WidgetList` does, and
`ShortcutsState` copies *out* of it at `initState` and `didUpdateWidget` rather
than at build — because a rebuild driven by `setState` or by an ambient value
runs `build` against the stored configuration, whose arena is long gone, while a
key arrives later still. There is a test that rebuilds four times and then fires
the shortcut.

### DIVERGENCE: tab order is build order

Flutter's default is `ReadingOrderTraversalPolicy`, which sorts descendants
geometrically. This is Flutter's other policy, `WidgetOrderTraversalPolicy`:
the traversable descendants of the innermost enclosing scope, in the order they
were built, wrapping at either end.

Two reasons. Source order is what the author of a menu already controls, and it
is the order they wrote. And it needs no rects, so Tab works before the first
layout — which reading order cannot. The cost: a two-column form tabs down the
columns only if it was built that way.

### Directional traversal, and why the band matters

The D-pad case, and the one that is a first-class requirement for a game rather
than an afterthought. Candidates are the same set Tab uses. From the focused
node's centre:

1. discard anything not strictly ahead in the requested direction;
2. prefer a candidate whose extent overlaps the current node's on the *other*
   axis — "in band" — because the row below should beat the row below and three
   columns across, even when the latter is nearer;
3. rank in-band candidates by how far ahead they are, and everything else by
   plain distance.

Step 2 is what the test pins: an off-axis node placed strictly closer along the
axis of travel still loses to the one directly below. Step 3's fallback is
tested separately with nothing in band at all.

DIVERGENCE: Flutter's `DirectionalFocusTraversalPolicy` keeps a history stack,
so moving down and then up returns to exactly where you started even in a ragged
layout. This is stateless, so a down-then-up round trip is only guaranteed in a
regular one. The stack is additive if a real screen wants it.

### Scopes are the traversal groups

Flutter separates `FocusScope` from `FocusTraversalGroup`, so one scope may hold
several independent tab orders. A scope is both here: Tab wraps within the
innermost scope enclosing the focused node, and the arrows search inside it. A
dialog that should keep the keyboard to itself is therefore just a scope, and
needs nothing else to trap traversal. The cost is that two independent orders
need two nested scopes rather than two groups in one — which is how it would be
written anyway.

Each scope remembers the child that last held the focus, so a scope regaining it
lands where it left off. Removing the focused node hands the focus to the
enclosing scope rather than dropping it, and every scope that remembered
something inside the removed subtree forgets it first — otherwise the scope
would hand the focus straight back to a node that no longer exists.

### Two objects that know each other, again

`FocusManager` and its root `FocusScopeNode` hold each other and each nulls the
other's pointer on the way out, exactly as `ScrollController` and
`ScrollPosition` do, and for the same reason M10 recorded: a consumer's node is
a local whose scope ends in whatever order it happens to end in relative to the
tree that used it.

The State that owns a node uses its *subscription* to that node as the liveness
token — a `Subscription` unhooks itself when its `Listenable` dies — so
`OwnedFocusNode::get()` returns null once a consumer's node is gone rather than
a pointer into it. That is the same trick `RenderScrollbarThumb` uses, spelled
with the mechanism M6 already provides.

Order matters on the way out: the subscription is detached *before* the node is,
because detaching a node moves the focus, and a focus change must not arrive as
a callback on a State whose element has already unmounted. `WatchState::dispose`
makes the same bargain for the same reason.

### `revealMinimally`, which M10 said this milestone would want

M10 left `ensureVisible(target, alignment)` and recorded that Flutter's
alignment policies were not there, because "M11's directional traversal is what
will want it". It does, so it is here: `RenderViewport::offsetToRevealMinimally`
answers the nearest offset that brings a target fully into view, and the current
one when it already is.

That is the right behaviour for arrowing down a list — the view creeps by one
row instead of jumping the row to an edge — and it is what `Focus` calls when
its node takes the focus and there is a `ScrollScope` above it. A node that is
already visible produces no scroll at all, which the test asserts by focusing
the same node twice.

### What is verified

- A tree with no scope registers no handler, publishes no marker, attaches no
  node, and reports Tab unconsumed; one scope registers exactly one handler
  however many nodes hang off it.
- Tab moves in build order and wraps; Shift-Tab goes back; a node that skips
  traversal is passed over but still takes the focus when asked directly; one
  that cannot request focus is never focusable; excluding a subtree removes
  everything below it, and un-excluding puts it back.
- A key walks from the focused node up, in order, and the first to take it ends
  the walk; a key nobody wanted is reported unconsumed.
- A D-pad moves to the nearest node that way, prefers one it lines up with over
  a nearer one off to the side, falls back to plain distance when nothing lines
  up, and leaves the key alone when there is nothing that way — or when the
  scope said the arrows are not its business, while Tab still works.
- A nested scope keeps traversal to itself, remembers where it was, and gives
  the focus back there; unfocusing returns it to the scope and forgets; removing
  the focused node leaves the focus on its scope and traversal starts over.
- A node notifies only when its own state moved, and focusing what is already
  focused is not a change.
- Autofocus claims an empty scope and never steals from a full one.
- A shortcut fires while the focus is below it, not when it is elsewhere,
  matches its modifiers exactly, and still works several builds after the arena
  that declared it was released.
- Moving the focus into a scrolled row brings it into view by the least it can,
  and does nothing at all when it is already visible.
- Moving the focus around allocates nothing.

Verified on GCC 13.3 and Clang 18.1 with zero warnings under `-Wall -Wextra
-Wpedantic -Wshadow -Wnon-virtual-dtor`, and under ASan + UBSan: 309 tests,
1776 checks. The library also compiles clean with `FLTR_ENABLE_CHECKS=OFF`.

## Milestone 12 — component behaviours

The layer Flutter kept private. `Checkbox`, `Switch`, `Slider` and `TextField`
live in `material/` and are entangled with `ThemeData`; the reusable pieces —
`ToggleableStateMixin`, `_RenderSlider`, `ButtonStyleButton`,
`WidgetStatesController` — are all internal. Everything below is those pieces,
promoted, with everything about theming discarded.

A component owns its value and the rules for changing it, its hit regions and
recognizers, its focus node and keyboard handling, and its interaction state. It
owns nothing about how any of that looks.

### The call-site question, resolved once

`INSTRUCTIONS2.md` asked for this to be settled at the start of the milestone
rather than during it. The answer is *both, layered*, and the layering is what
makes it cheap.

Every component publishes two things through one ambient `ComponentScope`: the
`WidgetStatesController` holding its `WidgetStates` bitset, and — for the
components that have one — a `ValueListenable<float>` giving its position in
0..1. A toggle's on-ness, a slider's value as a fraction of its range and a
progress bar's fill are the same quantity to a visual, so they are the same
field.

- **The observable path.** `ComponentScope::fractionOf(context)` and
  `statesOf(context)` hand back pointers to objects the component's `State`
  owns. Give one to a render object and hover costs no rebuild at all — the path
  M7 and M8 paid for, reached without any new mechanism.
- **The builder path.** `StatesBuilder` subscribes to the controller it finds
  and rebuilds its own subtree, and nothing above it. It stays thin because
  the general case was already `Watch`.

Neither accessor hands out the `ComponentScope` config itself. That config is
arena scratch and is stale the moment the build that made it ends, whereas the
pointers inside it stay good for as long as the component is mounted. Returning
the safe half is the difference between an API that can be held and one that
looks like it can.

Outside a component both accessors are null and `StatesBuilder` builds once with
nothing set, so a visual subtree written for a button also works on its own.

### Every component is the same envelope

`buildComponentShell` is one focus node, one pointer region, one publication and
one guard, and the components differ only in the gestures in the middle. Hover
is filled in there, because every component reports it identically.

Two properties of that envelope are load-bearing:

**Disabled is not a new axis in the focus tree.** A control that cannot be used
is one the keyboard should walk straight past, and `canRequestFocus` already
means exactly that — so `enabled = false` sets it false and the existing
`didChangePolicy` path moves the focus off. No component asks the focus
subsystem a question M11 did not already answer.

**The guard is always present.** Wrapping the subtree in an `AbsorbPointer` only
while disabled would change the shape of the tree at the moment a control is
greyed out, and the consumer's visuals — and any `State` they hold — would be
inflated afresh for a reason that has nothing to do with them. So the guard is
always there and only its flag moves. The cost is one proxy render object per
component, paid always rather than surprisingly.

### DIVERGENCE: a disabled component absorbs rather than ignores

Workstream I called for `IgnorePointer` *or* `AbsorbPointer` and left the choice
open. Both exist as widgets; components use absorbing. A disabled control drawn
over live content should swallow the click rather than let it fall through to
whatever is behind, which is the behaviour that is wrong-by-default in the other
direction — a disabled dialog button that dismisses the thing underneath it is a
bug, and a disabled HUD element that blocks the world is a wrap in
`IgnorePointer` away. The test asserts both halves.

Neither render object invalidates on change, because a hit test is resolved
afresh from the tree every time one runs. There is nothing recorded to throw
away.

### Consequence: press feedback follows the arena, not the finger

M9 chose to fire `onTapDown` when the tap *wins* the pointer rather than when
the pointer goes down, on the grounds that winning is when press feedback is
correct to show. M12 is where that choice becomes visible.

A button with only `onPressed` holds one recognizer, is the sole contender, wins
at the down, and is `Pressed` immediately. A button that *also* wants
`onLongPress` has two contenders, so nothing has won at the down and the button
is not pressed until either the long press starts or the release resolves it as
a tap. The long press therefore sets `Pressed` itself, which is not defensive
tidying — it is the only thing that reports the finger in that configuration.

This is recorded rather than worked around. Reaching for the eager signal would
mean either a second down callback on `Pointer` or reversing M9's decision, and
neither belongs in this milestone. The cost is confined to components that ask
for two gestures at once.

### The toggle: one machine, three shapes

`ToggleableStateMixin` is precisely the shared machine, and `RawToggle` is it
without the painter. A checkbox is the default; a radio is `canToggleOff =
false`, a member of a group only its siblings can clear; a switch is
`dragExtent > 0`, which is the only thing that creates a drag recognizer at all.
Tristate cycles Off, On, Mixed, which is Flutter's null third state named.

The position is an `AnimatedValue<float>` over an `AnimationDriver`, so
interruption re-bases from wherever the thumb had actually reached — M7's
property, reused rather than re-derived. A drag pins the position instead
(`setTween({p, p})` is "it is here and stays here"), and stops the driver, so
the thumb and the clock never fight over the same value. The switch drags from
the *down* point rather than from where the slop was cleared: a thumb that
lagged the first eighteen pixels of every drag would feel stuck.

Letting go is the interesting case, because the value is not the component's.
The toggle reports where it would go and then asks for a build unconditionally.
If the report was accepted, `didUpdateWidget` animates to the new value and
clears the pending settle; if it was refused — or was never a change — the
build settles the thumb back to where the value actually is. Flutter sets the
same flag and has the same hole when a consumer ignores `onChanged`; asking for
the build ourselves closes it.

### The slider maps absolutely

DIVERGENCE from Flutter's `Slider`, which tracks the thumb by accumulating drag
deltas: the value here is read off where the pointer *is*. The thumb reaches the
finger on the first frame and cannot drift away from it over a long drag.

The stated consequence is that there is no separate thumb hit region, because
pressing anywhere on the track is already a press on the thumb. What the thumb
does need is `thumbExtent` — Flutter's `_trackRect` inset, as one number — so
that the ends of the range stay reachable when the thumb has width.

The value runs the way the coordinate space does, so a vertical slider's
minimum is at the *top* and ArrowUp lowers it. That is the mechanical answer
rather than the expected one — a volume column wants its maximum at the top —
and it is documented on the field instead of being fixed with a `reversed` flag,
because inverting is one subtraction in the consumer's own value mapping and a
flag whose whole job is to negate a number earns its keep nowhere else.

Keyboard handling is where the slider meets M11. Only the two arrows *along* the
slider's axis are consumed; the pair across it is left alone and reaches
traversal, so a D-pad can leave a row of sliders rather than being trapped in
one. `Home` and `End` go to the ends, `PageUp` and `PageDown` move by a page.
One arrow moves by one division when there are divisions, and by `keyStep`
otherwise.

Like the toggle, the slider reports and assumes nothing: `fraction_` is derived
from the value the consumer pushed back, never optimistically from the gesture.
A consumer who clamps, snaps or ignores gets exactly what they asked for.

### `RawButton` is also the list item

The brief asked for "whatever list-item behaviour the first menu needs". Having
written the button, the answer is: `selected`. A menu's current entry differs
from a button by one published state, and publishing it is all this layer is
allowed to do about it — `ensureVisible` on focus already came free from M11.
No second component.

### Where the outlive rule ends

A controller and a focus node are consumer-owned and named by pointer, and the
standing rule is that they outlive the widget naming them. The subscription each
`Owned*` holds is a liveness token, and review made it worth saying exactly what
it does and does not buy.

What it buys is the window between the object's death and the next build: events
already in flight — a pointer callback, a focus notification — find a detached
subscription and go nowhere, instead of writing into freed memory. That window
is real, because teardown orderings are hard for a consumer to control.

What it does not buy is a *build* that still names the dead object. At that point
the rule has been broken outright, and the honest response is a contract
violation rather than a re-subscription. Both `OwnedStates::bind` and
`OwnedFocusNode::bind` previously fell through to `subscribe` in exactly that
case — a use-after-free reachable from ordinary consumer code, and one this
milestone inherited from M11 by copying the shape. Both now trap, and with checks
compiled out both stay memory-safe: the states controller publishes nowhere, and
the focus node falls back to the State's own so the tree stays whole.

The same reasoning left one contract behind in `buildComponentShell`. It fills in
hover for every component, which means a component that set its own `onEnter`
would have it silently overwritten. No component does, and chaining the two
callbacks would be machinery for a caller that does not exist — so the
overwriting is stated as a precondition instead, and a future component that
wants its own hover trips it rather than losing it.

### What is verified

`tests/test_components.cpp`, 34 tests:

- A controller notifies only when the set actually moved, and a component
  publishes into the consumer's controller when given one.
- A consumer's controller destroyed under a live component is let go of rather
  than written into, and a *build* that goes on naming the dead one is trapped
  rather than re-subscribed. Both confirmed by reverting the guards and watching
  ASan report the use-after-free.
- A slider maps, sizes and steps along whichever axis it was given: confirmed by
  breaking each of the three vertical branches in turn and watching the tests
  catch all three.
- A `StatesBuilder` rebuilds its own subtree and nothing above it, and the
  component itself never rebuilds for its own state — the zero-cost claim, as a
  build count. Outside a component it builds once with nothing set.
- Ignoring hides a subtree from the pointer and absorbing swallows it, both as
  hit-test path lengths; a disabled component leaks no click to what is behind,
  and one wrap in `IgnorePointer` makes it do the opposite.
- A button reports hover and press, fires on release, lets go of a press that
  travels off it, and holds exactly the recognizers its call site asked for.
- Space and Enter activate on the release; a held activator is one press; losing
  the focus mid-press lets go without firing, and the release lands elsewhere.
- A disabled button takes no input, drops its hover, becomes unfocusable, and
  Tab walks past it to the next thing.
- A long press keeps the button pressed and suppresses the tap.
- A toggle reports where it would go and moves nothing until the value comes
  back; it animates rather than jumping and settles off the clock; tristate
  cycles through mixed; a radio's own press cannot clear it.
- A switch drags with the finger, settles to the side it was left on, and
  reports nothing when it stopped short — and the thumb returns.
- A slider maps the pointer across its track, clamps past the ends, snaps to
  divisions, keeps the thumb inside the box, steps from the keyboard, and leaves
  the cross axis to traversal.
- Progress publishes a fraction and holds no ticker when determinate; the
  indeterminate sweep runs, wraps, and stops when it is turned off.
- A button in a tree with no focus scope registers no key handler, attaches no
  node, publishes no marker, reports Space unconsumed — and is a perfectly good
  button. M11's property, asserted from M12's side.
- A frame of hover and press allocates nothing.

Verified on GCC 13.3 and Clang 18.1 with zero warnings under `-Wall -Wextra
-Wpedantic -Wshadow -Wnon-virtual-dtor`, and under ASan + UBSan: 344 tests,
1945 checks. The library also compiles clean with `FLTR_ENABLE_CHECKS=OFF`.
