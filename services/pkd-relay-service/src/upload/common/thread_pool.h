#pragma once
/**
 * @file thread_pool.h
 * @brief Simple thread pool for async upload/sync processing
 *
 * Replaces detached std::thread with managed pool that supports
 * graceful shutdown (waits for running tasks to complete).
 * Thread-safe task queue with configurable pool size.
 */

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <spdlog/spdlog.h>

namespace common {

class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = 2) : stop_(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this, i] {
                spdlog::debug("[ThreadPool] Worker {} started", i);
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) break;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    try {
                        task();
                    } catch (const std::exception& e) {
                        spdlog::error("[ThreadPool] Task exception: {}", e.what());
                    } catch (...) {
                        spdlog::error("[ThreadPool] Task unknown exception");
                    }
                }
                spdlog::debug("[ThreadPool] Worker {} stopped", i);
            });
        }
        spdlog::info("[ThreadPool] Initialized with {} workers", numThreads);
    }

    ~ThreadPool() {
        shutdown();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief Submit a task to the pool
     * @return true if task was enqueued, false if pool is shutting down
     */
    bool submit(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) return false;
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
        return true;
    }

    /**
     * @brief Graceful shutdown — waits for all queued tasks to complete
     */
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) return;
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
        workers_.clear();
        spdlog::info("[ThreadPool] Shut down");
    }

    size_t pendingTasks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_;
};

} // namespace common
