#include "DebugDraw.h"

#include <Renderer/Overlay/Overlay.h>
#include <Renderer/RenderGraph.h>

#include <RHI/Public/CommandList.h>
#include <RHI/Public/Device.h>

#include <Core/Assert/Assert.h>
#include <Core/Logging/Log.h>
#include <Core/Profile/Profile.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace helio::render::debug {

namespace {

struct LinePushConsts {
    float    ViewProj[16];   ///< row-major 4x4
    uint32_t VertexBufferSlot;
    uint32_t _Pad0;
    uint32_t _Pad1;
    uint32_t _Pad2;
};
static_assert(sizeof(LinePushConsts) == 80, "LinePushConsts must match DebugLines.slang");

inline DebugDraw::LineVertex MakeVert(float3 P, uint32_t RGBA) {
    return DebugDraw::LineVertex{
        float(P.x), float(P.y), float(P.z), RGBA
    };
}

// Slang's SPIR-V output is column-major by default but `mul(M, v)` keeps HLSL
// row-vector-on-the-left semantics; the bytes the shader sees are interpreted
// as column-major. hlslpp stores row-major in CPU memory, so transpose at
// upload so the shader's `mul(ViewProj, vec4(pos,1))` does the right thing.
inline void StoreMat4(const float4x4& M, float Out[16]) {
    hlslpp::store_transposed(Out, M);
}

constexpr uint32_t kSphereSegs = 24;

} // anonymous namespace

// =============================================================================
// Construction
// =============================================================================
DebugDraw::DebugDraw(rhi::Device& Dev, rhi::Format TargetFormat, overlay::Overlay* Font)
    : m_dev(&Dev), m_targetFormat(TargetFormat), m_font(Font) {

    // Reserve the Default category at index 0.
    (void)ResolveCategory("Default");

    // ---- Per-frame line vertex ring (one host-upload slot per FrameInFlight)
    m_lineVertRing = std::make_unique<rhi::RingUploadBuffer>(
        Dev, kMaxLineVerticesPerFrame * sizeof(LineVertex), "DebugDraw.LineVerts");

    // ---- Pipeline ----------------------------------------------------------
    rhi::GraphicsPipelineDesc Pd{};
    Pd.ShaderPath           = "Shaders/Debug/DebugLines.spv";
    Pd.VertexEntry          = "VSMain";
    Pd.FragmentEntry        = "PSMain";
    Pd.ColorFormats[0]      = m_targetFormat;
    Pd.ColorAttachmentCount = 1;
    Pd.DepthFormat          = rhi::Format::Undefined;
    Pd.Topology             = rhi::PrimitiveTopology::LineList;
    Pd.Cull                 = rhi::CullMode::None;
    Pd.DepthTest            = false;
    Pd.DepthWrite           = false;
    Pd.PushConstantBytes    = sizeof(LinePushConsts);
    Pd.DebugName            = "DebugDraw.Lines";
    m_linePipeline = m_dev->CreateGraphicsPipeline(Pd);
    HELIO_CHECK(m_linePipeline.IsValid());

    HELIO_LOG_INFO("Renderer", "DebugDraw initialized (capacity: {} verts, {} text2D, {} text3D)",
                   kMaxLineVerticesPerFrame, kMaxText2DPerFrame, kMaxText3DPerFrame);
}

DebugDraw::~DebugDraw() {
    if (m_dev) {
        if (m_linePipeline.IsValid()) m_dev->DestroyPipeline(m_linePipeline);
        m_lineVertRing.reset();
    }
}

// =============================================================================
// Category management
// =============================================================================
uint16_t DebugDraw::ResolveCategory(const char* Name) {
    std::string Key{Name ? Name : "Default"};
    auto It = m_categoryIndex.find(Key);
    if (It != m_categoryIndex.end()) return It->second;
    auto Idx = static_cast<uint16_t>(m_categories.size());
    m_categories.push_back({Key, /*Visible=*/true});
    m_categoryIndex.emplace(std::move(Key), Idx);
    return Idx;
}

bool DebugDraw::CategoryEnabled(uint16_t Idx) const {
    return Idx < m_categories.size() && m_categories[Idx].Visible;
}

