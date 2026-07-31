#include "testing.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>

namespace {
std::size_t g_allocations = 0;
}

void* operator new(std::size_t n) {
  ++g_allocations;
  if (void* p = std::malloc(n == 0 ? 1 : n)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace fltrtest {

std::size_t allocationCount() { return g_allocations; }

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
  // Leading newline because the test's name has already been printed, without
  // one, by the runner below.
  std::printf("\n    FAIL %s:%d\n      %s\n", base ? base + 1 : file, line, msg.c_str());
}

}  // namespace fltrtest

int main(int argc, char** argv) {
  // Unbuffered, and the name goes out before the test runs: a test that takes
  // the whole process down with it is then named by the last line of output
  // rather than lost inside a buffer that never got flushed. Finding that out
  // the hard way, from a heap corruption on a platform with no debugger to
  // hand, is what this line is for.
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  const char* filter = argc > 1 ? argv[1] : nullptr;
  int ran = 0;
  int failedTests = 0;

  for (const auto& t : fltrtest::registry()) {
    if (filter && std::strstr(t.name, filter) == nullptr) continue;
    const int before = fltrtest::failureCount();
    ++ran;
    std::printf("%-72s ", t.name);
    try {
      t.fn();
    } catch (const std::exception& e) {
      fltrtest::reportFailure(t.file, 0, std::string("uncaught exception: ") + e.what());
    } catch (...) {
      fltrtest::reportFailure(t.file, 0, "uncaught non-standard exception");
    }
    const bool failed = fltrtest::failureCount() != before;
    if (failed) ++failedTests;
    std::printf("%s\n", failed ? "[FAIL]" : "[ ok ]");
  }

  std::printf("\n%d test(s), %ld check(s), %d failure(s) in %d test(s)\n", ran,
              fltrtest::checkCount(), fltrtest::failureCount(), failedTests);
  return fltrtest::failureCount() == 0 ? 0 : 1;
}
