# Build Prompt: Flutter-Architecture UI Framework in Modern C++

## What you are building

A retained-mode UI framework library in modern C++, whose architecture is a deliberate port of Flutter's core design — the Widget / Element / RenderObject three-tree split — but with no Dart, no garbage collection, and no coupling to any renderer.

The consumer is a custom-written game engine. The framework will be used for in-game UI: HUDs, scoreboards, menus, inventory screens. It must be embeddable as a library, driven from an existing game loop, and it must not own the window, the input system, or the GPU.

This is a from-scratch implementation informed by Flutter's design, not a transliteration of Flutter's code and not a wrapper around any existing toolkit.

## Why Flutter's architecture specifically

Do not treat this as an arbitrary starting point. The parts being borrowed are borrowed because they solve problems that are extremely expensive to solve later:

- The immutable-config / persistent-element / render-object split, which is what makes declarative UI compatible with incremental updates.
- The constraints-down / sizes-up single-pass layout protocol, which is what makes layout linear rather than iterative.
- Relayout and repaint boundaries, which are what make update cost proportional to the dirty set rather than the tree.
- The gesture arena, which is what makes overlapping interactive regions resolvable.

If you find yourself simplifying one of these away because it seems like overkill for a HUD, you have made a mistake. These are the load-bearing pieces. Everything else in Flutter is negotiable.

## Study the reference before designing

Before writing code, read the relevant parts of the Flutter framework source (`flutter/flutter`, `packages/flutter/lib/src`). At minimum:

- `widgets/framework.dart` — the Widget/Element/State/BuildOwner machinery, especially `Element.updateChild`, `Element.updateChildren`, `Widget.canUpdate`, and the inherited-widget dependency mechanism.
- `rendering/object.dart` — `RenderObject`, `PipelineOwner`, `PaintingContext`, `markNeedsLayout`, `layout`, and how relayout boundaries are computed.
- `rendering/box.dart` — `BoxConstraints`, `RenderBox`, the box protocol, box hit testing.
- `rendering/flex.dart` and `rendering/shifted_box.dart` — flex layout, padding, alignment.
- `rendering/stack.dart` — stacked/positioned layout and `sizedByParent`.
- `gestures/` — `hit_test.dart`, `arena.dart`, `recognizer.dart`, `tap.dart`.
- `rendering/mouse_tracker.dart` — how enter and exit are synthesized from hit-test results rather than from gesture recognition.
- `scheduler/ticker.dart` and `animation/` — the ticker lifecycle, the animated value and status model, the driver, curves, and interpolation.
- `widgets/transitions.dart` — how an animation is attached directly to a render object so that ticking causes repaint without rebuilding.
- `widgets/implicit_animations.dart` — how configuration changes are turned into re-targeted interpolations.

Understand the algorithms and invariants. Then design C++ that expresses them idiomatically. Do not port line by line, do not carry over Dart idioms that have better C++ equivalents, and do not carry over Flutter's debug/diagnostics infrastructure.

## Language and memory model

Target C++23 as the compilable baseline. Use C++26 features where they genuinely simplify the design, but gate anything with immature toolchain support behind feature detection and provide a working fallback. The build must succeed today on a current Clang or GCC on Linux.

Guidance on where newer features earn their place, without prescribing usage:
- Deducing `this` is a natural fit for the mixin-style composition Flutter uses in the render tree.
- Reflection, when available, is a natural fit for generating config comparison, debug tree dumps, and a future data-driven bindings layer. Design so these can be generated later; hand-write them now.
- Contracts are a natural fit for the layout protocol's invariants, which Flutter enforces through pervasive assertions.

The memory model is the point of the exercise, so get it right:

- Widget configurations are immutable and short-lived. They should be arena-allocated per build and released wholesale.
- Elements and RenderObjects are long-lived and strictly tree-owned, with unambiguous single ownership downward and non-owning references upward. No reference counting in the tree, no cycles.
- Prefer widget configurations that are trivially destructible, so arena reset stays a pointer operation. Where a widget genuinely needs to own heap memory, make that an explicit, visible exception rather than the default.

There must be no tracing garbage collection, no shared ownership in the hot path, and no per-frame heap churn in the steady state.

## Authoring ergonomics

The framework will be written against directly by hand in game code. Declaration sites must read like a UI description, not like C++ boilerplate.

The agreed direction: widgets are a real polymorphic class hierarchy, and each widget declares its fields once in a nested parameter aggregate that callers fill using designated initializers. Nesting children must not require explicit allocation at the call site.

