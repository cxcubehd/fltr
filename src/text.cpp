#include "fltr/paint/text.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace fltr {

namespace {

/// One indivisible measurement unit: a word, a whitespace stretch, or a
/// mandatory break. A real shaper produces these from a line-breaking pass over
/// shaped glyphs; the placeholder produces them from bytes.
struct Token {
  std::uint32_t spanIndex;
  std::uint32_t byteBegin;
  std::uint32_t byteEnd;
  float width;
  bool isSpace;
  bool isBreak;
};

struct Paragraph {
  ParagraphMetrics metrics;
  std::vector<LineMetrics> lines;
  std::vector<PositionedRun> runs;
  std::vector<TextStyle> spanStyles;
  float advanceRatio = 0.5f;
};

float advanceFor(const TextStyle& s, float ratio) noexcept {
  return s.size * ratio + s.letterSpacing;
}

}  // namespace

struct MonospaceTextService::Impl {
  float advanceRatio;
  ParagraphHandle nextHandle = 1;
  std::unordered_map<ParagraphHandle, Paragraph> paragraphs;

  explicit Impl(float r) : advanceRatio(r) {}
};

MonospaceTextService::MonospaceTextService(float advanceRatio)
    : impl_(std::make_unique<Impl>(advanceRatio)) {}

MonospaceTextService::~MonospaceTextService() = default;

std::size_t MonospaceTextService::liveParagraphs() const { return impl_->paragraphs.size(); }

ParagraphHandle MonospaceTextService::acquire(const ParagraphSpec& spec, float maxWidth) {
  Paragraph para;
  para.advanceRatio = impl_->advanceRatio;
  para.spanStyles.reserve(spec.spans.size());

  // --- Tokenize -----------------------------------------------------------
  std::vector<Token> tokens;
  for (std::uint32_t si = 0; si < spec.spans.size(); ++si) {
    const TextSpan& span = spec.spans[si];
    para.spanStyles.push_back(span.style);
    const float adv = advanceFor(span.style, impl_->advanceRatio);
    const std::string_view t = span.text;

    std::size_t i = 0;
    while (i < t.size()) {
      if (t[i] == '\n') {
        tokens.push_back({si, static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i + 1),
                          0.0f, false, true});
        ++i;
        continue;
      }
      const bool space = (t[i] == ' ' || t[i] == '\t');
      const std::size_t begin = i;
      while (i < t.size() && t[i] != '\n' && ((t[i] == ' ' || t[i] == '\t') == space)) ++i;
      tokens.push_back({si, static_cast<std::uint32_t>(begin), static_cast<std::uint32_t>(i),
                        adv * static_cast<float>(i - begin), space, false});
    }
  }

  // --- Greedy line breaking ----------------------------------------------
  struct PendingLine {
    std::size_t tokenBegin = 0;
    std::size_t tokenEnd = 0;  // exclusive
    float width = 0.0f;        // excluding trailing whitespace
    float widthWithTrailing = 0.0f;
  };

  std::vector<PendingLine> lines;
  PendingLine cur;
  const bool bounded = maxWidth < kInf;
  const int maxLines = spec.maxLines;

  auto flushLine = [&](std::size_t endToken) {
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
      // Leading whitespace produced by a wrap is dropped.
      cur.tokenBegin = ti + 1;
      cur.tokenEnd = ti + 1;
      continue;
    }
    cur.tokenEnd = ti + 1;
    cur.widthWithTrailing += tok.width;
    if (!tok.isSpace) cur.width = cur.widthWithTrailing;
  }
  if (cur.tokenEnd > cur.tokenBegin || lines.empty()) flushLine(tokens.size());

  if (maxLines > 0 && static_cast<int>(lines.size()) > maxLines) {
    lines.resize(static_cast<std::size_t>(maxLines));
    para.metrics.didExceedMaxLines = true;
  }
  // A real shaper replaces the tail of the last line with an ellipsis glyph and
  // re-measures. The placeholder only reports that it would have.
  para.metrics.didEllipsize =
      para.metrics.didExceedMaxLines && spec.overflow == TextOverflow::Ellipsis;

  // --- Position runs ------------------------------------------------------
  float y = 0.0f;
  float widest = 0.0f;
  for (const PendingLine& pl : lines) widest = std::max(widest, pl.width);
  const float paraWidth = bounded ? std::max(widest, 0.0f) : widest;

  for (const PendingLine& pl : lines) {
    // Line height is the tallest style contributing to the line.
    float lineHeight = 0.0f;
    for (std::size_t ti = pl.tokenBegin; ti < pl.tokenEnd; ++ti) {
      const TextStyle& st = para.spanStyles[tokens[ti].spanIndex];
      lineHeight = std::max(lineHeight, st.size * st.lineHeight);
    }
    if (lineHeight == 0.0f && !para.spanStyles.empty()) {
      const TextStyle& st = para.spanStyles.front();
      lineHeight = st.size * st.lineHeight;
    }

    float x = 0.0f;
    switch (spec.align) {
      case TextAlign::Left: x = 0.0f; break;
      case TextAlign::Center: x = (paraWidth - pl.width) * 0.5f; break;
      case TextAlign::Right: x = paraWidth - pl.width; break;
    }

    const std::uint32_t runBegin = static_cast<std::uint32_t>(para.runs.size());
    // Merge adjacent tokens from the same span into a single positioned run.
    std::size_t ti = pl.tokenBegin;
    while (ti < pl.tokenEnd) {
      const std::uint32_t span = tokens[ti].spanIndex;
      const std::uint32_t begin = tokens[ti].byteBegin;
      std::uint32_t end = tokens[ti].byteEnd;
      float w = tokens[ti].width;
      ++ti;
      while (ti < pl.tokenEnd && tokens[ti].spanIndex == span && tokens[ti].byteBegin == end &&
             !tokens[ti].isBreak) {
        end = tokens[ti].byteEnd;
        w += tokens[ti].width;
        ++ti;
      }
      const TextStyle& st = para.spanStyles[span];
      const float runHeight = st.size * st.lineHeight;
      PositionedRun run;
      run.offset = {x, y + (lineHeight - runHeight)};
      run.size = {w, runHeight};
      run.baseline = st.size;  // placeholder: baseline sits at the em height
      run.spanIndex = span;
      run.byteBegin = begin;
      run.byteEnd = end;
      para.runs.push_back(run);
      x += w;
    }

    LineMetrics lm;
    lm.bounds = Rect::fromLTWH(0.0f, y, pl.width, lineHeight);
    lm.baseline = y + lineHeight * (1.0f / 1.2f);
    lm.runBegin = runBegin;
    lm.runCount = static_cast<std::uint32_t>(para.runs.size()) - runBegin;
    para.lines.push_back(lm);
    y += lineHeight;
  }

  para.metrics.size = {widest, y};
  para.metrics.firstBaseline = para.lines.empty() ? 0.0f : para.lines.front().baseline;
  para.metrics.lastBaseline = para.lines.empty() ? 0.0f : para.lines.back().baseline;

  const ParagraphHandle h = impl_->nextHandle++;
  auto [it, ok] = impl_->paragraphs.emplace(h, std::move(para));
  (void)ok;
  it->second.metrics.lines = it->second.lines;
  it->second.metrics.runs = it->second.runs;
  return h;
}

