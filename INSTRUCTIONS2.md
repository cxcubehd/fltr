# Build Prompt II: from "the architecture is proved" to "you can build a game UI with it"

## What this document is

`INSTRUCTIONS.md` described a framework whose *architecture* had to be right:
the three-tree split, the constraints-down/sizes-up protocol, boundaries,
the arena, the gesture arena, the two animation consumption paths. Milestones
1–8 delivered that, and `DESIGN.md` records what each one cost.

None of that is what a game programmer touches. What they touch is: a menu
that scrolls, a button that reacts, a checkbox that toggles, a slider that
drags, a field they can type into, a list that keeps its scroll position. We
have none of those, and — more importantly — we are missing several of the
*mechanisms* they are built out of, not just the widgets themselves.

This document inventories what is missing and evaluates each candidate against
what it costs and what it unblocks. The scope chosen out of that evaluation is
recorded at the end, as milestones 9 through 15.

The rule from `INSTRUCTIONS.md` still stands and is restated here because this
phase is where it will be most tempting to break it: **be parallel to Flutter
unless there is a specific, stated reason to diverge, and when we diverge, say
what it costs.** Every workstream below names its Flutter reference. Read the
reference before designing the C++, exactly as in the first phase.

---

## Where M1–M8 left us

Worth stating explicitly, because several of these are assets this phase gets
to spend rather than build:

- **Repaint boundaries with independently recorded display lists**, positioned
  in the parent by a `DrawList` command carrying an offset. Moving a boundary
  re-records only the parent's (tiny) list. *This is the scrolling fast path,
  already built.* A viewport that scrolls a repaint-bounded child costs one
  re-record of a one-command list per frame of scrolling.
- **The render-attached animation path** (`observeForPaint`): a render object
  observes a `ValueListenable` and invalidates only its own painting. *This is
  the overscroll-stretch and press-feedback path, already built.*
- **A gesture arena with per-pointer routing**, and recognizers owned by the
  render object that offered them. Adding a drag recognizer is additive; the
  contest between a tap and a drag, or between two nested scrollables, is
  already modelled.
- **Hover resolved by diffing hit-test results per frame**, separate from
  gesture recognition. Button hover states get this for free.
- **`Observable<T>` / `ValueListenable<T>` / `Notifier` / intrusive
  `Subscription`**, with lifetime tied to the observer's storage and no shared
  ownership. Every "controller" object this phase introduces —
  `ScrollPosition`, `WidgetStatesController`, `TextEditingController` — is one
  of these, and needs no new mechanism.
- **Ambient (inherited) values with O(1) lookup and per-element dependency
  subscriptions.** Scroll position, focus scope, default text style, and the
  view metrics are all ambient values.
- **A ticker registry driven by a consumer-supplied delta**, with exact muting.
  Scroll physics run on this.
- **A headless harness with diffable tree and command dumps**, and per-frame
  counters (`layouts`, `paints`, `hitTestCount`, `activeTickerCount`). Every
  claim below about "costs nothing" is meant to be asserted, not argued.

---

## The gaps, stated plainly

**1. The input surface is half a surface.** The consumer can push
`Hover/Down/Move/Up/Cancel`. There is no drag, no velocity, no scroll wheel or
trackpad signal, no long press, no double tap, no keyboard at all, no text
input, and no mouse cursor request. Almost everything else in this document
sits on top of one of those.

**2. There is no scrolling.** Not a widget gap — a missing subsystem:
position, physics, activities, viewport, clipping, drag-to-fling, wheel,
scrollbar, and "make this visible".

**3. There is no interaction-behaviour layer.** `Pointer` gives raw
enter/exit/tap. Everything from there to "a button" — the pressed/hovered/
focused/disabled state machine, keyboard activation, drag-off-to-cancel,
toggle semantics, slider value mapping — has to be rewritten by every consumer,
differently, and wrongly.

**4. Text is measure-and-draw only.** `TextService` maps a point to a byte
offset, but not the inverse (byte offset to caret rectangle), has no range
boxes, and no cluster boundaries. A `Text` widget takes one `string_view` and
one style — no spans, no ambient default style, no baseline alignment.

**5. The layout catalogue has holes ordinary screens hit.** No `Center`, no
`Container`-equivalent, no `Wrap`, no grid, no `AspectRatio`, no
`IgnorePointer`/`AbsorbPointer` (which the disabled state of every component
needs), no `Visibility`, no intrinsics, no baseline cross-axis alignment, no
`LayoutBuilder`.

