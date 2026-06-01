#include "Handle.h"
#include <Core/Assert/Assert.h>

namespace helio::core {

Freelist::Freelist(uint32_t Count) : m_capacity(Count) {
    m_free.reserve(Count);
    // Push in reverse so allocations come out 0,1,2,... (more debuggable).
    for (uint32_t I = Count; I > 0; --I) {
        m_free.push_back(I - 1);
    }
}

uint32_t Freelist::Allocate() {
    if (m_free.empty()) {
        return Handle<void>::InvalidIndex;
    }
    uint32_t Slot = m_free.back();
    m_free.pop_back();
    return Slot;
}

void Freelist::Free(uint32_t Slot) {
    HELIO_ASSERT(Slot < m_capacity);
    m_free.push_back(Slot);
}

} // namespace helio::core
