#pragma once

#include <grpcpp/client_context.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace taskrelay {

inline void SetDeadline(
    grpc::ClientContext& context, std::chrono::milliseconds budget,
    std::chrono::system_clock::time_point parent = std::chrono::system_clock::time_point::max()) {
  if (budget <= std::chrono::milliseconds::zero() || budget > std::chrono::seconds(30)) {
    throw std::invalid_argument("RPC budget must be greater than zero and at most 30 seconds");
  }
  context.set_deadline(std::min(std::chrono::system_clock::now() + budget, parent));
}

}  // namespace taskrelay
