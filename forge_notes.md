# Forge — Thread Pool Project Notes

---

## 1. What is a thread pool and why does it exist

Creating a thread is expensive — it involves a kernel call, stack allocation, and scheduler
registration. A thread pool pays this cost once at startup and reuses those threads forever.

**Core idea:** N worker threads are created once and sit idle. Work is submitted into a shared
queue. Workers wake up, grab a task, run it, go back to sleep.

---

## 2. The three primitives and what each one does

### `std::mutex`
A lock. Only one thread can hold it at a time. Every thread that wants to touch shared data
must acquire the mutex first. Others wait until it is released.

```cpp
std::mutex m;
std::unique_lock<std::mutex> lock(m);  // acquired here
// ... touch shared data ...
// lock released automatically when it goes out of scope
```

### `std::condition_variable`
The sleep/wake mechanism. A worker that finds no work goes to sleep on the cv. A caller that
adds work wakes a sleeping worker via notify.

**Always works as a trio with:**
- a mutex (to protect the shared data)
- a predicate lambda (the condition that must be true before proceeding)

```cpp
// Worker side — sleep until predicate is true
cv.wait(lock, []() { return !queue.empty() || stopped; });

// Caller side — wake one worker
cv.notify_one();

// Shutdown side — wake all workers
cv.notify_all();
```

### `std::atomic<bool>`
Makes a single variable safe to read/write from multiple threads without a mutex.

**Critical distinction:**
- `std::atomic` protects the **data** — the read/write of the variable itself
- `std::mutex` protects the **timing** — the sequence of operations between threads

They solve different problems. You need both.

---

## 3. Why `unique_lock` and not `lock_guard`

`condition_variable::wait()` needs to release the mutex while the thread sleeps and reacquire
it on wakeup. `lock_guard` cannot do this — it holds the lock for its entire lifetime.
`unique_lock` can release and reacquire mid-scope, which is why it is required here.

---

## 4. The worker loop — line by line

```cpp
void ThreadPool::worker_func() {
    while (true) {
        Task task_to_run = nullptr;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // Sleep until queue has work OR shutdown signaled.
            // The predicate guards against spurious wakeups —
            // the OS can wake a thread for no reason. The predicate
            // ensures we only proceed when there is real work.
            cv_.wait(lock, [this]() {
                return !task_queue_.empty() || stopped_;
            });

            // Priority: tasks first, shutdown second.
            // Never abandon work just because stopped is true.
            if (!task_queue_.empty()) {
                task_to_run = task_queue_.front();
                task_queue_.pop();
            } else if (stopped_) {
                break;
            }

        } // ← mutex released HERE, before running the task

        // Task runs outside the mutex.
        // All workers can execute simultaneously at this point.
        if (task_to_run) {
            task_to_run();
        }
    }
}
```

**Why release the mutex before running the task:**
If you ran the task while holding the mutex, every other worker would be blocked waiting.
One worker at a time — completely defeating the purpose of a pool.

---

## 5. The worker's priority order on every wakeup

```
1. Queue has a task?           → pop it, run it, loop back
2. Queue empty AND stopped?    → exit
3. Neither (spurious wakeup)?  → predicate returns false, go back to sleep
```

`stopped_ = true` means "stop when you get a chance" — not "stop immediately."
Workers always honor their work commitment before honoring the shutdown signal.

**Real world analogy:** A chef at closing time does not throw food in the bin and walk out.
The chef finishes the remaining orders, then leaves.

---

## 6. The shutdown sequence — why order matters

```cpp
void ThreadPool::shutdown() noexcept {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stopped_) return;   // guard against double-shutdown
        stopped_ = true;        // set flag INSIDE the lock
    }
    cv_.notify_all();           // wake ALL workers OUTSIDE the lock
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}
```

**Why `stopped_ = true` must be inside the mutex:**

Without the mutex there is a dangerous window:

```
Worker                           Main
─────────────────────────────────────────────
predicate runs → stopped is false
predicate returns false
── tiny gap here ──
                                 stopped_ = true
                                 cv_.notify_all()  ← fires here
                                                   ← worker not sleeping yet!
cv_.wait() → goes to sleep
             misses the notification
             hangs forever
```

With the mutex, this gap is impossible. The worker cannot be between "predicate check" and
"going to sleep" while main holds the mutex. Either:
- Worker already sleeping → `notify_all` wakes it
- Worker not sleeping yet → it cannot call `cv_.wait()` until it gets the mutex,
  by which point `stopped_` is already true → predicate immediately true → never sleeps

**Why `notify_all` is outside the lock:**
Workers need to reacquire the mutex when they wake up. If you notify while holding the lock,
workers wake and immediately block trying to get the mutex.

**Why `notify_all` for shutdown but `notify_one` for new tasks:**
- New task: only one worker needs to handle it. Waking all to fight over one task is wasteful
  — this is called the **thundering herd problem**.
- Shutdown: every worker needs to see `stopped_ = true` and exit. All must be woken.

---

## 7. Shared mutable state — the rule

```
Shared + mutable   → needs a mutex
Shared + immutable → safe, no mutex needed
Not shared         → safe, no mutex needed
```

**Examples:**
- `task_queue_` — shared and mutable → protected by `queue_mutex_`
- `std::cout` — shared and mutable → protected by `cout_mutex`
- A local variable inside a task → not shared → no mutex needed
- A `const` lookup table read by all workers → shared but immutable → no mutex needed

