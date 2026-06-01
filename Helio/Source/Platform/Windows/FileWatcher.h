/// @file FileWatcher.h
/// @brief Recursive directory watcher using Win32 ReadDirectoryChangesW.
///
/// Spawns a worker thread that blocks on ReadDirectoryChangesW and dispatches
/// each filename change to a user-provided callback. The callback runs on the
/// worker thread — push work to your own queue before touching shared state.
///
/// Used by Phase 13 shader hot-reload (watches `Helio/Shaders/`).
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <thread>

namespace helio::platform::windows {

class FileWatcher {
public:
    using ChangedCallback = std::function<void(const std::filesystem::path& RelativePath)>;

    /// Watch `Directory` recursively for file modifications. `OnChanged` is
    /// invoked for each changed/created/renamed file with the path relative
    /// to `Directory`.
    FileWatcher(std::filesystem::path Directory, ChangedCallback OnChanged);
    ~FileWatcher();
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;
    FileWatcher(FileWatcher&&) = delete;
    FileWatcher& operator=(FileWatcher&&) = delete;

private:
    void Run();

    std::filesystem::path m_root;
    ChangedCallback m_callback;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    void* m_dirHandle{nullptr};
    void* m_cancelEvent{nullptr};
};

} // namespace helio::platform::windows
