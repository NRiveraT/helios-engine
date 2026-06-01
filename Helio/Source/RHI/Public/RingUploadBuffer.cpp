#include "RingUploadBuffer.h"
#include "Device.h"

#include <Core/Assert/Assert.h>

#include <cstdio>

namespace helio::rhi {

RingUploadBuffer::RingUploadBuffer(Device& Dev, uint64_t SizeBytes, const char* DebugName)
    : m_dev(&Dev), m_size(SizeBytes) {
    HELIO_CHECK(SizeBytes > 0);
    const uint32_t N = m_dev->FramesInFlight();
    m_slots.reserve(N);
    for (uint32_t I = 0; I < N; ++I) {
        char NameBuf[96]{};
        std::snprintf(NameBuf, sizeof(NameBuf), "%s[%u]",
                      DebugName ? DebugName : "RingUpload", I);
        BufferHandle H = m_dev->CreateBuffer({
            .Size      = SizeBytes,
            .Usage     = BufferUsage::Storage,
            .Memory    = MemoryUsage::HostUpload,
            .DebugName = NameBuf,
        });
        HELIO_CHECK(H.IsValid() && H.BindlessSlot != 0xFFFFFFFFu);
        m_slots.push_back(H);
    }
}

RingUploadBuffer::~RingUploadBuffer() {
    if (!m_dev) return;
    for (auto H : m_slots) {
        if (H.IsValid()) m_dev->DestroyBuffer(H);
    }
}

BufferHandle RingUploadBuffer::Current() const {
    return m_slots[m_dev->CurrentFrameIndex()];
}

void RingUploadBuffer::Write(uint64_t Offset, const void* Data, uint64_t Size) {
    HELIO_CHECK(Offset + Size <= m_size);
    m_dev->UploadToBuffer(Current(), Offset, Data, Size);
}

} // namespace helio::rhi
