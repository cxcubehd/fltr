#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "fltr/paint/text.hpp"
#include "raylib.h"

namespace demo {

/// A real `TextService` over a raylib font.
///
/// fltr ships only `MonospaceTextService`, which its own header calls a
/// development stand-in, so a consumer implements this exactly the way it
/// implements a rendering backend: the framework asks for a measured paragraph
/// and gets handles back; it never loads a font, never shapes, and never draws.
///
/// This one measures with `MeasureTextEx`, wraps greedily on word boundaries,
/// and aligns the runs of a line on a common baseline taken from the font's own
/// glyph metrics. It is not a shaper -- no bidi, no clusters, no ellipsis -- and
/// the places where it stops are exactly the places the interface says a real
/// implementation would keep going.
///
/// Glyphs are rasterised once per pixel size that the UI asks for, rather than
/// once at some atlas size and scaled: a downscaled atlas is the difference
/// between text that is legible at 12px and text that is a smear.
class RaylibTextService final : public fltr::TextService {
public:
  /// The paths are tried in order and the first that loads wins, ending with
  /// raylib's built-in bitmap font. Falling back rather than failing is the
  /// point of the boundary: nothing above `TextService` can tell which font it
  /// got, or whether one was found at all.
  explicit RaylibTextService(std::span<const char* const> fontPaths);
  ~RaylibTextService() override;

  RaylibTextService(const RaylibTextService&) = delete;
  RaylibTextService& operator=(const RaylibTextService&) = delete;

  fltr::ParagraphHandle acquire(const fltr::ParagraphSpec& spec, float maxWidth) override;
  const fltr::ParagraphMetrics& metrics(fltr::ParagraphHandle handle) const override;
  void release(fltr::ParagraphHandle handle) override;
  std::uint32_t byteOffsetAt(fltr::ParagraphHandle handle, fltr::Offset local) const override;

  /// Draws a measured paragraph. The backend hands `DrawParagraph` straight to
  /// this, so nothing but the text service knows what a run is.
  void draw(fltr::ParagraphHandle handle, fltr::Offset origin, fltr::Color tint) const;

  /// Outstanding handles. Zero after teardown is how a paragraph leak is caught.
  std::size_t liveParagraphs() const noexcept { return live_; }

private:
  struct Slot {
    bool live = false;
    /// One copy per span, so a run can be measured and drawn long after the
    /// `string_view` the framework passed in has gone away.
    std::vector<std::string> spans;
    std::vector<fltr::TextStyle> styles;
    std::vector<fltr::PositionedRun> runs;
    std::vector<fltr::LineMetrics> lines;
    fltr::ParagraphMetrics metrics;
  };

  /// A glyph atlas rasterised for one pixel size. `owned` is false for raylib's
  /// built-in font, which belongs to raylib and must not be unloaded here.
  struct Atlas {
    int pixels = 0;
    Font font{};
    bool owned = false;
  };

  /// The atlas for a style, rasterised on first use and kept for the lifetime of
  /// the service. A UI asks for a handful of sizes, so the cache stays small.
  Font fontFor(float size) const;
  float measure(const fltr::TextStyle& style, const char* text, std::size_t length) const;
  float ascent(const fltr::TextStyle& style) const noexcept;
  Slot& slotFor(fltr::ParagraphHandle handle);
  const Slot& slotFor(fltr::ParagraphHandle handle) const;

  std::string fontFile_;
  mutable std::vector<Atlas> atlases_;
  float ascentRatio_ = 0.78f;
  std::size_t live_ = 0;
  std::vector<Slot> slots_;
  std::vector<std::size_t> free_;
  mutable std::string scratch_;
};

}  // namespace demo
