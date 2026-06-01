/// @file Assert.h
/// @brief Assertion macros for debug + release builds.
///
/// Three flavors:
/// - HELIO_ASSERT: only active in Debug (or when HELIO_ASSERTS_ENABLED=1). Use
///   for invariants you want to catch in development but trust in release.
/// - HELIO_CHECK: ALWAYS active, even in shipping. Use for invariants whose
///   violation should crash rather than corrupt.
/// - HELIO_VERIFY: like HELIO_ASSERT but the condition is still evaluated in
///   release (return value still computed).
#pragma once

#include <Core/Logging/Log.h>

#ifdef _MSC_VER
    #define HELIO_DEBUGBREAK() __debugbreak()
#else
    #include <cstdlib>
    #define HELIO_DEBUGBREAK() std::abort()
#endif

#if !defined(HELIO_ASSERTS_ENABLED)
    #if defined(_DEBUG) || !defined(NDEBUG)
        #define HELIO_ASSERTS_ENABLED 1
    #else
        #define HELIO_ASSERTS_ENABLED 0
    #endif
#endif

#define HELIO_CHECK(Cond)                                                                    \
    do {                                                                                     \
        if (!(Cond)) {                                                                       \
            ::helio::log::Category("Assert").critical(                                       \
                "CHECK failed: " #Cond " ({}:{})", __FILE__, __LINE__);                      \
            HELIO_DEBUGBREAK();                                                              \
        }                                                                                    \
    } while (0)

#if HELIO_ASSERTS_ENABLED
    #define HELIO_ASSERT(Cond)                                                               \
        do {                                                                                 \
            if (!(Cond)) {                                                                   \
                ::helio::log::Category("Assert").critical(                                   \
                    "ASSERT failed: " #Cond " ({}:{})", __FILE__, __LINE__);                 \
                HELIO_DEBUGBREAK();                                                          \
            }                                                                                \
        } while (0)

    #define HELIO_VERIFY(Cond) HELIO_ASSERT(Cond)
#else
    #define HELIO_ASSERT(Cond) ((void)0)
    #define HELIO_VERIFY(Cond) ((void)(Cond))
#endif
