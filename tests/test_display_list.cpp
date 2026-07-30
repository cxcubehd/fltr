#include "testing.hpp"

#include "fltr/paint/display_list.hpp"
#include "fltr/paint/text.hpp"

using namespace fltr;

TEST(displaylist_records_in_order) {
  DisplayList dl;
  dl.beginRecording();
  dl.pushOpacity(0.5f);
  dl.drawRect(Rect::fromLTWH(0, 0, 10, 10), Color::argb(0xFFFF0000));
  dl.pop();
  dl.endRecording();

  const auto cmds = dl.commands();
  CHECK_EQ(cmds.size(), std::size_t{3});
  CHECK(cmds[0].op == PaintOp::PushOpacity);
  CHECK_EQ(cmds[0].pushOpacity.alpha, 0.5f);
  CHECK(cmds[1].op == PaintOp::DrawRect);
  CHECK_EQ(cmds[1].drawRect.rect, Rect::fromLTWH(0, 0, 10, 10));
  CHECK(cmds[2].op == PaintOp::Pop);
}

TEST(displaylist_unbalanced_push_is_a_contract_violation) {
  DisplayList dl;
  dl.beginRecording();
  dl.pushOpacity(1.0f);
  CHECK_THROWS(dl.endRecording());

  DisplayList dl2;
  dl2.beginRecording();
  CHECK_THROWS(dl2.pop());
}

TEST(displaylist_revision_advances_only_on_rerecord) {
  DisplayList dl;
  dl.beginRecording();
  dl.drawRect(Rect::fromLTWH(0, 0, 1, 1), Color{});
  dl.endRecording();
  const std::uint64_t r0 = dl.revision();

  // Reading does not change the revision, so a backend keyed on it can cache.
  (void)dl.commands();
  (void)dl.size();
  CHECK_EQ(dl.revision(), r0);

  dl.beginRecording();
  dl.endRecording();
  CHECK_EQ(dl.revision(), r0 + 1);
}

TEST(displaylist_rerecord_does_not_reallocate) {
  DisplayList dl;
  for (int i = 0; i < 64; ++i) {
    dl.beginRecording();
    for (int j = 0; j < 32; ++j) dl.drawRect(Rect::fromLTWH(0, 0, 1, 1), Color{});
    dl.endRecording();
  }
  const auto* firstCmd = dl.commands().data();
  dl.beginRecording();
  for (int j = 0; j < 32; ++j) dl.drawRect(Rect::fromLTWH(0, 0, 1, 1), Color{});
  dl.endRecording();
  // Capacity is retained across recordings: steady-state painting allocates nothing.
  CHECK_EQ(dl.commands().data(), firstCmd);
}

TEST(displaylist_sublist_reference_is_the_repaint_boundary_seam) {
  DisplayList child;
  child.beginRecording();
  child.drawRect(Rect::fromLTWH(0, 0, 5, 5), Color::argb(0xFF00FF00));
  child.endRecording();

  DisplayList root;
  root.beginRecording();
  root.drawList(&child, {10, 20});
  root.endRecording();

  CHECK_EQ(root.commands().size(), std::size_t{1});
  CHECK(root.commands()[0].op == PaintOp::DrawList);
  CHECK_EQ(root.commands()[0].drawList.list, &child);
  CHECK_EQ(root.commands()[0].drawList.offset, (Offset{10, 20}));

  // Re-recording the child leaves the root untouched: only the boundary's own
  // revision moves, which is exactly what makes a partial repaint cheap.
  const std::uint64_t rootRev = root.revision();
  child.beginRecording();
  child.drawRect(Rect::fromLTWH(0, 0, 5, 5), Color::argb(0xFF0000FF));
  child.endRecording();
  CHECK_EQ(root.revision(), rootRev);
  CHECK_EQ(root.commands()[0].drawList.list, &child);
}

// ---------------------------------------------------------------------------
// Text service boundary
// ---------------------------------------------------------------------------

namespace {
ParagraphSpec specFor(const TextSpan* spans, std::size_t n, TextAlign align = TextAlign::Left,
                      int maxLines = 0) {
  ParagraphSpec s;
  s.spans = {spans, n};
  s.align = align;
  s.maxLines = maxLines;
  return s;
}
}  // namespace

TEST(text_single_line_measures_from_constraints) {
  MonospaceTextService svc(0.5f);  // advance = 0.5 * size
  const TextStyle style{.size = 10.0f};
  const TextSpan spans[]{{"hello", style}};

  const ParagraphHandle h = svc.acquire(specFor(spans, 1), kInf);
  const ParagraphMetrics& m = svc.metrics(h);
  CHECK_EQ(m.size.width, 25.0f);  // 5 chars * 5px
  CHECK_EQ(m.size.height, 12.0f);  // size * lineHeight
  CHECK_EQ(m.lines.size(), std::size_t{1});
  CHECK_EQ(m.runs.size(), std::size_t{1});
  svc.release(h);
  CHECK_EQ(svc.liveParagraphs(), std::size_t{0});
}

