#pragma once

#include <type_traits>

namespace fltr {

/// A set of bits drawn from a scoped enum, so a bitset keeps the enum's name and
/// cannot be confused with an unrelated integer.
///
/// An enum opts in by specialising `kIsFlags`, which is what makes `A | B` on the
/// enumerators themselves produce a set rather than an int.
template <class E>
  requires std::is_enum_v<E>
class Flags {
public:
  using Bits = std::underlying_type_t<E>;

  constexpr Flags() = default;
  constexpr Flags(E value) noexcept : bits_(static_cast<Bits>(value)) {}
  static constexpr Flags fromBits(Bits bits) noexcept {
    Flags f;
    f.bits_ = bits;
    return f;
  }

  constexpr bool has(E value) const noexcept {
    return (bits_ & static_cast<Bits>(value)) == static_cast<Bits>(value);
  }
  constexpr bool hasAny(Flags other) const noexcept { return (bits_ & other.bits_) != 0; }
  constexpr bool any() const noexcept { return bits_ != 0; }
  constexpr Bits bits() const noexcept { return bits_; }

  constexpr Flags operator|(Flags o) const noexcept {
    return fromBits(static_cast<Bits>(bits_ | o.bits_));
  }
  constexpr Flags& operator|=(Flags o) noexcept { return *this = *this | o; }

  /// Reports whether the set actually moved, which is what the caller turns into
  /// a notification -- the same shape `Animatable::set` has.
  constexpr bool set(E value, bool present) noexcept {
    const Bits mask = static_cast<Bits>(value);
    const Bits next = static_cast<Bits>(present ? bits_ | mask : bits_ & ~mask);
    if (next == bits_) return false;
    bits_ = next;
    return true;
  }

  friend constexpr bool operator==(Flags, Flags) noexcept = default;

private:
  Bits bits_ = 0;
};

template <class E>
inline constexpr bool kIsFlags = false;

template <class E>
  requires kIsFlags<E>
constexpr Flags<E> operator|(E a, E b) noexcept {
  return Flags<E>(a) | Flags<E>(b);
}

}  // namespace fltr
