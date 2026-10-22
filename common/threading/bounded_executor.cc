#include "common/threading/bounded_executor.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace taskrelay {
BoundedExecutor::BoundedExecutor(std::size_t threads, std::size_t capacity) : capacity_(capacity) {
  if (threads == 0 || capacity == 0)
    throw std::invalid_argument("Executor limits must be positive");
  // Reserve before starting threads so vector allocation cannot strand waiting threads.
  threads_.reserve(threads);
  try {
    for (std::size_t i = 0; i < threads; ++i) threads_.emplace_back([this] { Run(); });
  } catch (...) {
    Stop();
    throw;
  }
}
BoundedExecutor::~BoundedExecutor() { Stop(); }
bool BoundedExecutor::Submit(std::function<void()> work) {
  std::lock_guard lock(mutex_);
  if (stopping_ || queue_.size() >= capacity_) return false;
  queue_.push_back(std::move(work));
  ready_.notify_one();
  return true;
}
void BoundedExecutor::Stop() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  ready_.notify_all();
  for (auto& thread : threads_)
    if (thread.joinable()) thread.join();
}
void BoundedExecutor::Run() {
  for (;;) {
    std::function<void()> work;
    {
      std::unique_lock lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) return;
      work = std::move(queue_.front());
      queue_.pop_front();
    }
    // Work items own their error reporting; a thrown callback must not terminate the process.
    try {
      work();
    } catch (...) {
      std::cerr << "Executor work item failed\n";
    }
  }
}
}  // namespace taskrelay
