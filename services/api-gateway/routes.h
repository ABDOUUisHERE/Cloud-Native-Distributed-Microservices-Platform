#pragma once
#include <drogon/HttpAppFramework.h>

#include "common/grpc/state_client.h"
#include "common/threading/bounded_executor.h"
#include "taskrelay/v1/router.grpc.pb.h"
namespace taskrelay {
class Routes {
 public:
  struct Targets {
    std::string router;
    std::string state;
  };
  explicit Routes(const Targets& targets);
  void Register();
  void Stop() { executor_.Stop(); }

 private:
  using Reply = std::function<void(const drogon::HttpResponsePtr&)>;
  void Enqueue(const drogon::HttpRequestPtr&, const Reply&, std::string id = "");
  struct RequestContext {
    std::string task_id;
    std::string correlation;
    TimePoint deadline;
  };
  void Handle(const drogon::HttpRequestPtr&, const Reply&, const RequestContext&);

  std::shared_ptr<grpc::Channel> router_channel_;
  std::unique_ptr<v1::TaskRouterService::Stub> router_;
  StateClient state_;
  BoundedExecutor executor_{4, 32};
};
}  // namespace taskrelay
