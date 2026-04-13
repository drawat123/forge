// ============================================================
//  forge/IRingBuffer.h
//
//  A Single Producer Single Consumer lock-free ring buffer.
//
//  CONTRACT:
//  - Exactly ONE thread may call push() at any time.
//  - Exactly ONE thread may call pop() at any time.
//  - These may be different threads running simultaneously.
//  - Violating the SPSC constraint = undefined behavior.
// ============================================================
#pragma once
#include <cstddef>

namespace forge {

template <typename T> class IRingBuffer {
public:
  virtual ~IRingBuffer() = default;

  // Producer side — called from the producer thread ONLY.
  //
  // Attempts to write one item into the next available slot.
  // Returns true  — item was written successfully.
  // Returns false — buffer is full, item was NOT written.
  //                 Caller must decide what to do (retry,
  //                 drop, apply backpressure).
  //
  // Memory ordering: store to tail_ must be memory_order_release
  // so the consumer sees the data written to the slot BEFORE
  // it sees the tail update.
  // [[nodiscard]] is an attribute introduced in C++17 that tells the compiler
  // to issue a warning if a function's return value is ignored by the caller.
  [[nodiscard]]
  virtual bool push(const T &item) noexcept = 0;

  // Consumer side — called from the consumer thread ONLY.
  //
  // Attempts to read one item from the next available slot.
  // Returns true  — item was read into `out` successfully.
  // Returns false — buffer is empty, `out` is untouched.
  //
  // Memory ordering: load of tail_ must be memory_order_acquire
  // so all writes the producer did before its release are
  // visible here before we read the slot data.
  [[nodiscard]]
  virtual bool pop(T &out) noexcept = 0;

  // Returns true if the buffer has no items to read.
  // NOTE: result may be stale by the time caller reads it.
  // Safe to call from either thread for diagnostics only.
  virtual bool empty() const noexcept = 0;

  // Returns true if the buffer has no space left to write.
  // NOTE: result may be stale by the time caller reads it.
  virtual bool full() const noexcept = 0;

  // The fixed capacity this buffer was constructed with.
  // One slot is always reserved to distinguish full from empty.
  // So a capacity-8 buffer holds at most 7 items.
  virtual std::size_t capacity() const noexcept = 0;
};

} // namespace forge