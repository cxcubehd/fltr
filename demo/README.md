# The demo

An interactive application built on fltr and raylib, shaped like the main menu
of a late-nineties shooter: a title screen, a settings screen, and a server
browser with a couple of hundred rows.

It exists for two reasons, and they are equally important.

**Evidence.** Layout, reactivity, animation, hit-testing and resizing are
claims. This runs them on a screen, and the F3 overlay reports what each frame
actually cost. The number that matters most is zero: with the cursor still,
every counter reads 0 and the scene revision is frozen. Nothing in this
framework polls, and the overlay is how you can tell.

**Reference.** Everything under `ui/` and `app/` is written the way a screen in
this framework is meant to be written. If you are looking for how to author UI
here, read `ui/button.cpp` first, then `app/page_settings.cpp`, then
`app/page_servers.cpp`.

## Building

The demo is off by default, and deliberately so: the library and `fltr_tests`
configure and build with no network access and no raylib anywhere.

```sh
cmake -S . -B build -DFLTR_BUILD_DEMO=ON
cmake --build build -j
./build/demo/fltr_demo
```

raylib is found first and fetched second. If `find_package(raylib 6.0)` finds an
installed copy, that one is used; otherwise CMake clones the pinned release tag
and builds it statically as part of this project. Nothing is vendored into this
repository. To point at your own build instead:

```sh
cmake -S . -B build -DFLTR_BUILD_DEMO=ON -Draylib_DIR=/path/to/raylib/lib/cmake/raylib
```

`FLTR_DEMO_RAYLIB_TAG` selects the tag to fetch if it comes to that.

### Linux packages

raylib builds its own GLFW, which needs the X11 (or Wayland) development
headers:

```sh
# Debian / Ubuntu
sudo apt install build-essential cmake libgl1-mesa-dev libx11-dev \
                 libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev

# Fedora
sudo dnf install gcc-c++ cmake mesa-libGL-devel libX11-devel libXrandr-devel \
                 libXinerama-devel libXcursor-devel libXi-devel
```

### Platforms

Verified here: **Linux, GCC 13.3, x86-64**, both windowed and headless (under
`xvfb-run` with software GL), warning-clean with the library's warning set.

Not verified here: macOS and Windows/MSVC. The demo is written to build on
both — C++23, no POSIX calls, no compiler-specific attributes outside a
`printf`-format annotation that is guarded, MSVC's warning set wired up in
`CMakeLists.txt` — but "should" is not "did", and this file will say so until
someone runs it.

## Running it

| Key | |
| --- | --- |
| `Esc` | back to the title screen |
| `F3` | the debug overlay |
| mouse wheel | scrolls the server list |

| Flag | |
| --- | --- |
| `--page title\|settings\|servers` | start on a page |
| `--size 1024x640` | start at a size — useful for checking that the layout is a layout |
| `--overlay` | start with F3 already on |
| `--frames N --screenshot out.png` | render N frames, save a screenshot, exit |
| `--font /path/to.ttf` | use a specific font |

### What the overlay is saying

