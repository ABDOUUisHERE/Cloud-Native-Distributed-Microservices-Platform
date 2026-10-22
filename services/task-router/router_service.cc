#include "services/task-router/router_service.h"
namespace taskrelay {
grpc::Status RouterService::SubmitTask(grpc::ServerContext* context,
                                       const v1::SubmitTaskRequest* request,
                                       v1::SubmitTaskResponse* response) {
  v1::CreateTaskRequest create;
  *create.mutable_spec() = request->spec();
  create.set_idempotency_key(request->idempotency_key());
  v1::CreateTaskResponse stored;
  auto status = state_.CreateTask(create, &stored, context->deadline(), Correlation(*context));
  if (status.ok()) {
    *response->mutable_task() = stored.task();
    response->set_replayed(stored.replayed());
  }
  return status;
}
}  // namespace taskrelay
