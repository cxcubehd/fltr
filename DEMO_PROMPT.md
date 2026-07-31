# Build Prompt: the fltr interactive demo

## What you are building

An interactive desktop application, in this repository, that drives the fltr UI
framework with raylib and proves it works. Its shape is a Counter-Strike 1.6
main menu: a title screen, a settings screen, and a mock server browser.

It has two jobs, and they are equally important:

1. **Evidence.** Someone who runs it must be able to see that the framework
   lays out, reacts, animates, hit tests, and resizes correctly — and be able to
   see, on screen, that a frame in which nothing changed costs nothing.
2. **Reference.** Someone who reads it must come away knowing how to author UI
   in fltr and how to write their own components — the widget, the render
   object where one is needed, the animation, the reactivity — without reading
   the framework's internals.

Work on the branch given to you for this task. Do not open a pull request
unless asked.

## Read before designing

- `INSTRUCTIONS.md` — the framework's own build prompt. Its rules about arenas,
  ownership, and per-frame cost apply to everything you write against it.
- `DESIGN.md` — the milestone-by-milestone design record, M1 through M8. The
  divergences from Flutter are recorded there and they are the ones that will
  bite you.
- The public headers, which are the API you are consuming:
  `widgets/binding.hpp` (the frame loop), `widgets/basic.hpp` (the catalogue),
  `widgets/framework.hpp` (Stateless/Stateful/State/Key/Args authoring),
  `widgets/reactive.hpp` (`Observable`, `Watch`, `Ambient`),
  `widgets/animated.hpp` (`AnimatedOpacity`, `AnimatedDecoration`,
  `AnimatedTransform`, `AnimatedAlign`, `TickerMode`),
  `paint/display_list.hpp` (what a backend receives),
  `paint/text.hpp` (the `TextService` boundary),
  `gestures/events.hpp` (what a consumer pushes in),
  `render/box.hpp`, `render/boxes.hpp` (what a custom render object subclasses).
- `tests/test_widgets.cpp` and `tests/test_reactivity.cpp` for the authoring
  idiom actually in use, and `tests/widget_harness.hpp` for how a tree is driven
  without a window.

