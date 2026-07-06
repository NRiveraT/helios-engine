#include "HelioObject.h"

#include <atomic>

namespace helio::scene {

namespace {
// Process-wide ID generator. Atomic so spawning actors from worker
// threads is safe. Starts at 1 so 0 can mean "invalid".
std::atomic<uint64_t> g_nextObjectId{1};
} // namespace

HelioObject::HelioObject()
    : m_id(g_nextObjectId.fetch_add(1, std::memory_order_relaxed)) {}

} // namespace helio::scene
