// Triangle.slang — Phase 6 verification.
// Hardcoded RGB triangle. No descriptor reads, no push constants.

struct VSOut {
    float4 Position : SV_Position;
    float3 Color    : COLOR0;
};

[shader("vertex")]
VSOut VSMain(uint VertexID : SV_VertexID) {
    const float2 Pos[3] = {
        float2( 0.0f,  -0.5f),
        float2(-0.5f, 0.5f),
        float2( 0.5f, 0.5f),
    };
    const float3 Col[3] = {
        float3(1.0f, 0.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, 0.0f, 1.0f),
    };
    VSOut Out;
    Out.Position = float4(Pos[VertexID], 0.0f, 1.0f);
    Out.Color = Col[VertexID];
    return Out;
}

[shader("fragment")]
float4 PSMain(VSOut In) : SV_Target {
    return float4(In.Color, 1.0f);
}
