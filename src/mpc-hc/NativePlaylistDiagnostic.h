#pragma once
// Temporary, opt-in playlist surface diagnostics. No rendering/input changes.
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
namespace PlaylistDiagnostic {
struct Session {
    std::wstring directory;
    std::vector<std::string> pending;
    unsigned captures = 0;
    bool enabled = false;
    Session() {
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;
        wchar_t* slash = wcsrchr(path, L'\\');
        if (!slash) return;
        slash[1] = 0;
        const std::wstring base(path);
        enabled = GetFileAttributesW((base + L"playlist-diagnostics.enabled").c_str()) != INVALID_FILE_ATTRIBUTES;
        if (!enabled) return;
        directory = base + L"playlist-diagnostics";
        CreateDirectoryW(directory.c_str(), nullptr);
        pending.reserve(2048);
    }
};
inline Session& Current() { static Session session; return session; }
inline void Trace(const char* stage, HWND window, HDC dc = nullptr, UINT message = 0, ULONG_PTR value = 0) {
    auto& s = Current();
    if (!s.enabled || s.pending.size() >= 12000) return;
    char line[512]{};
    RECT rect{}; if (window) GetWindowRect(window, &rect);
    sprintf_s(line, "%llu %s hwnd=%p parent=%p style=%08llX ex=%08llX visible=%d rect=%ld,%ld,%ld,%ld dc=%p dcType=%lu dcWindow=%p print=%p message=%04X value=%llu\r\n",
        GetTickCount64(), stage, window, window ? GetParent(window) : nullptr,
        window ? static_cast<unsigned long long>(GetWindowLongPtrW(window, GWL_STYLE)) : 0,
        window ? static_cast<unsigned long long>(GetWindowLongPtrW(window, GWL_EXSTYLE)) : 0,
        window ? IsWindowVisible(window) : 0, rect.left, rect.top, rect.right, rect.bottom, dc,
        dc ? GetObjectType(dc) : 0, dc ? WindowFromDC(dc) : nullptr,
        window ? GetPropW(window, L"MPC.PlaylistForegroundPrint") : nullptr,
        message, static_cast<unsigned long long>(value));
    s.pending.emplace_back(line);
}
inline void Flush() {
    auto& s = Current();
    if (!s.enabled || s.pending.empty()) return;
    HANDLE f = CreateFileW((s.directory + L"events.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    for (const auto& line : s.pending) { DWORD written; WriteFile(f, line.data(), static_cast<DWORD>(line.size()), &written, nullptr); }
    CloseHandle(f); s.pending.clear();
}
inline BOOL CALLBACK Child(HWND window, LPARAM) { Trace("child-at-fade", window); return TRUE; }
inline void SaveForeground(const DWORD* pixels, int width, int height) {
    auto& s = Current();
    if (!s.enabled || !pixels || width <= 0 || height <= 0 || s.captures >= 12) return;
    ++s.captures;
    wchar_t name[80]{}; swprintf_s(name, L"\\foreground-%lu-%02u.bmp", GetCurrentProcessId(), s.captures);
    HANDLE f = CreateFileW((s.directory + name).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
    const DWORD bytes = static_cast<DWORD>(width) * static_cast<DWORD>(height) * 4;
    file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(info); file.bfSize = file.bfOffBits + bytes;
    info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -height; info.biPlanes = 1; info.biBitCount = 32; info.biSizeImage = bytes;
    DWORD written;
    WriteFile(f, &file, sizeof(file), &written, nullptr);
    WriteFile(f, &info, sizeof(info), &written, nullptr);
    WriteFile(f, pixels, bytes, &written, nullptr);
    CloseHandle(f);
    Trace("foreground-file-saved", nullptr, nullptr, 0, s.captures);
}
}
#endif
