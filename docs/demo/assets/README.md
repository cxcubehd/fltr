# Assets

Nothing is committed here: a font is a large binary with its own licence. The
build fetches JetBrains Mono instead (`FetchContent`, pinned by hash), and that
is the face the demo uses.

Drop a monospace TTF here as `JetBrainsMono-Regular.ttf` to override it — the
path is tried first.

`RaylibTextService` falls back through a handful of system paths and finally to
raylib's built-in bitmap font if it finds nothing at all, so the demo still runs
on a machine that built offline — the text is simply less pleasant to look at.
The fallback is deliberate rather than defensive: the point of the `TextService`
boundary is that the framework never learns which of the two it got.
