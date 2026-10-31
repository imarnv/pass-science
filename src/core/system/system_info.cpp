#include "core/system/system_info.h"

#include <cstring>
#include <set>
#include <thread>
#include <utility>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <vector>
#else
#  include <fstream>
#  include <sys/utsname.h>
#  include <unistd.h>
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  if defined(_MSC_VER)
#    include <intrin.h>
#  else
#    include <cpuid.h>
#  endif
#  define WB_HAS_CPUID 1
#endif

#ifndef WB_BUILD_FLAGS
#  define WB_BUILD_FLAGS "unknown"
#endif
#ifndef WB_BUILD_TYPE
#  define WB_BUILD_TYPE "unknown"
#endif
#ifndef WB_VERSION
#  define WB_VERSION "dev"
#endif

namespace wb::sys {

namespace {

std::string compilerDescription() {
#if defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__MINGW64__)
    return std::string("GCC ") + __VERSION__ + " (MinGW-w64)";
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_FULL_VER);
#else
    return "unknown compiler";
#endif
}

std::string cpuBrandFromCpuid() {
#ifdef WB_HAS_CPUID
    unsigned regs[12] = {};
#  if defined(_MSC_VER)
    int info[4];
    __cpuid(info, 0x80000000);
    if (static_cast<unsigned>(info[0]) < 0x80000004) return {};
    for (int i = 0; i < 3; ++i) {
        __cpuid(info, 0x80000002 + i);
        std::memcpy(regs + 4 * i, info, sizeof(info));
    }
#  else
    if (__get_cpuid_max(0x80000000, nullptr) < 0x80000004) return {};
    for (unsigned i = 0; i < 3; ++i) {
        __get_cpuid(0x80000002 + i, &regs[4 * i], &regs[4 * i + 1], &regs[4 * i + 2], &regs[4 * i + 3]);
    }
#  endif
    char brand[49] = {};
    std::memcpy(brand, regs, 48);
    std::string s(brand);
    const auto first = s.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : s.substr(first);
#else
    return {};
#endif
}

#if defined(_WIN32)
void fillPlatform(SystemInfo& info) {
    info.os = "Windows";
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        OSVERSIONINFOW v{};
        v.dwOSVersionInfoSize = sizeof(v);
        if (fn && fn(&v) == 0) {
            info.os += (v.dwBuildNumber >= 22000 ? " 11" : " 10");
            info.os += " (build " + std::to_string(v.dwBuildNumber) + ")";
        }
    }

    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    std::vector<unsigned char> buffer(length);
    if (length && GetLogicalProcessorInformationEx(
                      RelationProcessorCore,
                      reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()), &length)) {
        unsigned cores = 0;
        for (DWORD offset = 0; offset < length;) {
            auto* item = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
            if (item->Relationship == RelationProcessorCore) ++cores;
            offset += item->Size;
        }
        info.physicalCores = cores;
    }

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) info.totalRamBytes = mem.ullTotalPhys;
}
#else
void fillPlatform(SystemInfo& info) {
    utsname u{};
    info.os = uname(&u) == 0 ? std::string(u.sysname) + " " + u.release : "POSIX";

    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line, modelName;
    std::set<std::pair<int, int>> cores; // (physical id, core id)
    int physicalId = 0;
    while (std::getline(cpuinfo, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, line.find_last_not_of(" \t", colon - 1) + 1);
        const std::string value = colon + 2 <= line.size() ? line.substr(colon + 2) : "";
        if (key == "model name" && modelName.empty()) modelName = value;
        else if (key == "physical id") physicalId = std::stoi(value);
        else if (key == "core id") cores.insert({physicalId, std::stoi(value)});
    }
    if (!modelName.empty()) info.cpuModel = modelName;
    info.physicalCores = static_cast<unsigned>(cores.size());

    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) {
        info.totalRamBytes = static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize);
    }
}
#endif

} // namespace

SystemInfo querySystemInfo() {
    SystemInfo info;
    info.cpuModel = cpuBrandFromCpuid();
    info.logicalCpus = std::thread::hardware_concurrency();
    fillPlatform(info);
    if (info.cpuModel.empty()) info.cpuModel = "unknown CPU";
    info.compiler = compilerDescription();
    info.buildType = WB_BUILD_TYPE;
    info.buildFlags = WB_BUILD_FLAGS;
    info.appVersion = WB_VERSION;
    return info;
}

} // namespace wb::sys
