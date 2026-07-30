#pragma once

// Feature detection for the C++26 features this framework would like to use.
// Every one of them has a working C++23 fallback; nothing here is required for
// the library to build. Baseline is C++23 on Clang 17+ / GCC 13+.

#include <stdexcept>
#include <string>

// --- Deducing `this` -------------------------------------------------------
// Used for the mixin-style composition in the render tree (a mixin can name the
// most-derived type without CRTP). Clang 17+ has it; GCC only from 14.
#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
#  define FLTR_HAS_DEDUCING_THIS 1
#else
#  define FLTR_HAS_DEDUCING_THIS 0
#endif

// --- Reflection ------------------------------------------------------------
// Would generate config comparison, debug tree dumps, and a future data-driven
// bindings layer. Everything it would generate is hand-written today; see
// FLTR_CONFIG_FIELDS in widgets/widget.hpp for the seam.
#if defined(__cpp_reflection) && __cpp_reflection >= 202411L
#  define FLTR_HAS_REFLECTION 1
#else
#  define FLTR_HAS_REFLECTION 0
#endif

// --- Contracts -------------------------------------------------------------
// The layout protocol has hard invariants (a child may only be laid out by its
// parent, size may only be read when the parent asked for it, paint may not
// mutate layout). Flutter enforces these with pervasive assertions. When
// contracts are available FLTR_ASSERT lowers to `contract_assert`; otherwise to
// a checked call that routes through a replaceable violation handler.
#if defined(__cpp_contracts) && __cpp_contracts >= 202502L
#  define FLTR_HAS_CONTRACTS 1
#else
#  define FLTR_HAS_CONTRACTS 0
#endif

namespace fltr {

/// Thrown by the default contract-violation handler. Tests install a handler
/// that throws so invariant violations are observable; a shipping game can
/// install one that logs and aborts, or compile with FLTR_CHECKS=0.
class ContractViolation : public std::logic_error {
public:
  explicit ContractViolation(const std::string& what) : std::logic_error(what) {}
};

namespace detail {
/// Replaceable. Default throws ContractViolation.
using ViolationHandler = void (*)(const char* expr, const char* msg, const char* file, int line);
ViolationHandler setViolationHandler(ViolationHandler h);
[[noreturn]] void reportViolation(const char* expr, const char* msg, const char* file, int line);

inline void check(bool ok, const char* expr, const char* msg, const char* file, int line) {
  if (!ok) reportViolation(expr, msg, file, line);
}
}  // namespace detail
}  // namespace fltr

#if defined(FLTR_CHECKS) && FLTR_CHECKS
#  if FLTR_HAS_CONTRACTS
#    define FLTR_ASSERT(cond, msg) contract_assert(cond)
#  else
#    define FLTR_ASSERT(cond, msg) ::fltr::detail::check(!!(cond), #cond, (msg), __FILE__, __LINE__)
#  endif
#else
#  define FLTR_ASSERT(cond, msg) ((void)0)
#endif

// Statement-level stand-ins for contract pre/post. When C++26 contracts are
// universally available these become `pre(...)` / `post(...)` on the function
// declarations themselves; the call sites below do not change meaning.
#define FLTR_EXPECTS(cond, msg) FLTR_ASSERT(cond, "precondition: " msg)
#define FLTR_ENSURES(cond, msg) FLTR_ASSERT(cond, "postcondition: " msg)
