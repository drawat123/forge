# Forge — SPSC Ring Buffer Project Notes

---

## 1. What is a ring buffer and why does it exist

A ring buffer is a fixed-size array where a producer writes items in at one end and a
consumer reads items out at the other end. When either reaches the end of the array, it
wraps back to the start — like a circular track.

**Why it exists:** It is the fastest possible way to move data between two threads. No heap
allocation per item, no dynamic resizing, no locks. One fixed block of memory, two indices,
and correct memory ordering.

**The SPSC constraint — Single Producer Single Consumer:**
Exactly one thread may write. Exactly one thread may read. This constraint is what makes
lock-free possible. If you need multiple producers or consumers, you need a mutex again.

---

## 2. The two indices and who owns them

```
slots:  [ ][ ][D][D][D][ ][ ][ ]
index:   0  1  2  3  4  5  6  7
                ↑           ↑
               head         tail
          consumer reads   producer writes
```

| Index | Owned by | Written by | Read by |
|---|---|---|---|
| `head_` | Consumer | Consumer only | Producer (to check if full) |
| `tail_` | Producer | Producer only | Consumer (to check if empty) |

Each thread owns one index completely. Neither thread writes to the other's index.
This is the foundation of why no mutex is needed.

---

## 3. The problem — CPUs and compilers reorder instructions

You write code in a specific order. You assume it runs in that order. It does not — always.

Both the compiler and CPU are allowed to reorder instructions for performance, as long as
the result looks correct from the perspective of a single thread. They have no obligation
to consider other threads.

```cpp
// Producer writes:
data   = 42;      // step 1
ready  = true;    // step 2

// CPU may actually execute:
ready  = true;    // step 2 first!
data   = 42;      // step 1 second
```

The consumer might see `ready = true` and read `data` before it contains 42. This happens
on real hardware — especially on ARM processors including Apple Silicon.

Memory ordering on atomics tells the CPU and compiler what reordering restrictions apply.

---

## 4. The three memory orderings

### `memory_order_relaxed`
No restrictions. Just make this one variable's read/write atomic. Everything else can
reorder freely around it.

Use when: the variable does not need to synchronize with any other data. Example: a
diagnostic counter, or reading your own index that only you write.

### `memory_order_release`
Used on a **store** (write). Guarantees: every write done before this store will be
visible to other threads before this store becomes visible.

```cpp
data = 42;
tail_.store(next_tail, std::memory_order_release);
// guaranteed: data = 42 is visible BEFORE tail update is visible
```

### `memory_order_acquire`
Used on a **load** (read). Guarantees: every read done after this load will see all
writes that happened before the matching release store.

```cpp
size_t t = tail_.load(std::memory_order_acquire);
use(data);  // safe — data = 42 is guaranteed visible here
```

### The handshake — release always pairs with acquire

```
Producer thread                  Consumer thread
───────────────                  ───────────────
write data into slot

tail_.store(                →→→  size_t t = tail_.load(
  memory_order_release)            memory_order_acquire)

                                 read data from slot — SAFE
```

The release and acquire form a synchronization point. Everything the producer wrote before
the release is guaranteed visible to the consumer after the acquire.

---

## 5. The memory ordering rule for SPSC

```
Your own index       → load with relaxed   (you own it, always current)
Other thread's index → load with acquire   (you need their latest value)
Storing your index   → store with release  (signal the other thread)
```

Applied:

```
push():
  head_ load  → acquire   producer reading consumer's index
  tail_ load  → relaxed   producer reading its own index
  tail_ store → release   producer signaling consumer

pop():
  tail_ load  → acquire   consumer reading producer's index
  head_ load  → relaxed   consumer reading its own index
  head_ store → release   consumer signaling producer
```

There are two handshakes happening simultaneously in opposite directions:
- Producer releases to consumer via `tail_`
- Consumer releases back to producer via `head_`

---

## 6. Why one slot is always reserved

Without a reserved slot, empty and full look identical:

```
Empty: head == tail == 0   ← both at start
Full:  head == tail == 0   ← wrapped all the way around
```

You cannot tell them apart. The fix — leave one slot permanently empty as a separator:

```
empty → head == tail
full  → (tail + 1) % capacity == head
```

Now they are always distinguishable. The cost is one slot of wasted space — the cheapest
possible tradeoff.

A capacity-8 buffer holds at most 7 items. Always construct with n+1 if you want n usable
slots.

---

## 7. push() and pop() — line by line

### push() — producer side

```cpp
bool RingBuffer<T>::push(const T& item) noexcept {
    // Read consumer's index — acquire so we see their latest head update
    std::size_t curr_head = head_.load(std::memory_order_acquire);

    // Read our own index — relaxed, we own it
    std::size_t curr_tail = tail_.load(std::memory_order_relaxed);
    std::size_t next_tail = (curr_tail + 1) % capacity_;

    // Full check — if next position is where consumer is, no space
    if (next_tail == curr_head) {
        return false;
    }

    // Write data into current slot BEFORE updating tail
    slots_[curr_tail] = item;

    // Release — consumer will see slots_[curr_tail] written before this
    tail_.store(next_tail, std::memory_order_release);

    return true;
}
```

