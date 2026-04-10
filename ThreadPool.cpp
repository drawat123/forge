#include "ThreadPool.h"

namespace forge {

ThreadPool::ThreadPool(std::size_t n_threads) : stopped_(false) {
  for (size_t i = 0; i < n_threads; i++) {
    workers_.emplace_back([this]() { this->worker_func(); });
  }
}

ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::submit(Task t) {
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    task_queue_.push(std::move(t));
  }
  cv_.notify_one();
}

void ThreadPool::shutdown() noexcept {
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (stopped_) {
      return;
    }
    stopped_ = true;
  }

  cv_.notify_all();

  for (auto &w : workers_) {
    w.join();
  }
}

std::size_t ThreadPool::thread_count() const noexcept {
  return workers_.size();
}

std::size_t ThreadPool::pending_count() const noexcept {
  std::unique_lock<std::mutex> lock(queue_mutex_);
  return task_queue_.size();
}

void ThreadPool::worker_func() {
  while (true) {
    Task task_to_run = nullptr;

    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      cv_.wait(lock, [this]() {
        return !task_queue_.empty() || stopped_;
      }); // predicate needs to be true to exit wait

      if (!task_queue_.empty()) {
        task_to_run = task_queue_.front();
        task_queue_.pop();
      } else if (stopped_) {
        break;
      }
    }

    if (task_to_run) {
      task_to_run();
    }
  }
}

} // namespace forge