#pragma once

#include <grpcpp/support/status.h>

#include <string>
#include <string_view>
#include <vector>

#include "taskrelay/v1/task.pb.h"

namespace taskrelay {
grpc::Status ValidateSpec(const v1::TaskSpec& spec);
bool SafeAscii(std::string_view value, std::size_t maximum);
bool DecodeUtf8(std::string_view value, std::vector<std::string>* codepoints = nullptr);
std::string CanonicalSpec(const v1::TaskSpec& spec);
std::string NewId();
}  // namespace taskrelay
