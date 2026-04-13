#include "RingBuffer.h"
#include "ThreadPool.h"
#include <iostream>
#include <memory>

int main() {
  // std::unique_ptr<forge::IThreadPool> thread_pool =
  //     std::make_unique<forge::ThreadPool>(4);

  std::mutex cout_mutex;

  // for (int i = 1; i <= 20; i++) {
  //   thread_pool->submit([i, &cout_mutex]() {
  //     std::unique_lock<std::mutex> lock(cout_mutex);
  //     std::cout << "Task " << i << ", " << std::this_thread::get_id() <<
  //     "\n";
  //   });
  // }

  // thread_pool->shutdown();

  std::unique_ptr<forge::IRingBuffer<int>> ring =
      std::make_unique<forge::RingBuffer<int>>(4);

  std::thread producer([&ring, &cout_mutex]() {
    for (int i = 1; i <= 20;) {
      if (ring->push(i)) {
        {
          std::unique_lock<std::mutex> lock(cout_mutex);
          std::cout << "Produced: " << i << "\n";
        }
        i++;
      } else {
        std::this_thread::yield(); // hint to OS: let another thread run
      }
    }
  });

  std::thread consumer([&ring, &cout_mutex]() {
    int items_received = 0;
    while (items_received < 20) {
      int val;
      if (ring->pop(val)) {
        {
          std::unique_lock<std::mutex> lock(cout_mutex);
          std::cout << "Consumed: " << val << "\n";
        }
        items_received++;
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  return 0;
}

// Run using:
// g++ -std=c++17 main.cpp ThreadPool.cpp -o forge
// ./forge