This demo is effectively milestone 9 of `INSTRUCTIONS.md` ("target screens as
integration tests"), scaled up into a real application. Treat it as such.

## Ground rules

**raylib is the consumer, not the framework.** The framework must stay ignorant
of it. raylib headers may appear in exactly two places: the display-list
backend and the text service, plus the small `main`/input pump. No raylib type,
constant, or call may appear in any widget, component, page, or piece of demo
state. If you find yourself wanting one there, the boundary is wrong.

**No raygui, and no other UI library.** Everything is drawn with raylib
primitives — filled rectangles, rounded rectangles, lines/borders, text, and
scissor. Every widget, every animation, every piece of interaction comes from
fltr.

**Do not vendor raylib into the repository.** Fetch it, or find it.

**Clean and minimal beats complete.** A small set of components done properly,
each one readable top to bottom, is the deliverable. Resist building a widget
catalogue: build the components the three screens need, and no more.

**The framework's authoring rules are not suggestions.** Widget configurations
live in the build arena and must stay trivially destructible. `Callback` stores
only trivially copyable captures of at most four pointers' worth — capture a
pointer to state, not the state. `Text::Args::text` is a `std::string_view`
that is *not* copied at the call site; it must point at storage that outlives
the build (see "Strings", below).

## The application

### Title screen

Centred menu: the title/logo block, then a vertical run of buttons — Find
Servers, Create Server, Options, Quit — with consistent spacing and padding.
A version string pinned to a corner. The whole thing stays centred and
correctly proportioned at any window size.

### Settings

A tab panel — Video, Audio, Controls — hosting:

- sliders (master volume, mouse sensitivity, brightness) with a live numeric
  readout;
- toggle buttons (invert mouse, raw input, vsync);
- a dropdown (resolution, or crosshair colour) that opens over the content
  below it and dismisses on outside click;
- Apply and Back buttons.

At least one setting must visibly affect something outside the panel that
changed it — that is the reactivity demonstration, and it must be a subtree
rebuild scoped by `Watch`, not a rebuild of the page.

### Server browser

A tab panel (Internet / Favourites / LAN) over a scrolling list of a couple of
hundred mock servers, with columns: name, map, players, ping, VAC. Row hover
highlight, row selection, clickable column headers that sort. A status line
with server and player counts, and Refresh / Connect buttons.

Sorting is the reconciliation demonstration: rows carry a `Key` derived from
the server's identity, so a re-sort permutes elements and their `State` rather
than rebuilding rows in place. Make that observable — for instance, per-row
state that would visibly reset if the key were dropped.

### Everywhere

- Page changes are animated (fade and/or slide in). Animating a subtree *out*
  as it is removed is explicitly not supported by the framework — read the
  warning in `INSTRUCTIONS.md` and design the transition so you never need it.
- Pages that are not visible are wrapped in `TickerMode{.enabled = false}` so
  their animations cost nothing while hidden.
- A debug overlay, toggled with F3, showing per-frame: elements built, dirty
  element count, layouts, paints, repainted boundaries, hit tests, active
  tickers, scene revision, and frame time. `BuildOwner::buildCount()`,
  `PipelineOwner::stats()`, `PointerBinding::hitTestCount()` and
  `TickerRegistry::activeTickerCount()` already expose all of it.

  This overlay is the single most convincing thing in the demo. Idle, with the
  cursor still, it must read zero builds, zero layouts, zero paints and a
  frozen scene revision. Hovering one button must show one repaint and no
  layout. Do not let the overlay itself dirty the tree every frame — if
  necessary, draw it outside the widget tree, straight to raylib, after
  submitting the scene.

## Structure

Suggested, not mandatory, but keep the boundary it encodes:

```
demo/
  CMakeLists.txt
  README.md            build instructions per platform, and a short
                       "how UI is authored in fltr" tour of this code
  platform/            the only place raylib appears
    raylib_renderer.*  DisplayList -> raylib draw calls
    raylib_text.*      TextService implementation
    input.*            raylib input -> PointerEvent, resize, dt
  ui/                  the component kit: theme, button, toggle, slider,
                       dropdown, panel, tabs, scroll view
  app/                 state, pages, the root widget, main()
```

### Build

- A `FLTR_BUILD_DEMO` option, **off by default**. The library and
  `fltr_tests` must keep configuring and building with no network access and no
  raylib present. Do not make the demo a precondition for anything that exists
  today.
- raylib via `find_package(raylib)` first, `FetchContent` as the fallback,
  pinned to a released tag. Note the Linux system packages raylib needs in the
  demo README.
- Same warning set as the library, and warning-clean. C++23.
- Must compile on Linux, macOS and Windows. That means MSVC as well as
  Clang/GCC: designated initialisers in declaration order, no VLAs, no POSIX
  headers, no GCC statement expressions, `/utf-8` where needed. You will
  probably only be able to build one of the three — write portably, and say in
  the README which platforms were actually verified.

## The two pieces that carry the framework

### The renderer

Walk `Scene::root` and translate. The list is a flat command array with a
balanced push/pop state stack; you maintain the stack yourself:

- **Clip.** `PushClipRect` intersects with the current clip; raylib's scissor
  is in window space and takes no account of any transform you have applied, so
  a clip nested under a transform must be transformed on the CPU before it
  becomes a scissor rectangle. Rounded clips have no scissor equivalent —
  decide what you do about them, and say so.
- **Opacity.** `PushOpacity` multiplies into the alpha of every colour drawn
  below it. There is no layer, so nested opacity multiplies.
- **Transform.** `Transform2D` is affine. Either compose it on the CPU and
  transform each primitive's corners, or use rlgl's matrix stack. Recommendation:
  keep the demo's transforms to translation and scale, note that in the
  renderer, and reject/degrade anything else — that keeps the scissor maths
  exact and is honest about the limit rather than silently wrong.
- **`DrawList`** is the repaint-boundary seam. A boundary's list is re-recorded
  only when that subtree repaints, and it carries a `revision()`; the demo can
  simply walk it every frame, but say in a comment what a caching backend would
  key on, since the representation exists for that.
- **`DrawRRect`** carries fill, border colour and border width; raylib's
  rounded-rectangle roundness is a fraction of the shorter side, so convert.

Nothing in the renderer may allocate per frame in the steady state.

### The text service

`MonospaceTextService` is a development placeholder. Replace it — the demo
must render real text with a real font, and it is the piece that proves the
`TextService` boundary was designed correctly.

Implement `TextService` over a raylib font: greedy word wrapping into the
`maxWidth` the layout hands you, correct `ParagraphMetrics` (size, line boxes,
baselines, positioned runs), `TextAlign` and `maxLines`/ellipsis handling, and
`byteOffsetAt`. `DrawParagraph` then draws each positioned run with `DrawTextEx`.

Three things that will otherwise cost you an afternoon each:

- The service **must own a copy of the text** behind each handle. The widget's
  `string_view` may be gone by the time you draw.
- `RenderParagraph` acquires a handle during layout and releases it on relayout
  and destruction. The service therefore has to outlive the `WidgetBinding` —
  construct it before, destroy it after — and a leak is directly observable by
  counting live handles, as `MonospaceTextService::liveParagraphs()` does.
  Keep that counter, and assert it returns to zero on shutdown.
- Measurement happens during layout, once per changed paragraph, so caching
  shaped results by (text, style, width) is worth it if the server list makes
  it worth it. Measure before you cache.

## The input pump

raylib's mouse position and buttons become `PointerEvent`s:
`Hover` when moving unpressed, `Down`, `Move` while pressed, `Up`, and `Cancel`
when the window loses focus. Push them with `WidgetBinding::dispatchPointer` as
they happen — they are not tied to the frame. `IsWindowResized` →
`setSurface`. `GetFrameTime` → `drawFrame(dt)`.

Keyboard is out of scope beyond Esc-to-go-back, which the app layer handles
directly. There is no focus system; do not build one.

## Components

Each component is an ordinary fltr widget with an `Args` aggregate filled by
designated initialisers, and a `Callback` for whatever it changes. Prefer
controlled components — the parent owns the value and receives the change —
and use `State` only for what genuinely belongs to the component, such as
hover, press, and open/closed.

What each one is here to demonstrate:

- **Button** — pointer callbacks, hover and press feedback through the
  render-attached animation path (`AnimatedDecoration`, `AnimatedOpacity`,
  `AnimatedTransform`), and a disabled state. Interrupting the hover animation
  by moving the cursor in and out quickly must stay continuous; that is what
  the driver's re-targeting is for.
- **ToggleButton** — a controlled boolean, animated between its two looks.
- **Slider** — a continuous value from a drag (see gaps, below).
- **Dropdown** — a menu drawn over the content below it: `Stack` plus
  `Positioned`, an open/close animation, and dismissal on outside click.
  Hit-test ordering is the interesting part — the menu must win over what is
  beneath it, and `HitTestBehavior` is how you say so.
- **Panel** — decoration, border, padding, optional title bar. The boring one;
  keep it boring.
- **TabPanel** — a tab strip with an animated indicator over keyed page bodies.
  Decide, deliberately, whether a hidden tab's state survives, and make the
  code say which.
- **ScrollPanel** — a custom render object (see gaps).
- **Layout helpers** — whatever centring, spacing and padding shorthands the
  pages actually want. Do not build a layout DSL.

## Gaps you will hit, and what to do about them

The framework does not have everything this demo needs. That is expected: it
was built to a scope, and the demo is what discovers where the scope ends.

**The rule:** compose from what exists first. When something is genuinely
missing, prefer adding it to the demo. Add it to fltr proper only when it is a
capability any consumer would need, and then do it in the library's style —
header, source, tests, and a note in `DESIGN.md` — rather than half-way. State
which side of that line you put each one on, and why, before you implement it.

1. **Scrolling.** Explicitly out of scope in the framework, and the demo needs
   it. What a scroll viewport requires is a render object that lays its child
   out with an unbounded main axis, takes its own size from its constraints,
   paints the child at a negative offset inside a clip, and hit tests with that
   same offset applied. `RenderBox::hitTest` already rejects anything outside
   `paintBounds()`, so once the viewport box is the right size, hit-test
   clipping comes for free — but check it rather than assuming it.

   Build this in the demo. It is the best showcase in the whole exercise of
   writing a custom component all the way down to its render object. Do not
   attempt slivers, lazy building, or scroll physics; a few hundred rows built
   eagerly is fine, and worth measuring in the overlay.

2. **The mouse wheel.** `PointerEvent` has no wheel phase, and adding one
   changes the routing model. Keep the framework as it is: read the wheel in
   the input pump and feed it into a demo-owned scroll position that the
   viewport observes.

3. **Drag.** The gesture layer has tap and hover, and nothing else. A slider
   needs drag, and so does a scrollbar thumb if you build one. This is the one
   gap where extending the library is probably right: a drag recognizer
   alongside `TapGestureRecognizer`, competing in the same arena with the same
   touch slop, and drag callbacks on the `Pointer` widget. If you do it, it
   needs tests — contesting with a tap, continuing to receive events after the
   pointer leaves the region, and cancelling. If you decide to keep it
   demo-local instead, say what that costs.

4. **Strings.** Server names, ping numbers, slider readouts — all dynamic, and
   `Text` does not copy at the call site. `RenderParagraph` *does* copy into a
   `std::string` when it adopts the text, so a formatted string only has to
   outlive the build that mentions it. Give the demo one deliberate answer:
   state-owned `std::string` for stable text, and a per-frame string arena,
   reset at a point where nothing can still reference it, for formatted values.
   Do not sprinkle `std::to_string` into build methods.

5. **Text metrics before the real service exists.** Do not build any screen
   against `MonospaceTextService` and then swap. Get the raylib service working
   first; layout that looked right under a fixed-advance font will not be.

## Resizing

Resizing must be correct, not merely survivable. Nothing in `ui/` or the pages
may contain a hardcoded window dimension. Everything is expressed in
constraints, flex, alignment and padding, so the layout protocol does the work.

Push the new surface size to the binding on resize and let the pipeline
invalidate what it must. Verify at 640×480, at a very wide window, and at a
very tall one; the server list should gain and lose rows, columns should keep
their proportions, and the title screen should stay centred.

If you want a UI scale factor, put it in an `Ambient<Theme>` derived from the
surface size — that also demonstrates that ambient propagation rebuilds only
the elements that read it.

## Verification

The demo must run, and you should look at it: screenshot the three screens and
attach them.

But running is not the evidence. Add tests, in the existing test suite or
alongside the demo, that drive the real pages through the harness with a
controlled time source and assert:

- an idle frame does zero builds, zero layouts, zero paints, and leaves
  `Scene::revision` unchanged;
- hovering a button repaints one boundary and lays out nothing;
- a hidden page holds no active tickers;
- re-sorting the server list preserves per-row `State` for the keyed rows and
  discards it for rows that genuinely left;
- interrupting a hover animation mid-flight produces a continuous value;
- no paragraph handles are outstanding after teardown.

`fltr_tests` must stay green, and the build must stay warning-clean.

## Milestones

Each one independently verifiable. Do not scaffold the whole application first.

1. Build plumbing, window, input pump, and the display-list renderer, driving a
   hand-written tree of a couple of boxes. Proves translation, resizing, and
   the frame loop.
2. The raylib text service, with handle accounting.
3. Theme, Button, Panel; the title screen; page switching with transitions and
   `TickerMode`.
4. Toggle, slider, dropdown; the settings screen.
5. The scroll viewport render object; the server browser with sorting,
   selection, and keyed rows.
6. Debug overlay, the tests above, the demo README, and a `DESIGN.md` section
   recording what the demo revealed and what, if anything, it added to the
   library.

## How to work

Explain any change to fltr proper before making it, and say what it costs —
the invariants in `INSTRUCTIONS.md` are expensive to reverse and the demo is
not a reason to bend one. Ask before deciding anything in that category.
Commit per milestone with clear messages, push to the branch, and keep the
library's comment voice: say why, not what.
