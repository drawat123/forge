#pragma once

#include "IThreadPool.h"
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>

namespace forge {

class ThreadPool : public IThreadPool {
public:
  // Creates the pool and spawns n_threads worker threads.
  // Workers should start waiting for tasks immediately.
  explicit ThreadPool(std::size_t n_threads);

  // No copying — a thread pool cannot be duplicated.
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ~ThreadPool();

  void submit(Task t) override;

  void shutdown() noexcept override;

  std::size_t thread_count() const noexcept override;
  std::size_t pending_count() const noexcept override;

private:
  void worker_func();

  std::atomic<bool> stopped_; // initialized first
  mutable std::mutex queue_mutex_;
  std::condition_variable cv_;
  std::queue<Task> task_queue_;
  std::vector<std::thread> workers_; // initialized last — workers start here
};

} // namespace forge
