#include "services/processing-worker/workloads.h"

#include <cstdint>
namespace taskrelay {
namespace {
// SplitMix64 fixes seeded output independently of library distribution implementations.
std::uint64_t Next(std::uint64_t& state) {
  auto value = (state += 0x9e3779b97f4a7c15ULL);
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}
grpc::Status Cancelled() {
  return {grpc::StatusCode::DEADLINE_EXCEEDED, "Processing was cancelled"};
}
}  // namespace
grpc::Status Process(const v1::TaskSpec& spec, v1::TaskResult* result,
                     const std::function<bool()>& cancelled) {
  auto status = ValidateSpec(spec);
  if (!status.ok()) return status;
  if (cancelled()) return Cancelled();
  result->Clear();
  if (spec.has_numeric()) {
    auto seed = spec.numeric().seed();
    std::uint64_t inside = 0;
    for (std::uint64_t i = 0; i < spec.numeric().iterations(); ++i) {
      if ((i & 1023) == 0 && cancelled()) return Cancelled();
      const double x = static_cast<double>(Next(seed) >> 11) * 0x1.0p-53;
      const double y = static_cast<double>(Next(seed) >> 11) * 0x1.0p-53;
      if (x * x + y * y <= 1.0) ++inside;
    }
    result->mutable_numeric()->set_value(4.0 * static_cast<double>(inside) /
                                         static_cast<double>(spec.numeric().iterations()));
    result->mutable_numeric()->set_iterations(spec.numeric().iterations());
  } else if (spec.has_transform()) {
    std::string output;
    if (spec.transform().operation() == v1::TransformInput::OPERATION_ASCII_UPPERCASE) {
      output = spec.transform().data();
      for (char& c : output)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    } else {
      std::vector<std::string> points;
      DecodeUtf8(spec.transform().data(), &points);
      for (auto it = points.rbegin(); it != points.rend(); ++it) output += *it;
    }
    result->mutable_transform()->set_data(output);
  } else {
    const auto& input = spec.network_simulation();
    auto seed = input.seed();
    std::uint32_t delivered = 0;
    // Every packet is entirely local. Simulated delay is model time, not a sleep.
    for (std::uint32_t i = 0; i < input.packet_count(); ++i) {
      if ((i & 63) == 0 && cancelled()) return Cancelled();
      const bool drop = Next(seed) % 1000000 < input.loss_parts_per_million();
      std::uint64_t checksum = 0;
      for (std::uint32_t byte = 0; byte < input.packet_size_bytes(); ++byte)
        checksum = (checksum * 33) ^ Next(seed);
      seed ^= checksum;
      if (!drop) ++delivered;
    }
    auto* output = result->mutable_network_simulation();
    output->set_delivered_packets(delivered);
    output->set_dropped_packets(input.packet_count() - delivered);
    output->set_delivered_bytes(static_cast<std::uint64_t>(delivered) * input.packet_size_bytes());
    const auto nanos =
        (input.synthetic_delay().seconds() * 1000000000LL + input.synthetic_delay().nanos()) *
        input.packet_count();
    output->mutable_simulated_duration()->set_seconds(nanos / 1000000000);
    output->mutable_simulated_duration()->set_nanos(static_cast<int>(nanos % 1000000000));
  }
  return cancelled() ? Cancelled() : grpc::Status::OK;
}
}  // namespace taskrelay
