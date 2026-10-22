#include <grpcpp/support/status_code_enum.h>

#include <iostream>
#include <string>

#include "common/grpc/health_client.h"

int main(int argc, char* argv[]) {
  if (argc > 2) {
    std::cerr << "Usage: taskrelay-router [worker-target]\n";
    return 2;
  }
  const std::string target = argc == 2 ? argv[1] : "dns:///localhost:50051";
  const auto channel = taskrelay::MakeWorkerChannel(target);
  const auto status = taskrelay::ProbeWorker(channel, taskrelay::kFoundationWorkerHealth);
  if (!status.ok()) {
    std::cerr << "Worker health probe failed (gRPC code " << static_cast<int>(status.error_code())
              << ")\n";
    return 1;
  }
  std::cout << "Worker foundation health: SERVING\n";
  return 0;
}
