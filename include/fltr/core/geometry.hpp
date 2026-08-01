#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "fltr/core/config.hpp"

namespace fltr {

inline constexpr float kInf = std::numeric_limits<float>::infinity();

/// Which of the two directions something is measured along. Here rather than
/// with the flex layout that first needed it, because scrolling, viewports and
/// scrollbars all take one and none of them wants a layout header.
enum class Axis : std::uint8_t { Horizontal, Vertical };

inline constexpr float lerpF(float a, float b, float t) noexcept { return a + (b - a) * t; }

/// The scalar overload of the interpolation every other type here provides, so
/// `lerp` is the one name an animated value needs whatever it carries.
inline constexpr float lerp(float a, float b, float t) noexcept { return lerpF(a, b, t); }

inline bool nearlyEqual(float a, float b, float eps = 1e-4f) noexcept {
  if (a == b) return true;  // handles inf == inf
  return std::fabs(a - b) <= eps;
}

// ---------------------------------------------------------------------------
// Offset
// ---------------------------------------------------------------------------

struct Offset {
  float dx = 0.0f;
  float dy = 0.0f;

  static constexpr Offset zero() noexcept { return {}; }

  constexpr Offset operator+(Offset o) const noexcept { return {dx + o.dx, dy + o.dy}; }
  constexpr Offset operator-(Offset o) const noexcept { return {dx - o.dx, dy - o.dy}; }
  constexpr Offset operator*(float s) const noexcept { return {dx * s, dy * s}; }
  constexpr Offset operator-() const noexcept { return {-dx, -dy}; }
  constexpr Offset& operator+=(Offset o) noexcept { dx += o.dx; dy += o.dy; return *this; }

  friend constexpr bool operator==(Offset, Offset) noexcept = default;

  float distance() const noexcept { return std::sqrt(dx * dx + dy * dy); }
  float distanceSquared() const noexcept { return dx * dx + dy * dy; }
};

inline constexpr Offset lerp(Offset a, Offset b, float t) noexcept {
  return {lerpF(a.dx, b.dx, t), lerpF(a.dy, b.dy, t)};
}

// ---------------------------------------------------------------------------
// Size
// ---------------------------------------------------------------------------

struct Size {
  float width = 0.0f;
  float height = 0.0f;

  static constexpr Size zero() noexcept { return {}; }
  static constexpr Size square(float d) noexcept { return {d, d}; }

  constexpr bool isEmpty() const noexcept { return width <= 0.0f || height <= 0.0f; }
  constexpr bool isFinite() const noexcept { return width != kInf && height != kInf; }
  constexpr Offset topLeft(Offset origin) const noexcept { return origin; }
  constexpr Offset center(Offset origin) const noexcept {
    return {origin.dx + width * 0.5f, origin.dy + height * 0.5f};
  }
  /// Resolves an alignment (-1..1 on each axis) to an offset within this size.
  constexpr Offset alongSize(struct Alignment a) const noexcept;

  friend constexpr bool operator==(Size, Size) noexcept = default;
};

inline constexpr Size lerp(Size a, Size b, float t) noexcept {
  return {lerpF(a.width, b.width, t), lerpF(a.height, b.height, t)};
}

// ---------------------------------------------------------------------------
// Rect
// ---------------------------------------------------------------------------

struct Rect {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;

  static constexpr Rect fromLTWH(float l, float t, float w, float h) noexcept {
    return {l, t, l + w, t + h};
  }
  static constexpr Rect fromOriginSize(Offset o, Size s) noexcept {
    return {o.dx, o.dy, o.dx + s.width, o.dy + s.height};
  }
  static constexpr Rect fromLTRB(float l, float t, float r, float b) noexcept { return {l, t, r, b}; }
  static constexpr Rect zero() noexcept { return {}; }

  constexpr float width() const noexcept { return right - left; }
  constexpr float height() const noexcept { return bottom - top; }
  constexpr Size size() const noexcept { return {width(), height()}; }
  constexpr Offset topLeft() const noexcept { return {left, top}; }
  constexpr Offset center() const noexcept { return {(left + right) * 0.5f, (top + bottom) * 0.5f}; }
  constexpr bool isEmpty() const noexcept { return right <= left || bottom <= top; }

