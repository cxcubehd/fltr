#include "platform/window.hh"

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

namespace demo {

using fltr::MouseCursor;

namespace {

#ifdef __EMSCRIPTEN__

/// The canvas fills the page, so its drawing buffer is the CSS box times the
/// device pixel ratio. Anything smaller is a buffer the browser has to stretch,
/// which is the blur this whole scaling path exists to avoid.
fltr::Size canvasPixels() {
  double width = 0.0;
  double height = 0.0;
  emscripten_get_element_css_size("#canvas", &width, &height);
  const double ratio = emscripten_get_device_pixel_ratio();
  return {static_cast<float>(width * ratio), static_cast<float>(height * ratio)};
}

/// Registered after `InitWindow`, which replaces raylib's own handler: that one
/// sizes the buffer in CSS pixels and knows nothing about the ratio.
bool trackCanvasSize(int, const EmscriptenUiEvent*, void*) {
  const fltr::Size pixels = canvasPixels();
  SetWindowSize(static_cast<int>(pixels.width), static_cast<int>(pixels.height));
  return true;
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

#ifdef __EMSCRIPTEN__
  const fltr::Size pixels = canvasPixels();
  width = static_cast<int>(pixels.width);
  height = static_cast<int>(pixels.height);
#endif

  InitWindow(width, height, title);

#ifdef __EMSCRIPTEN__
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, trackCanvasSize);
#endif

  // raylib closes the window on Escape unless told otherwise, which would take
  // the key the UI navigates with and turn it into "quit".
  SetExitKey(KEY_NULL);
  open_ = true;

  settings_ = GraphicsSettings{.fullscreen = false, .vsync = settings.vsync, .maxFps = -1};
  apply(settings);
}

void Window::close() {
  if (!open_) return;
  CloseWindow();
  open_ = false;
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

float Window::contentScale() const {
#ifdef __EMSCRIPTEN__
  return static_cast<float>(emscripten_get_device_pixel_ratio());
#else
  return 1.0f;
#endif
}

void Window::apply(const GraphicsSettings& next) {
  if (!open_ || next == settings_) return;

  if (next.fullscreen != settings_.fullscreen) {
    // raylib reports the state it is actually in, which is not always the state
    // that was asked for -- a compositor may refuse. The settings screen shows
    // what happened rather than what was requested.
    ToggleFullscreen();
  }

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
  settings_.fullscreen = IsWindowFullscreen();

#ifdef __EMSCRIPTEN__
  // A browser paces the frame itself, through the callback it drives the loop
  // with, and raylib's own wait is a suspend this build has no asyncify for.
  // Neither knob exists here, so neither is reported as having taken effect.
  settings_.vsync = true;
  settings_.maxFps = 0;
#endif
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
