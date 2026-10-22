#pragma once
#include <mutex>
#include <thread>

#include "common/grpc/state_client.h"
#include "taskrelay/v1/worker.grpc.pb.h"
namespace taskrelay {
class Dispatcher {
 public:
  struct Targets {
    std::string state;
    std::string worker;
  };
  explicit Dispatcher(const Targets& targets);
  ~Dispatcher();
  void Stop();

 private:
  void Run(const std::stop_token& stop);
  const std::string router_id_;
  StateClient state_;
  std::unique_ptr<v1::WorkerService::Stub> worker_;
  std::mutex mutex_;
  grpc::ClientContext* current_ = nullptr;
  std::jthread thread_;
};
}  // namespace taskrelay
