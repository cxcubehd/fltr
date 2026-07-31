#include <cstdlib>
#include <cstring>
#include <iterator>

#include "fltr/widgets/binding.hpp"
#include "platform/input.hpp"
#include "platform/raylib_renderer.hpp"
#include "platform/raylib_text.hpp"
#include "raylib.h"
#include "ui/drag.hpp"
#include "ui/theme.hpp"

namespace {

/// The atlas is rasterised once, at a size large enough that the theme's scale
/// only ever shrinks it. Bilinear filtering does the rest.
constexpr int kAtlasSize = 48;

const char* const kFontCandidates[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/Library/Fonts/Arial.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
};

/// A real font, because the demo has to prove the TextService boundary carries
/// one. No font is vendored: the demo finds one, or falls back to raylib's own
/// bitmap font, which is still a real font with real per-glyph advances.
Font loadUiFont(const char* override, bool& owned) {
  const char* paths[1 + std::size(kFontCandidates)] = {};
  std::size_t count = 0;
  if (override) paths[count++] = override;
  for (const char* candidate : kFontCandidates) paths[count++] = candidate;

  for (std::size_t i = 0; i < count; ++i) {
    if (!FileExists(paths[i])) continue;
    const Font font = LoadFontEx(paths[i], kAtlasSize, nullptr, 0);
    if (font.glyphCount > 0 && font.texture.id != 0) {
      SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
      owned = true;
      TraceLog(LOG_INFO, "fltr: using font %s", paths[i]);
      return font;
    }
  }
  owned = false;
  TraceLog(LOG_WARNING, "fltr: no system font found, falling back to raylib's default");
  return GetFontDefault();
}

}  // namespace

int main(int argc, char** argv) {
  const char* fontPath = nullptr;
  const char* screenshot = nullptr;
  int frameLimit = 0;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--font") == 0 && i + 1 < argc) fontPath = argv[++i];
    else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot = argv[++i];
    else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frameLimit = std::atoi(argv[++i]);
  }

  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
  InitWindow(1280, 800, "fltr - raylib demo");
  SetExitKey(KEY_NULL);
  SetTargetFPS(0);

  bool ownsFont = false;
  const Font font = loadUiFont(fontPath, ownsFont);

  {
    // The service outlives the binding, both ends: RenderParagraph releases its
    // handle from a destructor that runs while the element tree is being torn
    // down. Constructed before, destroyed after -- which the scope makes
    // structural rather than a comment.
    fltrdemo::RaylibTextService text;
    text.setFont(0, font);

    fltr::WidgetBinding binding(
        {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())}, text);
    fltrdemo::RaylibRenderer renderer(text);
    fltrdemo::InputPump input;
    fltrdemo::PointerRouter router;

    // `using namespace fltr` is deliberately absent from this file: raylib
    // declares a global `Color`, and main is the one place where the two
    // namespaces meet. Everything under ui/ and app/ is free of raylib and says
    // `using namespace fltr` at the top.
    binding.attachRoot([] {
      return fltr::Column::make({
          .mainAxisAlignment = fltr::MainAxisAlignment::Center,
          .spacing = 16.0f,
          .children = {
              fltr::DecoratedBox::make({
                  .decoration = {.color = fltr::Color::argb(0xFF2A3326),
                                 .radius = fltr::BorderRadius::all(4.0f),
                                 .borderColor = fltr::Color::argb(0xFF6F8163),
                                 .borderWidth = 1.0f},
                  .child = fltr::SizedBox::make({.size = {320.0f, 90.0f}}),
              }),
              fltr::Text::make({.text = "fltr / raylib", .style = {.size = 32.0f}}),
          },
      });
    });

    int frames = 0;
    while (!WindowShouldClose()) {
      input.syncSurface(binding);
      input.pump(binding, router, 48.0f);

      const fltr::Scene scene = binding.drawFrame(GetFrameTime());

      BeginDrawing();
      ClearBackground(Color{15, 19, 16, 255});
      renderer.submit(scene);
      EndDrawing();

      ++frames;
      if (frameLimit > 0 && frames >= frameLimit) {
        if (screenshot) TakeScreenshot(screenshot);
        break;
      }
    }
  }

  if (ownsFont) UnloadFont(font);
  CloseWindow();
  return 0;
}
