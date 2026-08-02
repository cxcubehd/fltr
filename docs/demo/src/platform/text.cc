#include "platform/text.hh"

#include <algorithm>
#include <cmath>

namespace demo {

using fltr::Color;
using fltr::LineMetrics;
using fltr::Offset;
using fltr::ParagraphHandle;
using fltr::ParagraphMetrics;
using fltr::ParagraphSpec;
using fltr::PositionedRun;
using fltr::Rect;
using fltr::Size;
using fltr::TextStyle;

namespace {

/// One indivisible piece of source text: a word with whatever spaces follow it,
/// or a hard break. Wrapping happens between tokens and never inside one.
struct Token {
  std::uint32_t span;
  std::uint32_t begin;
  std::uint32_t end;
  bool hardBreak;
};

bool isSpace(char c) noexcept { return c == ' ' || c == '\t'; }

/// Rasterisation sizes are clamped so that a nonsense style cannot ask for a
/// gigabyte of atlas, and the baseline ratio is read from a size in the middle
/// of that range where the hinting is representative.
constexpr int kMinSize = 6;
constexpr int kMaxSize = 128;
constexpr float kReferenceSize = 32.0f;

/// A run under construction: which span it came from and how much of it is on
/// the current line so far.
struct PendingRun {
  std::uint32_t span;
  std::uint32_t begin;
  std::uint32_t end;
  float width;
};

}  // namespace

RaylibTextService::RaylibTextService(std::span<const char* const> fontPaths) {
  for (const char* path : fontPaths) {
    if (path != nullptr && FileExists(path)) {
      fontFile_ = path;
      break;
    }
  }

  // The baseline is derived from the font rather than guessed: raylib places a
  // glyph at `offsetY` below the line top, so the bottom of a capital letter is
  // where the baseline sits. fltr's placeholder service hardcodes this ratio and
  // says so; a real one should not.
  const Font reference = fontFor(kReferenceSize);
  const int index = GetGlyphIndex(reference, 'H');
  if (index >= 0 && index < reference.glyphCount && reference.baseSize > 0) {
    const float top = static_cast<float>(reference.glyphs[index].offsetY);
    const float height = reference.recs[index].height;
    ascentRatio_ = (top + height) / static_cast<float>(reference.baseSize);
  }

  slots_.reserve(64);
}

RaylibTextService::~RaylibTextService() {
  for (const Atlas& atlas : atlases_) {
    if (atlas.owned) UnloadFont(atlas.font);
  }
}

Font RaylibTextService::fontFor(float size) const {
  const int pixels = std::clamp(static_cast<int>(std::lround(size)), kMinSize, kMaxSize);
  for (const Atlas& atlas : atlases_) {
    if (atlas.pixels == pixels) return atlas.font;
  }

  Atlas atlas{.pixels = pixels, .font = {}, .owned = !fontFile_.empty()};
  if (atlas.owned) atlas.font = LoadFontEx(fontFile_.c_str(), pixels, nullptr, 0);
  if (atlas.font.glyphCount <= 0) {
    atlas.font = GetFontDefault();
    atlas.owned = false;
  }
  // Glyphs are drawn at the size they were rasterised at, so this only matters
  // for the built-in fallback and for a style that lands between two sizes --
  // but when resampling does happen, smooth beats blocky.
  SetTextureFilter(atlas.font.texture, TEXTURE_FILTER_BILINEAR);
  return atlases_.emplace_back(atlas).font;
}

float RaylibTextService::ascent(const TextStyle& style) const noexcept {
  return style.size * ascentRatio_;
}

float RaylibTextService::measure(const TextStyle& style, const char* text,
                                 std::size_t length) const {
  if (length == 0) return 0.0f;
  scratch_.assign(text, length);
  const Vector2 size =
      MeasureTextEx(fontFor(style.size), scratch_.c_str(), style.size, style.letterSpacing);
  return size.x;
}

RaylibTextService::Slot& RaylibTextService::slotFor(ParagraphHandle handle) {
  FLTR_EXPECTS(handle != fltr::kNullParagraph && handle <= slots_.size(), "unknown paragraph");
  return slots_[static_cast<std::size_t>(handle) - 1u];
}

const RaylibTextService::Slot& RaylibTextService::slotFor(ParagraphHandle handle) const {
  FLTR_EXPECTS(handle != fltr::kNullParagraph && handle <= slots_.size(), "unknown paragraph");
  return slots_[static_cast<std::size_t>(handle) - 1u];
}

ParagraphHandle RaylibTextService::acquire(const ParagraphSpec& spec, float maxWidth) {
  std::size_t index;
  if (!free_.empty()) {
    index = free_.back();
    free_.pop_back();
  } else {
    index = slots_.size();
    slots_.emplace_back();
  }

  Slot& slot = slots_[index];
  slot.live = true;
  slot.spans.clear();
  slot.styles.clear();
  slot.runs.clear();
  slot.lines.clear();

  for (const fltr::TextSpan& span : spec.spans) {
    slot.spans.emplace_back(span.text);
    slot.styles.push_back(span.style);
  }

  // 1. Tokenise. Spaces stay attached to the word before them, so a break
  //    swallows the trailing space rather than starting the next line with it.
  std::vector<Token> tokens;
  for (std::uint32_t s = 0; s < slot.spans.size(); ++s) {
    const std::string& text = slot.spans[s];
    std::uint32_t i = 0;
    while (i < text.size()) {
      if (text[i] == '\n') {
        tokens.push_back({s, i, i + 1u, true});
        ++i;
        continue;
      }
      const std::uint32_t begin = i;
      while (i < text.size() && text[i] != '\n' && !isSpace(text[i])) ++i;
      while (i < text.size() && isSpace(text[i])) ++i;
      tokens.push_back({s, begin, i, false});
    }
  }

  // 2. Wrap. One pass, greedy, breaking between tokens only.
  const bool bounded = maxWidth < fltr::kInf;
  const int lineLimit = spec.maxLines > 0 ? spec.maxLines : -1;

  std::vector<PendingRun> lineRuns;
  float lineWidth = 0.0f;
  float top = 0.0f;
  float widest = 0.0f;
  bool exceeded = false;

  auto flushLine = [&]() {
    if (lineRuns.empty()) {
      // An empty line still occupies one line box, sized by the first style.
      const float size = slot.styles.empty() ? 14.0f : slot.styles.front().size;
      const float height = size * (slot.styles.empty() ? 1.2f : slot.styles.front().lineHeight);
      slot.lines.push_back(LineMetrics{.bounds = Rect::fromLTWH(0.0f, top, 0.0f, height),
                                       .baseline = size * ascentRatio_,
                                       .runBegin = static_cast<std::uint32_t>(slot.runs.size()),
                                       .runCount = 0});
      top += height;
      return;
    }

    float baseline = 0.0f;
    float depth = 0.0f;
    for (const PendingRun& run : lineRuns) {
      const TextStyle& style = slot.styles[run.span];
      baseline = std::max(baseline, ascent(style));
      depth = std::max(depth, style.size * style.lineHeight - ascent(style));
    }
    const float height = baseline + depth;

    // Alignment shifts the whole line. Against an unbounded measurement there is
    // nothing to align to yet, so that case is fixed up after every line is in.
    float x = 0.0f;
    if (bounded && spec.align != fltr::TextAlign::Left) {
      const float slack = std::max(0.0f, maxWidth - lineWidth);
      x = spec.align == fltr::TextAlign::Center ? slack * 0.5f : slack;
    }

    const std::uint32_t runBegin = static_cast<std::uint32_t>(slot.runs.size());
    for (const PendingRun& run : lineRuns) {
      const TextStyle& style = slot.styles[run.span];
      slot.runs.push_back(PositionedRun{
          .offset = {x, top + baseline - ascent(style)},
          .size = {run.width, style.size * style.lineHeight},
          .baseline = ascent(style),
          .spanIndex = run.span,
          .byteBegin = run.begin,
          .byteEnd = run.end,
      });
      x += run.width;
    }

    slot.lines.push_back(
        LineMetrics{.bounds = Rect::fromLTWH(0.0f, top, lineWidth, height),
                    .baseline = baseline,
                    .runBegin = runBegin,
                    .runCount = static_cast<std::uint32_t>(lineRuns.size())});

    widest = std::max(widest, lineWidth);
    top += height;
    lineRuns.clear();
    lineWidth = 0.0f;
  };

  auto atLimit = [&]() {
    return lineLimit >= 0 && static_cast<int>(slot.lines.size()) >= lineLimit;
  };

  for (const Token& token : tokens) {
    if (atLimit()) {
      exceeded = true;
      break;
    }
    if (token.hardBreak) {
      flushLine();
      continue;
    }

    const TextStyle& style = slot.styles[token.span];
    const std::string& text = slot.spans[token.span];
    const float width = measure(style, text.data() + token.begin, token.end - token.begin);

    if (bounded && !lineRuns.empty() && lineWidth + width > maxWidth) {
      flushLine();
      if (atLimit()) {
        exceeded = true;
        break;
      }
    }

    // Extend the run in progress when this token continues the same span
    // contiguously; otherwise start a new one. This is why a run is "a piece of
    // one span on one line" and not "one span" or "one word".
    if (!lineRuns.empty() && lineRuns.back().span == token.span &&
        lineRuns.back().end == token.begin) {
      lineRuns.back().end = token.end;
      lineRuns.back().width += width;
    } else {
      lineRuns.push_back({token.span, token.begin, token.end, width});
    }
    lineWidth += width;
  }

  if (!lineRuns.empty() && !atLimit()) flushLine();
  if (slot.lines.empty()) flushLine();

  // 3. Alignment against an unbounded measurement, now that the width is known.
  if (!bounded && spec.align != fltr::TextAlign::Left) {
    for (const LineMetrics& line : slot.lines) {
      const float slack = std::max(0.0f, widest - line.bounds.width());
      const float shift = spec.align == fltr::TextAlign::Center ? slack * 0.5f : slack;
      for (std::uint32_t r = line.runBegin; r < line.runBegin + line.runCount; ++r) {
        slot.runs[r].offset.dx += shift;
      }
    }
  }

  slot.metrics = ParagraphMetrics{
      .size = Size{bounded ? std::min(widest, maxWidth) : widest, top},
      .firstBaseline = slot.lines.empty() ? 0.0f : slot.lines.front().baseline,
      .lastBaseline = slot.lines.empty()
                          ? 0.0f
                          : slot.lines.back().bounds.top + slot.lines.back().baseline,
      .didExceedMaxLines = exceeded,
      // Ellipsis is expressible in the interface and not implemented here. The
      // demo never asks for it; see the "Gaps and rework" page.
      .didEllipsize = false,
      .lines = slot.lines,
      .runs = slot.runs,
  };

  ++live_;
  return static_cast<ParagraphHandle>(index + 1u);
}

const ParagraphMetrics& RaylibTextService::metrics(ParagraphHandle handle) const {
  const Slot& slot = slotFor(handle);
  FLTR_EXPECTS(slot.live, "metrics for a released paragraph");
  return slot.metrics;
}

void RaylibTextService::release(ParagraphHandle handle) {
  if (handle == fltr::kNullParagraph) return;
  Slot& slot = slotFor(handle);
  if (!slot.live) return;
  slot.live = false;
  free_.push_back(static_cast<std::size_t>(handle) - 1u);
  --live_;
}

std::uint32_t RaylibTextService::byteOffsetAt(ParagraphHandle handle, Offset local) const {
  const Slot& slot = slotFor(handle);
  if (slot.lines.empty()) return 0;

  const LineMetrics* line = &slot.lines.front();
  for (const LineMetrics& candidate : slot.lines) {
    if (local.dy >= candidate.bounds.top) line = &candidate;
  }
  if (line->runCount == 0) return 0;

  for (std::uint32_t r = line->runBegin; r < line->runBegin + line->runCount; ++r) {
    const PositionedRun& run = slot.runs[r];
    if (local.dx > run.offset.dx + run.size.width && r + 1u < line->runBegin + line->runCount) {
      continue;
    }
    const std::string& text = slot.spans[run.spanIndex];
    const TextStyle& style = slot.styles[run.spanIndex];
    float x = run.offset.dx;
    for (std::uint32_t byte = run.byteBegin; byte < run.byteEnd; ++byte) {
      const float advance = measure(style, text.data() + byte, 1);
      if (local.dx < x + advance * 0.5f) return byte;
      x += advance;
    }
    return run.byteEnd;
  }
  return 0;
}

void RaylibTextService::draw(ParagraphHandle handle, Offset origin, Color tint) const {
  const Slot& slot = slotFor(handle);
  if (!slot.live) return;

  for (const PositionedRun& run : slot.runs) {
    const std::string& text = slot.spans[run.spanIndex];
    const TextStyle& style = slot.styles[run.spanIndex];
    scratch_.assign(text, run.byteBegin, run.byteEnd - run.byteBegin);

    // The run's colour is the style's, modulated by the tint the display list
    // carried -- which is how an `Opacity` above a `Text` reaches the glyphs.
    const ::Color colour{
        static_cast<unsigned char>(style.color.r * tint.r / 255),
        static_cast<unsigned char>(style.color.g * tint.g / 255),
        static_cast<unsigned char>(style.color.b * tint.b / 255),
        static_cast<unsigned char>(style.color.a * tint.a / 255),
    };

    // raylib positions text by the top-left of its line box, which is exactly
    // what a `PositionedRun` offset is, so no baseline arithmetic is needed here.
    // Rounding to whole pixels keeps a glyph on the texel grid it was rasterised
    // on, which is the difference between a crisp stem and a grey one.
    const Vector2 at{std::round(origin.dx + run.offset.dx), std::round(origin.dy + run.offset.dy)};
    DrawTextEx(fontFor(style.size), scratch_.c_str(), at, style.size, style.letterSpacing, colour);
  }
}

}  // namespace demo
