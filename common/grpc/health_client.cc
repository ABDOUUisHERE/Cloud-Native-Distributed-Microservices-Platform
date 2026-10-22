#include "common/grpc/health_client.h"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>

#include "common/grpc/deadline.h"
#include "grpc/health/v1/health.grpc.pb.h"

namespace taskrelay {

std::shared_ptr<grpc::Channel> MakeWorkerChannel(const std::string& target) {
  grpc::ChannelArguments arguments;
  arguments.SetLoadBalancingPolicyName("round_robin");
  arguments.SetMaxReceiveMessageSize(kMaxGrpcMessageBytes);
  arguments.SetMaxSendMessageSize(kMaxGrpcMessageBytes);
  arguments.SetInt("grpc.enable_retries", 0);
  return grpc::CreateCustomChannel(target, grpc::InsecureChannelCredentials(), arguments);
}

grpc::Status ProbeWorker(const std::shared_ptr<grpc::Channel>& channel, const std::string& service,
                         std::chrono::milliseconds budget,
                         std::chrono::system_clock::time_point parent) {
  auto stub = grpc::health::v1::Health::NewStub(channel);
  grpc::ClientContext context;
  SetDeadline(context, budget, parent);
  context.set_wait_for_ready(true);
  grpc::health::v1::HealthCheckRequest request;
  request.set_service(service);
  grpc::health::v1::HealthCheckResponse response;
  auto status = stub->Check(&context, request, &response);
  if (!status.ok()) {
    return status;
  }
  if (response.status() != grpc::health::v1::HealthCheckResponse::SERVING) {
    return {grpc::StatusCode::UNAVAILABLE, "Worker is not serving"};
  }
  return grpc::Status::OK;
}

}  // namespace taskrelay
