#include "app/app.hh"

#include <cstdio>

#include "fltr/harness.hpp"
#include "raylib.h"
#include "app/loop.hh"
#include "ui/root.hh"
#include "ui/screens/pause.hh"

namespace demo {

namespace {

/// Tried in order. The bundled asset first, then two monospace faces that ship
/// with most Linux distributions, then raylib's built-in font -- so the demo
/// looks right on a machine that has none of them, without a binary in the repo.
constexpr const char* kFontPaths[] = {
    DEMO_ASSET_DIR "/JetBrainsMono-Regular.ttf",
    "/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
};

}  // namespace

App::App(const Options& options)
    : options_(options),
      clock_(options.smoke ? 1.0f / 60.0f : 0.0f),
      pauseEntry_([this](fltr::BuildContext& context) { return pauseOverlay(*this, context); }) {
  formatMaxFps();
}

App::~App() {
  // Order matters: the widget tree has to come down before the font it measured
  // with, and the window last. `binding_` owning the tree makes that a matter of
  // resetting in the right order rather than of hoping.
  binding_.reset();
  backend_.reset();
  text_.reset();
  window_.close();
}

void App::openWindow() {
  window_.open(options_.width, options_.height, "fltr // drift", graphics_.value());
  // Whatever the window actually did is what the settings screen shows.
  graphics_.set(window_.settings());
}

void App::attachUi() {
  // The font needs a GL context, so nothing here can be a constructor: the
  // window opens first, then the text service, then the binding that uses it.
  text_ = std::make_unique<RaylibTextService>(kFontPaths);
  backend_ = std::make_unique<Backend>(*text_);
  binding_ = std::make_unique<fltr::WidgetBinding>(window_.surface(), *text_);
  binding_->attachRoot([this] { return buildRoot(*this); });
}

void App::bindOverlay(fltr::OverlayState* overlay) noexcept { overlay_ = overlay; }

void App::goTo(Screen screen) {
  if (screen_.value() == screen) return;

  const bool leavingPlay = screen_.value() == Screen::Playing;
  screen_.set(screen);

  if (screen == Screen::Playing) {
    if (overlay_ != nullptr && !pauseEntry_.inserted()) overlay_->insert(pauseEntry_);
  } else if (leavingPlay) {
    setPaused(false);
    pauseEntry_.remove();
  }
}

void App::startLevel(std::size_t level) {
  session_.start(level, window_.surface());
  setPaused(false);
  goTo(Screen::Playing);
}

void App::abandonRun() {
  session_.progress().recordScore(session_.score.value());
  setPaused(false);
}

void App::setPaused(bool paused) {
  if (paused_.value() == paused) return;
  paused_.set(paused);
  session_.setPaused(paused);
}

void App::togglePause() {
  if (screen_.value() != Screen::Playing) {
    // Escape outside gameplay means "back", not "pause".
    goTo(screen_.value() == Screen::MainMenu ? Screen::MainMenu : Screen::MainMenu);
    return;
  }
  // A finished run cannot be un-paused: its panel is the result screen.
  if (session_.outcome.value() != Outcome::Flying) return;
  setPaused(!paused_.value());
}

void App::applyGraphics(const GraphicsSettings& settings) {
  window_.apply(settings);
  graphics_.set(window_.settings());
  formatMaxFps();
}

void App::formatMaxFps() {
  const int cap = graphics_.value().maxFps;
  std::snprintf(maxFpsText_, sizeof(maxFpsText_), "%d", cap);
}

bool App::gameHasFocus() const noexcept {
  return screen_.value() == Screen::Playing && !paused_.value() &&
         session_.outcome.value() == Outcome::Flying;
}

ShipInput App::shipInput() const noexcept {
  if (options_.smoke) return gameHasFocus() ? scripted_ : ShipInput{};
  return input_.ship();
}

void App::refreshDebugText() {
  const fltr::PipelineOwner::FrameStats& stats = binding_->pipeline().stats();
  std::snprintf(debugText_, sizeof(debugText_),
                "builds=%d layouts=%d paints=%d passes=%d tickers=%zu cmds=%zu",
                binding_->buildOwner().buildCount(), stats.layouts, stats.paints,
                stats.layoutPasses, binding_->tickers().activeTickerCount(),
                backend_->lastCommandCount());
}

void App::runScript(int frame) {
  // A fixed sequence through every screen, so the smoke run exercises the same
  // code a player would and does it the same way every time.
  scripted_ = ShipInput{};
  switch (frame) {
    case 10: goTo(Screen::Settings); break;
    case 30: applyGraphics({.fullscreen = false, .vsync = false, .maxFps = 60}); break;
    case 45: goTo(Screen::LevelSelect); break;
    case 70: startLevel(0); break;
    case 200: togglePause(); break;
    case 240: togglePause(); break;
    case 270: abandonRun(); goTo(Screen::MainMenu); break;
    default: break;
  }

  if (frame > 70 && frame < 200) {
    scripted_.thrust = (frame / 20) % 2 == 0;
    scripted_.left = (frame / 30) % 3 == 0;
    scripted_.fire = frame % 7 == 0;
  }
}

bool App::verifyIdleCostsNothing() {
  // Let everything settle: no input, no time, so no animation can be running.
  for (int i = 0; i < 30; ++i) runFrame(*this);
  for (int i = 0; i < 4; ++i) binding_->drawFrame(0.0f);

  const fltr::Scene before = binding_->drawFrame(0.0f);
  const std::uint64_t revision = before.revision;
  const fltr::Scene after = binding_->drawFrame(0.0f);

  if (after.revision != revision) {
    std::printf("FAIL: an idle frame re-recorded a display list (%llu -> %llu)\n",
                static_cast<unsigned long long>(revision),
                static_cast<unsigned long long>(after.revision));
    return false;
  }
  if (binding_->needsFrame()) {
    std::printf("FAIL: the binding still wants a frame after settling\n");
    return false;
  }
  return true;
}

int App::run() {
  openWindow();
  attachUi();

  int frame = 0;
  int worstPasses = 0;
  bool ok = true;

  while (!quitting_ && !window_.shouldClose()) {
    if (options_.smoke) runScript(frame);

    const fltr::Scene scene = runFrame(*this);
    worstPasses = std::max(worstPasses, binding_->pipeline().stats().layoutPasses);

    if (options_.smoke && frame + 1 >= options_.smokeFrames) {
      if (options_.dump && scene.root != nullptr) {
        std::printf("%s\n", fltr::dumpDisplayList(*scene.root).c_str());
      }
      if (options_.shot != nullptr) TakeScreenshot(options_.shot);
      break;
    }
    ++frame;
  }

  if (!options_.smoke) return 0;

  // ---- What the smoke run asserts ---------------------------------------

  if (worstPasses > 1) {
    std::printf("FAIL: layout did not converge in one pass (worst = %d)\n", worstPasses);
    ok = false;
  }

  setPaused(false);
  goTo(Screen::MainMenu);
  if (!verifyIdleCostsNothing()) ok = false;

  // Tearing the tree down releases every paragraph it acquired. Anything left
  // is a leak, and the count is the only way to see one without a profiler.
  binding_.reset();
  if (const std::size_t leaked = text_->liveParagraphs(); leaked != 0) {
    std::printf("FAIL: %zu paragraphs outlived the widget tree\n", leaked);
    ok = false;
  }

  std::printf("%s: %d frames, worst layout passes = %d\n", ok ? "PASS" : "FAIL", frame + 1,
              worstPasses);
  return ok ? 0 : 1;
}

}  // namespace demo
