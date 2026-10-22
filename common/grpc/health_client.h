#pragma once

#include <grpcpp/channel.h>
#include <grpcpp/support/status.h>

#include <chrono>
#include <memory>
#include <string>

namespace taskrelay {

inline constexpr char kFoundationWorkerHealth[] = "taskrelay.foundation.worker";
inline constexpr int kMaxGrpcMessageBytes = 1024 * 1024;

std::shared_ptr<grpc::Channel> MakeWorkerChannel(const std::string& target);

grpc::Status ProbeWorker(
    const std::shared_ptr<grpc::Channel>& channel, const std::string& service,
    std::chrono::milliseconds budget = std::chrono::milliseconds(500),
    std::chrono::system_clock::time_point parent = std::chrono::system_clock::time_point::max());

}  // namespace taskrelay
