#include "testing.hpp"

#include <cstdio>
#include <cstring>
#include <exception>

namespace fltrtest {

std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

Registrar::Registrar(const char* name, const char* file, void (*fn)()) {
  registry().push_back({name, file, fn});
}

namespace {
int g_failures = 0;
long g_checks = 0;
}  // namespace

void beginCheck() { ++g_checks; }
int failureCount() { return g_failures; }
long checkCount() { return g_checks; }

void reportFailure(const char* file, int line, const std::string& msg) {
  ++g_failures;
  const char* base = std::strrchr(file, '/');
  std::printf("    FAIL %s:%d\n      %s\n", base ? base + 1 : file, line, msg.c_str());
}

}  // namespace fltrtest

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int ran = 0;
  int failedTests = 0;

  for (const auto& t : fltrtest::registry()) {
    if (filter && std::strstr(t.name, filter) == nullptr) continue;
    const int before = fltrtest::failureCount();
    ++ran;
    try {
      t.fn();
    } catch (const std::exception& e) {
      fltrtest::reportFailure(t.file, 0, std::string("uncaught exception: ") + e.what());
    } catch (...) {
      fltrtest::reportFailure(t.file, 0, "uncaught non-standard exception");
    }
    const bool failed = fltrtest::failureCount() != before;
    if (failed) ++failedTests;
    std::printf("%s %s\n", failed ? "[FAIL]" : "[ ok ]", t.name);
  }

  std::printf("\n%d test(s), %ld check(s), %d failure(s) in %d test(s)\n", ran,
              fltrtest::checkCount(), fltrtest::failureCount(), failedTests);
  return fltrtest::failureCount() == 0 ? 0 : 1;
}
