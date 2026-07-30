#include <cstdlib>
#include <string>

#include "fltr/core/config.hpp"

namespace fltr::detail {
namespace {

[[noreturn]] void defaultViolationHandler(const char* expr, const char* msg, const char* file,
                                          int line) {
  std::string what = "fltr contract violation: ";
  what += msg ? msg : "";
  what += " [";
  what += expr ? expr : "";
  what += "] at ";
  what += file ? file : "?";
  what += ":";
  what += std::to_string(line);
  throw ContractViolation(what);
}

ViolationHandler gHandler = &defaultViolationHandler;

}  // namespace

ViolationHandler setViolationHandler(ViolationHandler h) {
  ViolationHandler previous = gHandler;
  gHandler = h ? h : &defaultViolationHandler;
  return previous;
}

void reportViolation(const char* expr, const char* msg, const char* file, int line) {
  gHandler(expr, msg, file, line);
  // A handler that returns is a programming error; there is no sane recovery
  // from a violated layout invariant.
  std::abort();
}

}  // namespace fltr::detail
