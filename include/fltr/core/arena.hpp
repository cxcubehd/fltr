#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include "fltr/core/config.hpp"

namespace fltr {

/// A bump allocator for objects with a bounded, uniform lifetime.
///
/// Widget configurations live here. They are created during a build phase and
/// released wholesale when it ends; `reset()` rewinds to the first chunk and
/// bumps a generation counter, which is a pointer store plus an increment. The
/// chunks themselves are retained, so the steady state performs no allocation.
///
/// Trivially destructible types are the intended case and go through
/// `create()`. A type that genuinely owns heap memory must be created with
/// `createOwning()`, which registers a destructor to run at reset. That call is
/// deliberately more verbose than the common path: owning widgets are an
/// exception and should be visible as one.
class Arena {
public:
  explicit Arena(std::size_t firstChunkBytes = 64 * 1024) : firstChunk_(firstChunkBytes) {}

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  ~Arena() { runDestructors(); }

  void* allocate(std::size_t bytes, std::size_t align) {
    std::uintptr_t p = reinterpret_cast<std::uintptr_t>(cur_);
    const std::uintptr_t aligned = (p + align - 1) & ~(static_cast<std::uintptr_t>(align) - 1);
    if (aligned + bytes > reinterpret_cast<std::uintptr_t>(end_)) {
      grow(bytes, align);
      p = reinterpret_cast<std::uintptr_t>(cur_);
      const std::uintptr_t a2 = (p + align - 1) & ~(static_cast<std::uintptr_t>(align) - 1);
      cur_ = reinterpret_cast<std::byte*>(a2 + bytes);
      bytesUsed_ += bytes;
      return reinterpret_cast<void*>(a2);
    }
    cur_ = reinterpret_cast<std::byte*>(aligned + bytes);
    bytesUsed_ += bytes;
    return reinterpret_cast<void*>(aligned);
  }

  /// The common path. Statically rejects anything needing destruction.
  template <class T, class... Args>
  T* create(Args&&... args) {
    static_assert(std::is_trivially_destructible_v<T>,
                  "Arena::create requires a trivially destructible type. A widget that owns heap "
                  "memory must use Arena::createOwning, which is deliberately more verbose.");
    void* mem = allocate(sizeof(T), alignof(T));
    return new (mem) T(std::forward<Args>(args)...);
  }

  /// The explicit exception. Registers a destructor to run at `reset()`.
  template <class T, class... Args>
  T* createOwning(Args&&... args) {
    void* mem = allocate(sizeof(T), alignof(T));
    T* obj = new (mem) T(std::forward<Args>(args)...);
    if constexpr (!std::is_trivially_destructible_v<T>) {
      destructors_.push_back({obj, [](void* p) { static_cast<T*>(p)->~T(); }});
    }
    return obj;
  }

  /// Copies `n` elements into arena storage and returns a pointer to the first.
  template <class T>
  T* createArray(const T* src, std::size_t n) {
    static_assert(std::is_trivially_destructible_v<T>);
    if (n == 0) return nullptr;
    void* mem = allocate(sizeof(T) * n, alignof(T));
    T* dst = static_cast<T*>(mem);
    for (std::size_t i = 0; i < n; ++i) new (dst + i) T(src[i]);
    return dst;
  }

  /// Releases everything allocated since the last reset and invalidates every
  /// pointer previously handed out. Retains the chunks.
  void reset() {
    runDestructors();
    chunkIndex_ = 0;
    if (!chunks_.empty()) {
      cur_ = chunks_[0].mem.get();
      end_ = cur_ + chunks_[0].size;
    } else {
      cur_ = end_ = nullptr;
    }
    ++generation_;
    highWater_ = highWater_ > bytesUsed_ ? highWater_ : bytesUsed_;
    bytesUsed_ = 0;
  }

  /// Incremented by every reset. A pointer handed out during generation N is
  /// dangling from generation N+1 onwards; WidgetRef uses this to catch stale
  /// dereferences in checked builds.
  std::uint32_t generation() const noexcept { return generation_; }
  std::size_t bytesUsed() const noexcept { return bytesUsed_; }
  std::size_t highWaterMark() const noexcept { return highWater_ > bytesUsed_ ? highWater_ : bytesUsed_; }
  std::size_t chunkCount() const noexcept { return chunks_.size(); }
  /// Total bytes ever requested from the system allocator.
  std::size_t reservedBytes() const noexcept {
    std::size_t t = 0;
    for (const auto& c : chunks_) t += c.size;
    return t;
  }

private:
  struct Chunk {
    std::unique_ptr<std::byte[]> mem;
    std::size_t size;
  };
  struct Dtor {
    void* obj;
    void (*fn)(void*);
  };

  void grow(std::size_t bytes, std::size_t align) {
    // Try the next already-allocated chunk before asking the system for more.
    while (chunkIndex_ + 1 < chunks_.size()) {
      ++chunkIndex_;
      Chunk& c = chunks_[chunkIndex_];
      if (c.size >= bytes + align) {
        cur_ = c.mem.get();
        end_ = cur_ + c.size;
        return;
      }
    }
    std::size_t size = chunks_.empty() ? firstChunk_ : chunks_.back().size * 2;
    while (size < bytes + align) size *= 2;
    chunks_.push_back({std::make_unique<std::byte[]>(size), size});
    chunkIndex_ = chunks_.size() - 1;
    cur_ = chunks_.back().mem.get();
    end_ = cur_ + size;
  }

  void runDestructors() {
    for (auto it = destructors_.rbegin(); it != destructors_.rend(); ++it) it->fn(it->obj);
    destructors_.clear();
  }

  std::vector<Chunk> chunks_;
  std::vector<Dtor> destructors_;
  std::size_t firstChunk_;
  std::size_t chunkIndex_ = 0;
  std::byte* cur_ = nullptr;
  std::byte* end_ = nullptr;
  std::uint32_t generation_ = 1;
  std::size_t bytesUsed_ = 0;
  std::size_t highWater_ = 0;
};

}  // namespace fltr
