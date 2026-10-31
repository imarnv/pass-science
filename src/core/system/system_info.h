#pragma once
// Host and build description reported next to every benchmark result.

#include <cstdint>
#include <string>

namespace wb::sys {

struct SystemInfo {
    std::string os;
    std::string cpuModel;
    unsigned physicalCores = 0; // 0 if the platform does not report it
    unsigned logicalCpus = 0;
    std::uint64_t totalRamBytes = 0;
    std::string compiler;
    std::string buildType;
    std::string buildFlags;
    std::string appVersion;
};

SystemInfo querySystemInfo();

} // namespace wb::sys
