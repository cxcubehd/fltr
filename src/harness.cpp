#include "fltr/harness.hpp"

#include "fltr/debug/format.hpp"
#include "fltr/render/box.hpp"

namespace fltr {

namespace {

void indent(std::string& out, int depth) { out.append(static_cast<std::size_t>(depth) * 2, ' '); }

void dumpNode(std::string& out, const RenderObject& node, int depth, Offset offsetInParent) {
  indent(out, depth);
  out += node.typeName();

  const Rect bounds = node.paintBounds();
  out += " size=" + dbg::str(bounds.size());
  if (offsetInParent != Offset::zero()) out += " at=" + dbg::str(offsetInParent);

  const std::string extra = node.describe();
  if (!extra.empty()) {
    out += ' ';
    out += extra;
  }

  if (node.isRepaintBoundary()) out += " [repaint-boundary]";
  if (node.sizedByParent()) out += " [sized-by-parent]";
  if (node.isRelayoutBoundary()) out += " [relayout-boundary]";
  if (node.needsLayout()) out += " [needs-layout]";
  if (node.needsPaint()) out += " [needs-paint]";
  out += '\n';

  node.visitChildrenWithOffsets(
      [&out, depth](RenderObject& child, Offset at) { dumpNode(out, child, depth + 1, at); });
}

void dumpCommands(std::string& out, const DisplayList& list, int depth) {
  for (const PaintCmd& cmd : list.commands()) {
    if (cmd.op == PaintOp::Pop) depth = depth > 0 ? depth - 1 : 0;
    indent(out, depth);
    out += toString(cmd.op);
    switch (cmd.op) {
      case PaintOp::PushTransform:
        out += ' ' + dbg::str(cmd.pushTransform.transform);
        ++depth;
        break;
      case PaintOp::PushClipRect:
        out += ' ' + dbg::str(cmd.pushClipRect.rect);
        if (!cmd.pushClipRect.radius.isZero()) out += " r=" + dbg::str(cmd.pushClipRect.radius);
        ++depth;
        break;
      case PaintOp::PushOpacity:
        out += ' ' + dbg::str(cmd.pushOpacity.alpha);
        ++depth;
        break;
      case PaintOp::Pop: break;
      case PaintOp::DrawRect:
        out += ' ' + dbg::str(cmd.drawRect.rect) + ' ' + dbg::str(cmd.drawRect.color);
        break;
      case PaintOp::DrawRRect:
        out += ' ' + dbg::str(cmd.drawRRect.rect) + ' ' + dbg::str(cmd.drawRRect.fill);
        if (!cmd.drawRRect.radius.isZero()) out += " r=" + dbg::str(cmd.drawRRect.radius);
        if (cmd.drawRRect.borderWidth > 0.0f) {
          out += " border=" + dbg::str(cmd.drawRRect.border) + '/' +
                 dbg::str(cmd.drawRRect.borderWidth);
        }
        break;
      case PaintOp::DrawImage:
        out += ' ' + dbg::str(cmd.drawImage.dst) + " image=" +
               std::to_string(cmd.drawImage.image);
        if (cmd.drawImage.tint != Color{255, 255, 255, 255}) {
          out += " tint=" + dbg::str(cmd.drawImage.tint);
        }
        break;
      case PaintOp::DrawParagraph:
        out += " #" + std::to_string(cmd.drawParagraph.paragraph) + " at=" +
               dbg::str(cmd.drawParagraph.offset) + ' ' + dbg::str(cmd.drawParagraph.tint);
        break;
      case PaintOp::DrawList: {
        out += " at=" + dbg::str(cmd.drawList.offset) + " rev=" +
               std::to_string(cmd.drawList.list->revision());
        out += '\n';
        dumpCommands(out, *cmd.drawList.list, depth + 1);
        continue;  // newline already emitted
      }
    }
    out += '\n';
  }
}

}  // namespace

std::string dumpRenderTree(const RenderObject& root) {
  std::string out;
  dumpNode(out, root, 0, Offset::zero());
  return out;
}

std::string dumpDisplayList(const DisplayList& list) {
  std::string out;
  dumpCommands(out, list, 0);
  return out;
}

std::string dumpScene(const Scene& scene) {
  std::string out = "Scene surface=" + dbg::str(scene.surface) +
                    " revision=" + std::to_string(scene.revision) + "\n";
  if (scene.root) dumpCommands(out, *scene.root, 1);
  return out;
}

}  // namespace fltr