void DebugDraw::SetCategoryVisible(const char* Category, bool Visible) {
    std::lock_guard L(m_mutex);
    auto Idx = ResolveCategory(Category);
    m_categories[Idx].Visible = Visible;
}

bool DebugDraw::IsCategoryVisible(const char* Category) const {
    std::lock_guard L(m_mutex);
    std::string Key{Category ? Category : "Default"};
    auto It = m_categoryIndex.find(Key);
    if (It == m_categoryIndex.end()) return true; // unknown == visible
    return m_categories[It->second].Visible;
}

// =============================================================================
// Primitive submission
// =============================================================================
void DebugDraw::AddLine(float3 P0, float3 P1, uint32_t RGBA, float Lifetime, const char* Category) {
    std::lock_guard L(m_mutex);
    auto Cat = ResolveCategory(Category);
    m_lines.push_back({MakeVert(P0, RGBA), MakeVert(P1, RGBA), Lifetime, Cat});
}

void DebugDraw::EmitBoxLines(float3 Min, float3 Max, uint32_t RGBA, float Lifetime, uint16_t Cat) {
    // 8 corners of an AABB.
    float3 C[8] = {
        float3(Min.x, Min.y, Min.z), float3(Max.x, Min.y, Min.z),
        float3(Max.x, Max.y, Min.z), float3(Min.x, Max.y, Min.z),
        float3(Min.x, Min.y, Max.z), float3(Max.x, Min.y, Max.z),
        float3(Max.x, Max.y, Max.z), float3(Min.x, Max.y, Max.z),
    };
    // 12 edges: 4 bottom, 4 top, 4 verticals.
    static constexpr int E[12][2] = {
        {0,1},{1,2},{2,3},{3,0},   // bottom rect
        {4,5},{5,6},{6,7},{7,4},   // top rect
        {0,4},{1,5},{2,6},{3,7},   // verticals
    };
    for (auto& Edge : E) {
        m_lines.push_back({MakeVert(C[Edge[0]], RGBA), MakeVert(C[Edge[1]], RGBA), Lifetime, Cat});
    }
}

void DebugDraw::AddBox(float3 Min, float3 Max, uint32_t RGBA, float Lifetime, const char* Category) {
    std::lock_guard L(m_mutex);
    auto Cat = ResolveCategory(Category);
    EmitBoxLines(Min, Max, RGBA, Lifetime, Cat);
}

void DebugDraw::EmitSphereLines(float3 Center, float Radius, uint32_t RGBA,
                                float Lifetime, uint16_t Cat) {
    // 3 great circles in XY, YZ, XZ planes, each `kSphereSegs` segments.
    constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;
    constexpr float Step  = TwoPi / float(kSphereSegs);

    const float Cx = float(Center.x);
    const float Cy = float(Center.y);
    const float Cz = float(Center.z);

    for (uint32_t I = 0; I < kSphereSegs; ++I) {
        const float A0 = Step * float(I);
        const float A1 = Step * float(I + 1);
        const float C0 = std::cos(A0) * Radius;
        const float S0 = std::sin(A0) * Radius;
        const float C1 = std::cos(A1) * Radius;
        const float S1 = std::sin(A1) * Radius;

        // XY
        m_lines.push_back({
            MakeVert(float3(Cx + C0, Cy + S0, Cz), RGBA),
            MakeVert(float3(Cx + C1, Cy + S1, Cz), RGBA), Lifetime, Cat});
        // YZ
        m_lines.push_back({
            MakeVert(float3(Cx, Cy + C0, Cz + S0), RGBA),
            MakeVert(float3(Cx, Cy + C1, Cz + S1), RGBA), Lifetime, Cat});
        // XZ
        m_lines.push_back({
            MakeVert(float3(Cx + C0, Cy, Cz + S0), RGBA),
            MakeVert(float3(Cx + C1, Cy, Cz + S1), RGBA), Lifetime, Cat});
    }
}

void DebugDraw::AddSphere(float3 Center, float Radius, uint32_t RGBA, float Lifetime, const char* Category) {
    std::lock_guard L(m_mutex);
    auto Cat = ResolveCategory(Category);
    EmitSphereLines(Center, Radius, RGBA, Lifetime, Cat);
}

