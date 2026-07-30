#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace fltr {

/// Identity across rebuilds.
///
/// Two widgets reconcile onto the same element only if their runtime type and
/// their key both match; a keyed child that moves within a sibling list keeps
/// its element, and therefore its State and its render object.
///
/// String keys must point at storage that outlives the element -- string
/// literals are the intended case. Elements adopt their configuration by value
/// and the key travels with it, so a key pointing into per-frame scratch would
/// dangle.
struct Key {
  enum class Kind : std::uint8_t { None, Int, Str };

  Kind kind = Kind::None;
  std::int64_t value = 0;
  const char* text = nullptr;

  static constexpr Key none() noexcept { return {}; }

  /// Constrained to integral types so that `Key::of(0)` is unambiguously an
  /// integer key rather than a null string.
  template <class T>
    requires std::is_integral_v<T> && (!std::is_same_v<std::remove_cv_t<T>, bool>)
  static constexpr Key of(T v) noexcept {
    return {Kind::Int, static_cast<std::int64_t>(v), nullptr};
  }

  static Key of(const char* s) noexcept { return {Kind::Str, static_cast<std::int64_t>(hash(s)), s}; }

  constexpr bool isSet() const noexcept { return kind != Kind::None; }

  friend bool operator==(const Key& a, const Key& b) noexcept {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
      case Kind::None: return true;
      case Kind::Int: return a.value == b.value;
      case Kind::Str:
        if (a.value != b.value) return false;
        if (a.text == b.text) return true;
        return a.text && b.text && std::strcmp(a.text, b.text) == 0;
    }
    return false;
  }

  std::uint64_t hashCode() const noexcept {
    return (static_cast<std::uint64_t>(kind) << 56) ^ static_cast<std::uint64_t>(value);
  }

private:
  static std::uint64_t hash(const char* s) noexcept {
    std::uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (; s && *s; ++s) {
      h ^= static_cast<unsigned char>(*s);
      h *= 1099511628211ull;
    }
    return h;
  }
};

static_assert(std::is_trivially_destructible_v<Key>);

}  // namespace fltr