**6. Nothing can be drawn outside its parent's subtree, and nothing can
animate out.** Tooltips, dropdown menus, context menus, drag-and-drop ghosts,
and modal dialogs all need an overlay. Dialogs and menus additionally need a
subtree to survive its own removal long enough to animate away —
`INSTRUCTIONS.md` deferred that and warned it complicates reconciliation.

---

## Principles for this phase

**Components carry behaviour, never style.** This is the one large, deliberate
divergence in this document, so it gets stated first and justified.

Flutter has no headless component layer. `Checkbox`, `Switch`, `Slider` and
`TextField` live in `material/`, are entangled with `ThemeData`, and the only
reusable pieces are internal (`ToggleableStateMixin`, `_RenderSlider`,
`ButtonStyleButton`, `WidgetStatesController`). The nearest thing to a raw
layer is the handful of `Raw*` widgets (`RawScrollbar`, `RawGestureDetector`,
`RawMagnifier`) plus `EditableText`.

We build the layer Flutter kept private. A component owns:

- its value and the rules for changing it,
- its hit regions and its gesture recognizers,
- its focus node and its keyboard handling,
- its interaction state, published as an observable,

and owns *nothing* about how it looks. The consumer supplies the visual
subtree. Concretely, the state a component publishes is a `WidgetStates`
bitset (`hovered`, `pressed`, `focused`, `disabled`, `selected`, `dragged`) —
Flutter's `WidgetStatesController`, promoted from an implementation detail to
the layer's public contract.

There are two ways to hand that to a consumer's visuals, and the choice is
worth making once, deliberately, for the whole layer:

- a **builder** (`Callback<WidgetRef(BuildContext&, WidgetStates)>`), which
  rebuilds the visual subtree whenever the state changes — simple, matches
  Flutter's `builder` idiom, costs a rebuild per hover;
- an **observable** the visuals subscribe to, feeding `AnimatedDecoration` and
  friends directly — no rebuild at all on hover or press, at the cost of a
  less obvious call site.

The recommendation is *both, layered*: the component publishes the observable;
a thin `StatesBuilder` helper turns it into a builder for consumers who want
one. That keeps the "hover costs zero rebuilds" property that M7/M8 paid for,
without forcing every consumer to think about it.

**No new dispatch mechanisms.** Flutter has `Notification` bubbling
(`ScrollNotification`, `OverscrollNotification`) as a second, parallel
dispatch path up the element tree. We should not add it. Every consumer of a
scroll signal in this design — the overscroll indicator, the scrollbar, a
sticky header, the consumer's own code — is inside or below the `Scrollable`,
and can read the `ScrollPosition` from the ambient scope, which is already
O(1) and already has correct subscription lifetimes. The cost is real and
should be recorded: an *ancestor* cannot passively observe a descendant's
scrolling without being handed the position explicitly, so
`NestedScrollView`-style coordination is off the table until we want it.

**Invariants that do not move.** Restated, because this phase adds subsystems
that will each be tempted to break one:

- Widget configs stay trivially destructible and arena-allocated. A
  `ScrollController` or `TextEditingController` is consumer-owned and referred
  to by pointer — never owned by a widget.
- Layout invalidation and paint invalidation stay separate paths. Scrolling
  must be a *paint* operation, not a layout operation. Overscroll stretch must
  be a paint operation. Press feedback must be a paint operation.
- Steady state allocates nothing. A frame of fling, a frame of wheel easing,
  and a frame of hover animation each allocate nothing.
- No shared ownership in the tree. Focus nodes, scroll positions and states
  controllers are owned by exactly one `State` and referred to non-owningly.

---

## Candidate workstreams

Each one names its Flutter reference, what it depends on, what it unblocks,
and a verdict. The verdicts feed the options at the end.

### A. Input foundations

**What.** Drag recognizers (vertical, horizontal, pan) with touch slop and a
velocity tracker; long-press and double-tap recognizers; a pointer *signal*
event for scroll wheel and trackpad pan, dispatched down a hit-test path
rather than through the arena; mouse-cursor requests attached to a region and
surfaced to the consumer; `PointerEvent` gaining the button and modifier bits
a real UI needs.