TEST(text_wraps_greedily_at_available_width) {
  MonospaceTextService svc(0.5f);
  const TextStyle style{.size = 10.0f};
  const TextSpan spans[]{{"alpha beta gamma", style}};

  // 12 columns => "alpha beta" (10 cols) fits, "gamma" wraps.
  const ParagraphHandle h = svc.acquire(specFor(spans, 1), 60.0f);
  const ParagraphMetrics& m = svc.metrics(h);
  CHECK_EQ(m.lines.size(), std::size_t{2});
  CHECK_EQ(m.size.height, 24.0f);
  CHECK_EQ(m.lines[0].bounds.width(), 50.0f);   // "alpha beta"
  CHECK_EQ(m.lines[1].bounds.width(), 25.0f);   // "gamma"
  CHECK_EQ(m.lines[1].bounds.top, 12.0f);
  svc.release(h);
}

TEST(text_hard_break_forces_a_line) {
  MonospaceTextService svc(0.5f);
  const TextStyle style{.size = 10.0f};
  const TextSpan spans[]{{"a\nb", style}};
  const ParagraphHandle h = svc.acquire(specFor(spans, 1), kInf);
  CHECK_EQ(svc.metrics(h).lines.size(), std::size_t{2});
  svc.release(h);
}

TEST(text_multiple_spans_produce_multiple_runs_on_one_line) {
  MonospaceTextService svc(0.5f);
  const TextStyle a{.size = 10.0f};
  const TextStyle b{.size = 20.0f};
  const TextSpan spans[]{{"ab", a}, {"cd", b}};
  const ParagraphHandle h = svc.acquire(specFor(spans, 2), kInf);
  const ParagraphMetrics& m = svc.metrics(h);
  CHECK_EQ(m.lines.size(), std::size_t{1});
  CHECK_EQ(m.runs.size(), std::size_t{2});
  CHECK_EQ(m.runs[0].offset.dx, 0.0f);
  CHECK_EQ(m.runs[0].size.width, 10.0f);  // 2 * 5
  CHECK_EQ(m.runs[1].offset.dx, 10.0f);
  CHECK_EQ(m.runs[1].size.width, 20.0f);  // 2 * 10
  // Line height comes from the tallest run, and the shorter run is bottom-aligned.
  CHECK_EQ(m.size.height, 24.0f);
  CHECK_EQ(m.runs[0].offset.dy, 12.0f);
  CHECK_EQ(m.runs[1].offset.dy, 0.0f);
  svc.release(h);
}

TEST(text_alignment_shifts_runs_within_the_paragraph) {
  MonospaceTextService svc(0.5f);
  const TextStyle style{.size = 10.0f};
  const TextSpan spans[]{{"aaaa\nbb", style}};

  const ParagraphHandle left = svc.acquire(specFor(spans, 1, TextAlign::Left), 100.0f);
  CHECK_EQ(svc.metrics(left).runs[1].offset.dx, 0.0f);
  svc.release(left);

  const ParagraphHandle right = svc.acquire(specFor(spans, 1, TextAlign::Right), 100.0f);
  // Paragraph width is the widest line (20), so the short line shifts by 10.
  CHECK_EQ(svc.metrics(right).runs[1].offset.dx, 10.0f);
  svc.release(right);

  const ParagraphHandle center = svc.acquire(specFor(spans, 1, TextAlign::Center), 100.0f);
  CHECK_EQ(svc.metrics(center).runs[1].offset.dx, 5.0f);
  svc.release(center);
}

TEST(text_max_lines_is_reported) {
  MonospaceTextService svc(0.5f);
  const TextStyle style{.size = 10.0f};
  const TextSpan spans[]{{"one two three four", style}};
  const ParagraphHandle h = svc.acquire(specFor(spans, 1, TextAlign::Left, 2), 30.0f);
  const ParagraphMetrics& m = svc.metrics(h);
  CHECK_EQ(m.lines.size(), std::size_t{2});
  CHECK(m.didExceedMaxLines);
  svc.release(h);
}

TEST(text_byte_offset_hit_testing) {
  MonospaceTextService svc(0.5f);
  const TextStyle style{.size = 10.0f};
  const TextSpan spans[]{{"abcdef", style}};
  const ParagraphHandle h = svc.acquire(specFor(spans, 1), kInf);
  CHECK_EQ(svc.byteOffsetAt(h, {0, 0}), std::uint32_t{0});
  CHECK_EQ(svc.byteOffsetAt(h, {12, 0}), std::uint32_t{2});   // 2.4 chars -> 2
  CHECK_EQ(svc.byteOffsetAt(h, {1000, 0}), std::uint32_t{6}); // past the end clamps
  svc.release(h);
}

TEST(text_handles_are_released_not_leaked) {
  MonospaceTextService svc;
  const TextStyle style{.size = 10.0f};
  const TextSpan spans[]{{"x", style}};
  ParagraphHandle handles[8];
  for (auto& h : handles) h = svc.acquire(specFor(spans, 1), kInf);
  CHECK_EQ(svc.liveParagraphs(), std::size_t{8});
  for (auto h : handles) svc.release(h);
  CHECK_EQ(svc.liveParagraphs(), std::size_t{0});
}
