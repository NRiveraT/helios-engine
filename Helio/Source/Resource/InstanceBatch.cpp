#include "InstanceBatch.h"

#include <RHI/Public/Device.h>

#include <Core/Assert/Assert.h>
#include <Core/Logging/Log.h>

namespace helio::resource {

InstanceBatch::InstanceBatch(rhi::Device& Dev, uint32_t MaxInstances, const char* DebugName)
    : m_capacity(MaxInstances)
    , m_ring(Dev, uint64_t(MaxInstances) * sizeof(MeshInstance), DebugName) {
    HELIO_CHECK(MaxInstances > 0);
    m_staging.reserve(MaxInstances);
}

InstanceBatch::~InstanceBatch() = default;

void InstanceBatch::Add(const MeshInstance& I) {
    if (m_staging.size() >= m_capacity) {
        HELIO_LOG_WARN("Resource", "InstanceBatch full ({} instances), dropping", m_capacity);
        return;
    }
    m_staging.push_back(I);
}

void InstanceBatch::Add(const Transform& T) {
    MeshInstance I{};
    I.Transform = T.ToMatrix();
    Add(I);
}

uint32_t InstanceBatch::End() {
    m_count = static_cast<uint32_t>(m_staging.size());
    if (m_count > 0) {
        m_ring.Write(0, m_staging.data(), uint64_t(m_count) * sizeof(MeshInstance));
    }
    return m_count;
}

} // namespace helio::resource
