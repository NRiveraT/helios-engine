/// @file InstanceBatch.h
/// @brief Per-frame instance-data ring for batched/instanced mesh draws.
///
/// Game code:
///   Batch.Begin();
///   for (auto& obj : scene) Batch.Add({.Transform = obj.WorldMatrix()});
///   Batch.End();
///   ... in render pass:
///   Cmd.Bind(MeshPipeline);
///   Cmd.Push(PC{ Mesh.VertexBuffer.BindlessSlot,
///                Batch.Buffer().BindlessSlot,
///                ViewProj });
///   Cmd.BindIndexBuffer(Mesh.IndexBuffer, IndexType::U32);
///   Cmd.DrawIndexed(Mesh.IndexCount, Batch.Count());
///
/// Backed by `RingUploadBuffer` so cross-frame writes are race-free.
/// `Buffer()` rotates to the current frame's slot — re-query each frame.
///
/// `MeshInstance` is 64 B, fitting one 4×4 transform plus reserved space for
/// future per-instance material data. Adjust `MaxInstances` based on your
/// peak object count.
#pragma once

#include <Core/Math/Math.h>
#include <Core/Math/Transform.h>

#include <RHI/Public/Buffer.h>
#include <RHI/Public/RingUploadBuffer.h>

#include <cstdint>
#include <vector>

namespace helio::rhi { class Device; }

namespace helio::resource {

/// 64-byte per-instance payload. Layout must match Shaders/Common/Vertex.slang.
/// `Transform` is a `Mat4Packed`, so assigning from a `float4x4` Just Works:
///     MeshInstance I{};
///     I.Transform = WorldMatrix;
///     Batch.Add(I);
struct MeshInstance {
    Mat4Packed Transform;   // 64 B — assign from hlslpp::float4x4 directly.
    // Future fields — material slot, instance color, etc. — bump size; update
    // the static_assert and Shader struct in lockstep.
};
static_assert(sizeof(MeshInstance) == 64, "MeshInstance must match shader-side layout");

class InstanceBatch {
public:
    InstanceBatch(rhi::Device& Dev, uint32_t MaxInstances,
                  const char* DebugName = "InstanceBatch");
    ~InstanceBatch();

    InstanceBatch(const InstanceBatch&) = delete;
    InstanceBatch& operator=(const InstanceBatch&) = delete;

    /// Reset the staging vector. Call once per frame before populating.
    void Begin() noexcept { m_staging.clear(); }

    /// Append one instance. Drops + warns if past `MaxInstances`.
    void Add(const MeshInstance& I);

    /// Convenience: stage a `helio::Transform`. Converts to a matrix inline.
    /// Use this when your game-side state holds a `Transform` and you don't
    /// want to deal with `MeshInstance` / `Mat4Packed` plumbing.
    void Add(const Transform& T);

    /// Upload staging to this frame's ring slot. Returns the instance count
    /// to feed into `DrawIndexed(..., InstanceCount)`.
    uint32_t End();

    /// Current-frame slot handle (re-query each frame — bindless slot rotates).
    [[nodiscard]] rhi::BufferHandle Buffer() const { return m_ring.Current(); }

    /// Number of instances written by the last `End()`.
    [[nodiscard]] uint32_t Count() const noexcept { return m_count; }

    /// Capacity passed to the constructor.
    [[nodiscard]] uint32_t Capacity() const noexcept { return m_capacity; }

private:
    uint32_t                  m_capacity;
    uint32_t                  m_count{0};
    std::vector<MeshInstance> m_staging;
    rhi::RingUploadBuffer     m_ring;
};

} // namespace helio::resource
