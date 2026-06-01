/// @file Clock.h
/// @brief Monotonic high-resolution clock backed by QueryPerformanceCounter.
#pragma once

#include <cstdint>

namespace helio::core {

class Clock {
public:
    Clock();

    /// Seconds since clock construction (or last Reset).
    [[nodiscard]] double SecondsSinceStart() const noexcept;

    /// Reset the start time to "now".
    void Reset() noexcept;

    /// Seconds since the last Tick() (or construction). First call returns 0.
    /// Useful for per-frame `dt`.
    [[nodiscard]] double Tick() noexcept;

private:
    int64_t m_frequency;
    int64_t m_start;
    int64_t m_lastTick;
};

} // namespace helio::core
