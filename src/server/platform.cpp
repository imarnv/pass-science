#include "server/platform.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <system_error>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <climits>
#  include <cstdlib>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace wb::server {

#if defined(_WIN32)
namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

bool launchEdgeAppWindow(const std::wstring& url) {
    const wchar_t* candidates[] = {L"ProgramFiles(x86)", L"ProgramFiles", L"LOCALAPPDATA"};
    for (const wchar_t* var : candidates) {
        wchar_t base[MAX_PATH];
        const DWORD len = GetEnvironmentVariableW(var, base, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) continue;
        const fs::path edge = fs::path(base) / L"Microsoft" / L"Edge" / L"Application" / L"msedge.exe";
        std::error_code ec;
        if (!fs::exists(edge, ec)) continue;
        const std::wstring args = L"--app=" + url + L" --new-window --window-size=1440,900";
        const auto result = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", edge.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL));
        if (result > 32) return true;
    }
    return false;
}
} // namespace
#endif

fs::path executableDirectory() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n < buffer.size()) {
            buffer.resize(n);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return fs::path(buffer).parent_path();
#else
    char buffer[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (n <= 0) return fs::current_path();
    buffer[n] = '\0';
    return fs::path(buffer).parent_path();
#endif
}

void openUserInterface(const std::string& url) {
#if defined(_WIN32)
    const std::wstring wurl = widen(url);
    if (launchEdgeAppWindow(wurl)) return;
    ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    const std::string cmd = "open '" + url + "' >/dev/null 2>&1 &";
    if (std::system(cmd.c_str()) != 0) std::cerr << "Open " << url << " in a browser.\n";
#else
    const std::string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
    if (std::system(cmd.c_str()) != 0) std::cerr << "Open " << url << " in a browser.\n";
#endif
}

void showFatalError(const std::string& message) {
#if defined(_WIN32)
    MessageBoxW(nullptr, widen(message).c_str(), L"Engineering Workbench", MB_OK | MB_ICONERROR);
#else
    std::cerr << "fatal: " << message << "\n";
#endif
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::open(const fs::path& preferredDirectory) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const fs::path& dir : {preferredDirectory, fs::temp_directory_path() / "EngineeringWorkbench"}) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path candidate = dir / "workbench.log";
        std::ofstream probe(candidate, std::ios::app);
        if (probe) {
            path_ = candidate;
            return;
        }
    }
}

void Logger::write(const char* level, const std::string& message) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    char stamp[32];
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
    char prefix[48];
    std::snprintf(prefix, sizeof(prefix), "%s.%03d %s ", stamp, static_cast<int>(ms), level);

    std::lock_guard<std::mutex> lock(mutex_);
#if !defined(_WIN32)
    std::cerr << prefix << message << "\n";
#endif
    if (path_.empty()) return;
    std::ofstream out(path_, std::ios::app);
    out << prefix << message << "\n";
}

} // namespace wb::server
