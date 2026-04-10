#include "ThreadPool.h"
#include <iostream>
#include <memory>

int main() {
  std::unique_ptr<forge::IThreadPool> thread_pool =
      std::make_unique<forge::ThreadPool>(4);

  std::mutex cout_mutex;

  for (int i = 1; i <= 20; i++) {
    thread_pool->submit([i, &cout_mutex]() {
      std::unique_lock<std::mutex> lock(cout_mutex);
      std::cout << "Task " << i << ", " << std::this_thread::get_id() << "\n";
    });
  }

  thread_pool->shutdown();

  return 0;
}

// Run using:
// g++ main.cpp ThreadPool.cpp -o forge
// ./forge