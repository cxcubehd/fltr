#include "platform/raylib_text.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace fltrdemo {
namespace {

using fltr::kInf;
using fltr::LineMetrics;
using fltr::Offset;
using fltr::ParagraphMetrics;
using fltr::PositionedRun;
using fltr::TextAlign;
using fltr::TextOverflow;
using fltr::TextStyle;

/// What every ellipsized line ends with. Three periods rather than U+2026,
/// because a font loaded for the ASCII range has no glyph for the latter and a
/// missing glyph is drawn as '?'.
constexpr const char* kEllipsis = "...";

/// One indivisible measurement unit: a word, a stretch of whitespace, or a
/// mandatory break. A real shaper produces these from a line-breaking pass over
/// shaped glyphs; this produces them from bytes, which is correct for the Latin
/// text the demo actually contains.
struct Token {
  std::uint32_t spanIndex;
  std::uint32_t byteBegin;  ///< within the span
  std::uint32_t byteEnd;
  float width;
  bool isSpace;
  bool isBreak;
};

struct Paragraph {
  /// Every span's text, concatenated, plus the ellipsis. Owned, because the
  /// view the widget passed may be a formatted value from a per-frame arena.
  std::string store;
  std::vector<std::uint32_t> spanBegin;
  std::vector<TextStyle> styles;
  std::vector<LineMetrics> lines;
  std::vector<PositionedRun> runs;
  /// Where each run's bytes live in `store`. Parallel to `runs`, which reports
  /// span-relative offsets because that is what the interface promises.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> runBytes;
  ParagraphMetrics metrics;
};

/// raylib carries no ascent, so the baseline is placed at a fixed fraction of
/// the em. Only `firstBaseline` / `lastBaseline` report it; drawing is done from
/// each run's top-left, so nothing in the demo depends on it being exact.
constexpr float kBaselineRatio = 0.8f;

float glyphAdvance(const Font& font, int codepoint, float scale) noexcept {
  const int index = GetGlyphIndex(font, codepoint);
  const float advance = font.glyphs[index].advanceX != 0
                            ? static_cast<float>(font.glyphs[index].advanceX)
                            : font.recs[index].width + static_cast<float>(font.glyphs[index].offsetX);
  return advance * scale;
}

}  // namespace

struct RaylibTextService::Impl {
  /// Small and linear: the demo registers two or three fonts, and a map would
  /// be slower as well as larger.
  std::vector<std::pair<fltr::FontHandle, Font>> fonts;
  /// Slots, so a handle is an index and `metrics()` can hand out a reference
  /// into storage that does not move when another paragraph is acquired.
  std::vector<std::unique_ptr<Paragraph>> slots;
  std::vector<std::size_t> free;
  std::size_t live = 0;
  std::size_t measures = 0;

  const Font& fontFor(const TextStyle& style) const {
    for (const auto& [handle, font] : fonts) {
      if (handle == style.font) return font;
    }
    FLTR_EXPECTS(!fonts.empty(), "the text service was asked to measure before a font was set");
    return fonts.front().second;
  }

  /// The size the glyph atlas is scaled by to reach `style.size`.
  float scaleFor(const Font& font, const TextStyle& style) const noexcept {
    return font.baseSize > 0 ? style.size / static_cast<float>(font.baseSize) : 1.0f;
  }

  float measureRange(const Font& font, std::string_view text, float scale,
                     float letterSpacing) const noexcept {
    float width = 0.0f;
    const char* p = text.data();
    const char* end = p + text.size();
    while (p < end) {
      int size = 0;
      const int cp = GetCodepointNext(p, &size);
      width += glyphAdvance(font, cp, scale) + letterSpacing;
      p += size;
    }
    return width;
  }

  Paragraph& at(fltr::ParagraphHandle h) const {
    FLTR_EXPECTS(h != fltr::kNullParagraph && h <= slots.size(), "unknown paragraph handle");
    Paragraph* p = slots[h - 1].get();
    FLTR_EXPECTS(p != nullptr, "paragraph handle used after release");
    return *p;
  }
};

RaylibTextService::RaylibTextService() : impl_(std::make_unique<Impl>()) {}
RaylibTextService::~RaylibTextService() = default;

void RaylibTextService::setFont(fltr::FontHandle handle, Font font) {
  for (auto& entry : impl_->fonts) {
    if (entry.first == handle) {
      entry.second = font;
      return;
    }
  }
  impl_->fonts.emplace_back(handle, font);
}

std::size_t RaylibTextService::liveParagraphs() const noexcept { return impl_->live; }
std::size_t RaylibTextService::measureCount() const noexcept { return impl_->measures; }
void RaylibTextService::resetMeasureCount() noexcept { impl_->measures = 0; }

