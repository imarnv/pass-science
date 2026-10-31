#pragma once
// Simulated adapters for the three device types. Nothing here touches real
// hardware, serial ports or USB: responses are generated from a seeded model
// so tests and demos are repeatable. Each adapter supports fault injection so
// the host's timeout/recovery path can be demonstrated without a board.

#include "core/hardware/device.h"

#include <mutex>
#include <random>

namespace wb::hw {

enum class FaultMode {
    None,
    TimeoutOnce,     // the next connect or telemetry request times out, then the device recovers
    TimeoutAlways,   // every request times out until the fault is cleared (cable unplugged)
};

class MockAdapterBase : public IDeviceAdapter {
public:
    MockAdapterBase(DeviceInfo info, std::uint64_t seed);

    const DeviceInfo& info() const override { return info_; }
    ConnectionState state() const override;

    void connect(std::chrono::milliseconds timeout) override;
    void disconnect() override;
    Telemetry readTelemetry(std::chrono::milliseconds timeout) override;

    void injectFault(FaultMode mode);
    FaultMode fault() const;

protected:
    // Fills device-specific telemetry values; called with the lock held.
    virtual void sample(Telemetry& t, std::mt19937_64& rng) = 0;
    double noise(std::mt19937_64& rng, double amplitude);

private:
    void maybeTimeout(const char* operation, std::chrono::milliseconds timeout);

    DeviceInfo info_;
    mutable std::mutex mutex_;
    ConnectionState state_ = ConnectionState::Disconnected;
    FaultMode fault_ = FaultMode::None;
    std::mt19937_64 rng_;
    std::uint64_t sequence_ = 0;
};

// Microcontroller board, e.g. an STM32 Nucleo-64 class board: 3.3 V I/O,
// host link through the on-board ST-LINK virtual COM port.
class MockMicrocontroller final : public MockAdapterBase {
public:
    explicit MockMicrocontroller(std::string id = "mcu-0");
protected:
    void sample(Telemetry& t, std::mt19937_64& rng) override;
private:
    double adcPhase_ = 0.0;
};

// FPGA board, e.g. a Digilent Arty A7-35T class board: 3.3 V I/O banks,
// 1.0 V core, host link through the on-board FTDI UART bridge.
class MockFpga final : public MockAdapterBase {
public:
    explicit MockFpga(std::string id = "fpga-0");
protected:
    void sample(Telemetry& t, std::mt19937_64& rng) override;
};

// JTAG debug adapter, e.g. an FT2232H-based probe: senses target VREF and
// level-translates TCK/TMS/TDI/TDO/nTRST to the target's I/O voltage.
class MockJtagAdapter final : public MockAdapterBase {
public:
    explicit MockJtagAdapter(std::string id = "jtag-0");
protected:
    void sample(Telemetry& t, std::mt19937_64& rng) override;
};

} // namespace wb::hw
