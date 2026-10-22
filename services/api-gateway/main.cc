#include <drogon/HttpAppFramework.h>
#include <pthread.h>
#include <signal.h>

#include <charconv>
#include <iostream>
#include <thread>

#include "common/grpc/state_client.h"
#include "services/api-gateway/routes.h"
int main(int argc, char* argv[]) {
  if (argc > 4) {
    std::cerr << "Usage: taskrelay-gateway [port] [router-target] [state-target]\n";
    return 2;
  }
  unsigned int port = 8080;
  if (argc >= 2) {
    const std::string value = argv[1];
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), port);
    if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() || port > 65535)
      return 2;
  }
  try {
    taskrelay::BlockStopSignals();
    taskrelay::Routes routes({.router = argc >= 3 ? argv[2] : "127.0.0.1:50053",
                              .state = argc == 4 ? argv[3] : "127.0.0.1:50052"});
    auto& app = drogon::app();
    app.disableSigtermHandling()
        .setThreadNum(2)
        .setLogLevel(trantor::Logger::kWarn)
        .setFileTypes({})
        .setMaxConnectionNum(128)
        .setIdleConnectionTimeout(5)
        .setClientMaxBodySize(65536)
        .setClientMaxMemoryBodySize(65536)
        .addListener("127.0.0.1", static_cast<std::uint16_t>(port));
    routes.Register();
    app.registerBeginningAdvice([&app] {
      for (const auto& listener : app.getListeners())
        std::cout << "HTTP bound port: " << listener.toPort() << '\n' << std::flush;
    });
    // The HTTP event loop remains responsive while the signal thread waits.
    std::jthread signal_thread([&app](const std::stop_token& stop) {
      sigset_t signals;
      sigemptyset(&signals);
      sigaddset(&signals, SIGINT);
      sigaddset(&signals, SIGTERM);
      while (!stop.stop_requested()) {
        timespec timeout{0, 100000000};
        if (sigtimedwait(&signals, nullptr, &timeout) > 0) {
          app.quit();
          return;
        }
      }
    });
    app.run();
    signal_thread.request_stop();
    signal_thread.join();
    routes.Stop();
    return 0;
  } catch (const std::exception&) {
    std::cerr << "Gateway startup failed\n";
    return 1;
  }
}
