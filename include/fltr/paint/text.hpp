#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "fltr/core/geometry.hpp"
#include "fltr/paint/display_list.hpp"

namespace fltr {

/// Fonts, like images, are consumer-owned and opaque to the framework.
using FontHandle = std::uint64_t;

struct TextStyle {
  FontHandle font = 0;
  float size = 14.0f;
  Color color = Color{255, 255, 255, 255};
  float letterSpacing = 0.0f;
  /// Multiple of `size`.
  float lineHeight = 1.2f;

  friend constexpr bool operator==(const TextStyle&, const TextStyle&) noexcept = default;
};

inline TextStyle lerp(const TextStyle& a, const TextStyle& b, float t) noexcept {
  return {b.font, lerpF(a.size, b.size, t), lerp(a.color, b.color, t),
          lerpF(a.letterSpacing, b.letterSpacing, t), lerpF(a.lineHeight, b.lineHeight, t)};
}

enum class TextAlign : std::uint8_t { Left, Center, Right };
enum class TextOverflow : std::uint8_t { Clip, Ellipsis };

/// One styled run of source text. A paragraph is a sequence of these; the
/// service is free to reorder, split, or merge them when it shapes.
struct TextSpan {
  std::string_view text;
  TextStyle style;
};

struct ParagraphSpec {
  std::span<const TextSpan> spans;
  TextAlign align = TextAlign::Left;
  /// 0 means unlimited.
  int maxLines = 0;
  TextOverflow overflow = TextOverflow::Clip;
};

/// A contiguous piece of one span placed on one line.
///
/// Explicitly not one-glyph-per-character and not one-run-per-span: a real
/// shaper will split a span across lines and may merge or reorder within a
/// line. The framework only ever reads positions and bounds from these.
struct PositionedRun {
  Offset offset;  ///< top-left, paragraph-relative
  Size size;
  float baseline = 0.0f;  ///< from the run's top edge
  std::uint32_t spanIndex = 0;
  std::uint32_t byteBegin = 0;
  std::uint32_t byteEnd = 0;
};

struct LineMetrics {
  Rect bounds;  ///< paragraph-relative
  float baseline = 0.0f;
  std::uint32_t runBegin = 0;
  std::uint32_t runCount = 0;
};

/// The measured result. Storage is owned by the TextService and stays valid
/// until the corresponding handle is released.
struct ParagraphMetrics {
  Size size;
  float firstBaseline = 0.0f;
  float lastBaseline = 0.0f;
  bool didExceedMaxLines = false;
  std::span<const LineMetrics> lines;
  std::span<const PositionedRun> runs;
};

/// The boundary through which the framework asks for text.
///
/// Text is consumed exactly the way a rendering backend is consumed: the
/// framework calls out, the consumer implements. Nothing above this interface
/// assumes single-line, fixed-width, ASCII, or one glyph per character, and
/// nothing above it does shaping, font matching, line breaking, rasterization,
/// or atlas management.
///
/// Lifetime: `acquire` returns a handle the caller owns and must `release`.
/// Render objects acquire on layout and release on re-layout and destruction.
class TextService {
public:
  virtual ~TextService() = default;

  /// Measure and lay out `spec` into at most `maxWidth` logical pixels.
  /// `maxWidth` may be infinite for an unbounded measurement.
  virtual ParagraphHandle acquire(const ParagraphSpec& spec, float maxWidth) = 0;

  /// Valid until the handle is released.
  virtual const ParagraphMetrics& metrics(ParagraphHandle h) const = 0;

  virtual void release(ParagraphHandle h) = 0;

  /// Byte offset in the source text nearest `local` (paragraph-relative).
  /// Present so hit testing inside text does not need a second mechanism later.
  virtual std::uint32_t byteOffsetAt(ParagraphHandle h, Offset local) const = 0;
};

/// Development stand-in: a fixed-advance font with greedy word wrapping.
///
/// Deliberately produces multiple lines and multiple positioned runs so that
/// layout, painting, and hit testing are exercised against the same shape of
/// data a real shaper will return. It is not a text renderer and makes no
/// attempt at correctness beyond ASCII.
class MonospaceTextService final : public TextService {
public:
  /// Advance width as a fraction of the style's font size.
  explicit MonospaceTextService(float advanceRatio = 0.5f);
  ~MonospaceTextService() override;

  ParagraphHandle acquire(const ParagraphSpec& spec, float maxWidth) override;
  const ParagraphMetrics& metrics(ParagraphHandle h) const override;
  void release(ParagraphHandle h) override;
  std::uint32_t byteOffsetAt(ParagraphHandle h, Offset local) const override;

  /// Number of handles currently outstanding. Tests assert this returns to
  /// zero, which is how paragraph leaks are caught.
  std::size_t liveParagraphs() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fltr
