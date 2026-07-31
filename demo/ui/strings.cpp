#include "ui/strings.hpp"

#include <cstdarg>
#include <cstdio>
#include <algorithm>

#include "fltr/core/config.hpp"

namespace fltrdemo {

char* FrameStrings::allocate(std::size_t bytes) {
  FLTR_EXPECTS(bytes <= kBlockSize, "a formatted string longer than one block");
  if (block_ >= blocks_.size()) {
    blocks_.push_back(std::make_unique<char[]>(kBlockSize));
  } else if (used_ + bytes > kBlockSize) {
    ++block_;
    used_ = 0;
    if (block_ >= blocks_.size()) blocks_.push_back(std::make_unique<char[]>(kBlockSize));
  }
  char* out = blocks_[block_].get() + used_;
  used_ += bytes;
  highWater_ = std::max(highWater_, block_ * kBlockSize + used_);
  return out;
}

std::string_view FrameStrings::format(const char* fmt, ...) {
  char scratch[512];
  std::va_list args;
  va_start(args, fmt);
  const int written = std::vsnprintf(scratch, sizeof(scratch), fmt, args);
  va_end(args);
  if (written <= 0) return {};

  const auto length = std::min(static_cast<std::size_t>(written), sizeof(scratch) - 1);
  char* out = allocate(length);
  std::copy_n(scratch, length, out);
  return {out, length};
}

void FrameStrings::reset() noexcept {
  block_ = 0;
  used_ = 0;
}

}  // namespace fltrdemo
