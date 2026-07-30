#include "fltr/core/geometry.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "fltr/core/config.hpp"

namespace fltr {

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

detail::ViolationHandler g_handler = &defaultViolationHandler;
}  // namespace

namespace detail {

ViolationHandler setViolationHandler(ViolationHandler h) {
  ViolationHandler prev = g_handler;
  g_handler = h ? h : &defaultViolationHandler;
  return prev;
}

void reportViolation(const char* expr, const char* msg, const char* file, int line) {
  g_handler(expr, msg, file, line);
  // A handler that returns is a programming error; there is no sane recovery
  // from a violated layout invariant.
  std::abort();
}

}  // namespace detail

Color lerp(Color a, Color b, float t) noexcept {
  auto mix = [t](std::uint8_t x, std::uint8_t y) {
    const float v = lerpF(static_cast<float>(x), static_cast<float>(y), t);
    return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5f));
  };
  return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

Transform2D Transform2D::rotation(float radians) noexcept {
  const float s = std::sin(radians);
  const float c = std::cos(radians);
  return {c, s, -s, c, 0.0f, 0.0f};
}

Transform2D Transform2D::aroundOrigin(const Transform2D& m, Offset origin) noexcept {
  return translation(-origin).then(m).then(translation(origin));
}

Rect Transform2D::applyToRect(Rect r) const noexcept {
  if (isTranslationOnly()) return r.shift({tx, ty});
  const Offset p0 = apply({r.left, r.top});
  const Offset p1 = apply({r.right, r.top});
  const Offset p2 = apply({r.right, r.bottom});
  const Offset p3 = apply({r.left, r.bottom});
  return {std::min(std::min(p0.dx, p1.dx), std::min(p2.dx, p3.dx)),
          std::min(std::min(p0.dy, p1.dy), std::min(p2.dy, p3.dy)),
          std::max(std::max(p0.dx, p1.dx), std::max(p2.dx, p3.dx)),
          std::max(std::max(p0.dy, p1.dy), std::max(p2.dy, p3.dy))};
}

bool Transform2D::invert(Transform2D& out) const noexcept {
  const float det = determinant();
  if (det == 0.0f || !std::isfinite(det)) return false;
  const float inv = 1.0f / det;
  out.a = d * inv;
  out.b = -b * inv;
  out.c = -c * inv;
  out.d = a * inv;
  out.tx = (c * ty - d * tx) * inv;
  out.ty = (b * tx - a * ty) * inv;
  return true;
}

}  // namespace fltr