fltr::ParagraphHandle RaylibTextService::acquire(const fltr::ParagraphSpec& spec, float maxWidth) {
  ++impl_->measures;

  auto owned = std::make_unique<Paragraph>();
  Paragraph& para = *owned;
  para.styles.reserve(spec.spans.size());
  para.spanBegin.reserve(spec.spans.size());

  // --- Own the text, then tokenize ----------------------------------------
  for (const fltr::TextSpan& span : spec.spans) {
    para.spanBegin.push_back(static_cast<std::uint32_t>(para.store.size()));
    para.store.append(span.text);
    para.styles.push_back(span.style);
  }
  const auto ellipsisBegin = static_cast<std::uint32_t>(para.store.size());
  para.store.append(kEllipsis);

  std::vector<Token> tokens;
  for (std::uint32_t si = 0; si < spec.spans.size(); ++si) {
    const TextStyle& style = para.styles[si];
    const Font& font = impl_->fontFor(style);
    const float scale = impl_->scaleFor(font, style);
    const std::string_view text = spec.spans[si].text;

    std::size_t i = 0;
    while (i < text.size()) {
      if (text[i] == '\n') {
        tokens.push_back({si, static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i + 1),
                          0.0f, false, true});
        ++i;
        continue;
      }
      const bool space = text[i] == ' ' || text[i] == '\t';
      const std::size_t begin = i;
      while (i < text.size() && text[i] != '\n' &&
             ((text[i] == ' ' || text[i] == '\t') == space)) {
        ++i;
      }
      const std::string_view slice = text.substr(begin, i - begin);
      tokens.push_back({si, static_cast<std::uint32_t>(begin), static_cast<std::uint32_t>(i),
                        impl_->measureRange(font, slice, scale, style.letterSpacing), space,
                        false});
    }
  }

  // --- Greedy line breaking -----------------------------------------------
  struct PendingLine {
    std::size_t tokenBegin = 0;
    std::size_t tokenEnd = 0;  ///< exclusive
    float width = 0.0f;        ///< excluding trailing whitespace
    float widthWithTrailing = 0.0f;
  };

  std::vector<PendingLine> lines;
  PendingLine cur;
  const bool bounded = maxWidth < kInf;

  const auto flushLine = [&](std::size_t endToken) {
    cur.tokenEnd = endToken;
    lines.push_back(cur);
    cur = PendingLine{endToken, endToken, 0.0f, 0.0f};
  };

  for (std::size_t ti = 0; ti < tokens.size(); ++ti) {
    const Token& tok = tokens[ti];
    if (tok.isBreak) {
      flushLine(ti);
      cur.tokenBegin = ti + 1;
      cur.tokenEnd = ti + 1;
      continue;
    }
    const bool lineEmpty = cur.tokenEnd == cur.tokenBegin;
    if (bounded && !lineEmpty && !tok.isSpace &&
        cur.widthWithTrailing + tok.width > maxWidth + 1e-3f) {
      flushLine(ti);
      cur.tokenBegin = ti;
      cur.tokenEnd = ti;
    }
    if (cur.tokenEnd == cur.tokenBegin && tok.isSpace && !lines.empty()) {
      cur.tokenBegin = ti + 1;  // whitespace a wrap left at the head of a line
      cur.tokenEnd = ti + 1;
      continue;
    }
    cur.tokenEnd = ti + 1;
    cur.widthWithTrailing += tok.width;
    if (!tok.isSpace) cur.width = cur.widthWithTrailing;
  }
  if (cur.tokenEnd > cur.tokenBegin || lines.empty()) flushLine(tokens.size());

  if (spec.maxLines > 0 && lines.size() > static_cast<std::size_t>(spec.maxLines)) {
    lines.resize(static_cast<std::size_t>(spec.maxLines));
    para.metrics.didExceedMaxLines = true;
  }
  const bool wantsEllipsis =
      para.metrics.didExceedMaxLines && spec.overflow == TextOverflow::Ellipsis && bounded;

  // --- Position the runs ---------------------------------------------------
  float widest = 0.0f;
  for (const PendingLine& line : lines) widest = std::max(widest, line.width);
  // Shrink-wrapped, like every other box: the paragraph is as wide as its
  // widest line, and alignment resolves within that.
  const float paraWidth = widest;

  float y = 0.0f;
  for (std::size_t li = 0; li < lines.size(); ++li) {
    const PendingLine& line = lines[li];
    const bool ellipsizeThis = wantsEllipsis && li + 1 == lines.size();

    float lineHeight = 0.0f;
    for (std::size_t ti = line.tokenBegin; ti < line.tokenEnd; ++ti) {
      const TextStyle& st = para.styles[tokens[ti].spanIndex];
      lineHeight = std::max(lineHeight, st.size * st.lineHeight);
    }
    if (lineHeight == 0.0f && !para.styles.empty()) {
      const TextStyle& st = para.styles.front();
      lineHeight = st.size * st.lineHeight;
    }

    // The tail the ellipsis has to fit into, measured in the style that will
    // draw it -- the last span on the line.
    float budget = kInf;
    float ellipsisWidth = 0.0f;
    std::uint32_t ellipsisSpan = 0;
    if (ellipsizeThis && line.tokenEnd > line.tokenBegin) {
      ellipsisSpan = tokens[line.tokenEnd - 1].spanIndex;
      const TextStyle& st = para.styles[ellipsisSpan];
      const Font& font = impl_->fontFor(st);
      ellipsisWidth =
          impl_->measureRange(font, kEllipsis, impl_->scaleFor(font, st), st.letterSpacing);
      budget = maxWidth - ellipsisWidth;
    }

    float x = 0.0f;
    switch (spec.align) {
      case TextAlign::Left: break;
      case TextAlign::Center: x = (paraWidth - line.width) * 0.5f; break;
      case TextAlign::Right: x = paraWidth - line.width; break;
    }

    const auto runBegin = static_cast<std::uint32_t>(para.runs.size());
    const float lineLeft = x;
    bool truncated = false;

    // Adjacent tokens from the same span merge into one positioned run, which
    // is what a shaper would produce and what keeps the draw loop short.
    std::size_t ti = line.tokenBegin;
    while (ti < line.tokenEnd && !truncated) {
      const std::uint32_t span = tokens[ti].spanIndex;
      const std::uint32_t begin = tokens[ti].byteBegin;
      std::uint32_t end = tokens[ti].byteEnd;
      float width = tokens[ti].width;
      ++ti;
      while (ti < line.tokenEnd && tokens[ti].spanIndex == span &&
             tokens[ti].byteBegin == end && !tokens[ti].isBreak) {
        end = tokens[ti].byteEnd;
        width += tokens[ti].width;
        ++ti;
      }

      const TextStyle& st = para.styles[span];
      if (ellipsizeThis && x + width > budget) {
        // Trim this run glyph by glyph rather than at the word boundary: a
        // clipped word with a real ellipsis reads far better than a word that
        // vanished.
        const Font& font = impl_->fontFor(st);
        const float scale = impl_->scaleFor(font, st);
        const std::string_view text{para.store.data() + para.spanBegin[span] + begin, end - begin};
        float used = 0.0f;
        std::uint32_t kept = 0;
        const char* p = text.data();
        const char* stop = p + text.size();
        while (p < stop) {
          int size = 0;
          const int cp = GetCodepointNext(p, &size);
          const float advance = glyphAdvance(font, cp, scale) + st.letterSpacing;
          if (x + used + advance > budget) break;
          used += advance;
          kept += static_cast<std::uint32_t>(size);
          p += size;
        }
        end = begin + kept;
        width = used;
        truncated = true;
      }

      const float runHeight = st.size * st.lineHeight;
      PositionedRun run;
      run.offset = {x, y + (lineHeight - runHeight)};
      run.size = {width, runHeight};
      run.baseline = runHeight * kBaselineRatio;
      run.spanIndex = span;
      run.byteBegin = begin;
      run.byteEnd = end;
      para.runs.push_back(run);
      para.runBytes.emplace_back(para.spanBegin[span] + begin, para.spanBegin[span] + end);
      x += width;
    }

    // Every run on an ellipsized line was placed within the budget, so the
    // ellipsis always fits -- and it is appended whether this line overflowed
    // or the lines *after* it were the ones dropped.
    if (ellipsizeThis && line.tokenEnd > line.tokenBegin) {
      const TextStyle& st = para.styles[ellipsisSpan];
      PositionedRun run;
      run.offset = {x, y + (lineHeight - st.size * st.lineHeight)};
      run.size = {ellipsisWidth, st.size * st.lineHeight};
      run.baseline = st.size * st.lineHeight * kBaselineRatio;
      run.spanIndex = ellipsisSpan;
      // The ellipsis is not in the source text, so it reports an empty range at
      // the end of what it replaced. Its glyphs live in `store`.
      run.byteBegin = run.byteEnd = para.runs.empty() ? 0 : para.runs.back().byteEnd;
      para.runs.push_back(run);
      para.runBytes.emplace_back(ellipsisBegin,
                                 ellipsisBegin + static_cast<std::uint32_t>(3));
      x += ellipsisWidth;
      para.metrics.didEllipsize = true;
    }

    LineMetrics lm;
    lm.bounds = fltr::Rect::fromLTWH(lineLeft, y, x - lineLeft, lineHeight);
    lm.baseline = y + lineHeight * kBaselineRatio;
    lm.runBegin = runBegin;
    lm.runCount = static_cast<std::uint32_t>(para.runs.size()) - runBegin;
    para.lines.push_back(lm);
    widest = std::max(widest, x);
    y += lineHeight;
  }

  para.metrics.size = {widest, y};
  para.metrics.firstBaseline = para.lines.empty() ? 0.0f : para.lines.front().baseline;
  para.metrics.lastBaseline = para.lines.empty() ? 0.0f : para.lines.back().baseline;
  para.metrics.lines = para.lines;
  para.metrics.runs = para.runs;

  std::size_t index;
  if (impl_->free.empty()) {
    impl_->slots.push_back(std::move(owned));
    index = impl_->slots.size() - 1;
  } else {
    index = impl_->free.back();
    impl_->free.pop_back();
    impl_->slots[index] = std::move(owned);
  }
  ++impl_->live;
  return static_cast<fltr::ParagraphHandle>(index + 1);
}

