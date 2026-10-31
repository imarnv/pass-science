#include "server/app_server.h"

#include "core/io/nodal_csv_parser.h"
#include "core/io/stl_parser.h"
#include "core/io/text_utils.h"
#include "core/report/csv_export.h"
#include "core/system/system_info.h"
#include "server/json_mapping.h"
#include "server/platform.h"

#include <httplib/httplib.h>

#include <cmath>
#include <thread>

namespace wb::server {

namespace {

constexpr int kMaxStudyMeshes = 8;
constexpr int kMaxDisplayElements = 100'000; // per mesh returned to the UI as JSON

long long steadyMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void sendJson(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

void sendError(httplib::Response& res, int status, const std::string& message, std::size_t line = 0) {
    json body{{"error", message}};
    if (line) body["line"] = line;
    sendJson(res, body, status);
}

void sendCsv(httplib::Response& res, const std::string& csv, const std::string& filename) {
    res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    res.set_content(csv, "text/csv");
}

json parseBody(const httplib::Request& req) {
    if (req.body.empty()) return json::object();
    json body = json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        throw std::invalid_argument("Request body must be a JSON object.");
    }
    return body;
}

std::vector<int> meshList(const json& body) {
    std::vector<int> meshes{10, 20, 40, 80};
    if (!body.contains("meshes")) return meshes;
    if (!body["meshes"].is_array() || body["meshes"].empty() || body["meshes"].size() > kMaxStudyMeshes) {
        throw std::invalid_argument("'meshes' must list between 1 and " + std::to_string(kMaxStudyMeshes) +
                                    " element counts.");
    }
    meshes.clear();
    for (const auto& v : body["meshes"]) {
        if (!v.is_number_integer() || v.get<int>() < 1 || v.get<int>() > kMaxDisplayElements) {
            throw std::invalid_argument("Each mesh size must be a whole number between 1 and " +
                                        std::to_string(kMaxDisplayElements) + ".");
        }
        meshes.push_back(v.get<int>());
    }
    return meshes;
}

// Wraps a handler so every failure becomes a JSON error with the right status.
template <typename Handler>
httplib::Server::Handler guarded(Handler handler) {
    return [handler](const httplib::Request& req, httplib::Response& res) {
        try {
            handler(req, res);
        } catch (const io::ParseError& e) {
            sendError(res, 422, e.what(), e.line());
        } catch (const std::invalid_argument& e) {
            sendError(res, 400, e.what());
        } catch (const hw::DeviceTimeout& e) {
            sendError(res, 504, e.what());
        } catch (const hw::DeviceStateError& e) {
            sendError(res, 409, e.what());
        } catch (const std::exception& e) {
            Logger::instance().error(req.method + " " + req.path + ": " + e.what());
            sendError(res, 500, std::string("Internal error: ") + e.what());
        }
    };
}

} // namespace

AppServer::AppServer(ServerOptions options) : options_(std::move(options)), http_(std::make_unique<httplib::Server>()) {
    lastContactMs_ = steadyMs();
    registerRoutes();
}

AppServer::~AppServer() = default;

int AppServer::bind() {
    for (int port = options_.preferredPort; port < options_.preferredPort + 10; ++port) {
        if (http_->bind_to_port(options_.host, port)) return port;
    }
    const int port = http_->bind_to_any_port(options_.host);
    if (port <= 0) throw std::runtime_error("Could not open a local network port for the user interface.");
    return port;
}

void AppServer::run() { http_->listen_after_bind(); }

void AppServer::stop() {
    jobs_.cancelAll();
    http_->stop();
}

void AppServer::markReady() {
    serverReadyMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                               options_.processStart)
                         .count();
    Logger::instance().info("Server ready " + std::to_string(serverReadyMs_) + " ms after launch.");
}

double AppServer::secondsSinceLastContact() const { return static_cast<double>(steadyMs() - lastContactMs_.load()) / 1000.0; }

void AppServer::touch() { lastContactMs_ = steadyMs(); }

