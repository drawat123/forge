// ============================================================
//  forge/RingBuffer.h
//
//  Concrete implementation — skeleton only.
//  Fill in the private members and method bodies yourself.
// ============================================================
#pragma once
#include "IRingBuffer.h"
#include <atomic>
#include <vector>

namespace forge {

template <typename T> class RingBuffer : public IRingBuffer<T> {
public:
  // Allocates the internal slot array.
  // Actual usable capacity is n - 1 (one slot reserved).
  // The explicit keyword prevents the compiler from performing implicit
  // conversions or copy-initialization.
  explicit RingBuffer(std::size_t n);

  // No copying — copying a live ring buffer makes no sense.
  RingBuffer(const RingBuffer &) = delete;
  RingBuffer &operator=(const RingBuffer &) = delete;

  // Guarantees a function won't throw an exception.
  bool push(const T &item) noexcept override;
  bool pop(T &out) noexcept override;
  bool empty() const noexcept override;
  bool full() const noexcept override;
  std::size_t capacity() const noexcept override;

private:
  std::vector<T> slots_;
  alignas(64) std::atomic<std::size_t> head_;
  alignas(64) std::atomic<std::size_t> tail_;
  std::size_t capacity_;
};

} // namespace forge

#include "RingBuffer.tpp"