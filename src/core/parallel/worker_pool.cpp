#include "core/parallel/worker_pool.h"

#include <stdexcept>

namespace wb::parallel {

WorkerPool::WorkerPool(std::size_t workerCount, std::size_t queueCapacity)
    : capacity_(queueCapacity == 0 ? 1 : queueCapacity) {
    if (workerCount == 0) throw std::invalid_argument("WorkerPool needs at least one worker.");
    workers_.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    taskAvailable_.notify_all();
    spaceAvailable_.notify_all();
    for (std::thread& t : workers_) t.join();
}

void WorkerPool::submit(std::function<void()> task) {
    std::unique_lock<std::mutex> lock(mutex_);
    spaceAvailable_.wait(lock, [this] { return queue_.size() < capacity_ || stopping_; });
    if (stopping_) throw std::runtime_error("WorkerPool is shutting down.");
    queue_.push_back(std::move(task));
    lock.unlock();
    taskAvailable_.notify_one();
}

void WorkerPool::wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    allDone_.wait(lock, [this] { return queue_.empty() && active_ == 0; });
    if (firstError_) {
        std::exception_ptr error = firstError_;
        firstError_ = nullptr;
        std::rethrow_exception(error);
    }
}

void WorkerPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            taskAvailable_.wait(lock, [this] { return !queue_.empty() || stopping_; });
            if (queue_.empty()) return; // stopping and drained
            task = std::move(queue_.front());
            queue_.pop_front();
            ++active_;
        }
        spaceAvailable_.notify_one();

        try {
            task();
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!firstError_) firstError_ = std::current_exception();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_;
            if (queue_.empty() && active_ == 0) allDone_.notify_all();
        }
    }
}

std::size_t logicalCpuCount() {
    const unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 1 : static_cast<std::size_t>(n);
}

} // namespace wb::parallel
