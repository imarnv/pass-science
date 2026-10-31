#pragma once
// Host-side device abstraction for the embedded workbench.
//
// Every board is reached through an IDeviceAdapter. The three adapter kinds
// are deliberately NOT interchangeable: a microcontroller and an FPGA are
// targets that run user logic and stream telemetry, while a JTAG adapter is a
// debug probe that drives TCK/TMS/TDI and reads TDO on *another* target. The
// capability list, not the kind, tells the host what an adapter can do.
//
// ALL adapters in this project are SIMULATED (see mock_devices.h).

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace wb::hw {

enum class DeviceKind { Microcontroller, Fpga, JtagAdapter };
enum class ConnectionState { Disconnected, Connected, Faulted };

std::string toString(DeviceKind kind);
std::string toString(ConnectionState state);

struct DeviceInfo {
    std::string id;           // stable identifier, e.g. "mcu-0"
    DeviceKind kind = DeviceKind::Microcontroller;
    std::string model;        // board family assumed by the design documents
    std::string transport;    // host link, e.g. "USB CDC-ACM (UART 115200 8N1)"
    std::string port;         // simulated OS port name
    std::string firmware;     // firmware / bitstream / probe firmware version
    double ioVoltage = 3.3;   // logic level of the device's user I/O [V]
    std::vector<std::string> capabilities;
    bool simulated = true;
};

struct Telemetry {
    std::string deviceId;
    std::uint64_t sequence = 0;
    std::int64_t timestampMs = 0;              // host time, ms since epoch
    std::map<std::string, double> values;      // name -> value
    std::map<std::string, std::string> units;  // name -> unit
};

// A link-level timeout. Recoverable: the host may retry with back-off.
class DeviceTimeout : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The operation is not valid in the current state (e.g. telemetry while
// disconnected). Not a device fault; indicates a host/user sequencing error.
class DeviceStateError : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

class IDeviceAdapter {
public:
    virtual ~IDeviceAdapter() = default;

    virtual const DeviceInfo& info() const = 0;
    virtual ConnectionState state() const = 0;

    virtual void connect(std::chrono::milliseconds timeout) = 0; // may throw DeviceTimeout
    virtual void disconnect() = 0;
    virtual Telemetry readTelemetry(std::chrono::milliseconds timeout) = 0;
};

} // namespace wb::hw
