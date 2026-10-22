#include "services/processing-worker/worker_service.h"

#include <atomic>
#include <future>

#include "common/task/validation.h"
#include "services/processing-worker/workloads.h"
namespace taskrelay {
WorkerService::WorkerService(const std::string& state_target)
    : worker_id_(NewId()), state_(state_target) {}
grpc::Status WorkerService::ExecuteTask(grpc::ServerContext* context,
                                        const v1::ExecuteTaskRequest* request,
                                        v1::ExecuteTaskResponse* response) {
  if (!request->has_lease() || !SafeAscii(request->lease().attempt_token(), 64))
    return {grpc::StatusCode::INVALID_ARGUMENT, "A task lease is required"};
  const auto& lease = request->lease();
  auto status = ValidateSpec(lease.task().spec());
  if (!status.ok()) return status;
  {
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::system_clock::now();
    std::erase_if(seen_, [now](const auto& item) { return item.second <= now; });
    if (seen_.contains(lease.attempt_token()))
      return {grpc::StatusCode::ALREADY_EXISTS, "Attempt has already been admitted"};
    if (seen_.size() >= 1024)
      return {grpc::StatusCode::RESOURCE_EXHAUSTED, "Worker attempt retention is full"};
    if (active_.size() >= 10)
      return {grpc::StatusCode::RESOURCE_EXHAUSTED, "Worker is at capacity"};
    if (!active_.insert(lease.attempt_token()).second)
      return {grpc::StatusCode::ALREADY_EXISTS, "Attempt is already executing"};
    seen_.emplace(lease.attempt_token(), now + std::chrono::seconds(60));
  }
  const auto guard = std::shared_ptr<void>(nullptr, [this, token = lease.attempt_token()](void*) {
    std::lock_guard lock(mutex_);
    active_.erase(token);
  });
  const auto correlation = Correlation(*context);
  v1::StartTaskRequest start;
  start.set_task_id(lease.task().id());
  start.set_token(lease.attempt_token());
  start.set_worker_id(worker_id_);
  v1::Task task;
  status = state_.StartTask(start, &task, context->deadline(), correlation);
  if (!status.ok()) return status;
  if (CanonicalSpec(task.spec()) != CanonicalSpec(lease.task().spec()) ||
      task.attempt_count() != lease.attempt_number())
    return {grpc::StatusCode::FAILED_PRECONDITION, "Lease does not match stored task"};
  const auto duration = std::chrono::seconds(task.spec().execution_timeout().seconds()) +
                        std::chrono::nanoseconds(task.spec().execution_timeout().nanos());
  const auto deadline = std::min({context->deadline(), FromTimestamp(task.deadline()),
                                  std::chrono::system_clock::now() + duration});
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  using Outcome = std::pair<grpc::Status, v1::TaskResult>;
  auto work =
      std::make_shared<std::packaged_task<Outcome()>>([spec = task.spec(), cancelled, deadline] {
        v1::TaskResult result;
        auto result_status = Process(spec, &result, [cancelled, deadline] {
          return cancelled->load() || std::chrono::system_clock::now() >= deadline;
        });
        return Outcome{result_status, result};
      });
  auto future = work->get_future();
  if (!executor_.Submit([work] { (*work)(); }))
    status = {grpc::StatusCode::RESOURCE_EXHAUSTED, "Worker queue is full"};
  else {
    auto renew_at = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (future.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready) {
      if (context->IsCancelled() || std::chrono::system_clock::now() >= deadline) {
        cancelled->store(true);
        status = {grpc::StatusCode::DEADLINE_EXCEEDED, "Execution budget expired"};
        break;
      }
      if (std::chrono::steady_clock::now() >= renew_at) {
        v1::RenewLeaseRequest renew;
        renew.set_task_id(task.id());
        renew.set_token(lease.attempt_token());
        renew.set_worker_id(worker_id_);
        v1::LeaseExpiry expiry;
        status = state_.RenewLease(renew, &expiry, deadline, correlation);
        if (!status.ok()) {
          cancelled->store(true);
          return status;
        }
        renew_at += std::chrono::seconds(5);
      }
    }
    if (status.ok()) {
      auto outcome = future.get();
      status = outcome.first;
      if (status.ok()) {
        v1::CompleteTaskRequest complete;
        complete.set_task_id(task.id());
        complete.set_token(lease.attempt_token());
        complete.set_worker_id(worker_id_);
        *complete.mutable_result() = outcome.second;
        v1::Task stored;
        status = state_.CompleteTask(complete, &stored, deadline, correlation);
        if (!status.ok()) return status;
        response->set_task_id(task.id());
        response->set_attempt_number(task.attempt_count());
        response->set_outcome(v1::ExecuteTaskResponse::OUTCOME_COMPLETED);
        return grpc::Status::OK;
      }
    }
  }
  v1::FailAttemptRequest fail;
  fail.set_task_id(task.id());
  fail.set_token(lease.attempt_token());
  fail.set_actor_id(worker_id_);
  fail.mutable_error()->set_code(status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED
                                     ? v1::TaskError::CODE_DEADLINE_EXCEEDED
                                     : v1::TaskError::CODE_PROCESSING_FAILED);
  v1::Task failed;
  // A separate bounded cleanup operation can outlive the cancelled execution RPC.
  auto failure = state_.FailAttempt(
      fail, &failed, std::chrono::system_clock::now() + std::chrono::milliseconds(500),
      correlation);
  if (!failure.ok()) return failure;
  response->set_task_id(task.id());
  response->set_attempt_number(task.attempt_count());
  response->set_outcome(v1::ExecuteTaskResponse::OUTCOME_FAILED);
  return grpc::Status::OK;
}
}  // namespace taskrelay
