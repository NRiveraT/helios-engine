// Vertex.slang — shared 48-byte interleaved vertex layout.
//
// Must match `helio::resource::Vertex` (Source/Resource/MeshData.h).
// Tangent.w is the bitangent sign (glTF convention).

struct Vertex {
    float3 Pos;       // 12
    float3 Normal;    // 12
    float2 UV;        //  8
    float4 Tangent;   // 16   (w = bitangent sign)
};

struct MeshInstance {
    float4x4 Transform;   // 64 — row-major
};

static const uint kVertexStrideBytes   = 48;
static const uint kInstanceStrideBytes = 64;

import Bindless;

// Load a Vertex by index from a bindless storage-buffer slot.
Vertex LoadVertex(uint VertexBufferSlot, uint Index) {
    ByteAddressBuffer B = GetStorageBuffer(VertexBufferSlot);
    uint Base = Index * kVertexStrideBytes;
    Vertex V;
    V.Pos     = asfloat(B.Load3(Base + 0));
    V.Normal  = asfloat(B.Load3(Base + 12));
    V.UV      = asfloat(B.Load2(Base + 24));
    V.Tangent = asfloat(B.Load4(Base + 32));
    return V;
}

// Load a MeshInstance transform matrix by index.
//
// CPU writes via `Mat4Packed::operator=` which calls `hlslpp::store_transposed`.
// That puts M^T in memory row-major — equivalently, each 16-byte chunk is a
// COLUMN of the original matrix M. We rebuild M by loading those columns and
// transposing back, so `mul(M, v_col)` in the calling shader behaves as
// standard column-vector math expects.
float4x4 LoadInstanceTransform(uint InstanceBufferSlot, uint Index) {
    ByteAddressBuffer B = GetStorageBuffer(InstanceBufferSlot);
    uint Base = Index * kInstanceStrideBytes;
    float4 C0 = asfloat(B.Load4(Base +  0));   // column 0 of M
    float4 C1 = asfloat(B.Load4(Base + 16));   // column 1 of M
    float4 C2 = asfloat(B.Load4(Base + 32));   // column 2 of M
    float4 C3 = asfloat(B.Load4(Base + 48));   // column 3 of M
    // float4x4(R0,R1,R2,R3) builds matrix with each Ri as a ROW. Our C0..C3
    // are the columns we want, so transpose the result.
    return transpose(float4x4(C0, C1, C2, C3));
}
