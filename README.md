# Forge

A lightweight, high-performance, and thread-safe C++ thread pool implementation.

Forge provides a simple and idiomatic way to manage a pool of worker threads and submit tasks for asynchronous execution. It is designed with a focus on clean architecture, using an interface-based approach that makes it easy to test and extend.

## Key Features

- **Interface-Based Design**: The core functionality is defined in the `IThreadPool` interface, allowing for easy mocking and dependency injection.
- **Thread-Safe Task Submission**: Multiple threads can safely submit tasks to the pool concurrently.
- **Graceful Shutdown**: Ensures all pending tasks in the queue are completed before the worker threads terminate.
- **Modern C++ Primitives**: Leverages `std::function`, `std::mutex`, `std::condition_variable`, and `std::atomic` for robust concurrency management.
- **Zero-Cost Abstractions**: Minimal overhead for task submission and worker management.

## Architecture

### `IThreadPool` Interface
The public contract for the thread pool. Any component that needs to submit work interacts with this interface.
- `submit(Task t)`: Adds a task to the queue.
- `shutdown()`: Initiates a graceful shutdown of the pool.
- `thread_count()`: Returns the number of worker threads.
- `pending_count()`: Returns the number of tasks currently waiting in the queue.

### `ThreadPool` Implementation
The concrete implementation of `IThreadPool`. It manages a fixed number of worker threads and a task queue protected by a mutex and condition variable.

### `Task`
A simple alias for `std::function<void()>`, allowing for the submission of free functions, lambdas, or bound member functions.

## Usage

### Simple Example

```cpp
#include "ThreadPool.h"
#include <iostream>
#include <memory>

int main() {
    // Create a pool with 4 worker threads
    std::unique_ptr<forge::IThreadPool> pool = std::make_unique<forge::ThreadPool>(4);

    // Submit tasks
    for (int i = 0; i < 10; ++i) {
        pool->submit([i]() {
            std::cout << "Executing task " << i << " on thread " << std::this_thread::get_id() << "\n";
        });
    }

    // Gracefully shut down the pool
    pool->shutdown();

    return 0;
}
```

## Building

To compile the project and run the example:

```bash
g++ main.cpp ThreadPool.cpp -o forge -pthread
./forge
```

## Design Philosophy

- **RAII**: The `ThreadPool` lifecycle is managed via RAII. The destructor automatically calls `shutdown()` to ensure resources are cleaned up properly.
- **Worker Priority**: During shutdown, workers prioritize draining the remaining tasks in the queue before exiting.
- **Minimized Contention**: The worker loop is designed to release the queue mutex before executing a task, allowing other workers to pick up work simultaneously.
- **Initialization Order**: Member variables are declared in a specific order in the header to ensure they are initialized correctly before worker threads start running.

## Project Structure

- `ITask.h`: Defines the `Task` type.
- `IThreadPool.h`: The abstract interface for the thread pool.
- `ThreadPool.h` / `ThreadPool.cpp`: The concrete implementation.
- `main.cpp`: An example usage and entry point.
- `forge_notes.md`: Detailed engineering notes on the implementation and concurrency primitives.

## Future Enhancements

- **`IWorkerObserver`**: An optional diagnostic hook for monitoring worker lifecycle events (start, task begin/end, stop).
- **Work-Stealing Scheduler**: To improve performance in heterogeneous workloads.
- **Lock-Free Queue**: To further reduce contention in high-throughput scenarios.