const ParagraphMetrics& RaylibTextService::metrics(fltr::ParagraphHandle h) const {
  return impl_->at(h).metrics;
}

void RaylibTextService::release(fltr::ParagraphHandle h) {
  if (h == fltr::kNullParagraph) return;
  FLTR_EXPECTS(h <= impl_->slots.size(), "release of an unknown paragraph handle");
  if (!impl_->slots[h - 1]) return;
  impl_->slots[h - 1].reset();
  impl_->free.push_back(h - 1);
  --impl_->live;
}

std::uint32_t RaylibTextService::byteOffsetAt(fltr::ParagraphHandle h, Offset local) const {
  const Paragraph& para = impl_->at(h);
  if (para.lines.empty()) return 0;

  const LineMetrics* line = &para.lines.front();
  for (const LineMetrics& lm : para.lines) {
    if (local.dy >= lm.bounds.top) line = &lm;
  }
  if (line->runCount == 0) return 0;

  const PositionedRun* last = nullptr;
  for (std::uint32_t i = 0; i < line->runCount; ++i) {
    const std::uint32_t index = line->runBegin + i;
    const PositionedRun& run = para.runs[index];
    last = &run;
    if (local.dx >= run.offset.dx + run.size.width) continue;

    const TextStyle& st = para.styles[run.spanIndex];
    const Font& font = impl_->fontFor(st);
    const float scale = impl_->scaleFor(font, st);
    const auto [from, to] = para.runBytes[index];
    const char* p = para.store.data() + from;
    const char* stop = para.store.data() + to;
    float x = run.offset.dx;
    while (p < stop) {
      int size = 0;
      const int cp = GetCodepointNext(p, &size);
      const float advance = glyphAdvance(font, cp, scale) + st.letterSpacing;
      // Nearest boundary, not the glyph containing the point: a caret snaps to
      // whichever side of the glyph the cursor is closer to.
      if (local.dx < x + advance * 0.5f) break;
      x += advance;
      p += size;
    }
    return run.byteBegin + static_cast<std::uint32_t>(p - (para.store.data() + from));
  }
  return last ? last->byteEnd : 0;
}

