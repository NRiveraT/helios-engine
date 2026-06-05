/// @file DebugDraw.h
/// @brief Immediate-mode debug-draw API for lines / boxes / spheres / text.
///
/// Two surfaces:
///
/// 1. **Class API** — `DebugDraw` owns the GPU resources and is the thing
///    you `Tick()` and `Render()` per frame:
///        helio::render::debug::DebugDraw DD(Rhi, ColorFmt, &Overlay);
///        DD.AddLine({0,0,0}, {1,0,0}, kColorRed);
///        DD.Tick(dt);
///        DD.Render(rg, target, viewProj, w, h);
///
/// 2. **Free-function API** in `helio::debug::` — forwards to a registered
///    singleton. Set it once at boot, then call from anywhere without
///    plumbing a pointer:
///        helio::debug::SetInstance(&DD);
///        helio::debug::Line({0,0,0}, {1,0,0}, 0xFFFF0000);  // anywhere
///
/// Categories let you toggle whole classes of primitives at runtime:
///        helio::debug::Box(Min, Max, kColorYellow, /*lifetime*/0, "Physics");
///        helio::debug::SetCategoryVisible("Physics", false);  // hides them
///
/// Lifetimes: `Lifetime = 0` means "this frame only" (the most common
/// pattern — re-submit every frame from your game tick). `Lifetime > 0` is
/// seconds remaining; primitives decay through `Tick(dt)` and disappear.
///
/// V1 ships **Line / Box / Sphere / Text2D / Text3D**. Arrow / Frustum /
/// Capsule live in Phase 13 polish (same pattern, more line generation).
#pragma once

#include <Core/Math/Math.h>

#include <RHI/Public/Buffer.h>
#include <RHI/Public/Pipeline.h>
#include <RHI/Public/RingUploadBuffer.h>
#include <RHI/Public/Texture.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace helio::rhi { class Device; class CommandList; }
namespace helio::render { class RenderGraph; }
namespace helio::render::overlay { class Overlay; }

namespace helio::render::debug {

inline constexpr uint32_t kMaxLineVerticesPerFrame = 1u << 16;   ///< 65 536 verts (~32K lines)
inline constexpr uint32_t kMaxText3DPerFrame       = 256;
inline constexpr uint32_t kMaxText2DPerFrame       = 256;

inline constexpr uint32_t kColorWhite  = 0xFFFFFFFFu;
inline constexpr uint32_t kColorRed    = 0xFF0000FFu;            ///< 0xAABBGGRR
inline constexpr uint32_t kColorGreen  = 0xFF00FF00u;
inline constexpr uint32_t kColorBlue   = 0xFFFF0000u;
inline constexpr uint32_t kColorYellow = 0xFF00FFFFu;
inline constexpr uint32_t kColorMagenta= 0xFFFF00FFu;
inline constexpr uint32_t kColorCyan   = 0xFFFFFF00u;

/// 0xAABBGGRR packed.
inline constexpr uint32_t PackColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255) {
    return  (uint32_t(A) << 24)
          | (uint32_t(B) << 16)
          | (uint32_t(G) <<  8)
          |  uint32_t(R);
}

class DebugDraw {
public:
    /// `TargetFormat` is the format of the color attachment passed to
    /// `Render()`. Must be consistent across the lifetime of the DebugDraw —
    /// pipeline is baked against it. Pass `nullptr` for `Font` if you don't
    /// need Text2D / Text3D (their submissions will silently drop).
    DebugDraw(rhi::Device& Dev, rhi::Format TargetFormat, overlay::Overlay* Font);
    ~DebugDraw();

    DebugDraw(const DebugDraw&) = delete;
    DebugDraw& operator=(const DebugDraw&) = delete;

    // ---- Primitives -----------------------------------------------------

    void AddLine  (float3 P0, float3 P1, uint32_t RGBA, float Lifetime = 0.0f, const char* Category = "Default");
    void AddBox   (float3 Min, float3 Max, uint32_t RGBA, float Lifetime = 0.0f, const char* Category = "Default");
    void AddSphere(float3 Center, float Radius, uint32_t RGBA, float Lifetime = 0.0f, const char* Category = "Default");
    void AddText2D(int X, int Y, std::string_view Text, uint32_t RGBA, float Lifetime = 0.0f, const char* Category = "Default");
    void AddText3D(float3 WorldPos, std::string_view Text, uint32_t RGBA, float Lifetime = 0.0f, const char* Category = "Default");

    // ---- Categories -----------------------------------------------------

    void SetCategoryVisible(const char* Category, bool Visible);
    [[nodiscard]] bool IsCategoryVisible(const char* Category) const;

    // ---- Lifecycle ------------------------------------------------------

    /// Drop everything immediately. Useful between scenes / level loads.
    void Clear() noexcept;

