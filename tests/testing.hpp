#pragma once

#include <string>
#include <type_traits>
#include <vector>

#include "fltr/debug/format.hpp"

namespace fltrtest {

struct TestCase {
  const char* name;
  const char* file;
  void (*fn)();
};

std::vector<TestCase>& registry();

struct Registrar {
  Registrar(const char* name, const char* file, void (*fn)());
};

void beginCheck();
void reportFailure(const char* file, int line, const std::string& msg);
int failureCount();
long checkCount();

/// Best-effort rendering for failure messages. Anything fltr::dbg can format
/// prints structurally; everything else prints as <?>.
template <class T>
std::string describe(const T& v) {
  if constexpr (std::is_same_v<T, bool>) {
    return v ? "true" : "false";
  } else if constexpr (std::is_enum_v<T>) {
    return std::to_string(static_cast<long long>(v));
  } else if constexpr (std::is_integral_v<T>) {
    return std::to_string(v);
  } else if constexpr (std::is_floating_point_v<T>) {
    return fltr::dbg::str(static_cast<float>(v));
  } else if constexpr (std::is_constructible_v<std::string, T>) {
    return std::string(v);
  } else if constexpr (requires { fltr::dbg::str(v); }) {
    return fltr::dbg::str(v);
  } else if constexpr (std::is_pointer_v<T>) {
    return v ? "<ptr>" : "null";
  } else {
    return "<?>";
  }
}

}  // namespace fltrtest

#define FLTR_TEST_CAT2(a, b) a##b
#define FLTR_TEST_CAT(a, b) FLTR_TEST_CAT2(a, b)

#define TEST(name)                                                                    \
  static void name();                                                                 \
  static ::fltrtest::Registrar FLTR_TEST_CAT(reg_, name)(#name, __FILE__, &name);     \
  static void name()

#define CHECK(expr)                                                                   \
  do {                                                                                \
    ::fltrtest::beginCheck();                                                         \
    if (!(expr)) ::fltrtest::reportFailure(__FILE__, __LINE__, "CHECK(" #expr ")");    \
  } while (0)

#define CHECK_EQ(a, b)                                                                \
  do {                                                                                \
    ::fltrtest::beginCheck();                                                         \
    const auto& fltr_a_ = (a);                                                        \
    const auto& fltr_b_ = (b);                                                        \
    if (!(fltr_a_ == fltr_b_)) {                                                      \
      ::fltrtest::reportFailure(__FILE__, __LINE__,                                    \
                                std::string("CHECK_EQ(" #a ", " #b ")\n      lhs = ") + \
                                    ::fltrtest::describe(fltr_a_) + "\n      rhs = " +  \
                                    ::fltrtest::describe(fltr_b_));                     \
    }                                                                                 \
  } while (0)

#define CHECK_NE(a, b)                                                                \
  do {                                                                                \
    ::fltrtest::beginCheck();                                                         \
    if ((a) == (b)) ::fltrtest::reportFailure(__FILE__, __LINE__, "CHECK_NE(" #a ", " #b ")"); \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                         \
  do {                                                                                \
    ::fltrtest::beginCheck();                                                         \
    const double fltr_a_ = static_cast<double>(a);                                     \
    const double fltr_b_ = static_cast<double>(b);                                     \
    if (!((fltr_a_ - fltr_b_) <= (eps) && (fltr_b_ - fltr_a_) <= (eps))) {             \
      ::fltrtest::reportFailure(__FILE__, __LINE__,                                    \
                                std::string("CHECK_NEAR(" #a ", " #b ")\n      lhs = ") + \
                                    ::fltrtest::describe(fltr_a_) + "\n      rhs = " +  \
                                    ::fltrtest::describe(fltr_b_));                     \
    }                                                                                 \
  } while (0)

#define CHECK_THROWS(expr)                                                            \
  do {                                                                                \
    ::fltrtest::beginCheck();                                                         \
    bool fltr_threw_ = false;                                                         \
    try {                                                                             \
      (void)(expr);                                                                   \
    } catch (...) {                                                                   \
      fltr_threw_ = true;                                                             \
    }                                                                                 \
    if (!fltr_threw_)                                                                 \
      ::fltrtest::reportFailure(__FILE__, __LINE__, "CHECK_THROWS(" #expr ") did not throw"); \
  } while (0)
