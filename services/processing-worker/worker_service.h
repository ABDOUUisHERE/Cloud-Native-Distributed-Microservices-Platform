#pragma once
#include <mutex>
#include <set>
#include <unordered_map>

#include "common/grpc/state_client.h"
#include "common/threading/bounded_executor.h"
#include "taskrelay/v1/worker.grpc.pb.h"
namespace taskrelay {
class WorkerService final : public v1::WorkerService::Service {
 public:
  explicit WorkerService(const std::string& state_target);
  grpc::Status ExecuteTask(grpc::ServerContext*, const v1::ExecuteTaskRequest*,
                           v1::ExecuteTaskResponse*) override;

 private:
  const std::string worker_id_;
  StateClient state_;
  BoundedExecutor executor_{2, 8};
  std::mutex mutex_;
  std::set<std::string> active_;
  std::unordered_map<std::string, TimePoint> seen_;
};
}  // namespace taskrelay
