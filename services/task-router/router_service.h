#pragma once
#include "common/grpc/state_client.h"
#include "taskrelay/v1/router.grpc.pb.h"
namespace taskrelay {
class RouterService final : public v1::TaskRouterService::Service {
 public:
  explicit RouterService(const std::string& state_target) : state_(state_target) {}
  grpc::Status SubmitTask(grpc::ServerContext*, const v1::SubmitTaskRequest*,
                          v1::SubmitTaskResponse*) override;

 private:
  StateClient state_;
};
}  // namespace taskrelay
