#include "services/state-service/memory_store.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <utility>

#include "common/task/validation.h"

namespace taskrelay {
namespace {
grpc::Status Invalid(const char* message) { return {grpc::StatusCode::INVALID_ARGUMENT, message}; }
grpc::Status Conflict() {
  return {grpc::StatusCode::FAILED_PRECONDITION, "Invalid or expired task ownership"};
}
bool Terminal(const v1::Task& task) {
  return task.state() == v1::TASK_STATE_COMPLETED || task.state() == v1::TASK_STATE_FAILED;
}
void SetFailed(v1::Task* task, TimePoint now, v1::TaskError::Code code) {
  task->set_state(v1::TASK_STATE_FAILED);
  *task->mutable_updated_at() = ToTimestamp(now);
  task->mutable_error()->set_code(code);
  task->mutable_error()->set_message("Task attempt could not complete within its lifetime");
}
bool ValidResult(const v1::TaskSpec& spec, const v1::TaskResult& result) {
  if (result.ByteSizeLong() > 65536) return false;
  switch (spec.type()) {
    case v1::TASK_TYPE_NUMERIC:
      return result.has_numeric() && std::isfinite(result.numeric().value()) &&
             result.numeric().value() >= 0 && result.numeric().value() <= 4 &&
             result.numeric().iterations() == spec.numeric().iterations();
    case v1::TASK_TYPE_TRANSFORM:
      return result.has_transform() &&
             result.transform().data().size() == spec.transform().data().size() &&
             DecodeUtf8(result.transform().data());
    case v1::TASK_TYPE_NETWORK_SIMULATION: {
      const auto expected_nanos =
          (spec.network_simulation().synthetic_delay().seconds() * 1000000000LL +
           spec.network_simulation().synthetic_delay().nanos()) *
          spec.network_simulation().packet_count();
      return result.has_network_simulation() &&
             static_cast<std::uint64_t>(result.network_simulation().delivered_packets()) +
                     result.network_simulation().dropped_packets() ==
                 spec.network_simulation().packet_count() &&
             result.network_simulation().delivered_bytes() ==
                 static_cast<std::uint64_t>(result.network_simulation().delivered_packets()) *
                     spec.network_simulation().packet_size_bytes() &&
             result.network_simulation().simulated_duration().seconds() ==
                 expected_nanos / 1000000000 &&
             result.network_simulation().simulated_duration().nanos() ==
                 expected_nanos % 1000000000;
    }
    default:
      return false;
  }
}
}  // namespace
MemoryStore::MemoryStore(std::size_t capacity, std::function<TimePoint()> now)
    : capacity_(capacity), now_(std::move(now)) {}

void MemoryStore::Maintain(TimePoint now) {
  // The phase 2 store is capped at 1024 entries: this maintenance scan is bounded.
  for (auto it = tasks_.begin(); it != tasks_.end();) {
    auto& entry = it->second;
    if (!Terminal(entry.task)) {
      if (now >= FromTimestamp(entry.task.deadline()))
        SetFailed(&entry.task, now, v1::TaskError::CODE_DEADLINE_EXCEEDED);
      else if (!entry.token.empty() && now >= entry.lease_expiry)
        SetFailed(&entry.task, now, v1::TaskError::CODE_LEASE_EXPIRED);
    }
    if (Terminal(entry.task) &&
        now >= FromTimestamp(entry.task.updated_at()) + std::chrono::minutes(10)) {
      keys_.erase(entry.key);
      created_.erase(entry.sequence);
      it = tasks_.erase(it);
    } else
      ++it;
  }
}
grpc::Status MemoryStore::Create(const v1::CreateTaskRequest& request,
                                 const std::string& correlation, v1::CreateTaskResponse* response) {
  auto status = ValidateSpec(request.spec());
  if (!status.ok()) return status;
  const auto& key = request.idempotency_key();
  if (key.empty() || key.size() > 128 ||
      !std::all_of(key.begin(), key.end(), [](unsigned char c) { return c >= 32 && c <= 126; }))
    return Invalid("Idempotency key must contain 1..128 printable ASCII characters");
  const auto canonical = CanonicalSpec(request.spec());
  std::lock_guard lock(mutex_);
  const auto now = now_();
  Maintain(now);
  const auto previous = keys_.find(key);
  if (previous != keys_.end()) {
    const auto& entry = tasks_.at(previous->second);
    if (entry.canonical != canonical)
      return {grpc::StatusCode::ALREADY_EXISTS,
              "Idempotency key has a different task specification"};
    *response->mutable_task() = entry.task;
    response->set_replayed(true);
    return grpc::Status::OK;
  }
  if (tasks_.size() >= capacity_)
    return {grpc::StatusCode::RESOURCE_EXHAUSTED, "Task retention capacity reached"};
  Entry entry;
  entry.key = key;
  entry.canonical = canonical;
  entry.sequence = ++sequence_;
  entry.task.set_id(NewId());
  *entry.task.mutable_spec() = request.spec();
  entry.task.mutable_spec()->DiscardUnknownFields();
  entry.task.set_state(v1::TASK_STATE_PENDING);
  *entry.task.mutable_created_at() = ToTimestamp(now);
  *entry.task.mutable_updated_at() = ToTimestamp(now);
  *entry.task.mutable_deadline() = ToTimestamp(now + std::chrono::seconds(60));
  entry.task.set_correlation_id(SafeAscii(correlation, 64) ? correlation : NewId());
  const auto id = entry.task.id();
  *response->mutable_task() = entry.task;
  response->set_replayed(false);
  created_.emplace(entry.sequence, id);
  keys_.emplace(key, id);
  tasks_.emplace(id, std::move(entry));
  return grpc::Status::OK;
}
grpc::Status MemoryStore::Get(const v1::GetTaskRequest& request, v1::Task* response) {
  std::lock_guard lock(mutex_);
  Maintain(now_());
  const auto it = tasks_.find(request.task_id());
  if (it == tasks_.end()) return {grpc::StatusCode::NOT_FOUND, "Task not found"};
  *response = it->second.task;
  return grpc::Status::OK;
}
grpc::Status MemoryStore::List(const v1::ListTasksRequest& request, v1::Page* response) {
  if (request.page_size() == 0 || request.page_size() > 100 || request.state_filter_size() > 5)
    return Invalid("Page size must be 1..100 with at most five state filters");
  std::set<int> filters;
  for (const auto state : request.state_filter()) {
    if (state < v1::TASK_STATE_PENDING || state > v1::TASK_STATE_RETRY_PENDING)
      return Invalid("Invalid task state filter");
    filters.insert(state);
  }
  std::string prefix = "v1/";
  for (const auto state : filters) prefix += std::to_string(state);
  prefix += '/';
  std::uint64_t after = 0;
  const auto& cursor = request.cursor();
  if (!cursor.empty()) {
    if (cursor.size() > 64 || !cursor.starts_with(prefix))
      return Invalid("Invalid pagination cursor");
    const auto tail = std::string_view(cursor).substr(prefix.size());
    const auto parsed = std::from_chars(tail.data(), tail.data() + tail.size(), after);
    if (tail.empty() || parsed.ec != std::errc() || parsed.ptr != tail.data() + tail.size())
      return Invalid("Invalid pagination cursor");
  }
  std::lock_guard lock(mutex_);
  Maintain(now_());
  response->Clear();
  auto it = created_.upper_bound(after);
  std::size_t scanned = 0;
  std::uint64_t last = after;
  while (it != created_.end() && scanned++ < 128 &&
         response->tasks_size() < static_cast<int>(request.page_size())) {
    last = it->first;
    const auto& task = tasks_.at(it->second).task;
    if (filters.empty() || filters.contains(task.state())) *response->add_tasks() = task;
    ++it;
  }
  if (it != created_.end()) response->set_next_cursor(prefix + std::to_string(last));
  return grpc::Status::OK;
}
grpc::Status MemoryStore::Acquire(const v1::AcquireTaskRequest& request,
                                  v1::AcquireTaskResponse* response) {
  if (!SafeAscii(request.router_id(), 64)) return Invalid("Invalid router identity");
  std::lock_guard lock(mutex_);
  const auto now = now_();
  Maintain(now);
  response->Clear();
  for (const auto& [sequence, id] : created_) {
    (void)sequence;
    auto& entry = tasks_.at(id);
    if (entry.task.state() != v1::TASK_STATE_PENDING || !entry.token.empty()) continue;
    entry.token = NewId();
    entry.router = request.router_id();
    entry.lease_expiry =
        std::min(now + std::chrono::seconds(15), FromTimestamp(entry.task.deadline()));
    entry.task.set_attempt_count(entry.task.attempt_count() + 1);
    auto* lease = response->mutable_lease();
    *lease->mutable_task() = entry.task;
    lease->set_attempt_token(entry.token);
    lease->set_attempt_number(entry.task.attempt_count());
    *lease->mutable_expires_at() = ToTimestamp(entry.lease_expiry);
    break;
  }
  return grpc::Status::OK;
}
grpc::Status MemoryStore::FindOwned(const Ownership& ownership, Entry** entry) {
  const auto it = tasks_.find(ownership.id);
  if (it == tasks_.end()) return {grpc::StatusCode::NOT_FOUND, "Task not found"};
  *entry = &it->second;
  if (ownership.token.empty() || (*entry)->token != ownership.token ||
      !SafeAscii(ownership.owner, 64))
    return Conflict();
  return grpc::Status::OK;
}

grpc::Status MemoryStore::Start(const v1::StartTaskRequest& request, v1::Task* response) {
  std::lock_guard lock(mutex_);
  const auto now = now_();
  Maintain(now);
  Entry* entry = nullptr;
  auto status = FindOwned(
      {.id = request.task_id(), .token = request.token(), .owner = request.worker_id()}, &entry);
  if (!status.ok()) return status;
  if (Terminal(entry->task) || (!entry->owner.empty() && entry->owner != request.worker_id()))
    return Conflict();
  entry->owner = request.worker_id();
  entry->task.set_state(v1::TASK_STATE_RUNNING);
  *entry->task.mutable_updated_at() = ToTimestamp(now);
  *response = entry->task;
  return grpc::Status::OK;
}
grpc::Status MemoryStore::Renew(const v1::RenewLeaseRequest& request, v1::LeaseExpiry* response) {
  std::lock_guard lock(mutex_);
  const auto now = now_();
  Maintain(now);
  Entry* entry = nullptr;
  auto status = FindOwned(
      {.id = request.task_id(), .token = request.token(), .owner = request.worker_id()}, &entry);
  if (!status.ok()) return status;
  if (entry->task.state() != v1::TASK_STATE_RUNNING || entry->owner != request.worker_id())
    return Conflict();
  entry->lease_expiry =
      std::min(now + std::chrono::seconds(15), FromTimestamp(entry->task.deadline()));
  *response->mutable_expires_at() = ToTimestamp(entry->lease_expiry);
  return grpc::Status::OK;
}
grpc::Status MemoryStore::Complete(const v1::CompleteTaskRequest& request, v1::Task* response) {
  std::lock_guard lock(mutex_);
  const auto now = now_();
  Maintain(now);
  Entry* entry = nullptr;
  auto status = FindOwned(
      {.id = request.task_id(), .token = request.token(), .owner = request.worker_id()}, &entry);
  if (!status.ok()) return status;
  if (entry->owner != request.worker_id() || !ValidResult(entry->task.spec(), request.result()))
    return Conflict();
  if (entry->task.state() == v1::TASK_STATE_COMPLETED) {
    if (entry->task.result().SerializeAsString() != request.result().SerializeAsString())
      return Conflict();
  } else {
    if (entry->task.state() != v1::TASK_STATE_RUNNING) return Conflict();
    entry->task.set_state(v1::TASK_STATE_COMPLETED);
    *entry->task.mutable_updated_at() = ToTimestamp(now);
    *entry->task.mutable_result() = request.result();
  }
  *response = entry->task;
  return grpc::Status::OK;
}
grpc::Status MemoryStore::Fail(const v1::FailAttemptRequest& request, v1::Task* response) {
  if (request.error().code() <= v1::TaskError::CODE_UNSPECIFIED ||
      request.error().code() > v1::TaskError::CODE_ATTEMPTS_EXHAUSTED ||
      request.error().message().size() > 256)
    return Invalid("Invalid task error");
  std::lock_guard lock(mutex_);
  const auto now = now_();
  Maintain(now);
  Entry* entry = nullptr;
  auto status = FindOwned(
      {.id = request.task_id(), .token = request.token(), .owner = request.actor_id()}, &entry);
  if (!status.ok()) return status;
  if (request.actor_id() != (entry->owner.empty() ? entry->router : entry->owner))
    return Conflict();
  if (Terminal(entry->task)) {
    if (entry->task.state() != v1::TASK_STATE_FAILED ||
        entry->task.error().code() != request.error().code())
      return Conflict();
  } else {
    entry->task.set_state(v1::TASK_STATE_FAILED);
    *entry->task.mutable_error() = request.error();
    entry->task.mutable_error()->set_message("Task execution failed");
    entry->task.mutable_error()->set_retryable(false);
    *entry->task.mutable_updated_at() = ToTimestamp(now);
  }
  *response = entry->task;
  return grpc::Status::OK;
}
}  // namespace taskrelay
