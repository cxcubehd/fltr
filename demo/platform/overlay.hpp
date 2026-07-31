#pragma once

#include <cstdint>

#include "app/app_state.hpp"
#include "fltr/widgets/binding.hpp"
#include "platform/raylib_renderer.hpp"
#include "platform/raylib_text.hpp"
#include "raylib.h"

namespace fltrdemo {

/// What the frame actually cost, drawn straight to raylib after the scene has
/// been submitted.
///
/// Deliberately *not* part of the widget tree. An overlay that reported the
/// frame's cost by being part of the frame would dirty the tree every frame,
/// and the number it exists to show -- zero -- would never appear.
///
/// Every counter here is one the framework already keeps: `BuildOwner::
/// buildCount()`, `PipelineOwner::stats()`, `PointerBinding::hitTestCount()`
/// and `TickerRegistry::activeTickerCount()`. The demo adds only two of its
/// own, and labels them as its own: the router's hit tests, and the paragraphs
/// the text service measured.
class DebugOverlay {
public:
  explicit DebugOverlay(Font font) noexcept : font_(font) {}

  void setVisible(bool visible) noexcept { visible_ = visible; }
  void toggle() noexcept { visible_ = !visible_; }
  bool visible() const noexcept { return visible_; }

  void draw(fltr::WidgetBinding& binding, AppState& app, const fltr::Scene& scene,
            const RaylibRenderer& renderer, RaylibTextService& text, float frameMs);

private:
  Font font_;
  bool visible_ = false;
  std::uint64_t lastRevision_ = 0;
  int lastHitTests_ = 0;
  int lastRouterHitTests_ = 0;
  float smoothedMs_ = 0.0f;
};

}  // namespace fltrdemo
