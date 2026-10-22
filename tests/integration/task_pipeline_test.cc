#include <grpcpp/server_builder.h>
#include <gtest/gtest.h>

#include <thread>

#include "common/grpc/health_client.h"
#include "services/processing-worker/worker_service.h"
#include "services/state-service/state_service.h"
#include "services/task-router/dispatcher.h"
#include "services/task-router/router_service.h"
namespace taskrelay {
namespace {
class Server {
 public:
  explicit Server(grpc::Service* service) {
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(service);
    server_ = builder.BuildAndStart();
    if (!server_) throw std::runtime_error("Test server failed");
    target = "127.0.0.1:" + std::to_string(port);
  }
  ~Server() {
    server_->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(1));
    server_->Wait();
  }
  std::string target;

 private:
  std::unique_ptr<grpc::Server> server_;
};
TEST(TaskPipeline, RealGrpcSubmissionExecutesAndPersistsAllThreeWorkloadTypes) {
  StateService state_service;
  Server state_server(&state_service);
  WorkerService worker_service(state_server.target);
  Server worker_server(&worker_service);
  RouterService router_service(state_server.target);
  Server router_server(&router_service);
  Dispatcher dispatcher({.state = state_server.target, .worker = worker_server.target});
  auto router = v1::TaskRouterService::NewStub(MakeWorkerChannel(router_server.target));
  StateClient state(state_server.target);
  for (int type = 1; type <= 3; ++type) {
    v1::SubmitTaskRequest submit;
    submit.set_idempotency_key("pipeline-" + std::to_string(type));
    auto* spec = submit.mutable_spec();
    spec->set_type(static_cast<v1::TaskType>(type));
    spec->mutable_execution_timeout()->set_seconds(2);
    if (type == 1) {
      spec->mutable_numeric()->set_operation(v1::NumericInput::OPERATION_MONTE_CARLO_PI);
      spec->mutable_numeric()->set_iterations(10000);
    } else if (type == 2) {
      spec->mutable_transform()->set_operation(v1::TransformInput::OPERATION_ASCII_UPPERCASE);
      spec->mutable_transform()->set_data("hello");
    } else {
      spec->mutable_network_simulation()->set_packet_count(5);
      spec->mutable_network_simulation()->set_packet_size_bytes(8);
    }
    grpc::ClientContext context;
    SetDeadline(context, std::chrono::milliseconds(3000));
    AddCorrelation(context, "pipeline-correlation");
    context.set_wait_for_ready(true);
    v1::SubmitTaskResponse accepted;
    ASSERT_TRUE(router->SubmitTask(&context, submit, &accepted).ok());
    ASSERT_FALSE(accepted.task().id().empty());
    v1::GetTaskRequest get;
    get.set_task_id(accepted.task().id());
    const auto until = std::chrono::system_clock::now() + std::chrono::seconds(5);
    v1::Task task;
    do {
      ASSERT_TRUE(state.GetTask(get, &task, until).ok());
      if (task.state() == v1::TASK_STATE_COMPLETED || task.state() == v1::TASK_STATE_FAILED) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::system_clock::now() < until);
    ASSERT_EQ(task.state(), v1::TASK_STATE_COMPLETED) << task.DebugString();
    EXPECT_EQ(task.attempt_count(), 1);
    EXPECT_EQ(task.correlation_id(), "pipeline-correlation");
    if (type == 1) {
      EXPECT_EQ(task.result().numeric().iterations(), 10000);
    }
    if (type == 2) {
      EXPECT_EQ(task.result().transform().data(), "HELLO");
    }
    if (type == 3) {
      EXPECT_EQ(task.result().network_simulation().delivered_bytes(), 40);
    }
  }
}
TEST(TaskPipeline, WorkerRejectsUnleasedAndForgedAttempts) {
  StateService state_service;
  Server state_server(&state_service);
  WorkerService worker_service(state_server.target);
  Server worker_server(&worker_service);
  auto worker = v1::WorkerService::NewStub(MakeWorkerChannel(worker_server.target));
  grpc::ClientContext context;
  SetDeadline(context, std::chrono::milliseconds(500));
  context.set_wait_for_ready(true);
  v1::ExecuteTaskRequest request;
  v1::ExecuteTaskResponse response;
  EXPECT_EQ(worker->ExecuteTask(&context, request, &response).error_code(),
            grpc::StatusCode::INVALID_ARGUMENT);
  auto* lease = request.mutable_lease();
  lease->set_attempt_token("forged-token");
  lease->mutable_task()->set_id("missing-task");
  auto* spec = lease->mutable_task()->mutable_spec();
  spec->set_type(v1::TASK_TYPE_NUMERIC);
  spec->mutable_execution_timeout()->set_seconds(1);
  spec->mutable_numeric()->set_operation(v1::NumericInput::OPERATION_MONTE_CARLO_PI);
  spec->mutable_numeric()->set_iterations(10);
  grpc::ClientContext forged_context;
  SetDeadline(forged_context, std::chrono::milliseconds(500));
  EXPECT_EQ(worker->ExecuteTask(&forged_context, request, &response).error_code(),
            grpc::StatusCode::NOT_FOUND);
}

TEST(TaskPipeline, WorkerRejectsCompletedAttemptReplayWithoutRecomputing) {
  StateService state_service;
  Server state_server(&state_service);
  StateClient state(state_server.target);
  WorkerService worker_service(state_server.target);
  Server worker_server(&worker_service);
  auto worker = v1::WorkerService::NewStub(MakeWorkerChannel(worker_server.target));
  v1::CreateTaskRequest create;
  create.set_idempotency_key("direct-worker");
  auto* spec = create.mutable_spec();
  spec->set_type(v1::TASK_TYPE_TRANSFORM);
  spec->mutable_execution_timeout()->set_seconds(1);
  spec->mutable_transform()->set_operation(v1::TransformInput::OPERATION_ASCII_UPPERCASE);
  spec->mutable_transform()->set_data("hello");
  v1::CreateTaskResponse created;
  const auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(3);
  ASSERT_TRUE(state.CreateTask(create, &created, deadline).ok());
  v1::AcquireTaskRequest acquire;
  acquire.set_router_id("direct-router");
  v1::AcquireTaskResponse acquired;
  ASSERT_TRUE(state.AcquireTask(acquire, &acquired, deadline).ok());
  ASSERT_TRUE(acquired.has_lease());
  v1::ExecuteTaskRequest request;
  *request.mutable_lease() = acquired.lease();
  v1::ExecuteTaskResponse response;
  grpc::ClientContext first;
  SetDeadline(first, std::chrono::milliseconds(2000), deadline);
  first.set_wait_for_ready(true);
  ASSERT_TRUE(worker->ExecuteTask(&first, request, &response).ok());
  ASSERT_EQ(response.outcome(), v1::ExecuteTaskResponse::OUTCOME_COMPLETED);
  grpc::ClientContext second;
  SetDeadline(second, std::chrono::milliseconds(500), deadline);
  EXPECT_EQ(worker->ExecuteTask(&second, request, &response).error_code(),
            grpc::StatusCode::ALREADY_EXISTS);
}
}  // namespace
}  // namespace taskrelay
