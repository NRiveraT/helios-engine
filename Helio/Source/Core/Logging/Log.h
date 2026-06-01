/// @file Log.h
/// @brief Logging facade over spdlog. Category-based, console + file sinks.
///
/// Usage:
/// @code
///     helio::log::Init();
///     HELIO_LOG_INFO("Game", "Hello {}", 42);
/// @endcode
///
/// Categories cluster logs by subsystem. Common ones: "Engine", "RHI",
/// "Renderer", "Input", "Game", "Assert". You can use any string.
#pragma once

#include <spdlog/spdlog.h>
#include <string_view>

namespace helio::log {

/// Initialize the logging system. Creates console + rotating file sink at
/// `<LogPath>` (relative to current working directory). Call once at startup
/// before any HELIO_LOG_* macro.
///
/// Default log path is `Saved/Logs/Game.log` relative to cwd, matching
/// `game/CMakeLists.txt` which sets the debugger working directory to the
/// binary dir.
void Init(std::string_view LogPath = "Saved/Logs/Game.log");

/// Flush sinks and tear down. Optional but recommended before shutdown so
/// the final lines persist.
void Shutdown();

/// Get (or lazily create) a category logger. Categories are cached.
[[nodiscard]] spdlog::logger& Category(std::string_view Name);

} // namespace helio::log

#define HELIO_LOG_TRACE(Cat, ...)    ::helio::log::Category(Cat).trace(__VA_ARGS__)
#define HELIO_LOG_DEBUG(Cat, ...)    ::helio::log::Category(Cat).debug(__VA_ARGS__)
#define HELIO_LOG_INFO(Cat, ...)     ::helio::log::Category(Cat).info(__VA_ARGS__)
#define HELIO_LOG_WARN(Cat, ...)     ::helio::log::Category(Cat).warn(__VA_ARGS__)
#define HELIO_LOG_ERROR(Cat, ...)    ::helio::log::Category(Cat).error(__VA_ARGS__)
#define HELIO_LOG_CRITICAL(Cat, ...) ::helio::log::Category(Cat).critical(__VA_ARGS__)
