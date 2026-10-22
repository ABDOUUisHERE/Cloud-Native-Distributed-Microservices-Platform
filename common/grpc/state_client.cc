#include "common/grpc/state_client.h"

#include <google/protobuf/util/time_util.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/server_builder.h>
#include <pthread.h>
#include <signal.h>

#include <iostream>

#include "common/grpc/health_client.h"
#include "common/task/validation.h"

namespace taskrelay {
google::protobuf::Timestamp ToTimestamp(TimePoint time) {
  return google::protobuf::util::TimeUtil::NanosecondsToTimestamp(
      std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count());
}
TimePoint FromTimestamp(const google::protobuf::Timestamp& time) {
  return TimePoint(std::chrono::duration_cast<TimePoint::duration>(
      std::chrono::seconds(time.seconds()) + std::chrono::nanoseconds(time.nanos())));
}
std::string Correlation(const grpc::ServerContext& context) {
  const auto found = context.client_metadata().find("x-correlation-id");
  if (found != context.client_metadata().end()) {
    std::string value(found->second.data(), found->second.size());
    if (SafeAscii(value, 64)) return value;
  }
  return NewId();
}
void AddCorrelation(grpc::ClientContext& context, const std::string& id) {
  if (!id.empty()) context.AddMetadata("x-correlation-id", id);
}
StateClient::StateClient(const std::string& target) : StateClient(MakeWorkerChannel(target)) {}
StateClient::StateClient(std::shared_ptr<grpc::Channel> channel)
    : stub_(v1::StateService::NewStub(std::move(channel))) {}
#define TASKRELAY_STATE_RPC(Name, Request, Response)                                       \
  grpc::Status StateClient::Name(const v1::Request& request, v1::Response* response,       \
                                 TimePoint parent, const std::string& correlation) const { \
    grpc::ClientContext context;                                                           \
    SetDeadline(context, std::chrono::milliseconds(500), parent);                          \
    context.set_wait_for_ready(true);                                                      \
    AddCorrelation(context, correlation);                                                  \
    return stub_->Name(&context, request, response);                                       \
  }
TASKRELAY_STATE_RPC(CreateTask, CreateTaskRequest, CreateTaskResponse)
TASKRELAY_STATE_RPC(GetTask, GetTaskRequest, Task)
TASKRELAY_STATE_RPC(ListTasks, ListTasksRequest, Page)
TASKRELAY_STATE_RPC(AcquireTask, AcquireTaskRequest, AcquireTaskResponse)
TASKRELAY_STATE_RPC(StartTask, StartTaskRequest, Task)
TASKRELAY_STATE_RPC(RenewLease, RenewLeaseRequest, LeaseExpiry)
TASKRELAY_STATE_RPC(CompleteTask, CompleteTaskRequest, Task)
TASKRELAY_STATE_RPC(FailAttempt, FailAttemptRequest, Task)
#undef TASKRELAY_STATE_RPC
void BlockStopSignals() {
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGTERM);
  if (pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0)
    throw std::runtime_error("Unable to configure shutdown signals");
}
int RunGrpcServer(const std::string& address, grpc::Service* service,
                  const std::string& health_name, const std::function<void()>& stop) {
  grpc::EnableDefaultHealthCheckService(true);
  grpc::ServerBuilder builder;
  int port = 0;
  builder.SetMaxReceiveMessageSize(kMaxGrpcMessageBytes);
  builder.SetMaxSendMessageSize(kMaxGrpcMessageBytes);
  builder.AddListeningPort(address, grpc::InsecureServerCredentials(), &port);
  builder.RegisterService(service);
  auto server = builder.BuildAndStart();
  if (!server) {
    std::cerr << "Unable to start gRPC server\n";
    return 1;
  }
  auto* health = server->GetHealthCheckService();
  health->SetServingStatus("", true);
  health->SetServingStatus(health_name, true);
  std::cout << "gRPC bound port: " << port << '\n' << std::flush;
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGTERM);
  int signal = 0;
  const int error = sigwait(&signals, &signal);
  health->Shutdown();
  if (stop) stop();
  server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(2));
  server->Wait();
  return error == 0 ? 0 : 1;
}
}  // namespace taskrelay
