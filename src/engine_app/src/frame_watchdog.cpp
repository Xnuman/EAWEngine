#include "engine_app/frame_watchdog.hpp"

#include "core/log.hpp"
#include "engine/text_format.hpp"

#include <string>

namespace eaw {

void FrameWatchdog::run()
{
    using namespace std::chrono_literals;
    std::unique_lock lock(mutex_);
    std::int64_t reported_since = 0;
    auto last_report = std::chrono::steady_clock::time_point{};
    while (!wake_.wait_for(lock, 500ms,
                           [&]
                           {
                               return stop_;
                           }))
    {
        const auto* step = step_.load(std::memory_order_acquire);
        const auto since = since_.load(std::memory_order_relaxed);
        if (step == nullptr)
        {
            continue;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto stuck = now - std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(since));
        if (stuck < std::chrono::seconds(threshold_seconds_.load(std::memory_order_relaxed)) || (since == reported_since && now - last_report < 5s))
        {
            continue;
        }
        reported_since = since;
        last_report = now;
        eaw::log_warning("sandbox", eaw::Error{"frame step '" + std::string(step) + "' has run for " + eaw::format_fixed(std::chrono::duration<double>(stuck).count(), 1) + " s (simulation tick " +
                                               std::to_string(tick_.load(std::memory_order_relaxed)) + ")"});
    }
}

} // namespace eaw