const ParagraphMetrics& MonospaceTextService::metrics(ParagraphHandle h) const {
  auto it = impl_->paragraphs.find(h);
  FLTR_EXPECTS(it != impl_->paragraphs.end(), "metrics() on an unknown paragraph handle");
  return it->second.metrics;
}

void MonospaceTextService::release(ParagraphHandle h) {
  if (h == kNullParagraph) return;
  impl_->paragraphs.erase(h);
}

std::uint32_t MonospaceTextService::byteOffsetAt(ParagraphHandle h, Offset local) const {
  auto it = impl_->paragraphs.find(h);
  FLTR_EXPECTS(it != impl_->paragraphs.end(), "byteOffsetAt() on an unknown paragraph handle");
  const Paragraph& p = it->second;
  if (p.lines.empty()) return 0;

  const LineMetrics* line = &p.lines.front();
  for (const LineMetrics& lm : p.lines) {
    if (local.dy >= lm.bounds.top) line = &lm;
  }
  if (line->runCount == 0) return 0;

  const PositionedRun* last = nullptr;
  for (std::uint32_t i = 0; i < line->runCount; ++i) {
    const PositionedRun& r = p.runs[line->runBegin + i];
    last = &r;
    if (local.dx < r.offset.dx + r.size.width) {
      const float adv = advanceFor(p.spanStyles[r.spanIndex], p.advanceRatio);
      if (adv <= 0.0f) return r.byteBegin;
      const float rel = std::max(0.0f, local.dx - r.offset.dx);
      const auto chars = static_cast<std::uint32_t>(std::lround(rel / adv));
      return std::min(r.byteBegin + chars, r.byteEnd);
    }
  }
  return last ? last->byteEnd : 0;
}

}  // namespace fltr
