#include <iostream>

#include "common/grpc/state_client.h"
#include "services/state-service/state_service.h"

int main(int argc, char* argv[]) {
  if (argc > 2) {
    std::cerr << "Usage: taskrelay-state [listen-address]\n";
    return 2;
  }
  try {
    taskrelay::BlockStopSignals();
    taskrelay::StateService service;
    return taskrelay::RunGrpcServer(argc == 2 ? argv[1] : "127.0.0.1:50052", &service,
                                    taskrelay::v1::StateService::service_full_name());
  } catch (const std::exception&) {
    std::cerr << "State Service startup failed\n";
    return 1;
  }
}
