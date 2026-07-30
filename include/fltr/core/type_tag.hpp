#pragma once

namespace fltr {

/// Runtime identity of a type without RTTI: the address of a per-type object,
/// which is unique and comparable in one instruction.
using TypeTag = const void*;

namespace detail {
template <class T>
inline constexpr char typeTagStorage = 0;
}

template <class T>
constexpr TypeTag typeTagOf() noexcept {
  return &detail::typeTagStorage<T>;
}

}  // namespace fltr