The first block is the framework's own accounting: elements built, dirty
elements, layouts, paints, boundaries repainted and relaid out, hit tests,
active tickers, and the scene revision. The second block is the demo's:
its own hit tests (the drag router's), paragraphs measured and live, and the
high-water mark of the per-frame string arena.

Things worth doing while it is on:

- **Hold still.** Everything reads 0 and the scene revision says `frozen`.
- **Hover one button.** One boundary repainted, no layouts, and — after the
  frame that rebuilt the button — no builds at all while the colour animates.
- **Drag the brightness slider on the Video tab.** The whole screen dims. The
  slider is in a panel that has never heard of the scrim doing the dimming;
  what connects them is one observable value and one `Watch` at the root.
- **Scroll the server list.** Zero builds, zero layouts. The viewport observes
  the scroll position for *paint*, and the scrollbar thumb is that position
  mapped to an alignment and handed to a render object.
- **Click a column header.** Two hundred rows re-sort, and none of them fades:
  they are keyed, so the sort permutes elements rather than rebuilding them.
- **Click Refresh.** Now some rows fade in — the ones that are genuinely new.
- **Leave a page.** Active tickers drop to zero. A hidden page is muted, not
  merely idle.

## How UI is authored here

### A widget is a configuration, not a thing

Widgets are immutable descriptions allocated in a per-build arena and thrown
away wholesale. `State` is what persists. So a widget's fields are read during
the build that created them and never after, which is the rule behind most of
what follows.

```cpp
return Button::make({
    .label = "Find Servers",
    .onPressed = Callback<void()>([state] { state->goTo(Page::Servers); }),
    .kind = ButtonKind::Menu,
    .width = theme.menuButtonWidth(),
});
```

Every widget takes one designated-initializer aggregate. There are no
constructor overloads to remember and no builder chains.

### Controlled components

No control in this demo owns the value it edits. A `Slider` is given a value and
a callback; a `Toggle` is given a bool and a callback; a `Dropdown` is given an
index and a callback. What they own is what is genuinely theirs — hover, press,
open-or-closed — and that lives in their `State`.

The values themselves are `Observable<T>` on plain application state
(`app/app_state.hpp`), which knows nothing about widgets. That is the shape the
framework asks for: the application pushes state, and the framework works out
what that implies.

### Callbacks have a budget

`Callback<Sig>` stores four pointers of trivially copyable capture inline. It
never allocates, and it cannot hold another `Callback`. Two consequences show up
constantly:

- A callback stored in a render object outlives the build that created it, so it
  may capture only things that outlive a build — a `State`, the application,
  never the widget. Read the current configuration through `widget()` when the
  callback fires; do not capture it.
- When a builder needs more than four pointers of context, describe the thing
  instead of capturing it. `app/page_settings.cpp` has constant `SliderSpec`s for
  exactly this reason, and the rows read better for it.

### Reactivity: two paths, and choosing between them

```cpp
// Path 1: rebuild the subtree. Use it when the *shape* changes.
Watch<float>::make({.value = &settings.brightness, .builder = &buildScrim});

// Path 2: hand the value to a render object. Use it when only the painting
// changes -- this rebuilds nothing at all.
Opacity::make({.opacity = 1.0f, .animation = &fade_, .child = row});
```

The server browser uses both: one `Watch` over the browser's revision covers the
tabs, the header, the list and the status line, and everything below it —
hovering, selecting, scrolling, fading — takes the second path.

Selection is worth calling out. It is its own `Observable`, so clicking a row
notifies all two hundred rows and rebuilds the two whose answer changed.

### Keys are identity

A keyed child that moves within its parent's list keeps its element, its `State`
and its render object. That is what makes a re-sort cheap and what makes a row's
fade-in mean something. Keys match only within one parent's child list, which is
why the list is one list and not chunks.

```cpp
.children = WidgetList::generate(servers.size(), [&](std::size_t i) {
    return ServerRow::make({.key = Key::of(servers[i]->id), ...});
})
```

### Animation, and the one that is not free

`AnimatedOpacity`, `AnimatedTransform` and `AnimatedDecoration` hand an animated
value to a render object: a frame of animation is one repaint, no layout, no
build. `AnimatedAlign` resolves during layout, so it lays out its subtree every
frame — the `Toggle` uses it deliberately, inside a fixed-size track where the
layout stops immediately.

A `State` that owns an `AnimationDriver` itself must read `TickerMode::of` in its
build. That single line is both the mute and the dependency that rebuilds the
State when a page is hidden; without it a hidden page keeps asking for frames.
`ServerRowState::build` shows it.

### Text and strings

`Text` does not copy what it is given, so a `string_view` handed to it must
survive the build. Three kinds of storage cover everything here:

- **Static** — literals and constant tables (`kTabs`, `kCrosshairs`).
- **Model-owned** — a server's name and map are `std::string`s the model owns.
- **Per-frame** — numbers formatted into `FrameStrings`, a bump arena reset at
  the top of each frame, which is the one point where nothing can still be
  holding a view into it. (The render object keeps its own copy, so the view
  only ever has to survive the build that mentions it.)

### The theme is ambient

`Theme` is an `Ambient<T>` derived from the surface size. Nothing in `ui/` or
`app/` contains a window dimension: everything is a multiple of `theme.unit()`,
and resizing re-derives one value and lets the layout protocol do the rest.
Changing it rebuilds exactly the elements whose last build read it.

## What the demo had to build itself

The framework does not have everything, on purpose. Four things here are the
demo's own, and each one names what it costs:

- **`ui/scroll.hpp`** — a scrolling viewport, written down to its render object:
  `sizedByParent`, a repaint boundary, child laid out unbounded, painted and hit
  tested at a negative offset. No slivers, no lazy building, no physics.
- **`ui/drag.hpp`** — a pointer router for drags. The gesture layer has tap and
  hover; a slider needs drag. Being outside the arena costs it contest
  resolution and touch slop, and one extra hit test per press.
- **`ui/menu.hpp`** — an overlay layer. Nothing here lets a child paint outside
  its parent, so an open menu is not a child of its control: an `Anchor`
  publishes the control's box once it is laid out, and a `MenuHost` puts the menu
  in a `Stack` above the page at that rectangle, over an opaque barrier that
  turns an outside click into a dismiss.
- **`platform/`** — raylib appears in exactly two places, a display-list backend
  and a text service, plus `main.cpp` and the input pump. `fltr_demo_ui` does not
  link raylib at all, which is what makes that boundary checkable rather than
  aspirational.

One thing was added to the library rather than worked around:
`WidgetList::generate`, for a child list whose length comes from data. The
reasoning is in `DESIGN.md`.

## The tests

```sh
./build/demo/fltr_demo_tests
```

Running the demo is not the evidence; these are. They drive the real pages —
`DemoApp` over a real `AppState` — through the same harness the library's own
tests use, with an explicit per-frame delta and no window at all.
