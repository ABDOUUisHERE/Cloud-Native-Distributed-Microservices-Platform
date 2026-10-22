#include "services/state-service/memory_store.h"

#include <gtest/gtest.h>

#include <thread>
namespace taskrelay {
namespace {
class StoreTest : public testing::Test {
 protected:
  TimePoint now = std::chrono::system_clock::now();
  MemoryStore store{4, [this] { return now; }};
  v1::CreateTaskRequest Request(const std::string& key = "key") {
    v1::CreateTaskRequest request;
    request.set_idempotency_key(key);
    auto* spec = request.mutable_spec();
    spec->set_type(v1::TASK_TYPE_TRANSFORM);
    spec->mutable_execution_timeout()->set_seconds(2);
    spec->mutable_transform()->set_operation(v1::TransformInput::OPERATION_ASCII_UPPERCASE);
    spec->mutable_transform()->set_data("hello");
    return request;
  }
  v1::TaskLease Lease() {
    v1::AcquireTaskRequest acquire;
    acquire.set_router_id("router");
    v1::AcquireTaskResponse response;
    EXPECT_TRUE(store.Acquire(acquire, &response).ok());
    return response.lease();
  }
  v1::StartTaskRequest StartRequest(const v1::TaskLease& lease,
                                    const std::string& worker = "worker") {
    v1::StartTaskRequest request;
    request.set_task_id(lease.task().id());
    request.set_token(lease.attempt_token());
    request.set_worker_id(worker);
    return request;
  }
};
TEST_F(StoreTest, ReplaysCanonicalInputAndRejectsConflictingKey) {
  v1::CreateTaskResponse a, b;
  ASSERT_TRUE(store.Create(Request(), "correlation", &a).ok());
  ASSERT_TRUE(store.Create(Request(), "different", &b).ok());
  EXPECT_EQ(a.task().id(), b.task().id());
  EXPECT_TRUE(b.replayed());
  EXPECT_EQ(b.task().correlation_id(), "correlation");
  auto changed = Request();
  changed.mutable_spec()->mutable_transform()->set_data("other");
  EXPECT_EQ(store.Create(changed, "id", &b).error_code(), grpc::StatusCode::ALREADY_EXISTS);
}
TEST_F(StoreTest, FencesOwnersAndAcceptsOnlyMatchingCompletionReplay) {
  v1::CreateTaskResponse created;
  ASSERT_TRUE(store.Create(Request(), "id", &created).ok());
  const auto lease = Lease();
  v1::Task task;
  auto start = StartRequest(lease);
  ASSERT_TRUE(store.Start(start, &task).ok());
  EXPECT_TRUE(store.Start(start, &task).ok());
  EXPECT_FALSE(store.Start(StartRequest(lease, "other"), &task).ok());
  v1::CompleteTaskRequest complete;
  complete.set_task_id(task.id());
  complete.set_token(lease.attempt_token());
  complete.set_worker_id("worker");
  complete.mutable_result()->mutable_transform()->set_data("HELLO");
  ASSERT_TRUE(store.Complete(complete, &task).ok());
  EXPECT_EQ(task.state(), v1::TASK_STATE_COMPLETED);
  EXPECT_TRUE(store.Complete(complete, &task).ok());
  complete.mutable_result()->mutable_transform()->set_data("OTHER");
  EXPECT_FALSE(store.Complete(complete, &task).ok());
  EXPECT_FALSE(store.Start(start, &task).ok());
}
TEST_F(StoreTest, ExpiredLeaseRejectsStaleStartAndCompletion) {
  v1::CreateTaskResponse created;
  ASSERT_TRUE(store.Create(Request(), "id", &created).ok());
  const auto lease = Lease();
  v1::Task task;
  ASSERT_TRUE(store.Start(StartRequest(lease), &task).ok());
  now += std::chrono::seconds(16);
  EXPECT_FALSE(store.Start(StartRequest(lease), &task).ok());
  v1::CompleteTaskRequest complete;
  complete.set_task_id(lease.task().id());
  complete.set_token(lease.attempt_token());
  complete.set_worker_id("worker");
  complete.mutable_result()->mutable_transform()->set_data("HELLO");
  EXPECT_FALSE(store.Complete(complete, &task).ok());
  v1::GetTaskRequest get;
  get.set_task_id(created.task().id());
  ASSERT_TRUE(store.Get(get, &task).ok());
  EXPECT_EQ(task.state(), v1::TASK_STATE_FAILED);
  EXPECT_EQ(task.error().code(), v1::TaskError::CODE_LEASE_EXPIRED);
}
TEST_F(StoreTest, RenewalExtendsCurrentLeaseOnly) {
  v1::CreateTaskResponse created;
  ASSERT_TRUE(store.Create(Request(), "id", &created).ok());
  const auto lease = Lease();
  v1::Task task;
  ASSERT_TRUE(store.Start(StartRequest(lease), &task).ok());
  now += std::chrono::seconds(10);
  v1::RenewLeaseRequest renew;
  renew.set_task_id(task.id());
  renew.set_token(lease.attempt_token());
  renew.set_worker_id("worker");
  v1::LeaseExpiry expiry;
  ASSERT_TRUE(store.Renew(renew, &expiry).ok());
  EXPECT_GT(FromTimestamp(expiry.expires_at()), FromTimestamp(lease.expires_at()));
  renew.set_token("stale");
  EXPECT_FALSE(store.Renew(renew, &expiry).ok());
}
TEST_F(StoreTest, BoundedPaginationAndFilterBoundCursors) {
  for (int i = 0; i < 3; ++i) {
    v1::CreateTaskResponse response;
    ASSERT_TRUE(store.Create(Request(std::to_string(i)), "id", &response).ok());
  }
  v1::ListTasksRequest list;
  list.set_page_size(1);
  v1::Page page;
  ASSERT_TRUE(store.List(list, &page).ok());
  ASSERT_EQ(page.tasks_size(), 1);
  const auto first = page.tasks(0).id();
  list.set_cursor(page.next_cursor());
  ASSERT_TRUE(store.List(list, &page).ok());
  EXPECT_NE(page.tasks(0).id(), first);
  list.add_state_filter(v1::TASK_STATE_COMPLETED);
  EXPECT_FALSE(store.List(list, &page).ok());
  list.clear_cursor();
  ASSERT_TRUE(store.List(list, &page).ok());
  EXPECT_EQ(page.tasks_size(), 0);
  list.set_page_size(101);
  EXPECT_FALSE(store.List(list, &page).ok());
}
TEST_F(StoreTest, CapacityAndTerminalRetentionAreBounded) {
  for (int i = 0; i < 4; ++i) {
    v1::CreateTaskResponse response;
    ASSERT_TRUE(store.Create(Request(std::to_string(i)), "id", &response).ok());
  }
  v1::CreateTaskResponse response;
  EXPECT_EQ(store.Create(Request("overflow"), "id", &response).error_code(),
            grpc::StatusCode::RESOURCE_EXHAUSTED);
  now += std::chrono::seconds(61);
  v1::ListTasksRequest list;
  list.set_page_size(10);
  v1::Page page;
  ASSERT_TRUE(store.List(list, &page).ok());
  EXPECT_EQ(page.tasks(0).state(), v1::TASK_STATE_FAILED);
  now += std::chrono::minutes(11);
  EXPECT_TRUE(store.Create(Request("overflow"), "id", &response).ok());
}
TEST_F(StoreTest, ConcurrentSubmissionsCreateOneTask) {
  std::vector<v1::CreateTaskResponse> responses(8);
  std::vector<grpc::Status> statuses(8);
  std::vector<std::jthread> threads;
  const auto request = Request();
  for (std::size_t i = 0; i < responses.size(); ++i)
    threads.emplace_back([&, i] { statuses[i] = store.Create(request, "id", &responses[i]); });
  for (auto& thread : threads) thread.join();
  for (std::size_t i = 0; i < responses.size(); ++i) {
    EXPECT_TRUE(statuses[i].ok());
    EXPECT_EQ(responses[i].task().id(), responses[0].task().id());
  }
}
}  // namespace
}  // namespace taskrelay
