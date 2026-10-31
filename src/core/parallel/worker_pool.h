#pragma once
// Fixed-size worker pool with a bounded task queue.
//
// * The number of threads is fixed at construction (bounded concurrency).
// * submit() blocks while the queue is full (bounded memory / back-pressure).
// * wait() blocks until every submitted task has finished.
// * The first exception thrown by a task is captured and rethrown by wait().

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace wb::parallel {

class WorkerPool {
public:
    WorkerPool(std::size_t workerCount, std::size_t queueCapacity);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void submit(std::function<void()> task);
    void wait();

    std::size_t workerCount() const noexcept { return workers_.size(); }

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> queue_;
    std::size_t capacity_;
    std::size_t active_ = 0;
    bool stopping_ = false;
    std::exception_ptr firstError_;

    std::mutex mutex_;
    std::condition_variable taskAvailable_;
    std::condition_variable spaceAvailable_;
    std::condition_variable allDone_;
};

// Number of logical CPUs reported by the OS (at least 1).
std::size_t logicalCpuCount();

} // namespace wb::parallel
