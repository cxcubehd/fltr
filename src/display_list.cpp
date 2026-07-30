#include "fltr/paint/display_list.hpp"

namespace fltr {

const char* toString(PaintOp op) noexcept {
  switch (op) {
    case PaintOp::PushTransform: return "PushTransform";
    case PaintOp::PushClipRect: return "PushClipRect";
    case PaintOp::PushOpacity: return "PushOpacity";
    case PaintOp::Pop: return "Pop";
    case PaintOp::DrawRect: return "DrawRect";
    case PaintOp::DrawRRect: return "DrawRRect";
    case PaintOp::DrawImage: return "DrawImage";
    case PaintOp::DrawParagraph: return "DrawParagraph";
    case PaintOp::DrawList: return "DrawList";
  }
  return "?";
}

}  // namespace fltr
