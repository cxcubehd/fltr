#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "fltr/core/observable.hpp"
#include "ui/scroll.hpp"

namespace fltrdemo {

/// One row of the browser. `name` and `map` are owned here, which is the first
/// half of the demo's answer to dynamic text: text that belongs to the model is
/// a `std::string` the model owns, and a `Text` widget may point at it because
/// the model outlives the build. Numbers are the other half, and go through the
/// per-frame string arena.
struct ServerInfo {
  std::uint32_t id = 0;
  std::string name;
  std::string map;
  int players = 0;
  int maxPlayers = 0;
  int ping = 0;
  bool vac = false;
  bool favourite = false;
  bool lan = false;
};

enum class ServerColumn : std::uint8_t { Name, Map, Players, Ping, Vac };
enum class ServerTab : std::uint8_t { Internet, Favourites, Lan };

/// The mock browser: a couple of hundred servers, a filter, a sort, and a
/// selection.
///
/// It is deliberately ordinary game state. Nothing here knows what a widget is;
/// the UI watches `revision()` and reads `visible()`, which is the plain
/// per-frame push the framework was designed to consume.
class ServerBrowser {
public:
  ServerBrowser();

  /// Regenerates the list the way a real refresh would: most servers come back,
  /// a few have gone, a few are new. That is what makes keyed reconciliation
  /// observable -- the survivors keep their `State`, and only genuinely new
  /// rows are new.
  void refresh();

  void setTab(ServerTab tab);
  ServerTab tab() const noexcept { return tab_; }

  /// Sorting the column that is already sorted reverses it.
  void sortBy(ServerColumn column);
  ServerColumn sortColumn() const noexcept { return column_; }
  bool ascending() const noexcept { return ascending_; }

  void select(std::uint32_t id);
  std::uint32_t selected() const noexcept { return selected_; }
  const ServerInfo* selectedServer() const noexcept;

  /// The filtered, sorted view. Pointers into storage that only `refresh()`
  /// replaces.
  std::span<const ServerInfo* const> visible() const noexcept { return view_; }
  int visiblePlayers() const noexcept { return players_; }

  /// Bumped whenever the view changed. One `Watch` over it rebuilds the list
  /// and nothing else on the page.
  fltr::ValueListenable<int>& revision() noexcept { return revision_; }

  ScrollController& scroll() noexcept { return scroll_; }

private:
  void rebuildView();

  std::vector<ServerInfo> all_;
  std::vector<const ServerInfo*> view_;
  fltr::Observable<int> revision_{0};
  ScrollController scroll_;
  ServerTab tab_ = ServerTab::Internet;
  ServerColumn column_ = ServerColumn::Ping;
  bool ascending_ = true;
  std::uint32_t selected_ = 0;
  std::uint32_t nextId_ = 1;
  std::uint32_t seed_ = 0x5eed1337u;
  int players_ = 0;
};

}  // namespace fltrdemo