void DebugDraw::AddText2D(int X, int Y, std::string_view Text, uint32_t RGBA, float Lifetime, const char* Category) {
    std::lock_guard L(m_mutex);
    if (m_text2D.size() >= kMaxText2DPerFrame) {
        HELIO_LOG_WARN("Renderer", "DebugDraw Text2D pool full, dropping");
        return;
    }
    m_text2D.push_back({X, Y, std::string{Text}, RGBA, Lifetime, ResolveCategory(Category)});
}

void DebugDraw::AddText3D(float3 WorldPos, std::string_view Text, uint32_t RGBA, float Lifetime, const char* Category) {
    std::lock_guard L(m_mutex);
    if (m_text3D.size() >= kMaxText3DPerFrame) {
        HELIO_LOG_WARN("Renderer", "DebugDraw Text3D pool full, dropping");
        return;
    }
    m_text3D.push_back({WorldPos, std::string{Text}, RGBA, Lifetime, ResolveCategory(Category)});
}

// =============================================================================
// Lifecycle
// =============================================================================
void DebugDraw::Clear() noexcept {
    std::lock_guard L(m_mutex);
    m_lines.clear();
    m_text2D.clear();
    m_text3D.clear();
}

void DebugDraw::ClearCategory(const char* Category) noexcept {
    std::lock_guard L(m_mutex);
    std::string Key{Category ? Category : "Default"};
    auto It = m_categoryIndex.find(Key);
    if (It == m_categoryIndex.end()) return;
    uint16_t Idx = It->second;

    auto Drop = [Idx](const auto& P){ return P.CategoryIdx == Idx; };
    m_lines .erase(std::remove_if(m_lines .begin(), m_lines .end(), Drop), m_lines .end());
    m_text2D.erase(std::remove_if(m_text2D.begin(), m_text2D.end(), Drop), m_text2D.end());
    m_text3D.erase(std::remove_if(m_text3D.begin(), m_text3D.end(), Drop), m_text3D.end());
}

void DebugDraw::Tick(float DeltaSeconds) {
    std::lock_guard L(m_mutex);

    auto Decay = [DeltaSeconds](auto& Container) {
        // Lifetime == 0 means "this frame only" — evict on first Tick after
        // submission. Lifetime > 0 decays each frame; reach <=0 → evict.
        Container.erase(
            std::remove_if(Container.begin(), Container.end(),
                [DeltaSeconds](auto& P) {
                    if (P.Lifetime <= 0.0f) return true;   // single-frame
                    P.Lifetime -= DeltaSeconds;
                    return P.Lifetime <= 0.0f;
                }),
            Container.end());
    };
    Decay(m_lines);
    Decay(m_text2D);
    Decay(m_text3D);
}

