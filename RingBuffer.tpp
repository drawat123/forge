#pragma once

#include "RingBuffer.h"

namespace forge {

template <typename T>
RingBuffer<T>::RingBuffer(std::size_t n)
    : slots_(n), head_(0), tail_(0), capacity_(n) {}

// Your own index  → load with relaxed  (you own it, always current)
// Other thread's index → load with acquire (you need their latest value)
// Storing your index   → store with release (signal the other thread)
template <typename T> bool RingBuffer<T>::push(const T &item) noexcept {
  std::size_t curr_head = head_.load(std::memory_order_acquire);

  std::size_t curr_tail = tail_.load(std::memory_order_relaxed);
  std::size_t next_tail = (curr_tail + 1) % capacity_;

  if (next_tail == curr_head) {
    return false;
  }

  slots_[curr_tail] = item;

  tail_.store(next_tail, std::memory_order_release);

  return true;
}

template <typename T> inline bool RingBuffer<T>::pop(T &out) noexcept {
  std::size_t curr_tail = tail_.load(std::memory_order_acquire);

  std::size_t curr_head = head_.load(std::memory_order_relaxed);
  std::size_t next_head = (curr_head + 1) % capacity_;

  if (curr_head == curr_tail) {
    return false;
  }

  out = slots_[curr_head];

  head_.store(next_head, std::memory_order_release);

  return true;
}

template <typename T> inline bool RingBuffer<T>::empty() const noexcept {
  std::size_t curr_head = head_.load(std::memory_order_relaxed);
  std::size_t curr_tail = tail_.load(std::memory_order_relaxed);

  return curr_head == curr_tail;
}

template <typename T> inline bool RingBuffer<T>::full() const noexcept {
  std::size_t curr_head = head_.load(std::memory_order_relaxed);
  std::size_t curr_tail = tail_.load(std::memory_order_relaxed);

  return ((curr_tail + 1) % capacity_) == curr_head;
}

template <typename T>
inline std::size_t RingBuffer<T>::capacity() const noexcept {
  return capacity_;
}

} // namespace forge
