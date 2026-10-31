#pragma once
// HTTP transport: maps REST endpoints to core functions and serves the web UI.

#include "core/hardware/device_manager.h"
#include "core/parallel/batch.h"
#include "server/job_manager.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace httplib {
class Server;
}

namespace wb::server {

struct ServerOptions {
    std::filesystem::path webRoot;
    std::string host = "127.0.0.1"; // loopback only: never exposed to the network
    int preferredPort = 8765;
    std::chrono::steady_clock::time_point processStart;
};

class AppServer {
public:
    explicit AppServer(ServerOptions options);
    ~AppServer();

    // Binds to the preferred port or any free port. Returns the bound port.
    int bind();
    // Blocks serving requests until stop() is called.
    void run();
    void stop();

    // Seconds since the UI last reported in (heartbeat or any API call).
    double secondsSinceLastContact() const;
    bool uiHasConnected() const { return uiConnected_.load(); }
    void markReady();
    // Set by the UI's Quit action; the launcher's watchdog then calls stop().
    bool shutdownRequested() const { return shutdownRequested_.load(); }

private:
    void registerRoutes();
    void touch();

    ServerOptions options_;
    std::unique_ptr<httplib::Server> http_;
    hw::DeviceManager devices_;
    JobManager jobs_;

    std::mutex benchMutex_;
    std::optional<parallel::BenchmarkReport> lastBenchmark_;

    std::atomic<long long> lastContactMs_{0};
    std::atomic<bool> uiConnected_{false};
    std::atomic<bool> shutdownRequested_{false};
    double serverReadyMs_ = 0.0;
    std::atomic<double> uiReadyMs_{-1.0};
};

} // namespace wb::server
