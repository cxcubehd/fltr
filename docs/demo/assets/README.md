# Assets

Drop a monospace TTF here as `JetBrainsMono-Regular.ttf` and the demo will use
it. Nothing is committed: a font is a large binary with its own licence, and the
demo does not need one to run.

`RaylibTextService` falls back to raylib's built-in bitmap font when the file is
missing, so the demo builds and runs either way — the text is simply less
pleasant to look at. The fallback is deliberate rather than defensive: the point
of the `TextService` boundary is that the framework never learns which of the two
it got.
