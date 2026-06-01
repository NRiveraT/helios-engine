#include "Clock.h"

#include <Windows.h>

namespace helio::core {

namespace {

int64_t QueryFrequency() {
    LARGE_INTEGER F{};
    ::QueryPerformanceFrequency(&F);
    return F.QuadPart;
}

int64_t QueryCounter() {
    LARGE_INTEGER C{};
    ::QueryPerformanceCounter(&C);
    return C.QuadPart;
}

} // namespace

Clock::Clock()
    : m_frequency(QueryFrequency())
    , m_start(QueryCounter())
    , m_lastTick(m_start) {}

double Clock::SecondsSinceStart() const noexcept {
    return static_cast<double>(QueryCounter() - m_start) / static_cast<double>(m_frequency);
}

void Clock::Reset() noexcept {
    m_start = QueryCounter();
    m_lastTick = m_start;
}

double Clock::Tick() noexcept {
    int64_t Now = QueryCounter();
    double Dt = static_cast<double>(Now - m_lastTick) / static_cast<double>(m_frequency);
    m_lastTick = Now;
    return Dt;
}

} // namespace helio::core
