#include <cstdlib>
#include <cstring>
#include <iterator>

#include "app/app_state.hpp"
#include "app/root.hpp"
#include "fltr/widgets/binding.hpp"
#include "platform/input.hpp"
#include "platform/overlay.hpp"
#include "platform/raylib_renderer.hpp"
#include "platform/raylib_text.hpp"
#include "raylib.h"
#include "ui/theme.hpp"

// `using namespace fltr` is deliberately absent from this file: raylib declares
// a global `Color`, and this is the one translation unit where the two
// namespaces meet. Everything under ui/ and app/ is free of raylib and says
// `using namespace fltr` at the top of the file.

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
/// one. None is vendored: the demo finds one, or falls back to raylib's own
/// bitmap font -- which is still a real font with real per-glyph advances, so
/// the layout stays correct and only the looks suffer.
Font loadUiFont(const char* preferred, bool& owned) {
  const char* paths[1 + std::size(kFontCandidates)] = {};
  std::size_t count = 0;
  if (preferred) paths[count++] = preferred;
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
  int startPage = 0;
  bool overlayOn = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--font") == 0 && i + 1 < argc) {
      fontPath = argv[++i];
    } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
      screenshot = argv[++i];
    } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      frameLimit = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--page") == 0 && i + 1 < argc) {
      const char* name = argv[++i];
      startPage = std::strcmp(name, "settings") == 0   ? static_cast<int>(fltrdemo::Page::Settings)
                  : std::strcmp(name, "servers") == 0  ? static_cast<int>(fltrdemo::Page::Servers)
                                                       : std::atoi(name);
    } else if (std::strcmp(argv[i], "--overlay") == 0) {
      overlayOn = true;
    }
  }

  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
  InitWindow(1280, 800, "fltr - Counter-Strike menu demo");
  SetExitKey(KEY_NULL);

  bool ownsFont = false;
  const Font font = loadUiFont(fontPath, ownsFont);

  // The service outlives the binding at both ends: RenderParagraph acquires a
  // handle during layout and releases it from a destructor that runs while the
  // element tree is being torn down. Declared before, destroyed after -- which
  // the nesting makes structural rather than a comment.
  fltrdemo::RaylibTextService text;
  text.setFont(0, font);

  {
    fltr::WidgetBinding binding(
        {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())}, text);
    fltrdemo::RaylibRenderer renderer(text);
    fltrdemo::InputPump input;
    fltrdemo::DebugOverlay overlay(font);
    overlay.setVisible(overlayOn);

    fltrdemo::AppState app;
    app.surface().set(
        {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())});
    if (startPage > 0 && startPage < fltrdemo::kPageCount) {
      app.goTo(static_cast<fltrdemo::Page>(startPage));
    }
    binding.attachRoot([&app] { return fltrdemo::DemoApp::make({.state = &app}); });

    int frames = 0;
    while (!WindowShouldClose() && !app.quitRequested()) {
      // The one point in the frame where nothing can still be holding a
      // formatted string: every build that mentions one runs inside drawFrame,
      // below.
      app.beginFrame();

      input.syncSurface(binding);
      if (IsWindowResized()) {
        app.surface().set(
            {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())});
      }
      const fltrdemo::Theme theme = fltrdemo::Theme::forSurface(app.surface().value());
      input.pump(binding, app.router(), theme.rowHeight() * 3.0f);
      if (IsKeyPressed(KEY_ESCAPE)) app.goBack();
      if (IsKeyPressed(KEY_F3)) overlay.toggle();

      const double before = GetTime();
      const fltr::Scene scene = binding.drawFrame(GetFrameTime());
      const double after = GetTime();

      BeginDrawing();
      ClearBackground(Color{0, 0, 0, 255});
      renderer.submit(scene);
      // Drawn straight to raylib, after the scene and outside the widget tree:
      // an overlay that reported the frame's cost by *being* part of the frame
      // would dirty the tree every frame and never read zero.
      overlay.draw(binding, app, scene, renderer, text,
                   static_cast<float>((after - before) * 1000.0));
      EndDrawing();

      ++frames;
      if (frameLimit > 0 && frames >= frameLimit) {
        if (screenshot) TakeScreenshot(screenshot);
        break;
      }
    }
  }

  // The binding is gone, so every RenderParagraph has released its handle.
  FLTR_ENSURES(text.liveParagraphs() == 0, "a paragraph handle outlived the widget tree");

  if (ownsFont) UnloadFont(font);
  CloseWindow();
  return 0;
}
