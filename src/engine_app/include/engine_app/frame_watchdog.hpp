#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace eaw::engine_app {

// Reports a frame step that runs longer than its threshold (a stall or an endless loop), naming the
// step and the simulation tick, from a background thread and every five seconds while it lasts, so
// a frozen window still leaves a diagnosis on stderr and in the log file. The threshold is 5 s on a
// hardware device and 60 s on WARP or with GPU validation, where whole frames take seconds.
class FrameWatchdog final
{
public:
    FrameWatchdog()
        : thread_(
              [this]
              {
                  run();
              })
    {}
    ~FrameWatchdog()
    {
        {
            const std::scoped_lock lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
    FrameWatchdog(const FrameWatchdog&) = delete;
    FrameWatchdog& operator=(const FrameWatchdog&) = delete;

    void set_threshold(const std::chrono::seconds threshold) noexcept
    {
        threshold_seconds_.store(threshold.count(), std::memory_order_relaxed);
    }

    // Marks the start of a frame step; `step` must be a string literal.
    void enter(const char* step, const std::uint64_t tick) noexcept
    {
        tick_.store(tick, std::memory_order_relaxed);
        since_.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
        step_.store(step, std::memory_order_release);
    }

private:
    void run();

    std::atomic<const char*> step_{nullptr};
    std::atomic<std::int64_t> since_{0};
    std::atomic<std::uint64_t> tick_{0};
    std::atomic<std::int64_t> threshold_seconds_{5};
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_{false};
    std::thread thread_;
};

} // namespace eaw::engine_app
