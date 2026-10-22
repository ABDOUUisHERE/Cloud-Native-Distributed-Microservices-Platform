#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace taskrelay {
class BoundedExecutor {
 public:
  explicit BoundedExecutor(std::size_t threads, std::size_t capacity);
  ~BoundedExecutor();
  BoundedExecutor(const BoundedExecutor&) = delete;
  BoundedExecutor& operator=(const BoundedExecutor&) = delete;
  bool Submit(std::function<void()> work);
  void Stop();

 private:
  void Run();
  const std::size_t capacity_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<std::function<void()>> queue_;
  bool stopping_ = false;
  std::vector<std::jthread> threads_;
};
}  // namespace taskrelay
