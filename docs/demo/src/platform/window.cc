#include "platform/window.hh"

#include <cmath>

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>
#include <emscripten/html5.h>
#endif

namespace demo {

using fltr::MouseCursor;

namespace {

#ifdef __EMSCRIPTEN__

/// Whether the document is showing the canvas full-screen. A browser grants and
/// revokes that on its own -- Escape leaves without asking anyone -- so it is
/// taken from the event rather than inferred from what was last requested.
bool canvasFullscreen = false;

/// Escape is how a browser leaves fullscreen, and how this interface goes back a
/// screen. Only one of them can have the key, and while the page is the whole
/// screen it should be the interface: the way out is holding Escape, which the
/// browser says so itself, or the entry that asked for fullscreen in the first
/// place. Chromium is the only engine that offers the key at all; elsewhere the
/// call is missing and Escape keeps meaning what the browser wants.
EM_JS(void, holdEscape, (bool hold), {
  if (!navigator.keyboard?.lock) return;
  if (hold) {
    navigator.keyboard.lock(['Escape']).catch(() => {});
  } else {
    navigator.keyboard.unlock();
  }
});

EM_BOOL trackFullscreen(int, const EmscriptenFullscreenChangeEvent* event, void*) {
  canvasFullscreen = event->isFullscreen;
  holdEscape(canvasFullscreen);
  return EM_FALSE;
}

void adoptCanvas() {
  // raylib sizes the drawing buffer to the page in CSS pixels, which on a dense
  // display is a buffer the browser has to stretch -- the blur this whole
  // scaling path exists to avoid. Registering a handler adds a listener rather
  // than replacing one, so raylib's is removed rather than merely outnumbered.
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, nullptr);
  emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE,
                                           trackFullscreen);
}

fltr::Size drawingBuffer() {
  int width = 0;
  int height = 0;
  emscripten_get_canvas_element_size("#canvas", &width, &height);
  return {static_cast<float>(width), static_cast<float>(height)};
}

/// The canvas fills the page, so the buffer that keeps it crisp is its CSS box
/// times the device pixel ratio.
fltr::Size pageSize() {
  double width = 0.0;
  double height = 0.0;
  emscripten_get_element_css_size("#canvas", &width, &height);
  const double ratio = emscripten_get_device_pixel_ratio();
  return {static_cast<float>(std::round(width * ratio)),
          static_cast<float>(std::round(height * ratio))};
}

void refreshSurface() {
  const fltr::Size wanted = pageSize();
  if (wanted.isEmpty() || wanted == drawingBuffer()) return;
  // Through raylib rather than around it: `SetWindowSize` resizes the buffer,
  // the viewport and the projection together, and it is also what tells GLFW the
  // pixels a mouse position is measured in -- a browser reports one in the
  // page's, and GLFW scales it by the ratio between the two. Compared against
  // the buffer rather than against the last request, because GLFW answers the
  // first resize made inside fullscreen with the screen's CSS pixels; asking
  // again on the next frame is what settles it.
  SetWindowSize(static_cast<int>(wanted.width), static_cast<int>(wanted.height));
}

bool fullscreenNow() { return canvasFullscreen; }

float displayScale() { return static_cast<float>(emscripten_get_device_pixel_ratio()); }

/// A browser grants fullscreen to a gesture rather than to a frame, so a request
/// made from the loop is deferred to the event that follows the click that asked
/// for it -- and answered by the document afterwards rather than here.
void setFullscreen(bool enter) {
  if (enter) {
    emscripten_request_fullscreen("#canvas", EM_TRUE);
  } else {
    emscripten_exit_fullscreen();
  }
}

#else

void adoptCanvas() {}

/// raylib's own window callbacks keep a desktop window's buffer and viewport in
/// step with the window, so there is nothing here to reconcile.
void refreshSurface() {}

bool fullscreenNow() { return IsWindowFullscreen(); }

float displayScale() { return 1.0f; }

void setFullscreen(bool enter) {
  if (enter != IsWindowFullscreen()) ToggleFullscreen();
}

#endif

int raylibCursorFor(MouseCursor cursor) noexcept {
  switch (cursor) {
    case MouseCursor::Click:
    case MouseCursor::Grab:
    case MouseCursor::Grabbing: return MOUSE_CURSOR_POINTING_HAND;
    case MouseCursor::Text: return MOUSE_CURSOR_IBEAM;
    case MouseCursor::Forbidden: return MOUSE_CURSOR_NOT_ALLOWED;
    case MouseCursor::Move: return MOUSE_CURSOR_RESIZE_ALL;
    case MouseCursor::ResizeLeftRight: return MOUSE_CURSOR_RESIZE_EW;
    case MouseCursor::ResizeUpDown: return MOUSE_CURSOR_RESIZE_NS;
    default: return MOUSE_CURSOR_DEFAULT;
  }
}

}  // namespace

void Window::open(int width, int height, const char* title, const GraphicsSettings& settings) {
  unsigned int flags = FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT;
  if (settings.vsync) flags |= FLAG_VSYNC_HINT;
  SetConfigFlags(flags);
  SetTraceLogLevel(LOG_WARNING);

  InitWindow(width, height, title);
  adoptCanvas();

  // raylib closes the window on Escape unless told otherwise, which would take
  // the key the UI navigates with and turn it into "quit".
  SetExitKey(KEY_NULL);
  open_ = true;

  // A frame cap of -1 is not one anything can ask for, which is what makes the
  // first `apply` unconditional -- and with it the first `refresh`.
  settings_ = GraphicsSettings{.fullscreen = false, .vsync = settings.vsync, .maxFps = -1};
  apply(settings);
}

void Window::close() {
  if (!open_) return;
  CloseWindow();
  open_ = false;
}

void Window::refresh() {
  if (!open_) return;
  refreshSurface();
  settings_.fullscreen = fullscreenNow();
  contentScale_.set(displayScale());
}

bool Window::shouldClose() const {
#ifdef __EMSCRIPTEN__
  // A page is closed, not a window. raylib's own answer is always "no" anyway,
  // and it arrives by way of a suspend only an asyncify build could make.
  return !open_;
#else
  return !open_ || WindowShouldClose();
#endif
}

fltr::Size Window::surface() const {
  if (!open_) return {};
  return {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
}

void Window::apply(const GraphicsSettings& next) {
  if (!open_ || next == settings_) return;

  if (next.fullscreen != settings_.fullscreen) setFullscreen(next.fullscreen);

#ifndef __EMSCRIPTEN__
  if (next.vsync != settings_.vsync) {
    if (next.vsync) {
      SetWindowState(FLAG_VSYNC_HINT);
    } else {
      ClearWindowState(FLAG_VSYNC_HINT);
    }
  }

  if (next.maxFps != settings_.maxFps) SetTargetFPS(next.maxFps);
#endif

  settings_ = next;

#ifdef __EMSCRIPTEN__
  // A browser paces the frame itself, through the callback it drives the loop
  // with, and raylib's own wait is a suspend this build has no asyncify for.
  // Neither knob exists here, so neither is reported as having taken effect.
  settings_.vsync = true;
  settings_.maxFps = 0;
#endif

  // A fullscreen request is one a compositor may refuse and a browser answers
  // later, so the settings screen shows what happened rather than what was asked
  // for. `refresh` is what keeps showing it.
  refresh();
}

void Window::applyCursor(MouseCursor cursor) {
  if (!open_ || cursor == cursor_) return;
  cursor_ = cursor;
  if (cursor == MouseCursor::None) {
    HideCursor();
    return;
  }
  ShowCursor();
  SetMouseCursor(raylibCursorFor(cursor));
}

}  // namespace demo