**Flutter.** `gestures/monodrag.dart`, `gestures/multidrag.dart`,
`gestures/velocity_tracker.dart` (least-squares fit over a bounded sample
window, not a two-sample difference), `gestures/long_press.dart`,
`gestures/multitap.dart`, `gestures/events.dart` (`PointerScrollEvent`,
`PointerPanZoom*`), `rendering/mouse_tracker.dart` and
`services/mouse_cursor.dart`.

**Depends on.** Nothing. Everything else depends on it.

**Unblocks.** Scrolling (drag + fling + wheel), sliders, switches,
drag-and-drop, scrollbar thumbs, text selection drags, context menus.

**Divergences to expect.** Pointer signals do not enter the arena — a wheel
event is not contested, it is offered to each scrollable on the hit-test path
innermost-first until one consumes it (browser chaining behaviour, and what
Flutter does via `_receivedPointerSignal`). That means `PointerBinding` grows
a second hit-testing entry point; today it only hit tests on `Down`. The hover
path already re-resolves per frame, so the machinery exists.

**Verdict.** Mandatory. Nothing in this document ships without it. It is also
the cheapest workstream per unit of unblocking.

### B. Scrolling core

**What.** `ScrollPosition` (an observable holding pixels, min/max extent and
viewport dimension), `ScrollActivity` (idle, dragging, ballistic, driven),
`ScrollPhysics` as a policy object (`applyPhysicsToUserOffset`,
`applyBoundaryConditions`, `createBallisticSimulation`), the simulations
themselves (friction, spring, clamping-Android, bouncing-iOS), a
`ScrollController` the consumer owns, a viewport render object that lays its
child out with an unbounded main axis and *paints* it at a negative offset
inside a clip, and the `Scrollable` widget wiring drag recognizers to the
position.

**Flutter.** `widgets/scroll_position.dart`,
`scroll_position_with_single_context.dart`, `scroll_activity.dart`,
`scroll_physics.dart`, `scroll_simulation.dart`, `physics/friction_simulation.dart`,
`physics/spring_simulation.dart`, `widgets/scrollable.dart`,
`widgets/single_child_scroll_view.dart` and its
`_RenderSingleChildViewport`, `widgets/scroll_controller.dart`.

**Depends on.** A (drag + velocity + wheel). Also needs the animation layer to
grow simulation support — see G.

**The important structural note.** Flutter's `Viewport` is sliver-based, and
slivers are a second layout protocol. `SingleChildScrollView` is not: it uses
a plain box viewport that lays one child out with `maxHeight: infinity` and
paints it shifted. **Building the box viewport first is parallel to Flutter,
not a divergence** — it is the same choice Flutter made for the non-lazy case.
Everything about position, physics, activities, controller, wheel and
scrollbar is identical between the two; only the child-management half
differs. Slivers/lazy children are workstream F and can land later without
revisiting any of this.

Scrolling must move a *paint offset*, never re-run layout. With the scrolled
content wrapped in a repaint boundary — which the viewport should do
implicitly — a frame of scrolling re-records one `DrawList` command and
nothing else, and that is directly assertable with the existing counters.

**Smooth wheel scrolling (the one improvement over Flutter).** Flutter applies
a wheel delta to `pixels` immediately: a jump, per notch, every notch. Modern
browsers ease toward an accumulating target instead, which is what we want.

