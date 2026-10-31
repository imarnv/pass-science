#pragma once
// Owns the adapters, performs discovery and applies the recovery policy.

#include "core/hardware/mock_devices.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace wb::hw {

struct RetryPolicy {
    int maxAttempts = 3;
    std::chrono::milliseconds timeout{250};
    std::chrono::milliseconds initialBackoff{50}; // doubled after each failed attempt
};

// One line of the host-side event log shown in the UI.
struct DeviceEvent {
    std::int64_t timestampMs = 0;
    std::string deviceId;
    std::string level;   // "info", "warning", "error"
    std::string message;
};

class DeviceManager {
public:
    DeviceManager();

    // Simulated enumeration. Returns the same three devices every time.
    std::vector<DeviceInfo> discover();

    // Connects with retries and exponential back-off. Returns the number of
    // attempts used. Throws DeviceTimeout if all attempts fail.
    int connect(const std::string& id);
    void disconnect(const std::string& id);
    // A single telemetry read; on timeout the device is marked faulted and
    // the caller is expected to reconnect (the UI offers a "Recover" action).
    Telemetry readTelemetry(const std::string& id);
    void injectFault(const std::string& id, FaultMode mode);

    ConnectionState state(const std::string& id) const;
    FaultMode fault(const std::string& id) const;
    std::vector<DeviceEvent> events() const;
    const RetryPolicy& policy() const { return policy_; }

    // Lets tests skip real sleeping during back-off.
    void setSleepFunction(std::function<void(std::chrono::milliseconds)> sleeper);

private:
    MockAdapterBase& find(const std::string& id) const;
    void log(const std::string& id, const std::string& level, const std::string& message);

    std::vector<std::unique_ptr<MockAdapterBase>> devices_;
    RetryPolicy policy_;
    std::function<void(std::chrono::milliseconds)> sleep_;
    mutable std::mutex logMutex_;
    std::vector<DeviceEvent> events_;
};

} // namespace wb::hw
