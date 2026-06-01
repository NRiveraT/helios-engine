// RayMarch.slang — animated SDF ray marcher, fully self-contained.
//
// No scene data, no buffers, no textures — just signed-distance-field math
// and a small fixed-step ray traversal. Great compute demo: every pixel
// independently casts a ray and shades the hit.
//
// Scene: a moving sphere on a checkered ground plane, with a soft sky.
// Dispatch: `Cmd->Dispatch2D(Width, Height, 8, 8);` after pushing PC.

import Bindless;

struct PC {
    uint  OutputSlot;
    float Time;        // seconds since startup — push from CPU
    float Width;       // ImageSize.x
    float Height;      // ImageSize.y
};
[[vk::push_constant]] PC pc;

// -----------------------------------------------------------------------------
// SDF building blocks. Each returns the signed distance from world-space `p`
// to the nearest surface of that primitive (negative inside, positive outside).
// -----------------------------------------------------------------------------
float sdSphere(float3 p, float r) {
    return length(p) - r;
}

float sdPlane(float3 p, float h) {
    return p.y - h;
}

// Smooth-min — blends two SDFs without a hard seam. Try `Scene` using sminstead
// of `min` for a melted-together look.
float smin(float a, float b, float k) {
    float h = saturate(0.5 + 0.5 * (b - a) / k);
    return lerp(b, a, h) - k * h * (1.0 - h);
}

// -----------------------------------------------------------------------------
// Scene: one bouncing sphere + an infinite ground plane.
// -----------------------------------------------------------------------------
float Scene(float3 p) {
    float3 spherePos = float3(sin(pc.Time) * 1.5, abs(sin(pc.Time * 2.0)) * 0.5, cos(pc.Time) * 0.5);
    float s  = sdSphere(p - spherePos, 0.6);
    float pl = sdPlane(p, -1.0);
    return min(s, pl);
}

// Numerical gradient of the SDF gives a usable normal at any hit point.
float3 SceneNormal(float3 p) {
    const float h = 0.001;
    return normalize(float3(
        Scene(p + float3(h, 0, 0)) - Scene(p - float3(h, 0, 0)),
        Scene(p + float3(0, h, 0)) - Scene(p - float3(0, h, 0)),
        Scene(p + float3(0, 0, h)) - Scene(p - float3(0, 0, h))
    ));
}

// March along a ray from `origin` in direction `dir`. Returns t such that
// origin + dir * t is the surface intersection, or a negative value on miss.
float March(float3 origin, float3 dir, out float3 hitPos) {
    float t = 0.0;
    [unroll(64)]
    for (int i = 0; i < 64; ++i) {
        hitPos = origin + dir * t;
        float d = Scene(hitPos);
        if (d < 0.001) return t;        // close enough to call it a hit
        if (t > 50.0)  return -1.0;     // ran out of march range — miss
        t += d;                          // sphere-tracing step
    }
    return -1.0;
}

// Cheap procedural ground-plane checker pattern.
float Checker(float2 p) {
    p *= 0.7;
    return (frac(floor(p.x) + floor(p.y)) < 0.5) ? 0.7 : 0.4;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CSMain(uint3 TID : SV_DispatchThreadID) {
    RWTexture2D<float4> Out = GetStorageImage(pc.OutputSlot);
    if (TID.x >= uint(pc.Width) || TID.y >= uint(pc.Height)) return;

    // Map pixel index → NDC in [-aspect, +aspect] x [-1, +1], Y up.
    float2 UV  = (float2(TID.xy) + 0.5) / float2(pc.Width, pc.Height);
    float2 NDC = UV * 2.0 - 1.0;
    NDC.x *= pc.Width / pc.Height;
    NDC.y *= -1.0;   // flip so +Y is up

    // Camera at (0, 1, -3) looking toward +Z.
    float3 RayOrigin = float3(0.0, 1.0, -3.0);
    float3 RayDir    = normalize(float3(NDC, 1.5));

    float3 HitPos;
    float  t = March(RayOrigin, RayDir, HitPos);

    float3 Color;
    if (t > 0.0) {
        float3 N = SceneNormal(HitPos);
        float3 L = normalize(float3(0.6, 1.0, -0.4));   // sun
        float  NdotL = saturate(dot(N, L));

        // Albedo: checker on the plane, blue on the sphere.
        float3 Albedo = (N.y > 0.95) ? float3(Checker(HitPos.xz).xxx) : float3(0.25, 0.55, 0.9);

        // Lambert + a touch of ambient + cheap fake fresnel.
        float Fresnel = pow(1.0 - saturate(dot(-RayDir, N)), 3.0);
        Color = Albedo * (NdotL * 0.9 + 0.1) + Fresnel * 0.2;
    } else {
        // Sky: vertical gradient + a small sun disk along L.
        float Up = saturate(RayDir.y * 0.5 + 0.5);
        float3 Sky = lerp(float3(0.55, 0.6, 0.75), float3(0.15, 0.3, 0.65), Up);
        float Sun = pow(saturate(dot(RayDir, normalize(float3(0.6, 1.0, -0.4)))), 256.0);
        Color = Sky + float3(1.0, 0.9, 0.7) * Sun;
    }

    Out[TID.xy] = float4(Color, 1.0);
}
