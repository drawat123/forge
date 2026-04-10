// ============================================================
//  forge/ITask.h
//
//  A Task is the unit of work you hand to the thread pool.
//  We use std::function<void()> directly rather than a virtual
//  class here — it's simpler and idiomatic for a task queue.
//  You will alias this in your implementation file.
//
//  WHY std::function?
//  It can wrap a free function, a lambda, or a bound member
//  function — all without the caller knowing which one.
//  Example usage:
//
//    pool.submit([]{ std::cout << "hello from worker\n"; });
//
// ============================================================

#pragma once

#include <functional>

namespace forge {

using Task = std::function<void()>;

}