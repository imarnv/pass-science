#include "core/hardware/device_manager.h"

#include <thread>

namespace wb::hw {

namespace {
constexpr std::size_t kMaxEvents = 200;

std::int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
} // namespace

DeviceManager::DeviceManager()
    : sleep_([](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); }) {
    devices_.push_back(std::make_unique<MockMicrocontroller>());
    devices_.push_back(std::make_unique<MockFpga>());
    devices_.push_back(std::make_unique<MockJtagAdapter>());
}

std::vector<DeviceInfo> DeviceManager::discover() {
    std::vector<DeviceInfo> found;
    for (const auto& d : devices_) found.push_back(d->info());
    log("host", "info", "[SIMULATED] Enumeration found " + std::to_string(found.size()) + " devices.");
    return found;
}

int DeviceManager::connect(const std::string& id) {
    MockAdapterBase& device = find(id);
    std::chrono::milliseconds backoff = policy_.initialBackoff;
    for (int attempt = 1; attempt <= policy_.maxAttempts; ++attempt) {
        try {
            device.connect(policy_.timeout);
            log(id, "info", "Connected on attempt " + std::to_string(attempt) + ".");
            return attempt;
        } catch (const DeviceTimeout& e) {
            log(id, "warning", std::string(e.what()) + " Attempt " + std::to_string(attempt) + "/" +
                                   std::to_string(policy_.maxAttempts) + ".");
            if (attempt == policy_.maxAttempts) {
                log(id, "error", "Giving up after " + std::to_string(attempt) +
                                     " attempts. Check the cable/power, then retry.");
                throw;
            }
            sleep_(backoff);
            backoff *= 2;
        }
    }
    return policy_.maxAttempts; // unreachable
}

void DeviceManager::disconnect(const std::string& id) {
    find(id).disconnect();
    log(id, "info", "Disconnected.");
}

Telemetry DeviceManager::readTelemetry(const std::string& id) {
    try {
        return find(id).readTelemetry(policy_.timeout);
    } catch (const DeviceTimeout& e) {
        log(id, "error", std::string(e.what()) + " Device marked faulted; reconnect to recover.");
        throw;
    }
}

void DeviceManager::injectFault(const std::string& id, FaultMode mode) {
    find(id).injectFault(mode);
    const char* label = mode == FaultMode::None          ? "cleared"
                        : mode == FaultMode::TimeoutOnce ? "single timeout armed"
                                                         : "persistent timeout armed (link down)";
    log(id, "warning", std::string("[SIMULATED] Fault injection: ") + label + ".");
}

ConnectionState DeviceManager::state(const std::string& id) const { return find(id).state(); }
FaultMode DeviceManager::fault(const std::string& id) const { return find(id).fault(); }

std::vector<DeviceEvent> DeviceManager::events() const {
    std::lock_guard<std::mutex> lock(logMutex_);
    return events_;
}

void DeviceManager::setSleepFunction(std::function<void(std::chrono::milliseconds)> sleeper) {
    sleep_ = std::move(sleeper);
}

MockAdapterBase& DeviceManager::find(const std::string& id) const {
    for (const auto& d : devices_) {
        if (d->info().id == id) return *d;
    }
    throw std::invalid_argument("Unknown device id '" + id + "'. Run discovery first.");
}

void DeviceManager::log(const std::string& id, const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex_);
    events_.push_back({nowMs(), id, level, message});
    if (events_.size() > kMaxEvents) events_.erase(events_.begin());
}

} // namespace wb::hw
