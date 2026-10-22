#include <iostream>

#include "common/grpc/state_client.h"
#include "services/processing-worker/worker_service.h"
int main(int argc, char* argv[]) {
  if (argc > 3) {
    std::cerr << "Usage: taskrelay-worker [listen-address] [state-target]\n";
    return 2;
  }
  try {
    taskrelay::BlockStopSignals();
    taskrelay::WorkerService service(argc == 3 ? argv[2] : "127.0.0.1:50052");
    return taskrelay::RunGrpcServer(argc >= 2 ? argv[1] : "127.0.0.1:50051", &service,
                                    taskrelay::v1::WorkerService::service_full_name());
  } catch (const std::exception&) {
    std::cerr << "Worker startup failed\n";
    return 1;
  }
}
