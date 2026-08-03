#pragma once

#include "fltr/core/geometry.hpp"
#include "fltr/core/observable.hpp"
#include "fltr/gestures/events.hpp"

namespace demo {

/// Everything the settings screen can change. Equality-comparable so the app can
/// apply it only when it actually moved -- raylib's window calls are not free.
struct GraphicsSettings {
  bool fullscreen = false;
  bool vsync = true;
  /// 0 means uncapped. The slider offers a handful of stops rather than a
  /// continuous range, which is what `RawSlider`'s `divisions` is for.
  int maxFps = 144;

  friend constexpr bool operator==(const GraphicsSettings&,
                                   const GraphicsSettings&) noexcept = default;
};

/// The window, and the only thing in the demo that owns one. fltr never does:
/// the surface size is pushed in, and the cursor is read out once a frame.
class Window {
public:
  void open(int width, int height, const char* title, const GraphicsSettings& settings);
  void close();

  /// Reads back what the platform is actually doing, once a frame. A browser
  /// resizes the page, changes the pixel ratio and leaves fullscreen without
  /// asking, and a compositor can refuse a request outright, so none of it can
  /// be inferred from what was last applied.
  void refresh();

  bool shouldClose() const;

  /// The drawing buffer, in the pixels it is drawn with. Not the window: on a
  /// dense display the two differ by the content scale, and it is the buffer the
  /// interface is laid out in so that a pixel of layout is a pixel on screen.
  fltr::Size surface() const;

  /// Physical pixels per logical pixel on the surface being drawn to, which the
  /// interface is scaled by before the player's own preference is applied. One
  /// everywhere the demo has no way to ask; in a browser it is the device pixel
  /// ratio exactly. Observable because a display can change its mind -- a window
  /// dragged to another monitor, a page zoomed -- and the tree is measured in it.
  fltr::Observable<float>& contentScale() noexcept { return contentScale_; }

  /// Applies whatever differs from what is already applied.
  void apply(const GraphicsSettings& settings);
  const GraphicsSettings& settings() const noexcept { return settings_; }

  void applyCursor(fltr::MouseCursor cursor);

private:
  GraphicsSettings settings_;
  fltr::Observable<float> contentScale_{1.0f};
  fltr::MouseCursor cursor_ = fltr::MouseCursor::Basic;
  bool open_ = false;
};

}  // namespace demo
