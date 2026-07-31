#include "app/servers.hpp"

#include <algorithm>
#include <array>

namespace fltrdemo {
namespace {

constexpr std::size_t kServerCount = 220;

constexpr std::array<const char*, 16> kMaps{
    "de_dust2",  "de_inferno", "cs_office",  "de_nuke",   "de_train",  "cs_italy",
    "de_aztec",  "de_prodigy", "cs_assault", "de_cbble",  "de_chateau", "as_oilrig",
    "de_piranesi", "cs_militia", "de_dust",  "de_tides",
};

constexpr std::array<const char*, 12> kPrefixes{
    "[EU]", "[NA]", "[RU]", "[BR]", "[AU]", "[UK]", "[DE]", "[PL]", "[SE]", "[FR]", "[TR]", "[JP]",
};

constexpr std::array<const char*, 14> kNames{
    "Zombie Plague",  "Deathmatch 24/7", "Pub Server",    "GunGame",     "Awp Only",
    "Classic 5v5",    "Surf Combat",     "Retake Arena",  "Bhop Paradise", "Executive Gaming",
    "Old School",     "Fun Servers",     "Headshot Only", "Scoutzknivez",
};

/// A tiny LCG rather than <random>: the demo has to produce the same list every
/// time it starts, so screenshots and tests describe one world.
std::uint32_t next(std::uint32_t& seed) noexcept {
  seed = seed * 1664525u + 1013904223u;
  return seed >> 8;
}

}  // namespace

ServerBrowser::ServerBrowser() {
  all_.reserve(kServerCount);
  for (std::size_t i = 0; i < kServerCount; ++i) {
    ServerInfo server;
    server.id = nextId_++;
    server.name = std::string(kPrefixes[next(seed_) % kPrefixes.size()]) + " " +
                  kNames[next(seed_) % kNames.size()] + " #" +
                  std::to_string(1 + next(seed_) % 99);
    server.map = kMaps[next(seed_) % kMaps.size()];
    server.maxPlayers = static_cast<int>(12 + (next(seed_) % 5) * 4);
    server.players = static_cast<int>(next(seed_) % static_cast<std::uint32_t>(server.maxPlayers + 1));
    server.ping = static_cast<int>(8 + next(seed_) % 240);
    server.vac = next(seed_) % 5 != 0;
    server.favourite = next(seed_) % 9 == 0;
    server.lan = next(seed_) % 23 == 0;
    all_.push_back(std::move(server));
  }
  rebuildView();
}

void ServerBrowser::refresh() {
  // A tenth of the list goes away and is replaced. Everything else keeps its
  // id, which is what the rows key on.
  std::vector<ServerInfo> kept;
  kept.reserve(all_.size());
  for (ServerInfo& server : all_) {
    if (next(seed_) % 10 == 0) continue;
    server.players =
        std::clamp(server.players + static_cast<int>(next(seed_) % 7) - 3, 0, server.maxPlayers);
    server.ping = std::clamp(server.ping + static_cast<int>(next(seed_) % 41) - 20, 5, 400);
    kept.push_back(std::move(server));
  }
  const std::size_t missing = kServerCount - kept.size();
  for (std::size_t i = 0; i < missing; ++i) {
    ServerInfo server;
    server.id = nextId_++;
    server.name = std::string(kPrefixes[next(seed_) % kPrefixes.size()]) + " " +
                  kNames[next(seed_) % kNames.size()] + " #" +
                  std::to_string(1 + next(seed_) % 99);
    server.map = kMaps[next(seed_) % kMaps.size()];
    server.maxPlayers = static_cast<int>(12 + (next(seed_) % 5) * 4);
    server.players = static_cast<int>(next(seed_) % static_cast<std::uint32_t>(server.maxPlayers + 1));
    server.ping = static_cast<int>(8 + next(seed_) % 240);
    server.vac = next(seed_) % 5 != 0;
    server.favourite = next(seed_) % 9 == 0;
    server.lan = next(seed_) % 23 == 0;
    kept.push_back(std::move(server));
  }
  all_ = std::move(kept);
  rebuildView();
}

void ServerBrowser::setTab(ServerTab tab) {
  if (tab_ == tab) return;
  tab_ = tab;
  scroll_.jumpTo(0.0f);
  rebuildView();
}

void ServerBrowser::sortBy(ServerColumn column) {
  if (column_ == column) {
    ascending_ = !ascending_;
  } else {
    column_ = column;
    ascending_ = true;
  }
  rebuildView();
}

void ServerBrowser::select(std::uint32_t id) {
  if (selected_ == id) return;
  selected_ = id;
  revision_.set(revision_.value() + 1);
}

const ServerInfo* ServerBrowser::selectedServer() const noexcept {
  for (const ServerInfo* server : view_) {
    if (server->id == selected_) return server;
  }
  return nullptr;
}

void ServerBrowser::rebuildView() {
  view_.clear();
  for (const ServerInfo& server : all_) {
    const bool matches = tab_ == ServerTab::Internet  ? !server.lan
                         : tab_ == ServerTab::Favourites ? server.favourite
                                                         : server.lan;
    if (matches) view_.push_back(&server);
  }

  const ServerColumn column = column_;
  const bool ascending = ascending_;
  std::sort(view_.begin(), view_.end(), [column, ascending](const ServerInfo* a,
                                                            const ServerInfo* b) {
    int order = 0;
    switch (column) {
      case ServerColumn::Name: order = a->name.compare(b->name); break;
      case ServerColumn::Map: order = a->map.compare(b->map); break;
      case ServerColumn::Players: order = a->players - b->players; break;
      case ServerColumn::Ping: order = a->ping - b->ping; break;
      case ServerColumn::Vac: order = static_cast<int>(a->vac) - static_cast<int>(b->vac); break;
    }
    // Ties break on id, so the order is total and a re-sort is a permutation
    // rather than a shuffle -- which is what makes keyed reconciliation
    // testable.
    if (order == 0) return a->id < b->id;
    return ascending ? order < 0 : order > 0;
  });

  players_ = 0;
  for (const ServerInfo* server : view_) players_ += server->players;
  revision_.set(revision_.value() + 1);
}

}  // namespace fltrdemo
