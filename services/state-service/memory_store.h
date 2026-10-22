#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

#include "common/grpc/state_client.h"

namespace taskrelay {
class MemoryStore {
 public:
  explicit MemoryStore(std::size_t capacity = 1024,
                       std::function<TimePoint()> now = std::chrono::system_clock::now);
  grpc::Status Create(const v1::CreateTaskRequest&, const std::string&, v1::CreateTaskResponse*);
  grpc::Status Get(const v1::GetTaskRequest&, v1::Task*);
  grpc::Status List(const v1::ListTasksRequest&, v1::Page*);
  grpc::Status Acquire(const v1::AcquireTaskRequest&, v1::AcquireTaskResponse*);
  grpc::Status Start(const v1::StartTaskRequest&, v1::Task*);
  grpc::Status Renew(const v1::RenewLeaseRequest&, v1::LeaseExpiry*);
  grpc::Status Complete(const v1::CompleteTaskRequest&, v1::Task*);
  grpc::Status Fail(const v1::FailAttemptRequest&, v1::Task*);

 private:
  struct Entry {
    v1::Task task;
    std::string key;
    std::string canonical;
    std::string token;
    std::string owner;
    std::string router;
    TimePoint lease_expiry{};
    std::uint64_t sequence = 0;
  };
  void Maintain(TimePoint now);
  struct Ownership {
    const std::string& id;
    const std::string& token;
    const std::string& owner;
  };
  grpc::Status FindOwned(const Ownership& ownership, Entry** entry);
  const std::size_t capacity_;
  const std::function<TimePoint()> now_;
  std::mutex mutex_;
  std::unordered_map<std::string, Entry> tasks_;
  std::unordered_map<std::string, std::string> keys_;
  std::map<std::uint64_t, std::string> created_;
  std::uint64_t sequence_ = 0;
};
}  // namespace taskrelay
