#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

#include "fltr/scroll/position.hpp"
#include "fltr/widgets/binding.hpp"
#include "fltr/widgets/overlay.hpp"
#include "app/clock.hh"
#include "game/session.hh"
#include "platform/backend.hh"
#include "platform/input.hh"
#include "platform/text.hh"
#include "platform/window.hh"
#include "ui/router.hh"

namespace demo {

struct Options {
  int width = 1280;
  int height = 720;
  /// Runs a scripted sequence and exits, instead of reading a keyboard.
  bool smoke = false;
  int smokeFrames = 240;
  /// Prints the display list of the last frame, which is what makes the smoke
  /// run inspectable rather than merely non-crashing.
  bool dump = false;
  /// Writes a PNG of the last frame, so the run is inspectable as pixels too.
  const char* shot = nullptr;
};

/// Everything the demo owns. The framework owns none of it: not the window, not
/// the clock, not the font, not the game.
class App {
public:
  explicit App(const Options& options);
  ~App();

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  int run();

  // ---- What the loop reaches for ----------------------------------------

  Window& window() noexcept { return window_; }
  Input& input() noexcept { return input_; }
  Clock& clock() noexcept { return clock_; }
  Backend& backend() noexcept { return *backend_; }
  RaylibTextService& text() noexcept { return *text_; }
  fltr::WidgetBinding& binding() noexcept { return *binding_; }
  const Options& options() const noexcept { return options_; }

  // ---- What the screens reach for ---------------------------------------

  Session& session() noexcept { return session_; }
  fltr::Observable<Screen>& screen() noexcept { return screen_; }
  fltr::Observable<bool>& paused() noexcept { return paused_; }
  fltr::Observable<GraphicsSettings>& graphics() noexcept { return graphics_; }
  fltr::ScrollController& levelScroll() noexcept { return levelScroll_; }
  fltr::OverlayEntry& pauseEntry() noexcept { return pauseEntry_; }

  /// Called from the build, on the way down, because `Overlay::of` is an
  /// ambient read and ambient reads only happen there. Inserting the entry is
  /// then a callback's job, not a build's.
  void bindOverlay(fltr::OverlayState* overlay) noexcept;

  void goTo(Screen screen);
  void startLevel(std::size_t level);
  void abandonRun();
  void setPaused(bool paused);
  void togglePause();

  /// Escape, everywhere. Gameplay pauses, a pause panel closes, any other screen
  /// returns to the one that opened it, and the main menu is the floor -- the
  /// demo is never left by pressing a navigation key.
  void back();
  void quit() noexcept { quitting_ = true; }
  bool quitting() const noexcept { return quitting_; }

  void applyGraphics(const GraphicsSettings& settings);
  std::string_view maxFpsText() const noexcept { return maxFpsText_; }
  std::string_view debugText() const noexcept { return debugText_; }

  /// Whether the ship should be listening to the keyboard: only while the
  /// gameplay screen is up and nothing is covering it.
  bool gameHasFocus() const noexcept;

  /// What the ship should do this frame. In a smoke run this is the script
  /// rather than the keyboard, which is what makes the run deterministic.
  ShipInput shipInput() const noexcept;

  /// Called once a frame by the loop, after the pipeline has run.
  void refreshDebugText();

private:
  void openWindow();
  void attachUi();
  void formatMaxFps();
  /// A run that ends banks its score and raises its own panel: the outcome is
  /// the game's to decide and the panel is the UI's to show, and this is the one
  /// line between them.
  void onOutcomeChanged();
  void runScript(int frame);
  /// Escape as the player presses it, so the smoke run tests the path a key
  /// actually takes rather than the method it ends up calling.
  void pressEscape();
  void check(bool condition, const char* what);
  /// A synthetic wheel notch over the level list, so the smoke run proves the
  /// list scrolls rather than merely showing a scrollbar.
  void scrollLevelList();
  bool verifyIdleCostsNothing();

  Options options_;
  Window window_;
  Clock clock_;
  Input input_;
  Session session_;

  std::unique_ptr<RaylibTextService> text_;
  std::unique_ptr<Backend> backend_;
  std::unique_ptr<fltr::WidgetBinding> binding_;

  fltr::Observable<Screen> screen_{Screen::MainMenu};
  fltr::Observable<bool> paused_{false};
  fltr::Observable<GraphicsSettings> graphics_{GraphicsSettings{}};
  fltr::ScrollController levelScroll_;

  /// Consumer-owned, as every overlay entry is. Inserted while the gameplay
  /// screen is up; its own content decides whether anything is visible.
  fltr::OverlayEntry pauseEntry_;
  fltr::OverlayState* overlay_ = nullptr;

  fltr::Subscription outcome_;
  bool quitting_ = false;
  ShipInput scripted_;
  float scrolled_ = 0.0f;
  bool scriptOk_ = true;
  char maxFpsText_[16] = "144";
  char debugText_[96] = "";
};

}  // namespace demo
