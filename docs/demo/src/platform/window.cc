#include "platform/window.hh"

#include "raylib.h"

namespace demo {

using fltr::MouseCursor;

namespace {

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
  open_ = true;

  settings_ = GraphicsSettings{.fullscreen = false, .vsync = settings.vsync, .maxFps = -1};
  apply(settings);
}

void Window::close() {
  if (!open_) return;
  CloseWindow();
  open_ = false;
}

bool Window::shouldClose() const { return !open_ || WindowShouldClose(); }

fltr::Size Window::surface() const {
  if (!open_) return {};
  return {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
}

void Window::apply(const GraphicsSettings& next) {
  if (!open_ || next == settings_) return;

  if (next.fullscreen != settings_.fullscreen) {
    // raylib reports the state it is actually in, which is not always the state
    // that was asked for -- a compositor may refuse. The settings screen shows
    // what happened rather than what was requested.
    ToggleFullscreen();
  }

  if (next.vsync != settings_.vsync) {
    if (next.vsync) {
      SetWindowState(FLAG_VSYNC_HINT);
    } else {
      ClearWindowState(FLAG_VSYNC_HINT);
    }
  }

  if (next.maxFps != settings_.maxFps) SetTargetFPS(next.maxFps);

  settings_ = next;
  settings_.fullscreen = IsWindowFullscreen();
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