  constexpr bool contains(Offset p) const noexcept {
    return p.dx >= left && p.dx < right && p.dy >= top && p.dy < bottom;
  }
  constexpr Rect shift(Offset o) const noexcept {
    return {left + o.dx, top + o.dy, right + o.dx, bottom + o.dy};
  }
  constexpr Rect intersect(Rect o) const noexcept {
    return {std::max(left, o.left), std::max(top, o.top), std::min(right, o.right),
            std::min(bottom, o.bottom)};
  }
  constexpr Rect expandToInclude(Rect o) const noexcept {
    if (isEmpty()) return o;
    if (o.isEmpty()) return *this;
    return {std::min(left, o.left), std::min(top, o.top), std::max(right, o.right),
            std::max(bottom, o.bottom)};
  }
  constexpr bool overlaps(Rect o) const noexcept {
    return left < o.right && o.left < right && top < o.bottom && o.top < bottom;
  }

  friend constexpr bool operator==(Rect, Rect) noexcept = default;
};

inline constexpr Rect lerp(Rect a, Rect b, float t) noexcept {
  return {lerpF(a.left, b.left, t), lerpF(a.top, b.top, t), lerpF(a.right, b.right, t),
          lerpF(a.bottom, b.bottom, t)};
}

// ---------------------------------------------------------------------------
// EdgeInsets
// ---------------------------------------------------------------------------

struct EdgeInsets {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;

  static constexpr EdgeInsets all(float v) noexcept { return {v, v, v, v}; }
  static constexpr EdgeInsets symmetric(float horizontal, float vertical) noexcept {
    return {horizontal, vertical, horizontal, vertical};
  }
  static constexpr EdgeInsets only(float l = 0, float t = 0, float r = 0, float b = 0) noexcept {
    return {l, t, r, b};
  }

  constexpr float horizontal() const noexcept { return left + right; }
  constexpr float vertical() const noexcept { return top + bottom; }
  constexpr Offset topLeft() const noexcept { return {left, top}; }
  constexpr Size deflateSize(Size s) const noexcept {
    return {std::max(0.0f, s.width - horizontal()), std::max(0.0f, s.height - vertical())};
  }
  constexpr Size inflateSize(Size s) const noexcept {
    return {s.width + horizontal(), s.height + vertical()};
  }
  constexpr Rect deflateRect(Rect r) const noexcept {
    return {r.left + left, r.top + top, r.right - right, r.bottom - bottom};
  }

  friend constexpr bool operator==(EdgeInsets, EdgeInsets) noexcept = default;
};

inline constexpr EdgeInsets lerp(EdgeInsets a, EdgeInsets b, float t) noexcept {
  return {lerpF(a.left, b.left, t), lerpF(a.top, b.top, t), lerpF(a.right, b.right, t),
          lerpF(a.bottom, b.bottom, t)};
}

// ---------------------------------------------------------------------------
// Alignment
// ---------------------------------------------------------------------------

/// -1 is left/top, 0 is centre, +1 is right/bottom.
struct Alignment {
  float x = 0.0f;
  float y = 0.0f;

  static constexpr Alignment topLeft() noexcept { return {-1, -1}; }
  static constexpr Alignment topCenter() noexcept { return {0, -1}; }
  static constexpr Alignment topRight() noexcept { return {1, -1}; }
  static constexpr Alignment centerLeft() noexcept { return {-1, 0}; }
  static constexpr Alignment center() noexcept { return {0, 0}; }
  static constexpr Alignment centerRight() noexcept { return {1, 0}; }
  static constexpr Alignment bottomLeft() noexcept { return {-1, 1}; }
  static constexpr Alignment bottomCenter() noexcept { return {0, 1}; }
  static constexpr Alignment bottomRight() noexcept { return {1, 1}; }

  /// Offset of a `child` positioned inside `container` under this alignment.
  constexpr Offset inscribe(Size child, Size container) const noexcept {
    const float fx = (x + 1.0f) * 0.5f;
    const float fy = (y + 1.0f) * 0.5f;
    return {(container.width - child.width) * fx, (container.height - child.height) * fy};
  }

  friend constexpr bool operator==(Alignment, Alignment) noexcept = default;
};

inline constexpr Alignment lerp(Alignment a, Alignment b, float t) noexcept {
  return {lerpF(a.x, b.x, t), lerpF(a.y, b.y, t)};
}

constexpr Offset Size::alongSize(Alignment a) const noexcept {
  return {(a.x + 1.0f) * 0.5f * width, (a.y + 1.0f) * 0.5f * height};
}

// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------

/// Straight (non-premultiplied) 8-bit RGBA. The framework never blends; it
/// hands colours to the backend as-is.
struct Color {
  std::uint8_t r = 0, g = 0, b = 0, a = 255;

  static constexpr Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                              std::uint8_t a = 255) noexcept {
    return {r, g, b, a};
  }
  /// 0xAARRGGBB, the order that reads correctly as a hex literal.
  static constexpr Color argb(std::uint32_t v) noexcept {
    return {static_cast<std::uint8_t>((v >> 16) & 0xFF), static_cast<std::uint8_t>((v >> 8) & 0xFF),
            static_cast<std::uint8_t>(v & 0xFF), static_cast<std::uint8_t>((v >> 24) & 0xFF)};
  }
  static constexpr Color transparent() noexcept { return {0, 0, 0, 0}; }

