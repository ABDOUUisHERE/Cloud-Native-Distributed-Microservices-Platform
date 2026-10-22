#include "common/threading/bounded_executor.h"

#include <gtest/gtest.h>

#include <future>
namespace taskrelay {
TEST(BoundedExecutor, RejectsOverflowAndDrainsAcceptedWorkOnStop) {
  BoundedExecutor pool(1, 1);
  auto entered = std::make_shared<std::promise<void>>();
  auto release = std::make_shared<std::promise<void>>();
  auto finished = std::make_shared<std::promise<void>>();
  auto ready = entered->get_future();
  auto gate = release->get_future().share();
  auto done = finished->get_future();
  ASSERT_TRUE(pool.Submit([entered, gate] {
    entered->set_value();
    gate.wait_for(std::chrono::seconds(2));
  }));
  ASSERT_EQ(ready.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  EXPECT_TRUE(pool.Submit([finished] { finished->set_value(); }));
  EXPECT_FALSE(pool.Submit([] {}));
  release->set_value();
  pool.Stop();
  EXPECT_EQ(done.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  EXPECT_FALSE(pool.Submit([] {}));
}
TEST(BoundedExecutor, InvalidConfigurationIsRejected) {
  EXPECT_THROW(BoundedExecutor(0, 1), std::invalid_argument);
  EXPECT_THROW(BoundedExecutor(1, 0), std::invalid_argument);
}
}  // namespace taskrelay