### pop() — consumer side

```cpp
bool RingBuffer<T>::pop(T& out) noexcept {
    // Read producer's index — acquire so we see their latest tail update
    std::size_t curr_tail = tail_.load(std::memory_order_acquire);

    // Read our own index — relaxed, we own it
    std::size_t curr_head = head_.load(std::memory_order_relaxed);

    // Empty check — if we have caught up to producer, nothing to read
    if (curr_head == curr_tail) {
        return false;
    }

    // Read data from current slot
    out = slots_[curr_head];

    std::size_t next_head = (curr_head + 1) % capacity_;

    // Release — producer will see our head update before its next full check
    head_.store(next_head, std::memory_order_release);

    return true;
}
```

---

## 8. False sharing — the hidden performance killer

### What a cache line is

CPUs do not read individual bytes from RAM. They read 64-byte chunks called cache lines.
When you access one byte, the entire 64-byte block containing it is loaded into the CPU's
cache.

### The problem

`head_` is written by the consumer. `tail_` is written by the producer. If they sit next
to each other in memory they land on the same 64-byte cache line.

```
Producer writes tail_ → invalidates consumer's cache line
Consumer must reload  → even though head_ didn't change
Consumer writes head_ → invalidates producer's cache line
Producer must reload  → even though tail_ didn't change
```

Two threads fighting over a cache line they do not logically share — only physically.
This is called **false sharing**. It can slow a lock-free structure to worse than a mutex.

### The fix — `alignas(64)`

```cpp
alignas(64) std::atomic<std::size_t> head_;   // own cache line
alignas(64) std::atomic<std::size_t> tail_;   // own cache line
```

Each index lives on its own dedicated 64-byte cache line. The producer can hammer `tail_`
all day without touching the consumer's cache line.

---

## 9. Why `capacity_` does not need to be atomic

`capacity_` is set once in the constructor and never written again. It is effectively
immutable after construction. Concurrent reads of the same value never conflict — only
concurrent writes, or a write racing with a read, require protection. No atomic needed.

---

## 10. Templates and `.tpp` files

Template implementations cannot live in a `.cpp` file because the compiler needs to see
the full implementation at the point of use to generate a concrete type.

Two options:

**Option 1 — Everything in `.hpp`:** Simple, fine for small templates.

**Option 2 — Split into `.tpp`:**
```
RingBuffer.hpp  ← declaration only
RingBuffer.tpp  ← full implementation
// bottom of RingBuffer.hpp:
#include "RingBuffer.tpp"
```

The `.tpp` extension signals to humans and build systems: "this is not a standalone
translation unit, do not compile it directly." Build systems auto-compile every `.cpp`
they find — naming it `.tpp` prevents that confusion.

---

## 11. Busy spinning and `std::this_thread::yield()`

When `push()` returns false (buffer full) or `pop()` returns false (buffer empty), the
caller must decide what to do. The simplest approach is to spin — try again immediately.

```cpp
while (!ring->push(i)) {
    std::this_thread::yield();  // let OS give time to another thread
}
```

`yield()` is not a sleep. It is a cooperative hint to the OS scheduler: "I have nothing
useful to do right now, give my time slice to someone else." This prevents burning 100%
CPU on an empty spin.

In a real system you would add backpressure instead — signal the producer to slow down
rather than spinning at all.

---

## 12. What lock-free actually means

Lock-free does not mean "no synchronization." It means "no mutual exclusion — no thread
ever has to wait for another thread to release a lock."

```
Mutex-based queue:    threads take turns — one at a time
Lock-free ring buffer: threads never wait — each owns its lane
```

The price of that speed is the SPSC constraint. The moment you need a second producer
or consumer, the guarantee breaks and you need a mutex again.

Lock-free is not always better than mutex-based. It is better when:
- The SPSC constraint is naturally satisfied
- Throughput and latency matter more than flexibility
- You can afford the complexity of reasoning about memory ordering

---

## 13. Bugs to watch for

| Bug | Cause | Fix |
|---|---|---|
| Empty and full indistinguishable | No reserved slot | Always keep one slot empty as separator |
| Wrong empty check in pop() | Checking slot value for null | Check `head == tail` instead |
| Implicit `seq_cst` load | Using atomic in expression directly | Always `.load()` explicitly with correct ordering |
| False sharing | `head_` and `tail_` on same cache line | `alignas(64)` on each index |
| 100% CPU spin | No yield on empty/full | `std::this_thread::yield()` in retry loops |

---

## 14. Concepts to carry into Helios

- Multiple SPSC ring buffers — one per source, feeding into a central dispatcher
- The dispatcher reads from all rings and routes events into the pipeline DAG
- Memory arena — pre-allocated slabs replace heap allocation on the hot path
- Backpressure — when a ring fills up, signal the source to slow down rather than spinning
- The ABA problem — the senior challenge waiting in the arena's lock-free free-list
