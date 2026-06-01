// Overlay.slang — instanced quad shader used by Helio's on-screen overlay.
//
// One draw call: vertex count = 6, instance count = number of glyphs/rects.
// The vertex shader expands each instance into a 6-vertex quad and looks up
// the entry's data from a storage-buffer SoA. The fragment shader samples the
// font texture (R8_UNORM, 128x48) and outputs alpha-tested color — UNLESS the
// `Code` is the reserved sentinel `0xFFFFFFFF`, in which case the entry is a
// solid colored rectangle and the texture sample is skipped.
//
// Push constants:
//   FontTextureSlot — bindless sampled-image slot of the baked font atlas.
//   GlyphBufferSlot — bindless storage-buffer slot of the glyph array.
//   ViewWidthPx     — target framebuffer width in pixels (for pixel→NDC).
//   ViewHeightPx    — target framebuffer height in pixels.
//
// Glyph layout (32 bytes, must match `helio::render::overlay::Overlay::Glyph`):
//   float2 PixelPos    — top-left corner in pixels
//   float2 Size        — quad pixel dimensions (8x8 for text glyphs)
//   uint   Code        — ASCII code, or 0xFFFFFFFF for solid rect
//   uint   RGBA        — packed 0xAABBGGRR
//   uint   _Pad[2]

import Bindless;

struct PushConsts {
    uint FontTextureSlot;
    uint GlyphBufferSlot;
    float ViewWidthPx;
    float ViewHeightPx;
};
[[vk::push_constant]] PushConsts pc;

struct VSOut {
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
    float4 Color    : COLOR0;
    nointerpolation uint IsSolid : COLOR1;
};

static const uint kGlyphStrideBytes = 32;
static const float kGlyphWidthPx    = 8.0f;
static const float kGlyphHeightPx   = 8.0f;
static const float kAtlasWidthPx    = 128.0f;
static const float kAtlasHeightPx   = 48.0f;
static const uint  kAtlasCols       = 16;
static const uint  kFirstPrintable  = 32; // ASCII ' '
static const uint  kSolidRectCode   = 0xFFFFFFFFu;

[shader("vertex")]
VSOut VSMain(uint VertexID : SV_VertexID, uint InstanceID : SV_InstanceID) {
    // Two-triangle quad corners in (x, y) ∈ {0,1}. Order matters for CCW.
    static const float2 kCorners[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(1, 0), float2(1, 1), float2(0, 1),
    };

    ByteAddressBuffer Glyphs = GetStorageBuffer(pc.GlyphBufferSlot);
    uint Base = InstanceID * kGlyphStrideBytes;
    float2 PixelPos = asfloat(Glyphs.Load2(Base + 0));
    float2 Size     = asfloat(Glyphs.Load2(Base + 8));
    uint   Code     = Glyphs.Load(Base + 16);
    uint   RGBA     = Glyphs.Load(Base + 20);

    uint   IsSolid = (Code == kSolidRectCode) ? 1u : 0u;

    // Out-of-range chars (non-solid path) collapse to blank space.
    if (IsSolid == 0u && (Code < kFirstPrintable || Code > 127)) {
        Code = kFirstPrintable;
    }

    float2 Corner = kCorners[VertexID];
    float2 Pixel  = PixelPos + Corner * Size;

    // Pixel → NDC. Vulkan SV_Position uses y-down NDC, matching our PixelPos
    // convention: PixelPos = (0, 0) → top-left → NDC (-1, -1).
    float2 NDC = float2(
         (Pixel.x / pc.ViewWidthPx)  * 2.0f - 1.0f,
         (Pixel.y / pc.ViewHeightPx) * 2.0f - 1.0f
    );

    // For glyphs, compute the sub-rect of the font atlas. For solid rects we
    // still emit something sensible (UV not used downstream).
    float2 AtlasUV = float2(0.0f, 0.0f);
    if (IsSolid == 0u) {
        uint GlyphIdx = Code - kFirstPrintable;
        uint AtlasCol = GlyphIdx % kAtlasCols;
        uint AtlasRow = GlyphIdx / kAtlasCols;
        float2 AtlasOrigin = float2(AtlasCol * kGlyphWidthPx, AtlasRow * kGlyphHeightPx);
        AtlasUV = (AtlasOrigin + Corner * float2(kGlyphWidthPx, kGlyphHeightPx))
                / float2(kAtlasWidthPx, kAtlasHeightPx);
    }

    // Unpack 0xAABBGGRR.
    float4 Color = float4(
        ((RGBA      ) & 0xFFu) / 255.0f,
        ((RGBA >>  8) & 0xFFu) / 255.0f,
        ((RGBA >> 16) & 0xFFu) / 255.0f,
        ((RGBA >> 24) & 0xFFu) / 255.0f
    );

    VSOut Out;
    Out.Position = float4(NDC, 0.0f, 1.0f);
    Out.UV       = AtlasUV;
    Out.Color    = Color;
    Out.IsSolid  = IsSolid;
    return Out;
}

[shader("fragment")]
float4 PSMain(VSOut In) : SV_Target {
    if (In.Color.a < 0.01f) discard;

    // Solid rect — no font sample, output the color (with alpha) directly.
    // No blend state yet in V1, so alpha is binary (RGBA's A byte: 0 = hidden,
    // non-zero = opaque). Phase 13 polish adds real blending so HUD elements
    // can fade in/out smoothly.
    if (In.IsSolid != 0u) {
        return float4(In.Color.rgb, 1.0f);
    }

    // Glyph path — 1-bit alpha test via discard so the underlying scene shows
    // through the empty texels.
    float Alpha = GetTexture2D(pc.FontTextureSlot)
                    .Sample(GetSampler(kSamplerPointClamp), In.UV).r;
    if (Alpha < 0.5f) discard;
    return float4(In.Color.rgb, 1.0f);
}
