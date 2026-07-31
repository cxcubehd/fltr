#pragma once

#include "app/servers.hpp"
#include "fltr/core/geometry.hpp"
#include "fltr/core/observable.hpp"
#include "ui/drag.hpp"
#include "ui/strings.hpp"

namespace fltrdemo {

enum class Page : int { Title, Settings, Servers };
inline constexpr int kPageCount = 3;

/// The settings, as a game would hold them: plain observable values that the UI
/// reads and writes, and that anything else in the game could read too.
///
/// Every one of them is a controlled value. No control in the demo owns the
/// setting it edits -- a control owns only what is genuinely its own, which is
/// hover, press, and open-or-closed.
struct Settings {
  fltr::Observable<float> masterVolume{0.75f};
  fltr::Observable<float> sensitivity{2.5f};
  /// The one that has to be visible from outside the panel that changes it.
  fltr::Observable<float> brightness{0.5f};
  fltr::Observable<bool> invertMouse{false};
  fltr::Observable<bool> rawInput{true};
  fltr::Observable<bool> vsync{true};
  fltr::Observable<int> crosshair{1};
  fltr::Observable<int> tab{0};
};

/// Everything the demo would have called "the game" if there were one.
///
/// It holds no widgets and knows nothing about the framework beyond
/// `Observable`, which is the shape the framework asks for: the consumer pushes
/// state, and the framework decides what that implies for work.
class AppState {
public:
  /// Resets the per-frame string arena. Called at the top of the frame, which
  /// is the one point where nothing can still be holding a formatted view --
  /// every build that mentioned one happens inside `drawFrame`.
  void beginFrame() { strings_.reset(); }

  fltr::Observable<Page>& page() noexcept { return page_; }
  void goTo(Page page) { page_.set(page); }
  /// Esc. The app layer handles it directly; there is no focus system and the
  /// demo does not want one.
  void goBack() {
    if (page_.value() != Page::Title) page_.set(Page::Title);
  }

  bool quitRequested() const noexcept { return quit_; }
  void requestQuit() noexcept { quit_ = true; }

  /// Pushed in on resize. The theme is derived from it, so one push re-derives
  /// every dimension in the demo.
  fltr::Observable<fltr::Size>& surface() noexcept { return surface_; }

  Settings settings;
  ServerBrowser& servers() noexcept { return servers_; }
  FrameStrings& strings() noexcept { return strings_; }
  PointerRouter& router() noexcept { return router_; }

private:
  fltr::Observable<Page> page_{Page::Title};
  fltr::Observable<fltr::Size> surface_{fltr::Size{1280.0f, 800.0f}};
  ServerBrowser servers_;
  FrameStrings strings_;
  PointerRouter router_;
  bool quit_ = false;
};

}  // namespace fltrdemo