Beyond that, ergonomics is your design problem. Optimize for:
- Reading a UI tree top to bottom without visual noise.
- Adding a field to a widget without breaking existing call sites.
- Compiler errors that name the actual mistake.

Where designated initializers fight the language — templates, lambdas, single-argument widgets — use whatever reads better instead. Consistency of style matters less than legibility at the call site.

## Reactivity

The framework must not rebuild the tree every frame. A frame in which nothing changed must perform no builds, no layout, and no repaint, and must be able to resubmit previously recorded draw commands.

Game state changes at arbitrary times and is pushed in from the game loop. Design an observable-value mechanism plus a subtree-scoped subscriber widget such that pushing an unchanged value is a no-op, and pushing a changed value dirties only the subtrees that actually read it. Inherited-widget-style propagation should exist for ambient data such as theme and layout scale, and must be O(1) to look up rather than an ancestor walk.

The API from the game side should be a plain per-frame push of current state, with the framework deciding what that implies for work.

## Rendering

The framework must not depend on any graphics API. Painting produces an ordered, backend-agnostic command representation which a consumer translates into draw calls. The intended backends include raylib, Vulkan, and D3D, chosen by the consumer, so the representation must not assume immediate-mode or retained-mode on the consumer's side.

Design so that recorded output can be cached and resubmitted when nothing changed, and so that a future compositing-layer implementation is additive rather than a rewrite. Repaint boundaries should exist as a concept from the start even if they initially do nothing.

Images and other GPU resources are consumer-owned; the framework refers to them by opaque handle and never loads or allocates them.

## Text and fonts: deferred, not excluded

Text rendering is explicitly out of scope for this phase. Do not implement shaping, font matching, line breaking, glyph rasterization, or atlas management.

It is, however, the single largest thing that will be added later, and retrofitting it into a design that assumed text was simple is a rewrite. So:

- Define now the interface boundary through which the framework requests text measurement and text drawing. Treat text as an externally provided service that the framework consumes, exactly as it consumes a rendering backend.
- The interface must be able to express what a real implementation will need: a measured paragraph with line breaks, an overall size, and positioned runs, given available width and style. It should not assume single-line, fixed-width, ASCII, or one-glyph-per-character.
- Provide a trivial placeholder implementation adequate for laying out and hit-testing a text widget during development.
- Ensure the layout protocol treats text as a normal child that resolves a size from constraints, so a real implementation drops in without touching layout.

Whenever a design decision would be different if text were hard, assume text is hard.

## Animation

Animation is a first-class requirement, not a later addition. The target uses are interaction feedback (button press, hover, focus), state transitions (a panel appearing, a value counting up), and ambient motion. Assume animation is running constantly and on many elements at once.

The single most important structural decision: there must be two distinct ways to consume an animated value, and both must exist.

- A general mechanism where an animation drives a subtree rebuild. Correct for anything whose structure changes as it animates.
- A mechanism where an animation is handed directly to a render object, which observes it and invalidates only its own painting. No element rebuild, no reconciliation, no per-frame widget allocation.

The second is the one that makes animation affordable at game frame rates, and it constrains the pipeline: paint invalidation must be a separate, cheaper path from layout invalidation, and render objects must be able to hold observer subscriptions with correct lifetime. Design both in from the start. Retrofitting the render-attached path means revisiting the invalidation model.

The layering to reproduce, in dependency order:

- A ticker abstraction that yields elapsed time per frame. Time comes from the consumer's game loop, which has variable frame duration and may run at high frame rates; do not assume a fixed timestep or a vsync-driven clock. Tickers are owned by the state object that created them, must be explicitly disposed on unmount, and must be muted when their subtree is not mounted or not visible. An animation in a hidden panel must cost nothing.
- An observable animated value carrying both a current value and a lifecycle status, with independent notification for each. Status changes are how dependent logic learns an animation settled or reversed.
- A driver over a normalized range with forward, reverse, retarget, and repeat behaviour. Retargeting must proceed from the current value rather than restarting. Interruption is the common case, not an edge case: pointer enter and exit will frequently arrive mid-animation, and the result must be continuous.
- Easing curves, including the ability to apply different curves in each direction.
- A generic interpolation abstraction with implementations for the value types the widget layer uses — scalars, colors, offsets, sizes, insets, rectangles — and a way to compose interpolation with easing.
- An implicit-animation layer, where changing a widget's configuration causes the affected properties to animate to their new values automatically. The mechanism is re-targeting interpolations from their current values on configuration update. This is the layer most UI code will actually be written against, so treat its ergonomics as seriously as the widget authoring syntax.

