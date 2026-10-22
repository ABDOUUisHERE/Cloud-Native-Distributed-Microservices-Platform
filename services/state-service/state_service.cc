#include "services/state-service/state_service.h"

namespace taskrelay {
grpc::Status StateService::CreateTask(grpc::ServerContext* context,
                                      const v1::CreateTaskRequest* request,
                                      v1::CreateTaskResponse* response) {
  return store_.Create(*request, Correlation(*context), response);
}
#define TASKRELAY_STATE_RPC(Name, Method, Request, Response)                        \
  grpc::Status StateService::Name(grpc::ServerContext*, const v1::Request* request, \
                                  v1::Response* response) {                         \
    return store_.Method(*request, response);                                       \
  }
TASKRELAY_STATE_RPC(GetTask, Get, GetTaskRequest, Task)
TASKRELAY_STATE_RPC(ListTasks, List, ListTasksRequest, Page)
TASKRELAY_STATE_RPC(AcquireTask, Acquire, AcquireTaskRequest, AcquireTaskResponse)
TASKRELAY_STATE_RPC(StartTask, Start, StartTaskRequest, Task)
TASKRELAY_STATE_RPC(RenewLease, Renew, RenewLeaseRequest, LeaseExpiry)
TASKRELAY_STATE_RPC(CompleteTask, Complete, CompleteTaskRequest, Task)
TASKRELAY_STATE_RPC(FailAttempt, Fail, FailAttemptRequest, Task)
#undef TASKRELAY_STATE_RPC
}  // namespace taskrelay
