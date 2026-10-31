// Entry point of the packaged application (Launch.exe on Windows).
//
// 1. Starts a loopback-only HTTP server that exposes the C++ core.
// 2. Opens the web UI in its own window.
// 3. Exits when the UI's Quit button is pressed or when no UI has been in
//    contact for a while (window closed), so no orphan process is left behind.
//
// Options (for development and the editor run configuration):
//   --no-browser      do not open a window (print the URL instead)
//   --port N          preferred port (default 8765; falls back to a free port)
//   --web DIR         serve the UI from DIR instead of <exe dir>/app/web
//   --idle-exit S     seconds without UI contact before exiting (default 180, 0 = never)

#include "server/app_server.h"
#include "server/platform.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace wb::server;

namespace {

struct LaunchOptions {
    bool openBrowser = true;
    int port = 8765;
    fs::path webRoot;
    double idleExitSeconds = 180.0; // browsers throttle background timers to 1/min
};

LaunchOptions parseArgs(int argc, char** argv) {
    LaunchOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-browser") o.openBrowser = false;
        else if (a == "--port" && i + 1 < argc) o.port = std::stoi(argv[++i]);
        else if (a == "--web" && i + 1 < argc) o.webRoot = argv[++i];
        else if (a == "--idle-exit" && i + 1 < argc) o.idleExitSeconds = std::stod(argv[++i]);
    }
    return o;
}

} // namespace

int main(int argc, char** argv) {
    const auto processStart = std::chrono::steady_clock::now();
    const fs::path exeDir = executableDirectory();
    Logger::instance().open(exeDir / "logs");

    try {
        const LaunchOptions opts = parseArgs(argc, argv);
        ServerOptions server;
        server.processStart = processStart;
        server.preferredPort = opts.port;
        server.webRoot = opts.webRoot.empty() ? exeDir / "app" / "web" : opts.webRoot;
        if (!fs::exists(server.webRoot / "index.html")) {
            throw std::runtime_error("The user interface files were not found next to the program (expected " +
                                     (server.webRoot / "index.html").string() +
                                     "). Extract the whole ZIP archive, not just Launch.exe.");
        }
        Logger::instance().info("Starting Engineering Workbench from " + exeDir.string());

        AppServer app(server);
        const int port = app.bind();
        const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
        app.markReady();
        Logger::instance().info("Listening on " + url);

        std::thread serverThread([&app] { app.run(); });
        if (opts.openBrowser) openUserInterface(url);
        else std::cout << "Engineering Workbench running at " << url << std::endl;

        // Watchdog: stops the server when the UI asks to quit, or when the UI
        // has been silent for idleExitSeconds (window closed). The first UI
        // contact gets at least 2 minutes (slow first start of the browser).
        std::atomic<bool> serverStopped{false};
        std::thread watchdog([&app, &serverStopped, idle = opts.idleExitSeconds] {
            while (!serverStopped.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                if (app.shutdownRequested()) {
                    app.stop();
                    return;
                }
                const double limit = app.uiHasConnected() ? idle : std::max(idle, 120.0);
                if (idle > 0 && app.secondsSinceLastContact() > limit) {
                    Logger::instance().info("No UI contact for " + std::to_string(static_cast<int>(limit)) +
                                            " s; shutting down.");
                    app.stop();
                    return;
                }
            }
        });

        serverThread.join();
        serverStopped = true;
        watchdog.join();
        Logger::instance().info("Stopped.");
        return 0;
    } catch (const std::exception& e) {
        Logger::instance().error(e.what());
        showFatalError(std::string(e.what()) + "\n\nLog file: " + Logger::instance().path().string());
        return 1;
    }
}