Provide render-attached animated variants for at least opacity, transform, alignment, and decoration, since those cover press, hover, and appearance transitions without touching layout.

Animating a property that affects layout must remain possible and must correctly invalidate layout — but the design should make it obvious at the call site which properties are cheap and which are not.

Consider whether animated values and the reactivity mechanism should share an observable abstraction. Unifying them is attractive, but do not let it erase the rebuild-versus-repaint distinction above.

## Explicitly out of scope

Do not build: accessibility or semantics trees; scrolling, viewports, or slivers; navigation, routes, or overlays; text editing or selection; a Material or Cupertino widget catalog; hot reload or an inspector; platform channels, windowing, or input device abstraction; internationalization or bidirectional text.

Defer, but design so they can be added: global keys and cross-tree reparenting; build-during-layout widgets; a compositing layer tree; intrinsic sizing; physics-based and spring animations.

Deferred with a specific warning: animating a subtree *out* as it is removed. This requires the element tree to retain a removed subtree, still ticking, until its animation completes, which complicates reconciliation meaningfully. Animating a subtree in is straightforward and should work from the start. Do not attempt the removal case in this phase, but be aware it exists so the reconciliation design does not actively preclude it.

The widget catalog should stay small — roughly the set needed to build a HUD and a tabular scoreboard: stacking and positioning, alignment, padding, fixed and constrained sizing, decoration, flex rows and columns with flexible children, spacing, opacity, a text widget, an icon or sprite widget, a pointer-interaction widget covering both discrete gestures and hover, and animated variants of the properties named in the animation section. Add widgets when a target screen needs them, not speculatively.

## Build order

Work in milestones. Each must be independently verifiable before proceeding. Do not scaffold the whole system up front.

1. Geometry, constraints, and the command representation. A backend stub that can be inspected.
2. The render tree: render object base, box protocol, and enough concrete render objects for flex, padding, alignment, stacking, and decoration. Constructed by hand in tests, with no widget layer at all.
3. The pipeline: dirty tracking, relayout boundaries, and the layout/paint flush phases, with phase separation enforced.
4. The widget and element layer on top, including state, the reconciliation algorithm with keys, and the build phase.
5. Hit testing, pointer routing, the gesture arena, and a tap recognizer. Pointer enter and exit tracking, which is a separate mechanism from gesture recognition: it requires re-resolving what is under the pointer when the pointer moves and when the tree changes beneath a stationary pointer, then diffing against the previous result.
6. Reactivity and ambient propagation.
7. Animation: tickers, the animated value abstraction, the driver, curves, and interpolation. Both consumption paths, with the render-attached path demonstrated on opacity.
8. The implicit-animation layer and the render-attached animated widgets.
9. Target screens as integration tests: a HUD and a tab scoreboard, with animated press and hover feedback.

Milestones 2 and 3 before 4 is deliberate. The render tree carries the invariants that cannot be fixed later, and it is testable without any widget or renderer.

## Verification

There is no renderer during most of this work, so correctness must be observable without pixels. Provide a headless harness that drives the pipeline and can dump the resulting trees and recorded commands in a stable, diffable textual form.

Test the properties that matter, not just that code runs:
- Layout results against hand-computed expectations, including flex distribution and cross-axis alignment.
- That dirtying a leaf relayouts a bounded set of nodes and stops at the expected boundary.
- That a rebuild producing an equivalent configuration performs no render-object mutation.
- That element state survives child reordering when keyed, and is correctly discarded when not.
- That a frame with no state change performs zero work.
- That overlapping interactive regions resolve to the expected recognizer.
- That advancing an animation which affects only painting causes no rebuilds and no relayout.
- That reversing an animation mid-flight produces a continuous value with no discontinuity, and that repeated interruption does not accumulate error.
- That an animation in an unmounted or hidden subtree consumes no time and holds no live subscription.

Animation correctness is easiest to verify by driving the harness with an explicit, controlled time source rather than real elapsed time. Build that in.

Performance targets are not incidental: this runs inside a game frame. A steady-state frame should cost effectively nothing, and a typical HUD update should touch a handful of nodes.

## How to work

Explain design decisions before implementing them, particularly where you diverge from Flutter's approach, and say what the divergence costs. If a requirement here conflicts with something you discover while reading the reference, raise it rather than silently choosing.

Prefer fewer, well-understood abstractions over configurable ones. Do not add extension points for hypothetical future needs; the extension points that matter — renderer, text, and consumer-supplied widgets — are already named above.

Ask before making a decision that would be expensive to reverse. Everything in the invariants section is expensive to reverse.
