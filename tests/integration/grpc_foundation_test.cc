#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "common/grpc/deadline.h"
#include "common/grpc/health_client.h"
#include "grpc/health/v1/health.grpc.pb.h"
#include "taskrelay/v1/worker.grpc.pb.h"

namespace {
using namespace std::chrono_literals;

class ControlledHealth final : public grpc::health::v1::Health::Service {
 public:
  std::atomic<bool> serving{true};
  std::atomic<bool> slow{false};
  std::atomic<int> calls{0};

  grpc::Status Check(grpc::ServerContext* context,
                     const grpc::health::v1::HealthCheckRequest* request,
                     grpc::health::v1::HealthCheckResponse* response) override {
    ++calls;
    if (request->service() != taskrelay::kFoundationWorkerHealth) {
      return {grpc::StatusCode::NOT_FOUND, "Unknown service"};
    }
    if (slow) {
      const auto stop = std::chrono::steady_clock::now() + 2s;
      while (!context->IsCancelled() && std::chrono::steady_clock::now() < stop) {
        std::this_thread::sleep_for(1ms);
      }
      return {grpc::StatusCode::CANCELLED, "Slow test operation cancelled"};
    }
    response->set_status(serving ? grpc::health::v1::HealthCheckResponse::SERVING
                                 : grpc::health::v1::HealthCheckResponse::NOT_SERVING);
    return grpc::Status::OK;
  }
};

class GrpcFoundationTest : public testing::Test {
 protected:
  void SetUp() override {
    grpc::EnableDefaultHealthCheckService(false);
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port_);
    builder.RegisterService(&health_);
    builder.RegisterService(&worker_);
    server_ = builder.BuildAndStart();
    ASSERT_NE(server_, nullptr);
    channel_ = taskrelay::MakeWorkerChannel("127.0.0.1:" + std::to_string(port_));
  }

  void TearDown() override {
    if (server_) {
      server_->Shutdown(std::chrono::system_clock::now() + 1s);
      server_->Wait();
    }
  }

  int port_ = 0;
  ControlledHealth health_;
  taskrelay::v1::WorkerService::Service worker_;
  std::unique_ptr<grpc::Server> server_;
  std::shared_ptr<grpc::Channel> channel_;
};

TEST_F(GrpcFoundationTest, ServingHealthSucceedsOverRealGrpc) {
  EXPECT_TRUE(taskrelay::ProbeWorker(channel_, taskrelay::kFoundationWorkerHealth).ok());
  EXPECT_EQ(health_.calls, 1);
}

TEST_F(GrpcFoundationTest, NonServingHealthFailsReadiness) {
  health_.serving = false;
  const auto status = taskrelay::ProbeWorker(channel_, taskrelay::kFoundationWorkerHealth);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE);
}

TEST_F(GrpcFoundationTest, UnknownServiceIsNotReportedHealthy) {
  const auto status = taskrelay::ProbeWorker(channel_, "taskrelay.v1.WorkerService");
  EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
}

TEST_F(GrpcFoundationTest, GeneratedBaseServiceLeavesExecutionUnimplemented) {
  auto stub = taskrelay::v1::WorkerService::NewStub(channel_);
  grpc::ClientContext context;
  taskrelay::SetDeadline(context, 500ms);
  taskrelay::v1::ExecuteTaskRequest request;
  taskrelay::v1::ExecuteTaskResponse response;
  const auto status = stub->ExecuteTask(&context, request, &response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::UNIMPLEMENTED);
}

TEST_F(GrpcFoundationTest, SlowCallStopsAtParentDeadline) {
  ASSERT_TRUE(taskrelay::ProbeWorker(channel_, taskrelay::kFoundationWorkerHealth).ok());
  health_.slow = true;
  const auto started = std::chrono::steady_clock::now();
  const auto status = taskrelay::ProbeWorker(channel_, taskrelay::kFoundationWorkerHealth, 500ms,
                                             std::chrono::system_clock::now() + 40ms);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::DEADLINE_EXCEEDED);
  EXPECT_EQ(health_.calls, 2);
  EXPECT_LT(std::chrono::steady_clock::now() - started, 1500ms);
}

TEST_F(GrpcFoundationTest, ExpiredParentBudgetCannotStartUnboundedWork) {
  const auto status = taskrelay::ProbeWorker(channel_, taskrelay::kFoundationWorkerHealth, 500ms,
                                             std::chrono::system_clock::now() - 1s);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::DEADLINE_EXCEEDED);
  EXPECT_EQ(health_.calls, 0);
}

TEST_F(GrpcFoundationTest, RoundRobinReachesBothResolvedWorkers) {
  ControlledHealth second_health;
  int second_port = 0;
  grpc::ServerBuilder builder;
  builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &second_port);
  builder.RegisterService(&second_health);
  auto second_server = builder.BuildAndStart();
  ASSERT_NE(second_server, nullptr);
  auto balanced = taskrelay::MakeWorkerChannel("ipv4:127.0.0.1:" + std::to_string(port_) +
                                               ",127.0.0.1:" + std::to_string(second_port));
  const auto stop = std::chrono::steady_clock::now() + 3s;
  while ((health_.calls == 0 || second_health.calls == 0) &&
         std::chrono::steady_clock::now() < stop) {
    EXPECT_TRUE(taskrelay::ProbeWorker(balanced, taskrelay::kFoundationWorkerHealth).ok());
    std::this_thread::sleep_for(10ms);
  }
  EXPECT_GT(health_.calls, 0);
  EXPECT_GT(second_health.calls, 0);
  second_server->Shutdown(std::chrono::system_clock::now() + 1s);
  second_server->Wait();
}

TEST(DeadlineTest, RejectsMissingOrExcessiveBudgets) {
  grpc::ClientContext context;
  EXPECT_THROW(taskrelay::SetDeadline(context, 0ms), std::invalid_argument);
  EXPECT_THROW(taskrelay::SetDeadline(context, -1ms), std::invalid_argument);
  EXPECT_THROW(taskrelay::SetDeadline(context, 31s), std::invalid_argument);
}

}  // namespace
