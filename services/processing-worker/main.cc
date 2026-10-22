#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <pthread.h>
#include <signal.h>

#include <chrono>
#include <iostream>
#include <string>

#include "common/grpc/health_client.h"
#include "taskrelay/v1/worker.grpc.pb.h"

int main(int argc, char* argv[]) {
  if (argc > 2) {
    std::cerr << "Usage: taskrelay-worker [listen-address]\n";
    return 2;
  }
  const std::string address = argc == 2 ? argv[1] : "127.0.0.1:50051";
  // Block before gRPC creates threads; the main thread receives shutdown synchronously.
  sigset_t stop_signals;
  sigemptyset(&stop_signals);
  sigaddset(&stop_signals, SIGINT);
  sigaddset(&stop_signals, SIGTERM);
  if (pthread_sigmask(SIG_BLOCK, &stop_signals, nullptr) != 0) {
    std::cerr << "Unable to configure shutdown signals\n";
    return 1;
  }
  grpc::EnableDefaultHealthCheckService(true);
  // The generated default returns UNIMPLEMENTED until phase 2 supplies execution.
  taskrelay::v1::WorkerService::Service worker_service;
  grpc::ServerBuilder builder;
  int selected_port = 0;
  builder.SetMaxReceiveMessageSize(taskrelay::kMaxGrpcMessageBytes);
  builder.SetMaxSendMessageSize(taskrelay::kMaxGrpcMessageBytes);
  builder.AddListeningPort(address, grpc::InsecureServerCredentials(), &selected_port);
  builder.RegisterService(&worker_service);
  auto server = builder.BuildAndStart();
  if (!server) {
    std::cerr << "Unable to start the foundation worker\n";
    return 1;
  }
  auto* health = server->GetHealthCheckService();
  health->SetServingStatus("", false);
  health->SetServingStatus(taskrelay::v1::WorkerService::service_full_name(), false);
  health->SetServingStatus(taskrelay::kFoundationWorkerHealth, true);
  std::cout << "Foundation worker bound port: " << selected_port << '\n' << std::flush;
  int received_signal = 0;
  const int wait_result = sigwait(&stop_signals, &received_signal);
  health->Shutdown();
  server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(1));
  server->Wait();
  if (wait_result != 0) {
    std::cerr << "Unable to wait for shutdown signal\n";
    return 1;
  }
  return 0;
}
