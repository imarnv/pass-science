#include "core/hardware/device.h"

namespace wb::hw {

std::string toString(DeviceKind kind) {
    switch (kind) {
    case DeviceKind::Microcontroller: return "microcontroller";
    case DeviceKind::Fpga: return "fpga";
    case DeviceKind::JtagAdapter: return "jtag-adapter";
    }
    return "unknown";
}

std::string toString(ConnectionState state) {
    switch (state) {
    case ConnectionState::Disconnected: return "disconnected";
    case ConnectionState::Connected: return "connected";
    case ConnectionState::Faulted: return "faulted";
    }
    return "unknown";
}

} // namespace wb::hw