  constexpr Color withAlpha(std::uint8_t na) const noexcept { return {r, g, b, na}; }
  constexpr Color scaleAlpha(float s) const noexcept {
    const float v = static_cast<float>(a) * s;
    return {r, g, b, static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5f))};
  }

  friend constexpr bool operator==(Color, Color) noexcept = default;
};

Color lerp(Color a, Color b, float t) noexcept;

// ---------------------------------------------------------------------------
// Border radius
// ---------------------------------------------------------------------------

struct Radius {
  float x = 0.0f;
  float y = 0.0f;
  static constexpr Radius circular(float v) noexcept { return {v, v}; }
  friend constexpr bool operator==(Radius, Radius) noexcept = default;
};

inline constexpr Radius lerp(Radius a, Radius b, float t) noexcept {
  return {lerpF(a.x, b.x, t), lerpF(a.y, b.y, t)};
}

struct BorderRadius {
  Radius topLeft, topRight, bottomRight, bottomLeft;

  static constexpr BorderRadius all(float v) noexcept {
    const Radius r = Radius::circular(v);
    return {r, r, r, r};
  }
  static constexpr BorderRadius zero() noexcept { return {}; }
  constexpr bool isZero() const noexcept {
    return topLeft == Radius{} && topRight == Radius{} && bottomRight == Radius{} &&
           bottomLeft == Radius{};
  }
  friend constexpr bool operator==(BorderRadius, BorderRadius) noexcept = default;
};

inline constexpr BorderRadius lerp(BorderRadius a, BorderRadius b, float t) noexcept {
  return {lerp(a.topLeft, b.topLeft, t), lerp(a.topRight, b.topRight, t),
          lerp(a.bottomRight, b.bottomRight, t), lerp(a.bottomLeft, b.bottomLeft, t)};
}

// ---------------------------------------------------------------------------
// Transform2D
// ---------------------------------------------------------------------------

/// 2D affine transform, row-major as
///     | a  c  tx |
///     | b  d  ty |
///     | 0  0  1  |
/// Kept 2D deliberately: the framework's hit testing must invert it exactly,
/// and a full 4x4 with perspective makes inversion a partial operation.
struct Transform2D {
  float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, tx = 0.0f, ty = 0.0f;

  static constexpr Transform2D identity() noexcept { return {}; }
  static constexpr Transform2D translation(Offset o) noexcept {
    return {1, 0, 0, 1, o.dx, o.dy};
  }
  static constexpr Transform2D scaling(float sx, float sy) noexcept { return {sx, 0, 0, sy, 0, 0}; }
  static Transform2D rotation(float radians) noexcept;
  /// Scale/rotate about `origin` rather than the local (0,0).
  static Transform2D aroundOrigin(const Transform2D& m, Offset origin) noexcept;

  constexpr bool isIdentity() const noexcept {
    return a == 1 && b == 0 && c == 0 && d == 1 && tx == 0 && ty == 0;
  }
  constexpr bool isTranslationOnly() const noexcept { return a == 1 && b == 0 && c == 0 && d == 1; }

  constexpr Offset apply(Offset p) const noexcept {
    return {a * p.dx + c * p.dy + tx, b * p.dx + d * p.dy + ty};
  }
  /// Axis-aligned bounds of the transformed rect.
  Rect applyToRect(Rect r) const noexcept;

  constexpr float determinant() const noexcept { return a * d - b * c; }
  bool invert(Transform2D& out) const noexcept;

  /// `this` then `m` (i.e. m * this in matrix order).
  constexpr Transform2D then(const Transform2D& m) const noexcept {
    return {m.a * a + m.c * b,
            m.b * a + m.d * b,
            m.a * c + m.c * d,
            m.b * c + m.d * d,
            m.a * tx + m.c * ty + m.tx,
            m.b * tx + m.d * ty + m.ty};
  }

  friend constexpr bool operator==(const Transform2D&, const Transform2D&) noexcept = default;
};

inline constexpr Transform2D lerp(const Transform2D& x, const Transform2D& y, float t) noexcept {
  return {lerpF(x.a, y.a, t),   lerpF(x.b, y.b, t),   lerpF(x.c, y.c, t),
          lerpF(x.d, y.d, t),   lerpF(x.tx, y.tx, t), lerpF(x.ty, y.ty, t)};
}

static_assert(sizeof(Transform2D) == 24);
static_assert(std::is_trivially_destructible_v<Rect>);

}  // namespace fltr
