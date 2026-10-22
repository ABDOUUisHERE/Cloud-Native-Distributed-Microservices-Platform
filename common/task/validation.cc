#include "common/task/validation.h"

#include <openssl/rand.h>

#include <array>
#include <cstdint>
#include <stdexcept>

namespace taskrelay {
bool SafeAscii(std::string_view value, std::size_t maximum) {
  if (value.empty() || value.size() > maximum) return false;
  for (unsigned char c : value) {
    if (c < 33 || c > 126) return false;
  }
  return true;
}

bool DecodeUtf8(std::string_view value, std::vector<std::string>* codepoints) {
  for (std::size_t i = 0; i < value.size();) {
    const auto lead = static_cast<unsigned char>(value[i]);
    std::size_t length = 0;
    std::uint32_t point = 0;
    if (lead < 0x80) {
      length = 1;
      point = lead;
    } else if (lead >= 0xc2 && lead <= 0xdf) {
      length = 2;
      point = lead & 0x1f;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      length = 3;
      point = lead & 0x0f;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      length = 4;
      point = lead & 0x07;
    } else
      return false;
    if (i + length > value.size()) return false;
    for (std::size_t j = 1; j < length; ++j) {
      const auto c = static_cast<unsigned char>(value[i + j]);
      if ((c & 0xc0) != 0x80) return false;
      point = (point << 6) | (c & 0x3f);
    }
    if ((length == 2 && point < 0x80) || (length == 3 && point < 0x800) ||
        (length == 4 && point < 0x10000) || (point >= 0xd800 && point <= 0xdfff) ||
        point > 0x10ffff)
      return false;
    if (codepoints) codepoints->emplace_back(value.substr(i, length));
    i += length;
  }
  return true;
}

grpc::Status ValidateSpec(const v1::TaskSpec& spec) {
  const auto invalid = [](const char* message) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, message);
  };
  const auto& timeout = spec.execution_timeout();
  if (timeout.seconds() < 0 || timeout.seconds() > 30 || timeout.nanos() < 0 ||
      timeout.nanos() >= 1000000000 || (timeout.seconds() == 30 && timeout.nanos() != 0) ||
      (timeout.seconds() == 0 && timeout.nanos() < 1000000))
    return invalid("execution_timeout must be between 1 ms and 30 s");
  switch (spec.type()) {
    case v1::TASK_TYPE_NUMERIC:
      if (!spec.has_numeric() ||
          spec.numeric().operation() != v1::NumericInput::OPERATION_MONTE_CARLO_PI ||
          spec.numeric().iterations() == 0 || spec.numeric().iterations() > 1000000)
        return invalid("numeric input requires Monte Carlo pi and 1..1000000 iterations");
      break;
    case v1::TASK_TYPE_TRANSFORM:
      if (!spec.has_transform() || spec.transform().data().size() > 32768 ||
          !DecodeUtf8(spec.transform().data()) ||
          (spec.transform().operation() != v1::TransformInput::OPERATION_ASCII_UPPERCASE &&
           spec.transform().operation() != v1::TransformInput::OPERATION_REVERSE_CODEPOINTS))
        return invalid(
            "transform input requires a supported operation and at most 32 KiB valid UTF-8");
      break;
    case v1::TASK_TYPE_NETWORK_SIMULATION: {
      const auto& input = spec.network_simulation();
      if (!spec.has_network_simulation() || input.packet_count() == 0 ||
          input.packet_count() > 10000 || input.packet_size_bytes() == 0 ||
          input.packet_size_bytes() > 4096 ||
          static_cast<std::uint64_t>(input.packet_count()) * input.packet_size_bytes() > 16777216 ||
          input.loss_parts_per_million() > 1000000 || input.synthetic_delay().seconds() < 0 ||
          input.synthetic_delay().seconds() > 1 || input.synthetic_delay().nanos() < 0 ||
          input.synthetic_delay().nanos() >= 1000000000 ||
          (input.synthetic_delay().seconds() == 1 && input.synthetic_delay().nanos() != 0))
        return invalid("network simulation input exceeds local workload limits");
      break;
    }
    default:
      return invalid("unsupported task type");
  }
  return grpc::Status::OK;
}

std::string CanonicalSpec(const v1::TaskSpec& spec) {
  // Typed, fixed-order fields with a length-delimited string; unknown fields are excluded.
  std::string out = std::to_string(spec.type()) + ":" +
                    std::to_string(spec.execution_timeout().seconds()) + ":" +
                    std::to_string(spec.execution_timeout().nanos()) + ":";
  if (spec.has_numeric()) {
    out += std::to_string(spec.numeric().operation()) + ":" +
           std::to_string(spec.numeric().iterations()) + ":" +
           std::to_string(spec.numeric().seed());
  } else if (spec.has_transform()) {
    out += std::to_string(spec.transform().operation()) + ":" +
           std::to_string(spec.transform().data().size()) + ":" + spec.transform().data();
  } else {
    const auto& n = spec.network_simulation();
    out += std::to_string(n.packet_count()) + ":" + std::to_string(n.packet_size_bytes()) + ":" +
           std::to_string(n.seed()) + ":" + std::to_string(n.loss_parts_per_million()) + ":" +
           std::to_string(n.synthetic_delay().seconds()) + ":" +
           std::to_string(n.synthetic_delay().nanos());
  }
  return out;
}

std::string NewId() {
  std::array<unsigned char, 16> bytes{};
  if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
    throw std::runtime_error("Unable to obtain operating-system randomness");
  bytes[6] = (bytes[6] & 0x0f) | 0x40;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) result += '-';
    result += hex[bytes[i] >> 4];
    result += hex[bytes[i] & 0x0f];
  }
  return result;
}
}  // namespace taskrelay