Implement it as an activity that holds a *target* offset: each wheel event
adds its delta to the target (clamped by the physics' boundary conditions) and
the activity eases the current position toward it. The easing must be
frame-rate independent, because our clock is a consumer-supplied variable
delta — an exponential approach (`pos += (target - pos) * (1 - exp(-dt/tau))`)
is the right primitive, not a fixed-duration tween, precisely because new
deltas keep arriving mid-flight. This is the same "retarget from the current
value rather than restarting" requirement M7 already met for
`AnimationDriver`, applied to a different quantity.

Trackpad pan deltas are *not* wheel notches — they are already-physical
displacements and must be applied directly, with fling handled from the pan's
own velocity. The event needs to distinguish the two; browsers and Flutter
both do.

**Also needed here.** `ensureVisible` — scroll a descendant into view — which
matters more than it sounds: it is what makes gamepad and keyboard focus
navigation inside a scrolling list work at all. It needs a render-object
capability we do not have; see the surgery list, item 1.

**Verdict.** The centrepiece of this phase, and the thing most likely to be
needed by the consumer's first real screen.

### C. Overscroll and scrollbar

**What.** An overscroll signal on the position (how far past the edge, in
which direction), an overscroll indicator implemented as a scale transform
about the overscrolled edge, and a raw scrollbar with a draggable thumb,
fade-out, and hover-to-expand behaviour but no styling.

**Flutter.** `widgets/overscroll_indicator.dart` (both
`GlowingOverscrollIndicator` and `StretchingOverscrollIndicator` — the latter
being the Android 12 stretch the brief asked for; it drives a `Transform`
scale on the child from an `AnimationController`), `widgets/scrollbar.dart`
(`RawScrollbar`).

**Depends on.** B. The scrollbar thumb also depends on A.

**Why it is nearly free given what exists.** The stretch is a transform driven
by an observable — exactly `RenderTransform`'s existing `setAnimation` path,
with the pivot at the overscrolled edge. It repaints and does not lay out or
rebuild, and that is assertable. Flutter's stretch is likewise an affine scale
of the whole child, so a plain 2D scale is not an approximation *of Flutter*,
though both are approximations of Android's true nonuniform rubber-band.

**Verdict.** Small, high-visibility, and the brief called it out by name.
Reasonable either as its own milestone or folded into B.

### D. Focus, keyboard, and directional traversal

**What.** A focus tree (`FocusNode`, focus scopes, one focused node per
scope), key events pushed in by the consumer and dispatched focused-node-first
then up the focus chain, a shortcuts/actions layer, traversal in tab order —
and **directional traversal for D-pad and stick input**, which is a first-class
requirement for a game and an afterthought almost everywhere else.

**Flutter.** `widgets/focus_manager.dart`, `widgets/focus_scope.dart`,
`widgets/focus_traversal.dart` (`DirectionalFocusTraversalPolicyMixin` is the
D-pad case), `widgets/shortcuts.dart`, `widgets/actions.dart`,
`services/keyboard_key.dart` and `hardware_keyboard.dart`.

**Depends on.** A new consumer input surface (key events), which is trivial to
add and belongs with A.

**Unblocks.** Keyboard activation of every component (space/enter on a button,
space on a checkbox, arrows on a slider), tab navigation, text fields at all,
gamepad-navigable menus, and `ensureVisible` becoming useful.

**Note.** Directional traversal wants each focusable's rectangle in a common
space — the same capability `ensureVisible` needs (surgery item 1). Doing A,
B and D in that order means paying for it once.

**Pointer-only must stay expressible, at both scales.** Focus-first is the
right build order, but it must not become a tax every consumer pays. Two
independent opt-outs, and both are Flutter's:

- *Per component.* A focus policy on every component in E, mirroring
  `FocusNode`'s own axes rather than inventing new ones: `canRequestFocus`
  (never focusable at all — pointer interaction only), `skipTraversal`
  (focusable by click or programmatically, but never reached by tab or D-pad),
  and `descendantsAreFocusable` for excluding a whole subtree, which is
  Flutter's `ExcludeFocus` / `FocusTraversalGroup(descendantsAreFocusable:
  false)`. A HUD element that is clickable but has no business in a menu's tab
  order is the common case, not an exotic one.
- *Per tree.* The focus subsystem is ambient, so a tree with no focus scope in
  it must register nothing, subscribe to nothing, and cost nothing — exactly
  the property `TickerMode` already has, and for the same reason. A component
  built without an ambient focus scope above it is pointer-only by
  construction, with no configuration and no null checks at the call site.

The second one is the load-bearing half: it means M12 depends on M11 to
*compile*, not to *run*, so a HUD that never wants keyboard or gamepad input
pays nothing for the fact that the machinery exists. It is also assertable —
a pointer-only tree holds zero focus nodes and zero key routes.

**Verdict.** Required for the component layer to be more than mouse-only, and
built so that mouse-only remains a first-class, zero-cost configuration.

### E. The component behaviour layer

**What.** `WidgetStates` + a controller that publishes it; then, on top:

- `RawButton` — hover, press, press-cancel-on-drag-off, keyboard activate,
  long-press, disabled, focus.
- `RawToggle` — the shared machine behind checkbox, switch and radio: value
  (optionally tristate), `onChanged`, a 0→1 position observable for the visual
  to animate on, and drag-to-toggle for the switch shape.
- `RawSlider` — value/min/max, optional divisions with snapping, thumb hit
  area distinct from track, drag with correct pointer-capture, keyboard step,
  page step, focus.
- `RawScrollbar` (if C is in scope), `RawProgress`, and a selectable/list-item
  behaviour if a menu needs one.
- `Label`/`Text` improvements belong in H, not here.

Every one of them takes the focus policy described in D — `canRequestFocus`,
`skipTraversal`, an optional consumer-owned focus node, and `autofocus` — so a
component can be declared pointer-only at its call site, and is pointer-only
by default in a tree that has no focus scope in it at all.

**Flutter.** `material/toggleable.dart` (`ToggleableStateMixin` is precisely
the shared toggle machine), `material/slider.dart`'s `_RenderSlider`,
`material/button_style_button.dart`, `widgets/widget_state.dart`
(`WidgetStatesController`, `WidgetStateProperty`). Read them for the state
machines and discard everything about theming.

**Depends on.** A (drag, long press) and, for anything keyboard-driven, D.

**The ergonomics question.** This is the layer game code writes against most,
so its call sites matter as much as the widget syntax did in M4. Resolve
early: does a component take a builder, an observable, or both (recommended:
both, layered — see Principles).

**Verdict.** This is the workstream that most directly answers "make it
usable". It is also the one that most depends on A and D landing first.

### F. Build-during-layout: `LayoutBuilder` and lazy children

**What.** The machinery that lets an element build while the render tree is in
its layout phase, then `LayoutBuilder`, then a lazily-populated list/grid that
builds only the children intersecting the viewport and recycles the rest.

**Flutter.** `widgets/layout_builder.dart`,
`rendering/sliver_multi_box_adaptor.dart`,
`widgets/sliver.dart` (`SliverMultiBoxAdaptorElement`, and the
`owner.buildScope(this, callback)` re-entry), `RenderObject.invokeLayoutCallback`.

**Depends on.** B for the viewport it populates.

**What it costs.** This is the most invasive item in the document, and
`INSTRUCTIONS.md` deferred it on purpose:

- The build arena must stay open across the layout phase, since children are
  inflated during it. Today `BuildScope` closes before layout runs.
- `PipelineOwner`'s phase separation is enforced, not documented. Building
  during layout requires an explicit, scoped exemption — Flutter's
  `invokeLayoutCallback` does exactly this, and it is the *only* legal way in.
- Element reconciliation must tolerate a parent whose children appear and
  disappear for reasons unrelated to its own configuration.
- A reduced version (a lazy box list, without adopting the full
  `SliverConstraints`/`SliverGeometry` protocol) gets 90% of the value.
  Adopting real slivers additionally buys pinned/floating headers, parallax,
  and mixing scroll behaviours in one viewport — and costs a second layout
  protocol, which would also force `Constraints` to become polymorphic
  (`DESIGN.md` records that we deliberately kept it monomorphic).

**When it actually matters.** A non-lazy scroll view builds and lays out every
child once, then costs nothing per frame in steady state. For an inventory of
200 slots or a settings menu, that is entirely fine. It becomes untenable for
a chat log, a full leaderboard, or an item database — thousands of rows.

**Verdict.** Defer unless a target screen genuinely has thousands of rows. If
it does, prefer the reduced lazy list over the full sliver protocol.

### G. Physics simulations in the animation layer

**What.** A `Simulation` concept (position and velocity as functions of time,
plus a done predicate), spring, friction and gravity implementations, and a
way to drive an animation from a simulation rather than from a normalized
duration.

**Flutter.** `physics/simulation.dart`, `spring_simulation.dart`,
`friction_simulation.dart`, `tolerance.dart`, and
`AnimationController.animateWith`.

**Depends on.** Nothing. B depends on *it*.

**Note.** `AnimationDriver` is normalized 0..1 over a duration. A simulation is
unbounded in value and settles by tolerance, so this is an addition alongside
the driver, not a change to it. It is also the natural home for spring-based
implicit animations, which `INSTRUCTIONS.md` listed as deferred-but-designed-for.

**Verdict.** Small, and effectively a prerequisite of B. Fold it into B rather
than making it a milestone.

### H. Text: spans, ambient style, baselines

**What.** A `Text` widget that accepts multiple styled spans (the
`ParagraphSpec` boundary already takes them; only the widget does not), an
ambient `DefaultTextStyle` with merge-on-descend semantics, `CrossAxisAlignment::Baseline`
in flex so text of different sizes sits on a common line, and text scaling as
an ambient view property.

**Flutter.** `widgets/text.dart`, `painting/text_span.dart`,
`widgets/default_text_style.dart`, `rendering/flex.dart`'s baseline handling.

**Depends on.** Nothing. Baseline flex needs `RenderBox` to expose a baseline
query, which `RenderParagraph` already computes.

**Verdict.** Cheap, and every screen benefits. Good candidate to bundle with
the layout catalogue (I).

### I. Layout catalogue and authoring ergonomics

**What.** The widgets whose absence is felt immediately: `Center`, a
`Container`-equivalent composing padding + decoration + constraints +
alignment + transform in one call site, `ColoredBox`, `Spacer`,
`AspectRatio`, `FractionallySizedBox`, `FittedBox`, `Wrap`, a grid, a table
or aligned-column layout for the scoreboard target,
`IgnorePointer`/`AbsorbPointer`, `Visibility`/`Offstage`, and intrinsic sizing
(`IntrinsicWidth`/`IntrinsicHeight`, deferred in phase one).

**Flutter.** `widgets/basic.dart`, `rendering/proxy_box.dart`,
`rendering/wrap.dart`, `rendering/grid.dart` / `sliver_grid.dart`,
`rendering/table.dart`, and the intrinsics half of `rendering/box.dart`.

**Depends on.** Nothing.

**Note on intrinsics.** Flutter's intrinsic queries are `O(n²)` in the worst
case and it says so loudly. They are needed by `IntrinsicWidth`, by table
column sizing, and by baseline-critical layouts. Add them as an explicit,
documented-as-expensive capability, not as a general layout tool.

**Note on `IgnorePointer`/`AbsorbPointer`.** Not optional — the disabled state
of every component in E needs one of them.

**Verdict.** Individually trivial, collectively the difference between "I can
express this screen" and "I have to write a render object". Good filler
alongside a heavier milestone.

### J. Overlay, portals, and animating a subtree out

**What.** An overlay layer painted and hit-tested above the main tree, with
entries inserted from anywhere in the tree; anchoring an entry to a widget's
rectangle (tooltips, dropdowns, context menus); and retaining a removed
subtree, still ticking, until its exit animation completes.

**Flutter.** `widgets/overlay.dart`, `CompositedTransformTarget`/`Follower`
and `LayerLink`, `widgets/animated_switcher.dart` for the exit case, and
`Draggable`/`DragTarget` for drag-and-drop, which needs the overlay for the
drag ghost.

**Depends on.** Anchoring needs surgery item 1. Drag-and-drop needs A.

**What it costs.** `INSTRUCTIONS.md` flagged the exit-animation case
specifically: the element tree must retain a removed subtree and keep it
mounted and ticking while it animates, which is the one thing that meaningfully
complicates reconciliation. The overlay itself is much cheaper than the exit
animation; they should be evaluated separately.

**Verdict.** The overlay is worth doing once menus and tooltips are wanted.
Exit animation is the single most expensive item per unit of user-visible
benefit in this document — worth it eventually, worth deferring now.

### K. Ambient view metrics

**What.** One ambient value carrying surface size, safe insets, UI scale
factor, text scale, and the current input mode (pointer / touch / gamepad) —
the last of which lets components decide, e.g., whether to show focus rings or
enlarge hit targets.

**Flutter.** `widgets/media_query.dart`, and `NavigationMode` /
`FocusManager.highlightMode` for the input-mode analogue.

**Depends on.** Nothing.

**Verdict.** Half a day, and it is the thing every consumer would otherwise
thread manually through their whole tree. Bundle it.

### L. Things to explicitly not build now

- **A `Notification` bubbling bus.** Ambient scroll position covers every
  current consumer. Recorded as a divergence with a named cost (see
  Principles).
- **A compositing layer tree.** The display-list-with-boundaries design
  already gives scrolling a cheap path. Revisit only when a real backend shows
  a need for GPU-side caching.
- **The full sliver protocol.** See F.
- **Navigation, routes, and a screen stack.** A game engine already has one.
  The overlay is the piece worth having.
- **Semantics and accessibility.** Out of scope per phase one — but note that
  the component layer in E is exactly where semantics would attach later, so
  building E does not close that door.
- **An inspector or hot reload.** Extend the harness dump instead when
  something is hard to debug.
- **Internationalization and bidi.** The `TextService` boundary was designed to
  permit it; nothing above it should assume otherwise.

### M. Text editing

**What.** A raw text field: an editing controller (text + selection +
composing region, as an observable), caret rendering and blinking, selection
rendering and drag-to-select, word/line motion, clipboard hooks, an input
formatter/filter hook, and an IME boundary — a service the consumer implements,
exactly as it implements `TextService`, through which the framework asks for a
platform text session and receives editing deltas.

**Flutter.** `widgets/editable_text.dart`, `rendering/editable.dart`,
`services/text_input.dart` (`TextInputClient`, `TextEditingDelta`),
`services/text_formatter.dart`, `widgets/text_selection.dart`.

**Depends on.** A (drags for selection), D (focus and key events), and a
`TextService` extension — see surgery item 4.

**What it costs.** This is the largest single item here, and the one where
"assume text is hard" bites hardest: caret placement, cluster-correct cursor
motion, selection rectangles across line breaks, and IME composing text are all
genuinely difficult and none of them can be faked convincingly with the
monospace placeholder. Scoping it to *single-line, no IME, no bidi, ASCII-ish*
makes it a fraction of the size and is honestly adequate for a name-entry or
chat-input field in a game, provided the `TextService` boundary is extended so
a real implementation drops in.

**Verdict.** Worth doing, worth scoping tightly, worth doing last.

---

## Surgery the existing code needs, whatever we choose

These are not milestones; they are concrete changes several workstreams
depend on, listed so they are budgeted rather than discovered.

1. **`RenderObject` cannot map a point or rectangle into an ancestor's
   space.** There is `visitChildrenWithOffsets` (downward) and hit testing
   resolves positions on the way down, but nothing walks up. Flutter's
   `applyPaintTransform(child, transform)` + `getTransformTo(ancestor)` is the
   parallel. Needed by: `ensureVisible`, directional focus traversal, overlay
   anchoring, and any consumer that wants to place a world-space marker over a
   UI element. Every shifting/transforming render object gains one small
   override.

2. **`PointerBinding` only hit tests on `Down`.** Pointer signals (wheel,
   trackpad pan) need their own hit test and their own innermost-first
   offering walk, not the arena. The per-frame hover resolution already does a
   hit test off the down path, so the shape exists.

3. **Phase separation forbids building during layout.** Required only by F,
   and the fix is Flutter's: a scoped, explicit exemption
   (`invokeLayoutCallback`), plus keeping the build arena open across layout.

4. **`TextService` is one-directional.** It answers "which byte is at this
   point" but not "where is the caret for this byte", "what rectangles cover
   this range", or "where is the next cluster boundary". M is impossible
   without these; adding them to the interface is cheap now and a breaking
   change later.

5. **`AnimationDriver` is duration-normalized.** Physics needs a simulation
   driver alongside it (workstream G).

6. **No keyboard input surface.** `WidgetBinding` needs a `dispatchKey`
   sibling to `dispatchPointer`, and a key-event type with logical/physical
   key separation and modifier state.

7. **`PointerEvent` has no button or modifier state.** Right-click context
   menus, shift-click range selection, and ctrl-scroll zoom all need it. Also,
   `HitTestResult` deliberately records resolved local positions rather than
   transforms (a recorded M5 divergence) — a drag inside a *rotated* subtree
   therefore cannot convert its global delta to local. Decide whether to lift
   that limitation now or record it as accepted.

8. **The `Configure` boilerplate tax.** `DESIGN.md` records it as known and
   deliberate. This phase roughly doubles the widget count, which is a
   reasonable moment to re-examine whether a macro or a different CRTP shape
   pays for itself after all.

---

## Dependency order

```
A  input foundations ──┬──> B  scrolling core ──┬──> C  overscroll + scrollbar
                       │      (+ G simulations)  └──> F  lazy children
                       │
                       ├──> D  focus + keyboard ──┬──> E  component behaviours
                       │                          └──> M  text editing
                       │                                   ^
                       └──> J  overlay + drag-drop          │
                                                            │
