#include "Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace helio::log {

namespace {

std::mutex g_mutex;
std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> g_loggers;
std::vector<spdlog::sink_ptr> g_sinks;
bool g_initialized = false;

std::shared_ptr<spdlog::logger> MakeLogger(std::string_view Name) {
    auto LoggerInst = std::make_shared<spdlog::logger>(std::string(Name), g_sinks.begin(), g_sinks.end());
    LoggerInst->set_level(spdlog::level::trace);
    LoggerInst->flush_on(spdlog::level::warn);
    return LoggerInst;
}

} // namespace

void Init(std::string_view LogPath) {
    std::lock_guard Lock(g_mutex);
    if (g_initialized) return;

    auto Console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    Console->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
    g_sinks.push_back(Console);

    // Best-effort file sink — if creating the dir fails, we keep going with console only.
    try {
        std::filesystem::path Path{LogPath};
        if (Path.has_parent_path()) {
            std::filesystem::create_directories(Path.parent_path());
        }
        auto File = std::make_shared<spdlog::sinks::basic_file_sink_mt>(Path.string(), /*truncate=*/false);
        File->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        g_sinks.push_back(File);
    } catch (const std::exception& Ex) {
        // Build a temporary logger over the console sink to report the failure.
        spdlog::logger Bootstrap("Helio.Logging", Console);
        Bootstrap.set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
        Bootstrap.warn("file sink disabled, could not open log path '{}': {}", LogPath, Ex.what());
    }

    g_initialized = true;
}

void Shutdown() {
    std::lock_guard Lock(g_mutex);
    for (auto& [_, LoggerInst] : g_loggers) {
        LoggerInst->flush();
    }
    g_loggers.clear();
    g_sinks.clear();
    g_initialized = false;
}

spdlog::logger& Category(std::string_view Name) {
    std::lock_guard Lock(g_mutex);
    std::string Key{Name};
    auto It = g_loggers.find(Key);
    if (It != g_loggers.end()) {
        return *It->second;
    }
    auto LoggerInst = MakeLogger(Name);
    auto [Inserted, _] = g_loggers.emplace(std::move(Key), std::move(LoggerInst));
    return *Inserted->second;
}

} // namespace helio::log
