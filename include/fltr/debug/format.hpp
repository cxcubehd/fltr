#pragma once

#include <cstdio>
#include <string>

#include "fltr/core/constraints.hpp"
#include "fltr/core/geometry.hpp"

/// Stable, diffable textual forms. Used by the headless harness for tree and
/// command dumps, and by the test framework for failure messages. Formatting is
/// deliberately snprintf-based rather than iostream- or std::format-based so
/// output does not vary with locale or standard-library version.
namespace fltr::dbg {

inline std::string str(float v) {
  if (v == kInf) return "inf";
  if (v == -kInf) return "-inf";
  if (v != v) return "nan";
  if (v == 0.0f) return "0";  // normalises -0
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.6g", static_cast<double>(v));
  return buf;
}

inline std::string str(int v) { return std::to_string(v); }
inline std::string str(bool v) { return v ? "true" : "false"; }

inline std::string str(Offset o) { return "(" + str(o.dx) + "," + str(o.dy) + ")"; }
inline std::string str(Size s) { return str(s.width) + "x" + str(s.height); }
inline std::string str(Rect r) {
  return "[" + str(r.left) + "," + str(r.top) + " " + str(r.width()) + "x" + str(r.height()) + "]";
}
inline std::string str(EdgeInsets e) {
  if (e.left == e.top && e.top == e.right && e.right == e.bottom) return "all(" + str(e.left) + ")";
  return "(" + str(e.left) + "," + str(e.top) + "," + str(e.right) + "," + str(e.bottom) + ")";
}
inline std::string str(Alignment a) { return "<" + str(a.x) + "," + str(a.y) + ">"; }

inline std::string str(Color c) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", c.a, c.r, c.g, c.b);
  return buf;
}

inline std::string str(BoxConstraints c) {
  return "w[" + str(c.minWidth) + ".." + str(c.maxWidth) + "] h[" + str(c.minHeight) + ".." +
         str(c.maxHeight) + "]";
}

inline std::string str(const BorderRadius& r) {
  if (r.isZero()) return "0";
  if (r.topLeft == r.topRight && r.topRight == r.bottomRight && r.bottomRight == r.bottomLeft) {
    return str(r.topLeft.x);
  }
  return str(r.topLeft.x) + "/" + str(r.topRight.x) + "/" + str(r.bottomRight.x) + "/" +
         str(r.bottomLeft.x);
}

inline std::string str(const Transform2D& m) {
  if (m.isIdentity()) return "identity";
  if (m.isTranslationOnly()) return "translate" + str(Offset{m.tx, m.ty});
  return "[" + str(m.a) + " " + str(m.c) + " " + str(m.tx) + "; " + str(m.b) + " " + str(m.d) + " " +
         str(m.ty) + "]";
}

}  // namespace fltr::dbg
