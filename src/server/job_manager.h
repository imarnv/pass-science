#pragma once
// Runs long computations off the HTTP threads so the UI stays responsive.
//
// One job at a time (the benchmark should not compete with itself for CPUs).
// A job's result is published only when it finishes successfully; a cancelled
// or failed job never exposes partial results.

#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace wb::server {

enum class JobState { Running, Succeeded, Failed, Cancelled };
std::string toString(JobState state);

class JobContext {
public:
    const std::atomic<bool>* cancelFlag() const { return &cancel_; }
    bool cancelled() const { return cancel_.load(); }
    void setProgress(const std::string& stage, double fraction);

private:
    friend class JobManager;
    std::atomic<bool> cancel_{false};
    std::mutex mutex_;
    std::string stage_ = "Starting";
    double fraction_ = 0.0;
};

struct JobSnapshot {
    std::string id;
    std::string kind;
    JobState state = JobState::Running;
    std::string stage;
    double progress = 0.0;
    std::string error;
    double elapsedSeconds = 0.0;
    std::optional<nlohmann::json> result;
};

class JobManager {
public:
    using Work = std::function<nlohmann::json(JobContext&)>;

    ~JobManager();

    // Returns the new job id, or std::nullopt if a job is already running.
    std::optional<std::string> start(const std::string& kind, Work work);
    std::optional<JobSnapshot> snapshot(const std::string& id);
    bool cancel(const std::string& id);
    void cancelAll();

private:
    struct Job {
        std::string id, kind;
        JobState state = JobState::Running;
        std::string error;
        std::optional<nlohmann::json> result;
        std::chrono::steady_clock::time_point started, finished;
        JobContext context;
    };

    std::mutex mutex_;
    std::shared_ptr<Job> current_; // most recent job (running or finished)
    std::thread worker_;
    unsigned long counter_ = 0;
};

} // namespace wb::server
