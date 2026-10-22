#pragma once

#include <grpcpp/server_context.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "common/grpc/deadline.h"
#include "taskrelay/v1/state.grpc.pb.h"

namespace taskrelay {
using TimePoint = std::chrono::system_clock::time_point;
google::protobuf::Timestamp ToTimestamp(TimePoint time);
TimePoint FromTimestamp(const google::protobuf::Timestamp& time);
std::string Correlation(const grpc::ServerContext& context);
void AddCorrelation(grpc::ClientContext& context, const std::string& id);

class StateClient {
 public:
  explicit StateClient(const std::string& target);
  explicit StateClient(std::shared_ptr<grpc::Channel> channel);
#define TASKRELAY_STATE_RPC(Name, Request, Response)                                      \
  grpc::Status Name(const v1::Request& request, v1::Response* response, TimePoint parent, \
                    const std::string& correlation = "") const;
  TASKRELAY_STATE_RPC(CreateTask, CreateTaskRequest, CreateTaskResponse)
  TASKRELAY_STATE_RPC(GetTask, GetTaskRequest, Task)
  TASKRELAY_STATE_RPC(ListTasks, ListTasksRequest, Page)
  TASKRELAY_STATE_RPC(AcquireTask, AcquireTaskRequest, AcquireTaskResponse)
  TASKRELAY_STATE_RPC(StartTask, StartTaskRequest, Task)
  TASKRELAY_STATE_RPC(RenewLease, RenewLeaseRequest, LeaseExpiry)
  TASKRELAY_STATE_RPC(CompleteTask, CompleteTaskRequest, Task)
  TASKRELAY_STATE_RPC(FailAttempt, FailAttemptRequest, Task)
#undef TASKRELAY_STATE_RPC
 private:
  std::unique_ptr<v1::StateService::Stub> stub_;
};

int RunGrpcServer(const std::string& address, grpc::Service* service,
                  const std::string& health_name, const std::function<void()>& stop = {});
void BlockStopSignals();
}  // namespace taskrelay
