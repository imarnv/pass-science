#pragma once
// Thin OS layer for the launcher: paths, opening the UI window, logging.

#include <filesystem>
#include <mutex>
#include <string>

namespace wb::server {

// Directory that contains the running executable.
std::filesystem::path executableDirectory();

// Opens the UI. On Windows, Microsoft Edge (present on every Windows 11
// install) is started in --app mode so the workbench gets its own window
// without browser chrome; if that fails the default browser is used.
void openUserInterface(const std::string& url);

// Shows a native error dialog (Windows) or prints to stderr (elsewhere).
// Used only for failures that happen before the UI is available.
void showFatalError(const std::string& message);

// Append-only, thread-safe log file. Falls back to the temp directory if the
// application folder is read-only (e.g. extracted under Program Files).
class Logger {
public:
    static Logger& instance();
    void open(const std::filesystem::path& preferredDirectory);
    void info(const std::string& message) { write("INFO ", message); }
    void warn(const std::string& message) { write("WARN ", message); }
    void error(const std::string& message) { write("ERROR", message); }
    std::filesystem::path path() const { return path_; }

private:
    void write(const char* level, const std::string& message);
    std::mutex mutex_;
    std::filesystem::path path_;
};

} // namespace wb::server
