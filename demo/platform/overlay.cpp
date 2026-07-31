#include "platform/overlay.hpp"

#include <cstdio>

namespace fltrdemo {
namespace {

constexpr float kLine = 17.0f;
constexpr float kSize = 15.0f;
constexpr Color kIdle{150, 165, 145, 255};
constexpr Color kBusy{216, 160, 42, 255};
constexpr Color kLabel{120, 132, 116, 255};

}  // namespace

void DebugOverlay::draw(fltr::WidgetBinding& binding, AppState& app, const fltr::Scene& scene,
                        const RaylibRenderer& renderer, RaylibTextService& text, float frameMs) {
  const fltr::PipelineOwner::FrameStats& stats = binding.pipeline().stats();
  const int builds = binding.buildOwner().buildCount();
  const int hitTests = binding.pointers().hitTestCount() - lastHitTests_;
  const int routerTests = app.router().hitTests() - lastRouterHitTests_;
  const bool sceneMoved = scene.revision != lastRevision_;
  const std::size_t measured = text.measureCount();

  lastHitTests_ = binding.pointers().hitTestCount();
  lastRouterHitTests_ = app.router().hitTests();
  lastRevision_ = scene.revision;
  text.resetMeasureCount();
  smoothedMs_ = smoothedMs_ * 0.9f + frameMs * 0.1f;

  if (!visible_) {
    DrawTextEx(font_, "F3", Vector2{8.0f, 8.0f}, kSize, 0.0f, Color{90, 100, 88, 255});
    return;
  }

  // Formatted into rows the overlay owns. raylib's TextFormat rotates a handful
  // of static buffers, so sixteen of them in one array all end up pointing at
  // the last four values -- which is exactly the kind of wrong number a debug
  // overlay must never show.
  struct Row {
    const char* label;
    char value[48];
    bool busy;
  };
  Row rows[16] = {};
  std::size_t count = 0;
  const auto add = [&rows, &count](const char* label, bool busy, const char* fmt, auto... args) {
    Row& row = rows[count++];
    row.label = label;
    row.busy = busy;
    std::snprintf(row.value, sizeof(row.value), fmt, args...);
  };

  add("elements built", builds > 0, "%d", builds);
  add("dirty elements", binding.buildOwner().dirtyElementCount() > 0, "%zu",
      binding.buildOwner().dirtyElementCount());
  add("layouts", stats.layouts > 0, "%d", stats.layouts);
  add("paints", stats.paints > 0, "%d", stats.paints);
  add("boundaries repainted", stats.boundariesRepainted > 0, "%d", stats.boundariesRepainted);
  add("boundaries relaid out", stats.boundariesRelaidOut > 0, "%d", stats.boundariesRelaidOut);
  add("hit tests", hitTests > 0, "%d", hitTests);
  add("active tickers", binding.tickers().hasActiveTickers(), "%zu",
      binding.tickers().activeTickerCount());
  add("scene revision", sceneMoved, "%llu %s", static_cast<unsigned long long>(scene.revision),
      sceneMoved ? "moved" : "frozen");
  add("draw commands", false, "%zu", renderer.commandCount());
  add("frame", false, "%.2f ms   %d fps", static_cast<double>(smoothedMs_), GetFPS());
  add("- the demo's own -", false, "%s", "");
  add("router hit tests", routerTests > 0, "%d", routerTests);
  add("paragraphs measured", measured > 0, "%zu", measured);
  add("paragraphs live", false, "%zu", text.liveParagraphs());
  add("string arena", false, "%zu bytes", app.strings().highWater());

  const float height = kLine * static_cast<float>(count) + 16.0f;
  DrawRectangleRec(Rectangle{8.0f, 8.0f, 268.0f, height}, Color{8, 10, 8, 225});
  DrawRectangleLinesEx(Rectangle{8.0f, 8.0f, 268.0f, height}, 1.0f, Color{68, 82, 60, 255});

  float y = 16.0f;
  for (std::size_t i = 0; i < count; ++i) {
    const Row& row = rows[i];
    DrawTextEx(font_, row.label, Vector2{18.0f, y}, kSize, 0.0f, kLabel);
    DrawTextEx(font_, row.value, Vector2{178.0f, y}, kSize, 0.0f, row.busy ? kBusy : kIdle);
    y += kLine;
  }
}

}  // namespace fltrdemo
