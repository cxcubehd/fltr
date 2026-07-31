#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace fltrdemo {

/// Storage for text that is computed rather than stored: a ping, a slider
/// readout, a player count.
///
/// `Text::Args::text` is a `std::string_view` that is *not* copied at the call
/// site, so a formatted value needs storage that outlives the build. It does
/// not need to outlive anything more than that: `RenderParagraph` copies into a
/// `std::string` when it adopts the text, and an element that does not rebuild
/// never reads the view it stored.
///
/// So the arena is reset once per frame, before the frame's build can run, and
/// the demo's answer to dynamic text is exactly two rules:
///
///   - text that belongs to the model is a `std::string` the model owns;
///   - text that is formatted for display goes here.
///
/// No `std::to_string` in a build method. Blocks are never reallocated, so a
/// view handed out earlier in the same frame stays valid however much is
/// formatted after it.
class FrameStrings {
public:
  /// printf-style, because that is what formatting a float to one decimal place
  /// costs least here -- and because the compiler checks the format.
#if defined(__GNUC__) || defined(__clang__)
  __attribute__((format(printf, 2, 3)))
#endif
  std::string_view format(const char* fmt, ...);

  /// Resets to empty, keeping every block. Called at the top of the frame, the
  /// one point where nothing can still be holding a view.
  void reset() noexcept;

  /// Bytes handed out in the frame that used the most. Reported in the debug
  /// overlay, because an arena whose high-water mark is unknown is a leak
  /// waiting to be discovered in a profiler.
  std::size_t highWater() const noexcept { return highWater_; }

private:
  static constexpr std::size_t kBlockSize = 4096;

  char* allocate(std::size_t bytes);

  std::vector<std::unique_ptr<char[]>> blocks_;
  std::size_t block_ = 0;
  std::size_t used_ = 0;
  std::size_t highWater_ = 0;
};

}  // namespace fltrdemo
