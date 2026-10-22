#include "services/processing-worker/workloads.h"

#include <gtest/gtest.h>
namespace taskrelay {
namespace {
v1::TaskSpec Numeric() {
  v1::TaskSpec spec;
  spec.set_type(v1::TASK_TYPE_NUMERIC);
  spec.mutable_execution_timeout()->set_seconds(2);
  spec.mutable_numeric()->set_operation(v1::NumericInput::OPERATION_MONTE_CARLO_PI);
  spec.mutable_numeric()->set_iterations(10000);
  spec.mutable_numeric()->set_seed(42);
  return spec;
}
TEST(Validation, RejectsInvalidLimitsAndMismatchedInput) {
  auto spec = Numeric();
  ASSERT_TRUE(ValidateSpec(spec).ok());
  spec.mutable_numeric()->set_iterations(1000001);
  EXPECT_FALSE(ValidateSpec(spec).ok());
  spec = Numeric();
  spec.set_type(v1::TASK_TYPE_TRANSFORM);
  EXPECT_FALSE(ValidateSpec(spec).ok());
  spec = Numeric();
  spec.mutable_execution_timeout()->set_seconds(31);
  EXPECT_FALSE(ValidateSpec(spec).ok());
  spec = Numeric();
  spec.mutable_execution_timeout()->set_nanos(-1);
  EXPECT_FALSE(ValidateSpec(spec).ok());
}
TEST(Validation, RejectsOverlongSurrogateAndTruncatedUtf8) {
  EXPECT_FALSE(DecodeUtf8(std::string("\xc0\xaf", 2)));
  EXPECT_FALSE(DecodeUtf8(std::string("\xed\xa0\x80", 3)));
  EXPECT_FALSE(DecodeUtf8(std::string("\xf0\x9f", 2)));
  EXPECT_FALSE(DecodeUtf8(std::string("\xf4\x90\x80\x80", 4)));
  EXPECT_TRUE(DecodeUtf8("hello \xc3\xa9 \xf0\x9f\x8c\x8d"));
}
TEST(Workloads, NumericIsSeededAndBounded) {
  const auto spec = Numeric();
  v1::TaskResult first, second;
  ASSERT_TRUE(Process(spec, &first, [] { return false; }).ok());
  ASSERT_TRUE(Process(spec, &second, [] { return false; }).ok());
  EXPECT_EQ(first.SerializeAsString(), second.SerializeAsString());
  EXPECT_GT(first.numeric().value(), 3.0);
  EXPECT_LT(first.numeric().value(), 3.3);
}
TEST(Workloads, ReversesCodepointsAndPreservesAsciiUppercaseSemantics) {
  v1::TaskSpec spec;
  spec.set_type(v1::TASK_TYPE_TRANSFORM);
  spec.mutable_execution_timeout()->set_seconds(1);
  auto* input = spec.mutable_transform();
  input->set_operation(v1::TransformInput::OPERATION_REVERSE_CODEPOINTS);
  input->set_data("a\xc3\xa9\xf0\x9f\x8c\x8d");
  v1::TaskResult result;
  ASSERT_TRUE(Process(spec, &result, [] { return false; }).ok());
  EXPECT_EQ(result.transform().data(),
            "\xf0\x9f\x8c\x8d\xc3\xa9"
            "a");
  input->set_operation(v1::TransformInput::OPERATION_ASCII_UPPERCASE);
  ASSERT_TRUE(Process(spec, &result, [] { return false; }).ok());
  EXPECT_EQ(result.transform().data(), "A\xc3\xa9\xf0\x9f\x8c\x8d");
}
TEST(Workloads, NetworkIsLocalDeterministicAndModelsDelay) {
  v1::TaskSpec spec;
  spec.set_type(v1::TASK_TYPE_NETWORK_SIMULATION);
  spec.mutable_execution_timeout()->set_seconds(1);
  auto* input = spec.mutable_network_simulation();
  input->set_packet_count(20);
  input->set_packet_size_bytes(32);
  input->set_seed(5);
  input->set_loss_parts_per_million(1000000);
  input->mutable_synthetic_delay()->set_nanos(1000000);
  v1::TaskResult result;
  ASSERT_TRUE(Process(spec, &result, [] { return false; }).ok());
  EXPECT_EQ(result.network_simulation().dropped_packets(), 20);
  EXPECT_EQ(result.network_simulation().simulated_duration().nanos(), 20000000);
  input->set_loss_parts_per_million(0);
  ASSERT_TRUE(Process(spec, &result, [] { return false; }).ok());
  EXPECT_EQ(result.network_simulation().delivered_bytes(), 640);
}
TEST(Workloads, CancellationStopsBeforeAndDuringComputation) {
  v1::TaskResult result;
  EXPECT_EQ(Process(Numeric(), &result, [] { return true; }).error_code(),
            grpc::StatusCode::DEADLINE_EXCEEDED);
  int polls = 0;
  EXPECT_EQ(Process(Numeric(), &result, [&polls] { return ++polls > 3; }).error_code(),
            grpc::StatusCode::DEADLINE_EXCEEDED);
}
TEST(Validation, CanonicalFingerprintUsesTypedValues) {
  auto a = Numeric(), b = a;
  b.mutable_numeric()->set_seed(43);
  EXPECT_NE(CanonicalSpec(a), CanonicalSpec(b));
  a.GetReflection()->MutableUnknownFields(&a)->AddVarint(200, 1);
  b = Numeric();
  EXPECT_EQ(CanonicalSpec(a), CanonicalSpec(b));
}
}  // namespace
}  // namespace taskrelay