void AppServer::registerRoutes() {
    httplib::Server& s = *http_;
    s.set_payload_max_length(io::kMaxInputBytes + 1024 * 1024);
    s.set_mount_point("/", options_.webRoot.string());
    s.set_file_request_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-cache");
    });
    s.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
        if (req.path.rfind("/api/", 0) == 0) touch();
        if (req.path == "/api/heartbeat" || req.path.rfind("/api/jobs/", 0) == 0 ||
            req.path.rfind("/api/hardware/telemetry", 0) == 0)
            return; // high-frequency polling: keep the log readable
        if (req.path.rfind("/api/", 0) == 0) {
            Logger::instance().info(req.method + " " + req.path + " -> " + std::to_string(res.status));
        }
    });

    // ---- application / system ------------------------------------------------
    s.Get("/api/system", guarded([this](const httplib::Request&, httplib::Response& res) {
              if (!uiConnected_.exchange(true)) {
                  uiReadyMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                         options_.processStart)
                                   .count();
                  Logger::instance().info("UI connected " + std::to_string(uiReadyMs_.load()) +
                                          " ms after launch.");
              }
              json body = toJson(sys::querySystemInfo());
              body["serverReadyMs"] = serverReadyMs_;
              body["uiReadyMs"] = uiReadyMs_.load();
              body["logFile"] = Logger::instance().path().filename().string();
              sendJson(res, body);
          }));

    s.Post("/api/heartbeat", [](const httplib::Request&, httplib::Response& res) { sendJson(res, {{"ok", true}}); });

    s.Post("/api/shutdown", [this](const httplib::Request&, httplib::Response& res) {
        Logger::instance().info("Shutdown requested from the user interface.");
        shutdownRequested_ = true;
        sendJson(res, {{"ok", true}});
    });

    // ---- Task B: geometry and nodal results ---------------------------------
    s.Post("/api/geometry/stl", guarded([](const httplib::Request& req, httplib::Response& res) {
               const auto start = std::chrono::steady_clock::now();
               const io::StlMesh mesh = io::parseAsciiStl(req.body);
               json body = toJson(mesh);
               body["parseMs"] =
                   std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
               body["bytes"] = req.body.size();
               sendJson(res, body);
           }));

    s.Post("/api/results/nodal-csv", guarded([](const httplib::Request& req, httplib::Response& res) {
               const auto start = std::chrono::steady_clock::now();
               const io::NodalDataset data = io::parseNodalCsv(req.body);
               json body = toJson(data);
               body["parseMs"] =
                   std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
               body["bytes"] = req.body.size();
               sendJson(res, body);
           }));

    // ---- Task C: thermal FEA -------------------------------------------------
    s.Post("/api/thermal/solve", guarded([](const httplib::Request& req, httplib::Response& res) {
               const json body = parseBody(req);
               const fea::RodProblem base = rodProblemFromJson(body);
               fea::validate(base);
               sendJson(res, toJson(report::runMeshStudy(base, meshList(body))));
           }));

    s.Post("/api/thermal/export", guarded([](const httplib::Request& req, httplib::Response& res) {
               const json body = parseBody(req);
               const auto study = report::runMeshStudy(rodProblemFromJson(body), meshList(body));
               const std::string kind = body.value("kind", "profiles");
               if (kind == "convergence") sendCsv(res, report::feaConvergenceCsv(study), "fea_convergence.csv");
               else sendCsv(res, report::feaProfileCsv(study), "fea_profiles.csv");
           }));

    // ---- Task D: multicore benchmark (asynchronous job) ----------------------
    s.Post("/api/performance/run", guarded([this](const httplib::Request& req, httplib::Response& res) {
               const parallel::BenchmarkConfig cfg = benchmarkConfigFromJson(parseBody(req));
               const auto id = jobs_.start("benchmark", [this, cfg](JobContext& ctx) {
                   parallel::BenchmarkReport report = parallel::runBenchmark(
                       cfg, ctx.cancelFlag(), [&ctx](const std::string& stage, double f) { ctx.setProgress(stage, f); });
                   json body = toJson(report);
                   std::lock_guard<std::mutex> lock(benchMutex_);
                   lastBenchmark_ = std::move(report);
                   return body;
               });
               if (!id) {
                   sendError(res, 409, "A benchmark is already running. Wait for it to finish or cancel it.");
                   return;
               }
               sendJson(res, {{"jobId", *id}}, 202);
           }));

    s.Get(R"(/api/jobs/([\w-]+))", guarded([this](const httplib::Request& req, httplib::Response& res) {
              const auto snap = jobs_.snapshot(req.matches[1]);
              if (!snap) {
                  sendError(res, 404, "Unknown job id.");
                  return;
              }
              json body{{"id", snap->id},           {"kind", snap->kind},
                        {"state", toString(snap->state)}, {"stage", snap->stage},
                        {"progress", snap->progress}, {"elapsedSeconds", snap->elapsedSeconds}};
              if (!snap->error.empty()) body["error"] = snap->error;
              if (snap->result) body["result"] = *snap->result;
              sendJson(res, body);
          }));

    s.Post(R"(/api/jobs/([\w-]+)/cancel)", guarded([this](const httplib::Request& req, httplib::Response& res) {
               sendJson(res, {{"cancelled", jobs_.cancel(req.matches[1])}});
           }));

    s.Get("/api/performance/export", guarded([this](const httplib::Request& req, httplib::Response& res) {
              std::lock_guard<std::mutex> lock(benchMutex_);
              if (!lastBenchmark_) {
                  sendError(res, 404, "No completed benchmark yet. Run one first.");
                  return;
              }
              const std::string kind = req.has_param("kind") ? req.get_param_value("kind") : "summary";
              if (kind == "runs") sendCsv(res, report::benchmarkRunsCsv(*lastBenchmark_), "bench_runs.csv");
              else if (kind == "system")
                  sendCsv(res, report::systemInfoText(sys::querySystemInfo(), &*lastBenchmark_), "bench_system.txt");
              else sendCsv(res, report::benchmarkSummaryCsv(*lastBenchmark_), "bench_summary.csv");
          }));

    // ---- Task E: embedded device mocks ----------------------------------------
    s.Get("/api/hardware/devices", guarded([this](const httplib::Request&, httplib::Response& res) {
              json list = json::array();
              for (const auto& d : devices_.discover()) {
                  json item = toJson(d);
                  item["state"] = hw::toString(devices_.state(d.id));
                  list.push_back(item);
              }
              sendJson(res, {{"devices", list}, {"policy",
                                                 {{"maxAttempts", devices_.policy().maxAttempts},
                                                  {"timeoutMs", devices_.policy().timeout.count()},
                                                  {"initialBackoffMs", devices_.policy().initialBackoff.count()}}}});
          }));

    s.Post(R"(/api/hardware/([\w-]+)/connect)", guarded([this](const httplib::Request& req, httplib::Response& res) {
               const int attempts = devices_.connect(req.matches[1]);
               sendJson(res, {{"state", hw::toString(devices_.state(req.matches[1]))}, {"attempts", attempts}});
           }));

    s.Post(R"(/api/hardware/([\w-]+)/disconnect)", guarded([this](const httplib::Request& req, httplib::Response& res) {
               devices_.disconnect(req.matches[1]);
               sendJson(res, {{"state", "disconnected"}});
           }));

    s.Get(R"(/api/hardware/telemetry/([\w-]+))", guarded([this](const httplib::Request& req, httplib::Response& res) {
              sendJson(res, toJson(devices_.readTelemetry(req.matches[1])));
          }));

    s.Post(R"(/api/hardware/([\w-]+)/fault)", guarded([this](const httplib::Request& req, httplib::Response& res) {
               const std::string mode = parseBody(req).value("mode", "none");
               hw::FaultMode fault = hw::FaultMode::None;
               if (mode == "timeout-once") fault = hw::FaultMode::TimeoutOnce;
               else if (mode == "timeout-always") fault = hw::FaultMode::TimeoutAlways;
               else if (mode != "none") throw std::invalid_argument("Unknown fault mode '" + mode + "'.");
               devices_.injectFault(req.matches[1], fault);
               sendJson(res, {{"fault", mode}, {"state", hw::toString(devices_.state(req.matches[1]))}});
           }));

    s.Get("/api/hardware/events", guarded([this](const httplib::Request&, httplib::Response& res) {
              json list = json::array();
              for (const auto& e : devices_.events()) list.push_back(toJson(e));
              sendJson(res, {{"events", list}});
          }));

    // ---- Task F: backpropagation -------------------------------------------------
    s.Post("/api/learning/backprop", guarded([](const httplib::Request& req, httplib::Response& res) {
               const json body = parseBody(req);
               ml::Params p;
               p.w = requireNumber(body, "w", p.w);
               p.b = requireNumber(body, "b", p.b);
               p.v = requireNumber(body, "v", p.v);
               p.c = requireNumber(body, "c", p.c);
               ml::Sample sample;
               sample.x = requireNumber(body, "x", sample.x);
               sample.y = requireNumber(body, "y", sample.y);
               const double lr = requireNumber(body, "learningRate", 0.1);
               const int steps = requireInt(body, "steps", 100);
               if (!(lr > 0.0 && lr <= 10.0)) throw std::invalid_argument("Learning rate must be in (0, 10].");
               if (steps < 1 || steps > 100000) throw std::invalid_argument("Steps must be between 1 and 100000.");
               for (double v : {p.w, p.b, p.v, p.c, sample.x, sample.y}) {
                   if (!std::isfinite(v)) throw std::invalid_argument("All parameters must be finite numbers.");
               }
               sendJson(res, backpropToJson(p, sample, lr, steps));
           }));

    s.Post("/api/learning/export", guarded([](const httplib::Request&, httplib::Response& res) {
               const ml::Params p0;
               const ml::Sample sample;
               const ml::Params p1 = ml::gradientStep(p0, ml::backward(p0, sample), 0.1);
               sendCsv(res, report::backpropLossCsv(ml::train(p1, sample, 0.1, 100)), "backprop_loss.csv");
           }));

    s.Get("/api/learning/gradients.csv", guarded([](const httplib::Request&, httplib::Response& res) {
              sendCsv(res, report::backpropGradientCsv(ml::Params{}, ml::Sample{}, 1e-6), "backprop_gradients.csv");
          }));
}

} // namespace wb::server
