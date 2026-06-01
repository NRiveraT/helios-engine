# Shaders/RT

Raytracing shaders for V1 verification.

V1 contents:
- `RayQueryShadow.slang` — compute shader that does `traceRayEXT` against `BuildVerificationTLAS()`, writes a binary occlusion image (Phase 8).
