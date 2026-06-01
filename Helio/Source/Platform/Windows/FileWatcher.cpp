#include "FileWatcher.h"

#include <Core/Logging/Log.h>
#include <Core/Assert/Assert.h>

#include <Windows.h>

#include <array>
#include <vector>

namespace helio::platform::windows {

namespace {

constexpr DWORD kBufferBytes = 64 * 1024;
constexpr DWORD kNotifyFilter =
    FILE_NOTIFY_CHANGE_LAST_WRITE |
    FILE_NOTIFY_CHANGE_CREATION |
    FILE_NOTIFY_CHANGE_FILE_NAME |
    FILE_NOTIFY_CHANGE_SIZE;

std::wstring ToWide(const std::filesystem::path& P) { return P.wstring(); }

} // namespace

FileWatcher::FileWatcher(std::filesystem::path Directory, ChangedCallback OnChanged)
    : m_root(std::move(Directory))
    , m_callback(std::move(OnChanged)) {
    HELIO_CHECK(m_callback);

    if (!std::filesystem::exists(m_root)) {
        HELIO_LOG_WARN("Platform", "FileWatcher: directory does not exist: '{}'", m_root.string());
        return;
    }

    HANDLE Dir = ::CreateFileW(
        ToWide(m_root).c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (Dir == INVALID_HANDLE_VALUE) {
        HELIO_LOG_ERROR("Platform", "FileWatcher: CreateFileW failed for '{}' (error {})",
                        m_root.string(), ::GetLastError());
        return;
    }

    m_dirHandle = Dir;
    m_cancelEvent = ::CreateEventW(nullptr, /*manualReset=*/TRUE, FALSE, nullptr);
    HELIO_CHECK(m_cancelEvent);

    m_thread = std::thread([this] { Run(); });

    HELIO_LOG_INFO("Platform", "FileWatcher watching '{}'", m_root.string());
}

FileWatcher::~FileWatcher() {
    m_stop.store(true, std::memory_order_release);
    if (m_cancelEvent) {
        ::SetEvent(static_cast<HANDLE>(m_cancelEvent));
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_dirHandle) {
        ::CloseHandle(static_cast<HANDLE>(m_dirHandle));
        m_dirHandle = nullptr;
    }
    if (m_cancelEvent) {
        ::CloseHandle(static_cast<HANDLE>(m_cancelEvent));
        m_cancelEvent = nullptr;
    }
}

void FileWatcher::Run() {
    std::vector<std::byte> Buffer(kBufferBytes);

    while (!m_stop.load(std::memory_order_acquire)) {
        OVERLAPPED Ov{};
        Ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

        BOOL Ok = ::ReadDirectoryChangesW(
            static_cast<HANDLE>(m_dirHandle),
            Buffer.data(),
            static_cast<DWORD>(Buffer.size()),
            /*recursive=*/TRUE,
            kNotifyFilter,
            nullptr,
            &Ov,
            nullptr
        );

        if (!Ok) {
            HELIO_LOG_ERROR("Platform", "ReadDirectoryChangesW failed (error {})", ::GetLastError());
            ::CloseHandle(Ov.hEvent);
            break;
        }

        HANDLE WaitOn[2] = { Ov.hEvent, static_cast<HANDLE>(m_cancelEvent) };
        DWORD Result = ::WaitForMultipleObjects(2, WaitOn, FALSE, INFINITE);
        ::CloseHandle(Ov.hEvent);

        if (m_stop.load(std::memory_order_acquire) || Result != WAIT_OBJECT_0) {
            break;
        }

        DWORD Bytes = 0;
        if (!::GetOverlappedResult(static_cast<HANDLE>(m_dirHandle), &Ov, &Bytes, FALSE) || Bytes == 0) {
            continue;
        }

        // Walk the FILE_NOTIFY_INFORMATION linked list.
        std::byte* Cursor = Buffer.data();
        for (;;) {
            const auto* Info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(Cursor);
            const DWORD NameChars = Info->FileNameLength / sizeof(WCHAR);
            std::wstring NameW(Info->FileName, NameChars);
            std::filesystem::path Relative{NameW};
            try {
                m_callback(Relative);
            } catch (const std::exception& Ex) {
                HELIO_LOG_ERROR("Platform", "FileWatcher callback threw: {}", Ex.what());
            }
            if (Info->NextEntryOffset == 0) break;
            Cursor += Info->NextEntryOffset;
        }
    }
}

} // namespace helio::platform::windows
