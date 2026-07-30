#pragma once

// Feature detection for the C++26 features this framework would like to use.
// Every one has a working C++23 fallback; nothing here is required to build.
// Baseline is C++23 on Clang 17+ / GCC 13+.

#include <stdexcept>
#include <string>

// Lets a mixin name the most-derived type without CRTP. Clang 17+; GCC 14+.
#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
#  define FLTR_HAS_DEDUCING_THIS 1
#else
#  define FLTR_HAS_DEDUCING_THIS 0
#endif

// Would generate config comparison and debug tree dumps, both hand-written now.
#if defined(__cpp_reflection) && __cpp_reflection >= 202411L
#  define FLTR_HAS_REFLECTION 1
#else
#  define FLTR_HAS_REFLECTION 0
#endif

#if defined(__cpp_contracts) && __cpp_contracts >= 202502L
#  define FLTR_HAS_CONTRACTS 1
#else
#  define FLTR_HAS_CONTRACTS 0
#endif

namespace fltr {

/// Thrown by the default violation handler, so a test can assert that an
/// invariant actually trips. A shipping game installs a handler that logs and
/// aborts, or compiles with FLTR_CHECKS=0.
class ContractViolation : public std::logic_error {
public:
  explicit ContractViolation(const std::string& what) : std::logic_error(what) {}
};

namespace detail {

using ViolationHandler = void (*)(const char* expr, const char* msg, const char* file, int line);

/// Returns the handler that was previously installed.
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
// declarations themselves; the call sites do not change meaning.
#define FLTR_EXPECTS(cond, msg) FLTR_ASSERT(cond, "precondition: " msg)
#define FLTR_ENSURES(cond, msg) FLTR_ASSERT(cond, "postcondition: " msg)
