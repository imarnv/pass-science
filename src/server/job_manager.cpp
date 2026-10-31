#include "server/job_manager.h"

#include "core/parallel/batch.h"
#include "server/platform.h"

namespace wb::server {

std::string toString(JobState state) {
    switch (state) {
    case JobState::Running: return "running";
    case JobState::Succeeded: return "succeeded";
    case JobState::Failed: return "failed";
    case JobState::Cancelled: return "cancelled";
    }
    return "unknown";
}

void JobContext::setProgress(const std::string& stage, double fraction) {
    std::lock_guard<std::mutex> lock(mutex_);
    stage_ = stage;
    fraction_ = fraction;
}

JobManager::~JobManager() {
    cancelAll();
    if (worker_.joinable()) worker_.join();
}

std::optional<std::string> JobManager::start(const std::string& kind, Work work) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_ && current_->state == JobState::Running) return std::nullopt;
    if (worker_.joinable()) worker_.join(); // previous job already finished

    auto job = std::make_shared<Job>();
    job->id = kind + "-" + std::to_string(++counter_);
    job->kind = kind;
    job->started = std::chrono::steady_clock::now();
    current_ = job;

    worker_ = std::thread([this, job, work = std::move(work)] {
        JobState finalState = JobState::Succeeded;
        std::string error;
        std::optional<nlohmann::json> result;
        try {
            result = work(job->context);
        } catch (const parallel::Cancelled&) {
            finalState = JobState::Cancelled;
        } catch (const std::exception& e) {
            finalState = JobState::Failed;
            error = e.what();
        }
        std::lock_guard<std::mutex> guard(mutex_);
        job->state = finalState;
        job->error = error;
        job->result = std::move(result);
        job->finished = std::chrono::steady_clock::now();
        Logger::instance().info("Job " + job->id + " finished: " + toString(finalState) +
                                (error.empty() ? "" : " (" + error + ")"));
    });
    Logger::instance().info("Job " + job->id + " started.");
    return job->id;
}

std::optional<JobSnapshot> JobManager::snapshot(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!current_ || current_->id != id) return std::nullopt;
    JobSnapshot s;
    s.id = current_->id;
    s.kind = current_->kind;
    s.state = current_->state;
    s.error = current_->error;
    s.result = current_->result;
    {
        std::lock_guard<std::mutex> ctx(current_->context.mutex_);
        s.stage = current_->context.stage_;
        s.progress = current_->context.fraction_;
    }
    if (s.state == JobState::Cancelled) s.stage = "Cancelled - no results were kept";
    const auto end = s.state == JobState::Running ? std::chrono::steady_clock::now() : current_->finished;
    s.elapsedSeconds = std::chrono::duration<double>(end - current_->started).count();
    return s;
}

bool JobManager::cancel(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!current_ || current_->id != id || current_->state != JobState::Running) return false;
    current_->context.cancel_ = true;
    Logger::instance().info("Cancellation requested for job " + id + ".");
    return true;
}

void JobManager::cancelAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_) current_->context.cancel_ = true;
}

} // namespace wb::server