**Design principle:** Shared resources should own their own protection internally.
Callers should not have to remember to lock before every use. The mutex is an implementation
detail, not the caller's responsibility. This is why `queue_mutex_` is private.

---

## 8. Non-determinism — why output order is unpredictable

When you spawn N threads, the OS schedules them based on CPU load, priorities, and what else
is running. You have zero control over which thread runs first.

This is not a bug — it is the fundamental nature of threads. The same program produces
different output order on different runs. This is called **non-determinism**.

**What makes it dangerous:** If the *print order* is unpredictable, so is the order of writes
to shared variables. Two threads writing to the same memory without a mutex can corrupt it.
A mutex forces ordering — only one thread proceeds at a time.

---

## 9. Capturing loop variables in lambdas

```cpp
// SAFE — capture by value, each task gets its own copy of i
task_queue_.push([i]() { std::cout << "Task " << i << "\n"; });

// DANGEROUS — capture by reference, all tasks share the same i
// By the time they run, the loop may have incremented i to 21
task_queue_.push([&i]() { std::cout << "Task " << i << "\n"; });
```

**Rule:** If a thread's lifetime can outlast the variable it references, always capture by value.

---

## 10. `mutable` on a mutex in a `const` method

A `const` method promises not to modify the object. But locking a mutex modifies it.
The `mutable` keyword tells the compiler "this member is an exception to const — it can
be modified even in a const method."

```cpp
// In the header:
mutable std::mutex queue_mutex_;

// In the implementation — compiles correctly despite being const:
std::size_t ThreadPool::pending_count() const noexcept {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return task_queue_.size();
}
```

---

## 11. Member variable declaration order matters

Members are initialized in the order they are **declared in the header**, not the order they
appear in the constructor initializer list. Declare members in the order they must be ready:

```cpp
// Correct order — flag and mutex ready before workers start
std::atomic<bool> stopped_;
mutable std::mutex queue_mutex_;
std::condition_variable cv_;
std::queue<Task> task_queue_;
std::vector<std::thread> workers_;  // workers start last
```

If `workers_` were declared first, threads could start running before `queue_mutex_` and
`stopped_` are initialized — undefined behavior.

---

## 12. Why the interface exists (`IThreadPool`)

The concrete `ThreadPool` class is hidden behind an abstract interface. Callers use
`IThreadPool*` and never see the implementation.

```cpp
std::unique_ptr<forge::IThreadPool> pool = std::make_unique<forge::ThreadPool>(4);
pool->submit(my_task);
```

**Two benefits:**
1. In tests you can swap in a `FakeThreadPool` that runs tasks synchronously — no real threads,
   fully deterministic, easy to debug.
2. If you later replace the implementation (different scheduling algorithm, work stealing),
   none of the calling code changes.

---

## 13. Naming conventions used in this project

| Thing | Convention | Example |
|---|---|---|
| Methods | `snake_case` | `thread_count()`, `worker_func()` |
| Member variables | `snake_case` with trailing `_` | `task_queue_`, `stopped_` |
| Local variables | `snake_case`, no underscore | `task_to_run`, `lock` |
| Types / classes | `PascalCase` | `ThreadPool`, `Task` |

The trailing underscore on members instantly signals "this is a member, not a local."
You never need to scroll up to check.

---

## 14. Bugs hit and fixed during development

| Bug | Cause | Fix |
|---|---|---|
| Tasks dropped silently | `stopped` checked before queue, exited early | Check queue first, exit only when queue also empty |
| Garbled output | Two workers writing to `cout` simultaneously | `cout_mutex` protecting all writes |
| Missed wakeup / hang | `stopped = true` set outside mutex, notification fired before worker slept | Set `stopped` inside mutex |
| Double-shutdown undefined behavior | `shutdown()` called twice, join on already-joined thread | Guard with `if (stopped_) return` inside mutex |
| `pending_count()` data race | Reading `task_queue_.size()` without holding mutex | Lock `queue_mutex_` first; mark mutex `mutable` |

---

## 15. Pending implementation

```cpp
// ============================================================
//  forge/IWorkerObserver.hpp
//
//  Optional diagnostic hook. If you attach one of these to
//  the pool, it gets called at key points in a worker's life.
//
//  WHY a separate observer interface?
//  Mixing logging/telemetry into the pool's core logic makes
//  it harder to read and test. The observer separates concerns.
//
//  All methods are called FROM a worker thread — keep them
//  fast and non-blocking. Writing to stdout is fine for now.
// ============================================================
#pragma once
#include <cstddef>

namespace forge {

class IWorkerObserver {
public:
    virtual ~IWorkerObserver() = default;

    // Called when a worker thread starts up and enters its loop.
    virtual void on_worker_start(std::size_t worker_id) noexcept = 0;

    // Called just before a worker picks up and runs a task.
    virtual void on_task_begin(std::size_t worker_id) noexcept = 0;

    // Called immediately after a worker finishes a task.
    virtual void on_task_end(std::size_t worker_id) noexcept = 0;

    // Called when a worker exits its loop and is about to terminate.
    virtual void on_worker_stop(std::size_t worker_id) noexcept = 0;
};

} // namespace forge
```

---

## 15. Concepts to carry forward into Project 2

- Lock-free data structures — no mutex at all, using `std::atomic` with explicit memory ordering
- `memory_order_acquire` / `memory_order_release` — why `seq_cst` everywhere is wrong
- SPSC ring buffer — single producer, single consumer, two atomic indices
- False sharing — why the two indices need to be on separate cache lines (`alignas(64)`)
