/* Temporary geometry-only diagnostics for the native-controls experiment. GPLv3+. */
#pragma once
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace GeometryDiagnostics {
class Log final {
    HANDLE m_file = INVALID_HANDLE_VALUE;
    SRWLOCK m_lock = SRWLOCK_INIT;
    DWORD m_written = 0;
    struct Entry {
        HWND window = nullptr;
        char tag[64] = {};
        char text[1024] = {};
    } m_last[24];

public:
    Log() {
        wchar_t path[32768] = {};
        const DWORD length = ::GetModuleFileNameW(nullptr, path, _countof(path));
        if (!length || length >= _countof(path)) return;
        wchar_t* name = wcsrchr(path, L'\\');
        if (!name || _countof(path) - size_t(name + 1 - path) < _countof(L"MPC-HC-geometry.log")) return;
        wcscpy_s(name + 1, _countof(path) - size_t(name + 1 - path), L"MPC-HC-geometry.log");
        m_file = ::CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_file != INVALID_HANDLE_VALUE) {
            char header[256] = {};
            const int count = _snprintf_s(header, sizeof(header), _TRUNCATE,
                "# Native geometry diagnostics v1; build %s %s; pid=%lu\r\n"
                "# Rectangles: [left,top,right,bottom]; screen and client coordinates labelled.\r\n",
                __DATE__, __TIME__, ::GetCurrentProcessId());
            DWORD written = 0;
            if (count > 0) ::WriteFile(m_file, header, DWORD(count), &written, nullptr);
        }
    }
    ~Log() { if (m_file != INVALID_HANDLE_VALUE) ::CloseHandle(m_file); }

    void Record(const char* tag, HWND window, const RECT* source, const RECT* destination, HRESULT result) {
        if (m_file == INVALID_HANDLE_VALUE || !window) return;
        RECT client = {}, screen = {}, outer = {};
        const BOOL valid = ::GetClientRect(window, &client);
        ::GetWindowRect(window, &outer);
        POINT origin = {0, 0};
        ::ClientToScreen(window, &origin);
        screen = client;
        ::OffsetRect(&screen, origin.x, origin.y);
        const RECT zero = {};
        const RECT& src = source ? *source : zero;
        const RECT& dst = destination ? *destination : zero;
        const bool outside = destination && valid && (dst.left < client.left || dst.top < client.top
            || dst.right > client.right || dst.bottom > client.bottom);
        char state[1024] = {};
        _snprintf_s(state, sizeof(state), _TRUNCATE,
            "%s hwnd=%p parent=%p visible=%d style=%llx ex=%llx valid=%d "
            "outerScreen=[%ld,%ld,%ld,%ld] client=[%ld,%ld,%ld,%ld] "
            "clientScreen=[%ld,%ld,%ld,%ld] hasSrc=%d src=[%ld,%ld,%ld,%ld] "
            "hasDst=%d dst=[%ld,%ld,%ld,%ld] destOutsideClient=%d hr=%08lx",
            tag, static_cast<void*>(window), static_cast<void*>(::GetParent(window)), int(::IsWindowVisible(window)),
            static_cast<unsigned long long>(::GetWindowLongPtrW(window, GWL_STYLE)),
            static_cast<unsigned long long>(::GetWindowLongPtrW(window, GWL_EXSTYLE)), int(valid),
            outer.left, outer.top, outer.right, outer.bottom, client.left, client.top, client.right, client.bottom,
            screen.left, screen.top, screen.right, screen.bottom, int(source != nullptr), src.left, src.top, src.right, src.bottom,
            int(destination != nullptr), dst.left, dst.top, dst.right, dst.bottom, int(outside), static_cast<unsigned long>(result));

        // Never wait for a logger lock while the renderer may hold its own lock.
        if (!::TryAcquireSRWLockExclusive(&m_lock)) return;
        Entry* entry = nullptr;
        for (auto& candidate : m_last) {
            if (candidate.window == window && strcmp(candidate.tag, tag) == 0) { entry = &candidate; break; }
            if (!entry && !candidate.window) entry = &candidate;
        }
        if (!entry) entry = &m_last[0];
        if (entry->window != window || strcmp(entry->tag, tag) || strcmp(entry->text, state)) {
            entry->window = window;
            strcpy_s(entry->tag, tag);
            strcpy_s(entry->text, state);
            if (m_written < 4 * 1024 * 1024) {
                char line[1152] = {};
                const int count = _snprintf_s(line, sizeof(line), _TRUNCATE,
                    "tick=%llu tid=%lu %s\r\n", ::GetTickCount64(), ::GetCurrentThreadId(), state);
                DWORD written = 0;
                if (count > 0) ::WriteFile(m_file, line, DWORD(count), &written, nullptr);
                m_written += written;
            }
        }
        ::ReleaseSRWLockExclusive(&m_lock);
    }
};

inline void Record(const char* tag, HWND window, const RECT* source = nullptr,
    const RECT* destination = nullptr, HRESULT result = S_OK) {
    static Log log;
    log.Record(tag, window, source, destination, result);
}
}
#endif
