/* Temporary in-process UI state diagnostics. No layout or drawing changes. GPLv3+. */
#pragma once
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <map>
#include <string>

namespace NativeUiDiagnostics {
class Log final {
    HANDLE m_file = INVALID_HANDLE_VALUE;
    SRWLOCK m_lock = SRWLOCK_INIT;
    std::map<std::pair<UINT_PTR, std::string>, std::string> m_last;
    DWORD m_written = 0;
public:
    Log() {
        wchar_t path[32768] = {};
        const DWORD length = GetModuleFileNameW(nullptr, path, _countof(path));
        if (!length || length >= _countof(path)) return;
        wchar_t* name = wcsrchr(path, L'\\');
        if (!name || _countof(path) - size_t(name + 1 - path) < _countof(L"MPC-HC-ui-state.log")) return;
        wcscpy_s(name + 1, _countof(path) - size_t(name + 1 - path), L"MPC-HC-ui-state.log");
        m_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_file != INVALID_HANDLE_VALUE) {
            char header[256] = {};
            const int count = _snprintf_s(header, sizeof(header), _TRUNCATE,
                "# UI state diagnostics v2; build %s %s; pid=%lu\r\n# No media names or URLs recorded.\r\n",
                __DATE__, __TIME__, GetCurrentProcessId());
            DWORD written = 0;
            if (count > 0) WriteFile(m_file, header, DWORD(count), &written, nullptr);
        }
    }
    ~Log() { if (m_file != INVALID_HANDLE_VALUE) CloseHandle(m_file); }
    void Write(HWND window, const char* tag, const char* state) {
        if (m_file == INVALID_HANDLE_VALUE || !TryAcquireSRWLockExclusive(&m_lock)) return;
        auto& previous = m_last[{reinterpret_cast<UINT_PTR>(window), tag}];
        if (previous != state && m_written < 8 * 1024 * 1024) {
            previous = state;
            char line[2048] = {};
            const int count = _snprintf_s(line, sizeof(line), _TRUNCATE,
                "tick=%llu tid=%lu hwnd=%p %s %s\r\n", GetTickCount64(), GetCurrentThreadId(),
                static_cast<void*>(window), tag, state);
            DWORD written = 0;
            if (count > 0) WriteFile(m_file, line, DWORD(count), &written, nullptr);
            m_written += written;
        }
        ReleaseSRWLockExclusive(&m_lock);
    }
};
inline void Write(HWND window, const char* tag, const char* state) {
    static Log log;
    log.Write(window, tag, state);
}
inline void Toolbar(CToolBar& bar, const char* phase) {
    auto& control = bar.GetToolBarCtrl();
    CRect client, screen;
    bar.GetClientRect(client); bar.GetWindowRect(screen);
    char state[512] = {}, tag[128] = {};
    _snprintf_s(state, sizeof(state), _TRUNCATE,
        "parent=%p client=[%ld,%ld,%ld,%ld] screen=[%ld,%ld,%ld,%ld] visible=%d count=%d",
        static_cast<void*>(::GetParent(bar.m_hWnd)), client.left, client.top, client.right, client.bottom,
        screen.left, screen.top, screen.right, screen.bottom, int(bar.IsWindowVisible()), control.GetButtonCount());
    _snprintf_s(tag, sizeof(tag), _TRUNCATE, "toolbar.%s", phase);
    Write(bar.m_hWnd, tag, state);
    for (int i = 0; i < control.GetButtonCount(); ++i) {
        TBBUTTON button = {}; CRect rect(0,0,0,0);
        if (!control.GetButton(i, &button)) continue;
        control.GetItemRect(i, rect); // raw common-control getter: no MFC layout commit
        TBBUTTONINFO info = {sizeof(info)}; info.dwMask = TBIF_SIZE;
        control.GetButtonInfo(button.idCommand, &info);
        _snprintf_s(state, sizeof(state), _TRUNCATE,
            "index=%d id=%d state=%02x style=%02x imageOrSeparatorWidth=%d explicitWidth=%u rect=[%ld,%ld,%ld,%ld]",
            i, button.idCommand, unsigned(button.fsState), unsigned(button.fsStyle), button.iBitmap,
            unsigned(info.cx), rect.left, rect.top, rect.right, rect.bottom);
        _snprintf_s(tag, sizeof(tag), _TRUNCATE, "toolbar.%s.item.%d", phase, i);
        Write(bar.m_hWnd, tag, state);
    }
}
inline void Label(HWND window, HDC dc, bool right, const RECT& item, const char* phase) {
    RECT clip = {}, bounds = {}; POINT origin = {};
    const int clipType = GetClipBox(dc, &clip);
    GetWindowRect(window, &bounds); GetDCOrgEx(dc, &origin);
    char state[1024] = {}, tag[128] = {};
    _snprintf_s(state, sizeof(state), _TRUNCATE,
        "rightAligned=%d textLength=%d parent=%p visible=%d dc=%p font=%p clipType=%d clip=[%ld,%ld,%ld,%ld] item=[%ld,%ld,%ld,%ld] screen=[%ld,%ld,%ld,%ld] dcOrigin=[%ld,%ld]",
        int(right), GetWindowTextLengthW(window), static_cast<void*>(GetParent(window)), int(IsWindowVisible(window)),
        static_cast<void*>(dc), static_cast<void*>(GetCurrentObject(dc, OBJ_FONT)), clipType,
        clip.left, clip.top, clip.right, clip.bottom, item.left, item.top, item.right, item.bottom,
        bounds.left, bounds.top, bounds.right, bounds.bottom, origin.x, origin.y);
    _snprintf_s(tag, sizeof(tag), _TRUNCATE, "label.%s", phase);
    Write(window, tag, state);
}
}
#endif
