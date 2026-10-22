#pragma once
#include <functional>

#include "common/task/validation.h"
namespace taskrelay {
grpc::Status Process(const v1::TaskSpec& spec, v1::TaskResult* result,
                     const std::function<bool()>& cancelled);
}  // namespace taskrelay