H  text spans/baselines ────────────────────────────────────┘
I  layout catalogue      (independent)
K  ambient view metrics  (independent)
```

The critical path is A → B and A → D → E. H, I and K are independent and can
ride along with whichever milestone has room.

---

## The committed plan

Decided. Milestones 9 through 15, in this order. Each is independently
verifiable before the next begins, as in phase one.

**M9 — Input foundations.** Workstream A, plus surgery items 1, 2, 6 and 7.
Drag recognizers with slop, a real velocity tracker, long press, double tap,
pointer signals for wheel and trackpad pan, mouse-cursor requests, buttons and
modifiers on `PointerEvent`, a key-event surface on `WidgetBinding`, and
`applyPaintTransform` / `getTransformTo` on `RenderObject`. Small, and
everything below is gated on it.

**M10 — Scrolling.** Workstreams B, G and C together. `ScrollPosition`,
activities, physics as a policy object, friction/spring/clamping/bouncing
simulations, a consumer-owned `ScrollController`, drag-to-fling, smooth wheel
against an accumulating target, overscroll stretch through the existing
render-attached transform path, a raw scrollbar, and `ensureVisible`.

**Non-lazy box viewport only.** One child, unbounded main axis, painted at a
negative offset inside a clip — the same choice Flutter made for
`SingleChildScrollView`. No build-during-layout, no hole in phase separation,
no arena-lifetime change. Position, physics, activities, controller, wheel,
overscroll and scrollbar are all identical whether or not children are lazy, so
workstream F stays available later without revisiting any of M10. The accepted
cost, stated plainly: a scroll view builds and lays out every child once, so a
few hundred children are fine and a few thousand are not.

**M11 — Focus and keyboard.** Workstream D, including directional traversal for
D-pad and stick. Before components, deliberately: every component is then
written once, with keyboard activation, focus state and gamepad navigation
designed in rather than retrofitted. This is slower to the first visible
button and correct.

Pointer-only stays a first-class configuration, not a phase we passed through:
per component via `canRequestFocus` and `skipTraversal`, and per tree by making
the focus subsystem ambient, so a tree with no focus scope above it holds no
focus nodes, no key routes and no subscriptions. M12 therefore depends on M11
to compile, not to run.

**M12 — Component behaviours.** Workstream E. `WidgetStates` and its
controller, then `RawButton`, `RawToggle` (checkbox, switch, radio),
`RawSlider`, `RawProgress`, and whatever list-item behaviour the first menu
needs. Behaviour, hit regions, focus and keyboard only — no style. Resolve the
builder-versus-observable call-site question at the start of this milestone,
not during it. Pulls in the `IgnorePointer` / `AbsorbPointer` piece of
workstream I, which the disabled state needs.

**M13 — Overlay and portals.** The cheap half of workstream J: an overlay layer
painted and hit-tested above the main tree, entries insertable from anywhere,
and anchoring an entry to a widget's rectangle — which is why surgery item 1
lands in M9. Unblocks tooltips, dropdowns, context menus, and the ghost that
drag-and-drop needs.

**M14 — Exit animations.** The expensive half of workstream J, and the item
`INSTRUCTIONS.md` warned about by name: the element tree retains a removed
subtree, still mounted and still ticking, until its animation completes.
Separate from M13 because it is the only item in this phase that touches
reconciliation, and it should be able to fail without taking the overlay with
it.

**M15 — Integration screens.** The original milestone 9, now worth more: a HUD,
a tab scoreboard, and a settings menu that scrolls, has real controls, is fully
navigable by gamepad, and animates its panels in and out. The only honest test
of whether any of this is usable.

### Deferred, with the reason

- **Lazy children and slivers (F).** Revisit when a target screen genuinely has
  thousands of rows. Prefer the reduced lazy list over the full sliver protocol
  when that day comes.
- **Text editing (M).** Not in this phase. One thing is still owed now:
  extending `TextService` with byte-to-caret, range boxes and cluster
  boundaries is cheap today and a breaking change later, so it should land
  whenever the interface is next touched, ahead of any implementation.
- **The layout catalogue and text polish (H, I, K).** Not a milestone. The rule
  from `INSTRUCTIONS.md` applies unchanged — *add widgets when a target screen
  needs them, not speculatively* — so `Center`, a `Container`-equivalent,
  `Wrap`, a grid, aligned columns for the scoreboard, rich text spans, an
  ambient `DefaultTextStyle` and baseline flex alignment get pulled in by
  whichever milestone first needs them, and M15 will need several. The only
  pieces with a fixed home are `IgnorePointer` / `AbsorbPointer` in M12.
- **Everything in section L**, unchanged.