    /// Drop primitives in `Category` only.
    void ClearCategory(const char* Category) noexcept;

    /// Decay `Lifetime` by `DeltaSeconds` and evict expired primitives.
    /// Call once per frame (after game tick, before Render).
    void Tick(float DeltaSeconds);

    /// Draw all surviving primitives.
    ///   - `Target` is the color attachment (must match `TargetFormat`,
    ///     usually the same texture you Present)
    ///   - `ViewProj` is the camera matrix used to project lines + Text3D
    ///   - `(W, H)` are the target's pixel dimensions, needed for Text3D's
    ///     world→pixel conversion
    ///
    /// Schedules a `LoadOp::Load` graphics pass into Rg. Text2D / Text3D
    /// get forwarded to the registered `Overlay` (so call `Overlay::Render`
    /// AFTER this).
    void Render(RenderGraph& Rg, rhi::TextureHandle Target,
                const float4x4& ViewProj, uint32_t TargetWidthPx, uint32_t TargetHeightPx);

    // GPU-facing vertex layout (must match DebugLines.slang). Public so
    // local helpers in the .cpp's anonymous namespace can construct one.
    struct LineVertex {
        float    Px, Py, Pz;
        uint32_t RGBA;
    };
    static_assert(sizeof(LineVertex) == 16, "LineVertex must match DebugLines.slang");

private:

    struct LinePrim {
        LineVertex A;
        LineVertex B;
        float      Lifetime;
        uint16_t   CategoryIdx;
    };

    struct Text2DPrim {
        int         X, Y;
        std::string Text;
        uint32_t    RGBA;
        float       Lifetime;
        uint16_t    CategoryIdx;
    };

    struct Text3DPrim {
        float3      WorldPos;
        std::string Text;
        uint32_t    RGBA;
        float       Lifetime;
        uint16_t    CategoryIdx;
    };

    [[nodiscard]] uint16_t ResolveCategory(const char* Name);
    [[nodiscard]] bool     CategoryEnabled(uint16_t Idx) const;

    // Expand a wireframe box into 12 line segments.
    void EmitBoxLines(float3 Min, float3 Max, uint32_t RGBA,
                      float Lifetime, uint16_t Cat);
    // Expand a wireframe sphere (3 great circles, kSphereSegs each) into lines.
    void EmitSphereLines(float3 Center, float Radius, uint32_t RGBA,
                         float Lifetime, uint16_t Cat);

    rhi::Device*        m_dev{nullptr};
    rhi::Format         m_targetFormat{};
    overlay::Overlay*   m_font{nullptr};

    rhi::PipelineHandle m_linePipeline{};
    /// Per-frame ring buffer for the line vertex SoA. Replaces the prior
    /// single host-upload buffer (which raced with FramesInFlight>1).
    std::unique_ptr<rhi::RingUploadBuffer> m_lineVertRing;

    mutable std::mutex  m_mutex;
    std::vector<LinePrim>   m_lines;
    std::vector<Text2DPrim> m_text2D;
    std::vector<Text3DPrim> m_text3D;

    struct Category {
        std::string Name;
        bool        Visible;
    };
    std::vector<Category> m_categories;
    std::unordered_map<std::string, uint16_t> m_categoryIndex;
};

} // namespace helio::render::debug

// =============================================================================
// Free-function API. Calls forward to the registered singleton (set with
// `SetInstance`). If no instance is registered, calls are no-ops — safe to
// sprinkle debug calls through code that may run before DebugDraw exists.
// =============================================================================
namespace helio::debug {

void SetInstance(render::debug::DebugDraw* Instance);
[[nodiscard]] render::debug::DebugDraw* GetInstance();

/// Decay submission lifetimes by `DeltaSeconds`. Items with `Lifetime <= 0`
/// after this call are evicted. Call ONCE per frame (typically next to your
/// world tick). Without this, lifetime-0 submissions live forever.
void Tick(float DeltaSeconds);

void Line  (float3 P0, float3 P1, uint32_t RGBA, float Lifetime = 0.0f, const char* Category = "Default");
void Box   (float3 Min, float3 Max, uint32_t RGBA, float Lifetime = 0.0f, const char* Category = "Default");
void Sphere(float3 Center, float Radius, uint32_t RGBA, float Lifetime = 0.0f, const char* Category = "Default");
void Text2D(int X, int Y, std::string_view Text, uint32_t RGBA, float Lifetime = 0.0f, const char* Category = "Default");
void Text3D(float3 WorldPos, std::string_view Text, uint32_t RGBA, float Lifetime = 0.0f, const char* Category = "Default");

void SetCategoryVisible(const char* Category, bool Visible);
[[nodiscard]] bool IsCategoryVisible(const char* Category);
void Clear();
void ClearCategory(const char* Category);

} // namespace helio::debug
