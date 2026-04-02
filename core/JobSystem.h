
/* ============================================================================
   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/JobSystem.h
   Module:      Background Jobs
   Purpose:     Small background worker queue for non-SDL work.

   Notes:
   - Header-only on purpose so it does not require build list changes.
   - Intended for file IO, validation, and other CPU/IO tasks that do not touch
     SDL renderer objects.
   ============================================================================ */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <algorithm>
#include <utility>

namespace strova
{
namespace jobs
{
class JobSystem
{
public:
    explicit JobSystem(std::size_t workerCount = defaultWorkerCount())
    {
        start(workerCount);
    }

    ~JobSystem()
    {
        stop();
    }

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void start(std::size_t workerCount = defaultWorkerCount())
    {
        stop();

        if (workerCount == 0) workerCount = 1;
        stopping_ = false;
        for (std::size_t i = 0; i < workerCount; ++i)
            workers_.emplace_back([this]() { workerLoop(); });
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();

        for (std::thread& worker : workers_)
        {
            if (worker.joinable()) worker.join();
        }
        workers_.clear();

        std::lock_guard<std::mutex> lk(mutex_);
        std::queue<std::function<void()>> empty;
        std::swap(queue_, empty);
        activeCount_ = 0;
    }

    template <class Fn>
    void enqueue(Fn&& fn)
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (stopping_) return;
            queue_.push(std::function<void()>(std::forward<Fn>(fn)));
        }
        cv_.notify_one();
    }

    int pendingCount() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return static_cast<int>(queue_.size());
    }

    int activeCount() const
    {
        return activeCount_.load();
    }

    static std::size_t defaultWorkerCount()
    {
        const unsigned hc = std::thread::hardware_concurrency();
        if (hc <= 2) return 1;
        return std::min<std::size_t>(2, hc - 1);
    }

private:
    void workerLoop()
    {
        for (;;)
        {
            std::function<void()> job;
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this]() { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty())
            {
                lk.unlock();
                return;
            }
            job = std::move(queue_.front());
            queue_.pop();
            activeCount_.fetch_add(1);
            lk.unlock();

            try
            {
                job();
            }
            catch (...)
            {
            }

            activeCount_.fetch_sub(1);
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    std::atomic<int> activeCount_{ 0 };
    bool stopping_ = false;
};
} // namespace jobs
} // namespace strova
