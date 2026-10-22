#include <iostream>
#include <string>

#include "common/grpc/health_client.h"
#include "services/task-router/dispatcher.h"
#include "services/task-router/router_service.h"
int main(int argc, char* argv[]) {
  if (argc >= 2 && std::string(argv[1]) == "--probe") {
    if (argc > 3) return 2;
    const auto status = taskrelay::ProbeWorker(
        taskrelay::MakeWorkerChannel(argc == 3 ? argv[2] : "127.0.0.1:50051"),
        taskrelay::v1::WorkerService::service_full_name());
    if (!status.ok()) {
      std::cerr << "Worker health probe failed\n";
      return 1;
    }
    std::cout << "Worker health: SERVING\n";
    return 0;
  }
  if (argc > 4) {
    std::cerr << "Usage: taskrelay-router [listen-address] [state-target] [worker-target]\n";
    return 2;
  }
  try {
    taskrelay::BlockStopSignals();
    const std::string state = argc >= 3 ? argv[2] : "127.0.0.1:50052";
    taskrelay::RouterService service(state);
    taskrelay::Dispatcher dispatcher(
        {.state = state, .worker = argc == 4 ? argv[3] : "127.0.0.1:50051"});
    return taskrelay::RunGrpcServer(argc >= 2 ? argv[1] : "127.0.0.1:50053", &service,
                                    taskrelay::v1::TaskRouterService::service_full_name(),
                                    [&dispatcher] { dispatcher.Stop(); });
  } catch (const std::exception&) {
    std::cerr << "Router startup failed\n";
    return 1;
  }
}