// =============================================================================
// Render
// =============================================================================
void DebugDraw::Render(RenderGraph& Rg, rhi::TextureHandle Target,
                       const float4x4& ViewProj, uint32_t TargetWidthPx, uint32_t TargetHeightPx) {
    HELIO_PROFILE_ZONE("DebugDraw::Render");

    std::lock_guard L(m_mutex);

    // ---- Build the line vertex array (visible categories only) ------------
    uint32_t LineVertCount = 0;
    {
        // Stage straight into a CPU array sized to capacity, then upload only
        // the used prefix.
        static thread_local std::vector<LineVertex> Staging;
        Staging.clear();
        Staging.reserve(m_lines.size() * 2);

        for (const auto& Pr : m_lines) {
            if (!CategoryEnabled(Pr.CategoryIdx)) continue;
            if (Staging.size() + 2 > kMaxLineVerticesPerFrame) {
                HELIO_LOG_WARN("Renderer", "DebugDraw line buffer full ({} verts), dropping rest",
                               kMaxLineVerticesPerFrame);
                break;
            }
            Staging.push_back(Pr.A);
            Staging.push_back(Pr.B);
        }

        LineVertCount = static_cast<uint32_t>(Staging.size());
        if (LineVertCount > 0) {
            m_lineVertRing->Write(/*Offset=*/0, Staging.data(),
                                  LineVertCount * sizeof(LineVertex));
        }
    }

    // ---- Schedule the line pass (only if we have something to draw) -------
    if (LineVertCount > 0) {
        // Stash everything the lambda needs by value (lambda survives the
        // ResourceUse lifetime; data is plain trivial types or ptrs we own).
        LinePushConsts Pc{};
        StoreMat4(ViewProj, Pc.ViewProj);
        Pc.VertexBufferSlot = m_lineVertRing->Current().BindlessSlot;
        auto Pipe  = m_linePipeline;
        auto Count = LineVertCount;

        Rg.Graphics("DebugDraw.Lines")
          .ColorLoad(Target)
          .Execute([Pc, Pipe, Count](rhi::CommandList& Cmd) {
              Cmd.Bind(Pipe);
              Cmd.SetViewportFull();
              Cmd.Push(Pc);
              Cmd.Draw(Count);
          });
    }

    // ---- Forward Text2D / Text3D to the Overlay font ----------------------
    if (m_font && (TargetWidthPx > 0) && (TargetHeightPx > 0)) {
        for (const auto& T : m_text2D) {
            if (!CategoryEnabled(T.CategoryIdx)) continue;
            m_font->DrawText(T.X, T.Y, T.Text, T.RGBA);
        }

        for (const auto& T : m_text3D) {
            if (!CategoryEnabled(T.CategoryIdx)) continue;

            // World → clip → NDC → pixel. Skip if behind the camera or
            // outside the frustum (cheap culling so off-screen text doesn't
            // blow the glyph budget).
            float4 World{float(T.WorldPos.x), float(T.WorldPos.y), float(T.WorldPos.z), 1.0f};
            float4 Clip = hlslpp::mul(ViewProj, World);
            const float W = float(Clip.w);
            if (W <= 0.0f) continue;
            const float NdcX = float(Clip.x) / W;
            const float NdcY = float(Clip.y) / W;
            if (NdcX < -1.5f || NdcX > 1.5f || NdcY < -1.5f || NdcY > 1.5f) continue;

            int Px = static_cast<int>((NdcX * 0.5f + 0.5f) * float(TargetWidthPx));
            int Py = static_cast<int>((NdcY * 0.5f + 0.5f) * float(TargetHeightPx));
            m_font->DrawText(Px, Py, T.Text, T.RGBA);
        }
    }
}

} // namespace helio::render::debug

// =============================================================================
// Free-function API forwarders
// =============================================================================
namespace helio::debug {

namespace { render::debug::DebugDraw* g_instance = nullptr; }

void SetInstance(render::debug::DebugDraw* Instance) { g_instance = Instance; }
render::debug::DebugDraw* GetInstance() { return g_instance; }

void Tick(float DeltaSeconds) {
    if (g_instance) g_instance->Tick(DeltaSeconds);
}
void Line(float3 P0, float3 P1, uint32_t RGBA, float Lifetime, const char* Category) {
    if (g_instance) g_instance->AddLine(P0, P1, RGBA, Lifetime, Category);
}
void Box(float3 Min, float3 Max, uint32_t RGBA, float Lifetime, const char* Category) {
    if (g_instance) g_instance->AddBox(Min, Max, RGBA, Lifetime, Category);
}
void Sphere(float3 Center, float Radius, uint32_t RGBA, float Lifetime, const char* Category) {
    if (g_instance) g_instance->AddSphere(Center, Radius, RGBA, Lifetime, Category);
}
void Text2D(int X, int Y, std::string_view Text, uint32_t RGBA, float Lifetime, const char* Category) {
    if (g_instance) g_instance->AddText2D(X, Y, Text, RGBA, Lifetime, Category);
}
void Text3D(float3 WorldPos, std::string_view Text, uint32_t RGBA, float Lifetime, const char* Category) {
    if (g_instance) g_instance->AddText3D(WorldPos, Text, RGBA, Lifetime, Category);
}
void SetCategoryVisible(const char* Category, bool Visible) {
    if (g_instance) g_instance->SetCategoryVisible(Category, Visible);
}
bool IsCategoryVisible(const char* Category) {
    return g_instance ? g_instance->IsCategoryVisible(Category) : true;
}
void Clear()                                  { if (g_instance) g_instance->Clear(); }
void ClearCategory(const char* Category)      { if (g_instance) g_instance->ClearCategory(Category); }

} // namespace helio::debug
