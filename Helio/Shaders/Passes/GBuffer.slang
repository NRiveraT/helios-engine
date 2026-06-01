import Fullscreen;

public struct GBufferOut
{
    float4 alb : SV_Target0;
    float4 nor : SV_Target1;
    float4 emi : SV_Target2;
};

[shader("vertex")]
FullscreenVSOut VSMain(uint VertexID : SV_VertexID) { return FullscreenVS(VertexID); }

[shader("fragment")]
GBufferOut PSMain(FullscreenVSOut In) {
    GBufferOut out;

    out.alb = float4(.8, 0.5,0.5,1.0);
    out.nor = float4(0.5,0.5,1.0,1.0);
    out.emi = float4(0,0,1,1);

    return out;
}