void RaylibTextService::draw(fltr::ParagraphHandle h, Offset at, fltr::Color tint,
                             float scale) const {
  const Paragraph& para = impl_->at(h);
  const Color color{tint.r, tint.g, tint.b, tint.a};

  for (std::size_t i = 0; i < para.runs.size(); ++i) {
    const PositionedRun& run = para.runs[i];
    const TextStyle& st = para.styles[run.spanIndex];
    const Font& font = impl_->fontFor(st);
    const float size = st.size * scale;
    const float glyphScale = font.baseSize > 0 ? size / static_cast<float>(font.baseSize) : 1.0f;

    // The command's tint is the colour, not a modulation of the run's: the
    // framework's Text builds one span whose style colour *is* that tint, and
    // multiplying the two would square it. A multi-span consumer would blend
    // `st.color` in here instead.
    Vector2 pen{at.dx + run.offset.dx * scale, at.dy + run.offset.dy * scale};
    const auto [from, to] = para.runBytes[i];
    const char* p = para.store.data() + from;
    const char* stop = para.store.data() + to;
    while (p < stop) {
      int step = 0;
      const int cp = GetCodepointNext(p, &step);
      if (cp != ' ') DrawTextCodepoint(font, cp, pen, size, color);
      pen.x += glyphAdvance(font, cp, glyphScale) + st.letterSpacing * scale;
      p += step;
    }
  }
}

}  // namespace fltrdemo
