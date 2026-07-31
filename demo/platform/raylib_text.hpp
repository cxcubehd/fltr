#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "fltr/paint/text.hpp"
#include "raylib.h"

namespace fltrdemo {

/// Real text for the demo: greedy word wrapping over raylib's per-glyph
/// advances, with line boxes, positioned runs, alignment, maxLines and a real
/// ellipsis.
///
/// Two rules the boundary imposes, both learned the hard way:
///
///   - the service owns a copy of every string it is handed. The `string_view`
///     in the widget may be a formatted value from a per-frame arena and is
///     gone long before the paragraph is drawn.
///   - it must outlive the WidgetBinding. `RenderParagraph` acquires a handle
///     during layout and releases it on relayout and on destruction, so the
///     last release happens while the element tree is being torn down.
///     Construct it before the binding, destroy it after, and assert
///     `liveParagraphs() == 0` on the way out.
///
/// Fonts are consumer-owned, exactly as images are: this maps the handle a
/// `TextStyle` carries onto a `Font` that `main` loaded and will unload.
class RaylibTextService final : public fltr::TextService {
public:
  RaylibTextService();
  ~RaylibTextService() override;

  RaylibTextService(const RaylibTextService&) = delete;
  RaylibTextService& operator=(const RaylibTextService&) = delete;

  /// Handle 0 is the fallback every style gets when it names no font.
  void setFont(fltr::FontHandle handle, Font font);

  fltr::ParagraphHandle acquire(const fltr::ParagraphSpec& spec, float maxWidth) override;
  const fltr::ParagraphMetrics& metrics(fltr::ParagraphHandle h) const override;
  void release(fltr::ParagraphHandle h) override;
  std::uint32_t byteOffsetAt(fltr::ParagraphHandle h, fltr::Offset local) const override;

  /// Draws a measured paragraph, run by run. It lives here rather than in the
  /// renderer because only the service knows which bytes a positioned run
  /// covers, and glyph-by-glyph drawing is what keeps the draw path free of the
  /// NUL-terminated copy `DrawTextEx` would need.
  void draw(fltr::ParagraphHandle h, fltr::Offset at, fltr::Color tint, float scale) const;

  /// Outstanding handles. Tests assert this returns to zero, which is how a
  /// paragraph leak is caught.
  std::size_t liveParagraphs() const noexcept;

  /// Paragraphs measured since the last reset. The demo reports it in the debug
  /// overlay, because "how much text did that resize re-measure" is the one
  /// number that decides whether a shaping cache is worth having.
  std::size_t measureCount() const noexcept;
  void resetMeasureCount() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fltrdemo
