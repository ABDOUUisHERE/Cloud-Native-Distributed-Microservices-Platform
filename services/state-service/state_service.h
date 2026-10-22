#pragma once

#include "services/state-service/memory_store.h"

namespace taskrelay {
class StateService final : public v1::StateService::Service {
 public:
#define TASKRELAY_STATE_RPC(Name, Request, Response) \
  grpc::Status Name(grpc::ServerContext*, const v1::Request*, v1::Response*) override;
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
  MemoryStore store_;
};
}  // namespace taskrelay
