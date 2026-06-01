/// @file Profile.h
/// @brief Tracy profiler macros + GPU stub for V1.
///
/// Compiled out via the HELIO_TRACY_ENABLED define (set in Helio.Core's
/// CMakeLists.txt from the HELIO_TRACY option).
///
/// Common macros:
/// - HELIO_PROFILE_ZONE(Name)      — CPU zone covering enclosing scope
/// - HELIO_PROFILE_FRAME()         — mark end of frame (call once per frame)
/// - HELIO_PROFILE_PLOT(Name, V)   — plot a numeric value over time
/// - HELIO_PROFILE_GPU(Cmd, Name)  — GPU zone (no-op in Phase 2; wired in Phase 4)
#pragma once

#if HELIO_TRACY_ENABLED
    #include <tracy/Tracy.hpp>

    #define HELIO_PROFILE_ZONE(Name) ZoneScopedN(Name)
    #define HELIO_PROFILE_FRAME() FrameMark
    #define HELIO_PROFILE_PLOT(Name, Value) TracyPlot(Name, Value)
    /// GPU zone is a stub for Phase 2. Replaced with TracyVkZone in Phase 4
    /// once a Vulkan TracyVkCtx exists.
    #define HELIO_PROFILE_GPU(Cmd, Name) ((void)(Cmd))
#else
    #define HELIO_PROFILE_ZONE(Name) ((void)0)
    #define HELIO_PROFILE_FRAME() ((void)0)
    #define HELIO_PROFILE_PLOT(Name, Value) ((void)0)
    #define HELIO_PROFILE_GPU(Cmd, Name) ((void)0)
#endif
