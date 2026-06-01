/// @file AccelStructure.h
/// @brief Public types for the ray-tracing acceleration structure hierarchy.
///
/// Vulkan RT uses a two-level hierarchy:
///
/// - **BLAS** (bottom-level): a geometry container. Typically built once per
///   static mesh at load time; expensive to build but cheap to instance.
/// - **TLAS** (top-level): an array of `BLAS` instances with per-instance
///   transform + custom data. Typically rebuilt every frame from the visible
///   scene; cheap to build (just bounding-box reorganization).
///
/// Both ray queries (callable from any shader stage that touches the bound
/// TLAS at bindless slot 4) and full RT pipelines (raygen/miss/closest-hit/
/// any-hit + SBT) read from the same TLAS.
///
/// V1 surface is build + destroy + bind-active. Per-frame TLAS rebuild is just
/// `BuildTLAS()` again with the current instance list; the deletion queue keeps
/// the previous frame's TLAS alive until the GPU is done with it.
#pragma once

#include <RHI/Public/Buffer.h>
#include <RHI/Public/Formats.h>

#include <cstdint>

namespace helio::rhi {

/// One geometry slot inside a BLAS. A BLAS can contain multiple geometries
/// (e.g. a multi-material mesh) but most static meshes use a single geometry.
struct BLASGeometry {
    /// Vertex buffer holding positions (interleaved or position-only).
    /// Must have been created with `BufferUsage::AccelStructureBuild`.
    BufferHandle Vertices;
    /// Bytes per vertex. For position-only this is `sizeof(float)*3 = 12`.
    /// For interleaved vertex buffers, set to the full vertex stride —
    /// AS build only reads the position field at offset 0.
    uint32_t VertexStride{12};
    uint32_t VertexCount{0};
    /// Vertex position format. Almost always `Format::RGB32F`.
    Format VertexFormat{Format::RGB32F};
    uint64_t VertexBufferOffset{0};

    /// Optional index buffer. If `IndexBuffer.IsValid()` is false the BLAS
    /// reads vertices as triangles directly (vertexCount must be a multiple of 3).
    BufferHandle Indices;
    /// `U16` or `U32` — must match the contents of `Indices`.
    uint8_t IndexFormat{1}; // 0 = U16, 1 = U32
    uint32_t IndexCount{0}; // triangles = IndexCount / 3
    uint64_t IndexBufferOffset{0};

    /// Opaque geometries skip the any-hit shader (RT pipeline path) and
    /// always commit hits. Set to `false` for alpha-cut foliage etc.
    bool Opaque{true};
};

struct BLASDesc {
    const BLASGeometry* Geometries{nullptr};
    uint32_t GeometryCount{0};
    /// Pass `true` for assets that won't change vertex data after build
    /// (e.g. static meshes). The driver picks the better build heuristic.
    bool PreferFastTrace{true};
    const char* DebugName{nullptr};
};

struct BLASHandle {
    uint64_t Id{0};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return Id != 0; }
};

/// One instance entry in a TLAS — a BLAS positioned in world space.
struct TLASInstance {
    BLASHandle BLAS;
    /// 3x4 row-major affine transform (translation in the last column).
    /// Identity is `{ {1,0,0,0}, {0,1,0,0}, {0,0,1,0} }`.
    float Transform[12]{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    /// Surfaces this byte AND'd against the ray's mask. Use to skip whole
    /// categories (e.g. shadow rays ignoring transparent/foliage instances).
    uint32_t Mask{0xFFu};
    /// User-defined value readable in shaders via `InstanceID()` (RT pipeline)
    /// or `CommittedInstanceID()` (ray query). Typical use: material ID or
    /// per-instance storage-buffer slot.
    uint32_t CustomIndex{0};
    /// Shader-binding-table record offset for hit-group selection in the RT
    /// pipeline path. Ignored by ray queries. Defaults to 0.
    uint32_t SBTOffset{0};
};

struct TLASDesc {
    const TLASInstance* Instances{nullptr};
    uint32_t InstanceCount{0};
    /// `true` for rebuilds (typical for per-frame TLAS); `false` to hint
    /// the driver that this TLAS is mostly static.
    bool AllowUpdate{false};
    const char* DebugName{nullptr};
};

struct TLASHandle {
    uint64_t Id{0};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return Id != 0; }
};

} // namespace helio::rhi
