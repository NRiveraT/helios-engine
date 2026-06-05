#include "MeshPrimitives.h"

#include <cmath>
#include <numbers>

namespace helio::resource::primitives {

namespace {
    // Helper: build a Vertex with all attributes set.
    inline Vertex MakeVert(float Px, float Py, float Pz,
                           float Nx, float Ny, float Nz,
                           float U,  float V,
                           float Tx = 0.0f, float Ty = 0.0f, float Tz = 0.0f, float Tw = 1.0f) {
        Vertex Vt{};
        Vt.Pos[0] = Px; Vt.Pos[1] = Py; Vt.Pos[2] = Pz;
        Vt.Normal[0] = Nx; Vt.Normal[1] = Ny; Vt.Normal[2] = Nz;
        Vt.UV[0] = U; Vt.UV[1] = V;
        Vt.Tangent[0] = Tx; Vt.Tangent[1] = Ty; Vt.Tangent[2] = Tz; Vt.Tangent[3] = Tw;
        return Vt;
    }
}

// =============================================================================
// Cube — 6 faces × 4 verts/face (hard normals between faces) = 24 verts, 36 idx
// =============================================================================
MeshData Cube(float Size) {
    const float H = 0.5f * Size;
    MeshData M;
    M.Vertices.reserve(24);
    M.Indices.reserve(36);

    // Face definition: 6 faces, each with normal, tangent, and 4 corners (CCW
    // looking down -normal so the front faces outward in LH +Z-forward).
    struct Face {
        float Nx, Ny, Nz;            // normal
        float Tx, Ty, Tz;            // tangent (xyz; w = +1)
        float Px[4], Py[4], Pz[4];   // 4 corners (CCW)
        float U [4], V [4];          // matching UVs
    };

    const Face Faces[6] = {
        // +X face — normal (+1,0,0), tangent (0,0,-1)
        {+1,0,0,  0,0,-1,
         { H, H, H, H}, { H, H,-H,-H}, {-H, H, H,-H},
         {0,1,1,0}, {1,1,0,0}},
        // -X face — normal (-1,0,0), tangent (0,0,+1)
        {-1,0,0,  0,0,+1,
         {-H,-H,-H,-H}, { H, H,-H,-H}, { H,-H,-H, H},
         {0,1,1,0}, {1,1,0,0}},
        // +Y face — normal (0,+1,0), tangent (+1,0,0).
        // Winding is CCW when viewed from above (looking down -Y), matching
        // the same "outward-CCW" convention used by the X/Z faces and by the
        // `Plane` primitive below.
        {0,+1,0, +1,0,0,
         {-H,-H, H, H}, { H, H, H, H}, {-H, H, H,-H},
         {0,0,1,1}, {1,0,0,1}},
        // -Y face — normal (0,-1,0), tangent (+1,0,0). CCW from below.
        {0,-1,0, +1,0,0,
         {-H,-H, H, H}, {-H,-H,-H,-H}, { H,-H,-H, H},
         {0,0,1,1}, {1,0,0,1}},
        // +Z face — normal (0,0,+1), tangent (+1,0,0)
        {0,0,+1, +1,0,0,
         {-H, H, H,-H}, {-H,-H, H, H}, { H, H, H, H},
         {0,1,1,0}, {1,1,0,0}},
        // -Z face — normal (0,0,-1), tangent (-1,0,0)
        {0,0,-1, -1,0,0,
         { H,-H,-H, H}, {-H,-H, H, H}, {-H,-H,-H,-H},
         {0,1,1,0}, {1,1,0,0}},
    };

    for (const auto& F : Faces) {
        const uint32_t Base = static_cast<uint32_t>(M.Vertices.size());
        for (int I = 0; I < 4; ++I) {
            M.Vertices.push_back(MakeVert(F.Px[I], F.Py[I], F.Pz[I],
                                          F.Nx, F.Ny, F.Nz,
                                          F.U[I], F.V[I],
                                          F.Tx, F.Ty, F.Tz, 1.0f));
        }
        // Two triangles per face: (0,1,2) (0,2,3) — CCW front-facing.
        M.Indices.push_back(Base + 0);
        M.Indices.push_back(Base + 1);
        M.Indices.push_back(Base + 2);
        M.Indices.push_back(Base + 0);
        M.Indices.push_back(Base + 2);
        M.Indices.push_back(Base + 3);
    }

    M.RecomputeBounds();
    return M;
}

// =============================================================================
// UV Sphere
// =============================================================================
MeshData Sphere(float Radius, uint32_t Segments, uint32_t Rings) {
    if (Segments < 3) Segments = 3;
    if (Rings    < 2) Rings    = 2;

    MeshData M;
    M.Vertices.reserve((Segments + 1) * (Rings + 1));
    M.Indices .reserve(Segments * Rings * 6);

    constexpr float Pi    = std::numbers::pi_v<float>;
    constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;

    for (uint32_t R = 0; R <= Rings; ++R) {
        const float V    = float(R) / float(Rings);              // 0..1
        const float Phi  = V * Pi;                               // 0..Pi (north → south)
        const float SinP = std::sin(Phi), CosP = std::cos(Phi);

        for (uint32_t S = 0; S <= Segments; ++S) {
            const float U    = float(S) / float(Segments);        // 0..1
            const float Th   = U * TwoPi;                         // 0..2Pi (longitude)
            const float SinT = std::sin(Th), CosT = std::cos(Th);

            const float Nx = SinP * CosT;
            const float Ny = CosP;
            const float Nz = SinP * SinT;
            const float Px = Radius * Nx;
            const float Py = Radius * Ny;
            const float Pz = Radius * Nz;
            // Tangent points in +U direction (longitude tangent).
            const float Tx = -SinT;
            const float Ty =  0.0f;
            const float Tz =  CosT;
            M.Vertices.push_back(MakeVert(Px, Py, Pz, Nx, Ny, Nz, U, V, Tx, Ty, Tz, 1.0f));
        }
    }

    const uint32_t Stride = Segments + 1;
    for (uint32_t R = 0; R < Rings; ++R) {
        for (uint32_t S = 0; S < Segments; ++S) {
            const uint32_t A = (R + 0) * Stride + S;
            const uint32_t B = (R + 0) * Stride + S + 1;
            const uint32_t C = (R + 1) * Stride + S;
            const uint32_t D = (R + 1) * Stride + S + 1;
            // Outward-CCW: walk A → B → C and B → D → C so cross(E1,E2)
            // points AWAY from the sphere center. Matches the cube and
            // plane convention; previous order had the cross pointing
            // inward, so the sphere rendered with inverted lighting.
            M.Indices.push_back(A); M.Indices.push_back(B); M.Indices.push_back(C);
            M.Indices.push_back(B); M.Indices.push_back(D); M.Indices.push_back(C);
        }
    }

    M.RecomputeBounds();
    return M;
}

// =============================================================================
// Plane — XZ, normal up
// =============================================================================
MeshData Plane(float Width, float Depth, uint32_t SubdivX, uint32_t SubdivZ) {
    if (SubdivX < 1) SubdivX = 1;
    if (SubdivZ < 1) SubdivZ = 1;

    MeshData M;
    M.Vertices.reserve((SubdivX + 1) * (SubdivZ + 1));
    M.Indices .reserve(SubdivX * SubdivZ * 6);

    const float HX = 0.5f * Width;
    const float HZ = 0.5f * Depth;

    for (uint32_t Z = 0; Z <= SubdivZ; ++Z) {
        const float Tz = float(Z) / float(SubdivZ);
        const float Pz = -HZ + Tz * Depth;
        for (uint32_t X = 0; X <= SubdivX; ++X) {
            const float Tx = float(X) / float(SubdivX);
            const float Px = -HX + Tx * Width;
            M.Vertices.push_back(MakeVert(Px, 0.0f, Pz,
                                          0.0f, 1.0f, 0.0f,
                                          Tx, Tz,
                                          1.0f, 0.0f, 0.0f, 1.0f));
        }
    }

    const uint32_t Stride = SubdivX + 1;
    for (uint32_t Z = 0; Z < SubdivZ; ++Z) {
        for (uint32_t X = 0; X < SubdivX; ++X) {
            const uint32_t A = (Z + 0) * Stride + X;
            const uint32_t B = (Z + 0) * Stride + X + 1;
            const uint32_t C = (Z + 1) * Stride + X;
            const uint32_t D = (Z + 1) * Stride + X + 1;
            // Normal is +Y, so CCW when viewed from above means (A, C, B) (B, C, D).
            M.Indices.push_back(A); M.Indices.push_back(C); M.Indices.push_back(B);
            M.Indices.push_back(B); M.Indices.push_back(C); M.Indices.push_back(D);
        }
    }

    M.RecomputeBounds();
    return M;
}

// =============================================================================
// Cylinder — Y-axis, with caps
// =============================================================================
MeshData Cylinder(float Radius, float Height, uint32_t Segments) {
    if (Segments < 3) Segments = 3;

    MeshData M;
    // Side: 2 rows of (Segments+1) verts
    // Caps: 1 center + Segments rim per cap
    M.Vertices.reserve(2 * (Segments + 1) + 2 + 2 * Segments);
    M.Indices .reserve(Segments * 6 + Segments * 3 * 2);

    constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;
    const float H = 0.5f * Height;

    // ---- Side -----------------------------------------------------------
    const uint32_t SideBase = 0;
    for (uint32_t Row = 0; Row < 2; ++Row) {
        const float Y = (Row == 0) ? -H : +H;
        for (uint32_t S = 0; S <= Segments; ++S) {
            const float U = float(S) / float(Segments);
            const float Th = U * TwoPi;
            const float Cs = std::cos(Th), Sn = std::sin(Th);
            const float Nx = Cs, Nz = Sn;
            // Tangent runs along +U direction.
            const float Tx = -Sn, Tz = Cs;
            M.Vertices.push_back(MakeVert(Radius * Cs, Y, Radius * Sn,
                                          Nx, 0.0f, Nz,
                                          U, (Row == 0) ? 0.0f : 1.0f,
                                          Tx, 0.0f, Tz, 1.0f));
        }
    }
    const uint32_t Stride = Segments + 1;
    for (uint32_t S = 0; S < Segments; ++S) {
        const uint32_t A = SideBase + 0 * Stride + S;
        const uint32_t B = SideBase + 0 * Stride + S + 1;
        const uint32_t C = SideBase + 1 * Stride + S;
        const uint32_t D = SideBase + 1 * Stride + S + 1;
        M.Indices.push_back(A); M.Indices.push_back(C); M.Indices.push_back(B);
        M.Indices.push_back(B); M.Indices.push_back(C); M.Indices.push_back(D);
    }

    // ---- Top cap (+Y) ---------------------------------------------------
    const uint32_t TopCenter = static_cast<uint32_t>(M.Vertices.size());
    M.Vertices.push_back(MakeVert(0.0f, +H, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f,
                                  1.0f, 0.0f, 0.0f, 1.0f));
    const uint32_t TopRim0 = static_cast<uint32_t>(M.Vertices.size());
    for (uint32_t S = 0; S < Segments; ++S) {
        const float U  = float(S) / float(Segments);
        const float Th = U * TwoPi;
        const float Cs = std::cos(Th), Sn = std::sin(Th);
        M.Vertices.push_back(MakeVert(Radius * Cs, +H, Radius * Sn,
                                      0.0f, 1.0f, 0.0f,
                                      0.5f + 0.5f * Cs, 0.5f + 0.5f * Sn,
                                      1.0f, 0.0f, 0.0f, 1.0f));
    }
    // Outward-CCW from above: walk Center → B → A so the cross product
    // points +Y. (Center → A → B winds inward, giving -Y.)
    for (uint32_t S = 0; S < Segments; ++S) {
        const uint32_t A = TopRim0 + S;
        const uint32_t B = TopRim0 + ((S + 1) % Segments);
        M.Indices.push_back(TopCenter);
        M.Indices.push_back(B);
        M.Indices.push_back(A);
    }

    // ---- Bottom cap (-Y) ------------------------------------------------
    const uint32_t BotCenter = static_cast<uint32_t>(M.Vertices.size());
    M.Vertices.push_back(MakeVert(0.0f, -H, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f,
                                  1.0f, 0.0f, 0.0f, 1.0f));
    const uint32_t BotRim0 = static_cast<uint32_t>(M.Vertices.size());
    for (uint32_t S = 0; S < Segments; ++S) {
        const float U  = float(S) / float(Segments);
        const float Th = U * TwoPi;
        const float Cs = std::cos(Th), Sn = std::sin(Th);
        M.Vertices.push_back(MakeVert(Radius * Cs, -H, Radius * Sn,
                                      0.0f, -1.0f, 0.0f,
                                      0.5f + 0.5f * Cs, 0.5f + 0.5f * Sn,
                                      1.0f, 0.0f, 0.0f, 1.0f));
    }
    // Outward-CCW from below: walk Center → A → B so the cross product
    // points -Y. (Center → B → A winds inward, giving +Y.)
    for (uint32_t S = 0; S < Segments; ++S) {
        const uint32_t A = BotRim0 + S;
        const uint32_t B = BotRim0 + ((S + 1) % Segments);
        M.Indices.push_back(BotCenter);
        M.Indices.push_back(A);
        M.Indices.push_back(B);
    }

    M.RecomputeBounds();
    return M;
}

} // namespace helio::resource::primitives
