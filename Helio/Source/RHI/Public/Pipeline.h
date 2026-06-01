/// @file Pipeline.h
/// @brief Public Pipeline handle + descriptors.
///
/// One Slang source -> one compiled `.spv` module containing every entry point
/// tagged with `[shader("...")]`. The pipeline picks entries by name.
///
/// All pipelines share Helio's bindless descriptor set + a single push-constant
/// range (default 128 bytes — the Vulkan-guaranteed minimum).
#pragma once

#include <RHI/Public/Formats.h>

#include <cstdint>

namespace helio::rhi {

enum class CompareOp : uint8_t {
    Never, Less, Equal, LessEq, Greater, NotEq, GreaterEq, Always,
};

enum class CullMode : uint8_t {
    None, Front, Back,
};

/// Which winding the rasterizer treats as "front-facing" in framebuffer
/// coordinates. Vulkan's default + most CCW-from-outside meshes (glTF, our
/// procedural primitives) want `CounterClockwise`. Pipelines whose projection
/// matrix flips Y (so world-up = screen-up under Vulkan's Y-down framebuffer)
/// need `Clockwise` to undo the resulting winding inversion.
enum class FrontFace : uint8_t {
    CounterClockwise, Clockwise,
};

enum class PrimitiveTopology : uint8_t {
    TriangleList, TriangleStrip, LineList, PointList,
};

struct GraphicsPipelineDesc {
    /// Path to a compiled `.spv` (e.g. `"Shaders/Passes/Triangle.spv"`,
    /// resolved relative to the binary directory). Slang source extension
    /// (`.slang`) is also accepted — it's stripped and replaced with `.spv`.
    const char* ShaderPath{nullptr};
    const char* VertexEntry{"VSMain"};
    const char* FragmentEntry{"PSMain"};

    /// Color attachment formats for dynamic rendering. Up to 8 attachments.
    Format ColorFormats[8]{};
    uint32_t ColorAttachmentCount{1};
    /// Depth attachment format; `Format::Undefined` disables depth.
    Format DepthFormat{Format::Undefined};

    PrimitiveTopology Topology{PrimitiveTopology::TriangleList};
    CullMode Cull{CullMode::None};
    FrontFace Front{FrontFace::CounterClockwise};
    bool DepthTest{false};
    bool DepthWrite{false};
    CompareOp DepthCompare{CompareOp::Less};

    /// Push-constant payload size (≤128 bytes guaranteed by Vulkan minimums).
    uint32_t PushConstantBytes{128};

    const char* DebugName{nullptr};
};

struct ComputePipelineDesc {
    const char* ShaderPath{nullptr};
    const char* Entry{"CSMain"};
    uint32_t PushConstantBytes{128};
    const char* DebugName{nullptr};
};

struct PipelineHandle {
    uint64_t Id{0};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return Id != 0; }
};

} // namespace helio::rhi
