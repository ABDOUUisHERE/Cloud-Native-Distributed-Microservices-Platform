#include "services/task-router/dispatcher.h"

#include "common/grpc/health_client.h"
#include "common/task/validation.h"
namespace taskrelay {
Dispatcher::Dispatcher(const Targets& targets)
    : router_id_(NewId()),
      state_(targets.state),
      worker_(v1::WorkerService::NewStub(MakeWorkerChannel(targets.worker))),
      thread_([this](const std::stop_token& stop) { Run(stop); }) {}
Dispatcher::~Dispatcher() { Stop(); }
void Dispatcher::Stop() {
  thread_.request_stop();
  {
    std::lock_guard lock(mutex_);
    if (current_) current_->TryCancel();
  }
  if (thread_.joinable()) thread_.join();
}
void Dispatcher::Run(const std::stop_token& stop) {
  while (!stop.stop_requested()) {
    v1::AcquireTaskRequest acquire;
    acquire.set_router_id(router_id_);
    v1::AcquireTaskResponse acquired;
    auto status = state_.AcquireTask(
        acquire, &acquired, std::chrono::system_clock::now() + std::chrono::milliseconds(500));
    if (!status.ok() || !acquired.has_lease()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
      continue;
    }
    const auto& lease = acquired.lease();
    const auto& task = lease.task();
    grpc::ClientContext context;
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::seconds(task.spec().execution_timeout().seconds()) +
        std::chrono::nanoseconds(task.spec().execution_timeout().nanos()));
    SetDeadline(context, millis, FromTimestamp(task.deadline()));
    AddCorrelation(context, task.correlation_id());
    context.set_wait_for_ready(true);
    {
      std::lock_guard lock(mutex_);
      current_ = &context;
      if (stop.stop_requested()) context.TryCancel();
    }
    v1::ExecuteTaskRequest execute;
    *execute.mutable_lease() = lease;
    v1::ExecuteTaskResponse outcome;
    status = worker_->ExecuteTask(&context, execute, &outcome);
    {
      std::lock_guard lock(mutex_);
      current_ = nullptr;
    }
    if (!status.ok() && !stop.stop_requested()) {
      // State rejects this failure if a worker already owns the lease.
      v1::FailAttemptRequest fail;
      fail.set_task_id(task.id());
      fail.set_token(lease.attempt_token());
      fail.set_actor_id(router_id_);
      fail.mutable_error()->set_code(v1::TaskError::CODE_DEPENDENCY_UNAVAILABLE);
      v1::Task failed;
      state_.FailAttempt(fail, &failed,
                         std::chrono::system_clock::now() + std::chrono::milliseconds(500),
                         task.correlation_id());
    }
  }
}
}  // namespace taskrelay
