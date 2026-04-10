// ============================================================
//  forge/IThreadPool.h
//
//  The public contract of Forge. Any code that wants to submit
//  work talks to this interface — never to the concrete class.
//
//  WHY an abstract interface for a thread pool?
//  In tests you can swap in a fake pool that runs tasks
//  synchronously on the calling thread. This makes your code
//  testable without real threads.
// ============================================================
#pragma once
#include "ITask.h"
#include <cstddef>

namespace forge {

class IThreadPool {
public:
  virtual ~IThreadPool() = default;

  // Submit a task for execution by a worker thread.
  //
  // This call MUST be thread-safe — multiple callers may
  // call submit() concurrently. That means you need to
  // hold the mutex while pushing onto the queue.
  //
  // After pushing, you must notify a waiting worker via
  // condition_variable::notify_one().
  virtual void submit(Task t) = 0;

  // Begin a graceful shutdown:
  //   1. Set a "stopped" flag so workers know to exit.
  //   2. Wake ALL sleeping workers (notify_all) so they
  //      can see the flag and exit their wait loops.
  //   3. join() every worker thread.
  //
  // Tasks already in the queue SHOULD be drained before
  // threads exit — this is a design decision you must make
  // and document in your implementation.
  virtual void shutdown() noexcept = 0;

  // How many worker threads are alive right now.
  virtual std::size_t thread_count() const noexcept = 0;

  // How many tasks are sitting in the queue, waiting.
  // Note: this is a snapshot — it may be stale by the time
  // the caller reads it. That is acceptable for diagnostics.
  virtual std::size_t pending_count() const noexcept = 0;
};

} // namespace forge