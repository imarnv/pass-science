#include "core/hardware/device_manager.h"
#include "test_framework.h"

using namespace wb::hw;

// DeviceManager owns mutexes and is intentionally non-movable, so tests
// construct it in place and use this macro to disable real back-off sleeps.
#define MAKE_MANAGER(m) \
    DeviceManager m;    \
    m.setSleepFunction([](std::chrono::milliseconds) {})

TEST_CASE("hw: discovery reports one device of each kind, all simulated") {
    MAKE_MANAGER(m);
    const auto devices = m.discover();
    CHECK(devices.size() == 3);
    int mcu = 0, fpga = 0, jtag = 0;
    for (const auto& d : devices) {
        CHECK(d.simulated);
        CHECK(!d.capabilities.empty());
        mcu += d.kind == DeviceKind::Microcontroller;
        fpga += d.kind == DeviceKind::Fpga;
        jtag += d.kind == DeviceKind::JtagAdapter;
    }
    CHECK(mcu == 1 && fpga == 1 && jtag == 1);
}

TEST_CASE("hw: connect, telemetry and disconnect for every device") {
    MAKE_MANAGER(m);
    for (const auto& d : m.discover()) {
        CHECK(m.state(d.id) == ConnectionState::Disconnected);
        CHECK(m.connect(d.id) == 1);
        CHECK(m.state(d.id) == ConnectionState::Connected);
        const Telemetry t1 = m.readTelemetry(d.id);
        const Telemetry t2 = m.readTelemetry(d.id);
        CHECK(t1.deviceId == d.id);
        CHECK(!t1.values.empty());
        CHECK(t1.values.size() == t1.units.size());
        CHECK(t2.sequence == t1.sequence + 1);
        m.disconnect(d.id);
        CHECK(m.state(d.id) == ConnectionState::Disconnected);
    }
}

TEST_CASE("hw: JTAG adapter reports debug capabilities, not telemetry streaming") {
    MAKE_MANAGER(m);
    for (const auto& d : m.discover()) {
        bool hasScan = false, hasStream = false;
        for (const auto& c : d.capabilities) {
            hasScan |= c == "jtag.scan_chain";
            hasStream |= c == "telemetry.stream";
        }
        CHECK(hasScan == (d.kind == DeviceKind::JtagAdapter));
        CHECK(hasStream == (d.kind != DeviceKind::JtagAdapter));
    }
}

TEST_CASE("hw: telemetry while disconnected is a state error") {
    MAKE_MANAGER(m);
    CHECK_THROWS_WITH(m.readTelemetry("mcu-0"), DeviceStateError, "not connected");
    CHECK_THROWS_WITH(m.connect("nope"), std::invalid_argument, "Unknown device");
}

TEST_CASE("hw: single timeout on connect is recovered by retry") {
    MAKE_MANAGER(m);
    m.injectFault("fpga-0", FaultMode::TimeoutOnce);
    CHECK(m.connect("fpga-0") == 2);
    CHECK(m.state("fpga-0") == ConnectionState::Connected);
    CHECK(m.fault("fpga-0") == FaultMode::None);
}

TEST_CASE("hw: telemetry timeout faults the device; reconnect recovers") {
    MAKE_MANAGER(m);
    m.connect("mcu-0");
    m.injectFault("mcu-0", FaultMode::TimeoutOnce);
    CHECK_THROWS_WITH(m.readTelemetry("mcu-0"), DeviceTimeout, "SIMULATED");
    CHECK(m.state("mcu-0") == ConnectionState::Faulted);
    CHECK_THROWS_AS(m.readTelemetry("mcu-0"), DeviceStateError);
    CHECK(m.connect("mcu-0") == 1);
    CHECK(m.readTelemetry("mcu-0").sequence == 1);
}

TEST_CASE("hw: persistent timeout exhausts retries, clearing the fault recovers") {
    MAKE_MANAGER(m);
    m.injectFault("jtag-0", FaultMode::TimeoutAlways);
    CHECK_THROWS_AS(m.connect("jtag-0"), DeviceTimeout);
    CHECK(m.state("jtag-0") == ConnectionState::Faulted);
    m.injectFault("jtag-0", FaultMode::None);
    CHECK(m.connect("jtag-0") == 1);
    bool sawError = false;
    for (const auto& e : m.events()) sawError |= e.level == "error";
    CHECK(sawError);
}
