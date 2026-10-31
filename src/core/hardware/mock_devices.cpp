#include "core/hardware/mock_devices.h"

#include <cmath>

namespace wb::hw {

namespace {

std::int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void put(Telemetry& t, const std::string& name, double value, const std::string& unit) {
    t.values[name] = value;
    t.units[name] = unit;
}

} // namespace

MockAdapterBase::MockAdapterBase(DeviceInfo info, std::uint64_t seed) : info_(std::move(info)), rng_(seed) {
    info_.simulated = true;
}

ConnectionState MockAdapterBase::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void MockAdapterBase::maybeTimeout(const char* operation, std::chrono::milliseconds timeout) {
    if (fault_ == FaultMode::None) return;
    if (fault_ == FaultMode::TimeoutOnce) fault_ = FaultMode::None;
    // A real link would block for `timeout` here; the mock reports immediately
    // so the UI stays responsive, but the message carries the configured value.
    state_ = ConnectionState::Faulted;
    throw DeviceTimeout("[SIMULATED] " + info_.id + ": no response to " + operation + " within " +
                        std::to_string(timeout.count()) + " ms.");
}

void MockAdapterBase::connect(std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == ConnectionState::Connected) return; // idempotent
    maybeTimeout("connect handshake", timeout);
    state_ = ConnectionState::Connected;
    sequence_ = 0;
}

void MockAdapterBase::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = ConnectionState::Disconnected;
}

Telemetry MockAdapterBase::readTelemetry(std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == ConnectionState::Disconnected) {
        throw DeviceStateError(info_.id + " is not connected. Connect the device before requesting telemetry.");
    }
    if (state_ == ConnectionState::Faulted) {
        throw DeviceStateError(info_.id + " is in a faulted state. Reconnect to recover.");
    }
    maybeTimeout("telemetry request", timeout);
    Telemetry t;
    t.deviceId = info_.id;
    t.sequence = ++sequence_;
    t.timestampMs = nowMs();
    sample(t, rng_);
    return t;
}

void MockAdapterBase::injectFault(FaultMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    fault_ = mode;
}

FaultMode MockAdapterBase::fault() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fault_;
}

double MockAdapterBase::noise(std::mt19937_64& rng, double amplitude) {
    const double u = static_cast<double>(rng() >> 11) * 0x1.0p-53; // [0, 1)
    return amplitude * (2.0 * u - 1.0);
}

// ---------------------------------------------------------------------------

MockMicrocontroller::MockMicrocontroller(std::string id)
    : MockAdapterBase(
          DeviceInfo{std::move(id), DeviceKind::Microcontroller, "STM32 Nucleo-64 class (Cortex-M4F)",
                     "USB CDC-ACM virtual COM, UART 115200 8N1, newline-delimited JSON", "SIM-COM3",
                     "telemetry-fw 0.3.1 (simulated)", 3.3,
                     {"telemetry.stream", "gpio.read", "gpio.write", "adc.12bit", "uart.115200"}, true},
          0x4D4355) {}

void MockMicrocontroller::sample(Telemetry& t, std::mt19937_64& rng) {
    adcPhase_ += 0.35;
    put(t, "die_temperature", 36.5 + noise(rng, 0.4), "C");
    put(t, "vdd", 3.30 + noise(rng, 0.01), "V");
    put(t, "adc_ch0", 1.65 + 0.8 * std::sin(adcPhase_) + noise(rng, 0.02), "V");
    put(t, "loop_rate", 1000.0 + noise(rng, 3.0), "Hz");
}

MockFpga::MockFpga(std::string id)
    : MockAdapterBase(
          DeviceInfo{std::move(id), DeviceKind::Fpga, "Digilent Arty A7-35T class (Artix-7)",
                     "USB-UART bridge (FTDI channel B), 921600 8N1, framed binary", "SIM-COM4",
                     "bitstream telemetry_top r12 (simulated)", 3.3,
                     {"telemetry.stream", "xadc.temperature", "xadc.vccint", "clock.status", "register.read"},
                     true},
          0x465047) {}

void MockFpga::sample(Telemetry& t, std::mt19937_64& rng) {
    put(t, "core_temperature", 48.0 + noise(rng, 0.6), "C");
    put(t, "vccint", 1.00 + noise(rng, 0.005), "V");
    put(t, "vccaux", 1.80 + noise(rng, 0.008), "V");
    put(t, "mmcm_locked", 1.0, "bool");
    put(t, "fabric_clock", 100.0, "MHz");
}

MockJtagAdapter::MockJtagAdapter(std::string id)
    : MockAdapterBase(
          DeviceInfo{std::move(id), DeviceKind::JtagAdapter, "FT2232H-based JTAG probe (MPSSE)",
                     "USB 2.0 High-Speed bulk (vendor driver), JTAG via MPSSE", "SIM-USB-0403:6010",
                     "probe-fw n/a (FTDI MPSSE, simulated)", 3.3,
                     {"jtag.scan_chain", "jtag.idcode", "jtag.tck_config", "target.vref_sense"}, true},
          0x4A5441) {}

void MockJtagAdapter::sample(Telemetry& t, std::mt19937_64& rng) {
    put(t, "target_vref", 3.29 + noise(rng, 0.01), "V");
    put(t, "tck_frequency", 6.0, "MHz");
    put(t, "chain_length", 1.0, "devices");
    // IDCODE of the (simulated) device on the scan chain: Xilinx XC7A35T.
    put(t, "idcode", static_cast<double>(0x0362D093u), "hex");
}

} // namespace wb::hw
