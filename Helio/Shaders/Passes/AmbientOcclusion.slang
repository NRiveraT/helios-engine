import Vertex;
import Frame;
import Bindless;
import Fullscreen;

struct AOPushConsts {
  uint FrameSlot;
  uint NormalSamplerSlot;
  uint DepthSlot;
  float4x4 View;
};

static const uint SAMPLES = 16;
static const float BIAS = 0.025;
static const float RADIUS = 0.5;
static const float STRENGTH = 1.5;
static const float PI = 3.14159265;
static const float GOLDEN_RATIO = 2.399962;

[[vk::push_constant]]
AOPushConsts pc;

[shader("vertex")]
FullscreenVSOut VSMain(uint id: SV_VertexID) { return FullscreenVS(id); }

// van der Corput radical inverse → Hammersley low-discrepancy set (no uploaded
// kernel)
float RadicalInverse(uint b) {
  b = (b << 16) | (b >> 16);
  b = ((b & 0x55555555u) << 1) | ((b & 0xAAAAAAAAu) >> 1);
  b = ((b & 0x33333333u) << 2) | ((b & 0xCCCCCCCCu) >> 2);
  b = ((b & 0x0F0F0F0Fu) << 4) | ((b & 0xF0F0F0F0u) >> 4);
  b = ((b & 0x00FF00FFu) << 8) | ((b & 0xFF00FF00u) >> 8);
  return float(b) * 2.3283064365386963e-10; // ÷ 2^32
}
float2 Hammersley(uint i, uint n) {
  return float2(float(i) / n, RadicalInverse(i));
}

float3 ReconstructViewPos(float2 uv, float d, float4x4 invProj) {
  float3 ndc = float3(uv * 2.0 - 1, d);
  float4 vh = mul(invProj, float4(ndc, 1.0));
  return vh.xyz / vh.w;
}

float DepthAt(uint Slot, float2 uv) {
    return GetTexture2D(Slot).SampleLevel(GetSampler(kSamplerPointClamp), uv, 0.0).r;
}

float InterleaveGradientNoise(float2 p) {
  return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

float GoldenRatio(uint i, float seed) {
  return frac(seed + i * 0.61803398875);
}

[shader("fragment")]
float4 PSMain(FullscreenVSOut In) : SV_Target {
  FrameConstants Frame = LoadFrameConstants(pc.FrameSlot);

  float d = DepthAt(pc.DepthSlot, In.UV);
  if (d <= 0.0)
    return float4(1, 1, 1, 1);

  float3 P = ReconstructViewPos(In.UV, d, Frame.InvProj);
  float3 Nw = normalize(GetTexture2D(pc.NormalSamplerSlot).SampleLevel(GetSampler(kSamplerPointClamp), In.UV, 0.0).rgb);
  float3 N = normalize(mul((float3x3)pc.View, Nw));

  float ang = InterleaveGradientNoise(In.UV * Frame.ViewportAO.xy) * 2.0 * PI;
  float3 rand = float3(cos(ang), sin(ang), 0.0);
  float3 T = normalize(rand - N * dot(rand, N));
  float3 B = cross(N, T);

  float occ = 0.0;
  for (uint i = 0; i < SAMPLES; ++i) {
    float2 xi = float2(GoldenRatio(i, SAMPLES), GoldenRatio(i, SAMPLES));
    float z = xi.x;
    float r = sqrt(1.0 - z * z);
    float ph = 2.0 * PI * xi.y;
    float3 k = float3(r * cos(ph), r * sin(ph), z);
    k *= lerp(0.1, 1.0, float(i * i) / float(SAMPLES * SAMPLES));

    float3 samplePos = P + (T * k.x + B * k.y + N * k.z) * RADIUS;

    float4 sc = mul(Frame.Proj, float4(samplePos, 1.0));
    float2 su = (sc.xy / sc.w) * 0.5 + 0.5;
    if (any(su < 0.0) || any(su > 1.0))
      continue;

    float sd = DepthAt(pc.DepthSlot, su);
    if (sd <= 0.0)
      continue;

    float storedZ = ReconstructViewPos(su, sd, Frame.InvProj).z;

    float range = smoothstep(0.0, 1.0, RADIUS / abs(P.z - storedZ));
    occ += (storedZ < samplePos.z - BIAS ? 1.0 : 0.0) * range;
  }

  float ao = pow(saturate(1.0 - occ / float(SAMPLES)), STRENGTH);
  return float4(ao, ao, ao, 1);
}
