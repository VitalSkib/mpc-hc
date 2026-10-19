/* Whole-surface native chrome motion; GPLv3+. */
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <dwmapi.h>

struct NativeChromeImage {
    CRect rect = CRect(0, 0, 0, 0); // screen coordinates
    std::vector<DWORD> pixels;
    bool Valid() const {
        return rect.Width() > 0 && rect.Height() > 0
            && pixels.size() == static_cast<size_t>(rect.Width()) * rect.Height();
    }
    void Clear() { pixels.clear(); rect.SetRectEmpty(); }
    void Blend(const CRect& sourceRect, const CSize& size, const std::vector<DWORD>& source) {
        if (!Valid() || size.cx <= 0 || size.cy <= 0
                || source.size() != static_cast<size_t>(size.cx) * size.cy) return;
        for (int y = 0; y < size.cy; ++y) {
            const int dy = sourceRect.top - rect.top + y;
            if (dy < 0 || dy >= rect.Height()) continue;
            for (int x = 0; x < size.cx; ++x) {
                const int dx = sourceRect.left - rect.left + x;
                if (dx < 0 || dx >= rect.Width()) continue;
                const DWORD s = source[static_cast<size_t>(y) * size.cx + x];
                const unsigned a = s >> 24, inverse = 255 - a;
                if (!a) continue;
                auto& d = pixels[static_cast<size_t>(dy) * rect.Width() + dx];
                const auto channel = [inverse](DWORD foreground, DWORD background) {
                    return std::min<DWORD>(255, foreground + (background * inverse + 127) / 255);
                };
                d = (channel(a, d >> 24) << 24)
                    | (channel((s >> 16) & 255, (d >> 16) & 255) << 16)
                    | (channel((s >> 8) & 255, (d >> 8) & 255) << 8)
                    | channel(s & 255, d & 255);
            }
        }
    }
    static bool PrintToolbar(CWnd& window, COLORREF fallbackKey, std::vector<DWORD>& pixels, CSize& size) {
        CRect client; window.GetClientRect(client);
        size = client.Size();
        if (size.cx <= 0 || size.cy <= 0) return false;
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = size.cx; info.bmiHeader.biHeight = -size.cy;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        HDC dc = ::CreateCompatibleDC(nullptr); DWORD* bits = nullptr;
        HBITMAP bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS,
            reinterpret_cast<void**>(&bits), nullptr, 0);
        if (!dc || !bitmap || !bits) {
            if (bitmap) ::DeleteObject(bitmap);
            if (dc) ::DeleteDC(dc);
            return false;
        }
        HGDIOBJ old = ::SelectObject(dc, bitmap);
        COLORREF key = fallbackKey; BYTE opacity = 255; DWORD flags = 0;
        ::GetLayeredWindowAttributes(window.m_hWnd, &key, &opacity, &flags);
        if (!(flags & LWA_COLORKEY)) key = fallbackKey;
        HBRUSH background = ::CreateSolidBrush(key);
        ::FillRect(dc, client, background); ::DeleteObject(background);
        window.SendMessage(WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
        ::GdiFlush();
        const DWORD rgbKey = (GetRValue(key) << 16) | (GetGValue(key) << 8) | GetBValue(key);
        pixels.resize(static_cast<size_t>(size.cx) * size.cy);
        bool artwork = false;
        for (size_t i = 0; i < pixels.size(); ++i) {
            const DWORD rgb = bits[i] & 0xffffff;
            pixels[i] = rgb == rgbKey ? 0 : rgb | 0xff000000;
            artwork = artwork || pixels[i] != 0;
        }
        ::SelectObject(dc, old); ::DeleteObject(bitmap); ::DeleteDC(dc);
        return artwork;
    }
};

class CNativeChromeSlide final : public CWnd {
    struct Mask { HWND window; HRGN original; };
    std::vector<Mask> m_masks;
    NativeChromeImage m_image;
    HDC m_dc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HGDIOBJ m_old = nullptr;
    DWORD* m_bits = nullptr;
    ULONGLONG m_start = 0;
    bool m_entering = false, m_top = false, m_running = false, m_finishing = false;
    bool Draw(double progress) {
        const int width = m_image.rect.Width(), height = m_image.rect.Height();
        const double position = m_entering ? 1.0 - std::pow(1.0 - progress, 3.0)
            : 1.0 - progress * progress * progress;
        const int offset = static_cast<int>((1.0 - position) * height) * (m_top ? -1 : 1);
        ::ZeroMemory(m_bits, static_cast<size_t>(width) * height * sizeof(DWORD));
        for (int y = 0; y < height; ++y) {
            const int sourceY = y - offset;
            if (sourceY >= 0 && sourceY < height)
                memcpy(m_bits + static_cast<size_t>(y) * width,
                    m_image.pixels.data() + static_cast<size_t>(sourceY) * width, width * sizeof(DWORD));
        }
        POINT destination = m_image.rect.TopLeft(), source{};
        SIZE size = {width, height}; BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        return !!::UpdateLayeredWindow(m_hWnd, nullptr, &destination, &size, m_dc, &source, 0, &blend, ULW_ALPHA);
    }
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        if (message == WM_ERASEBKGND) return TRUE;
        if (message == WM_PAINT) { CPaintDC dc(this); return 0; }
        if (message == WM_NCHITTEST) return HTTRANSPARENT;
        if (message == WM_TIMER && wParam == 1) {
            if (!m_running) return 0;
            if (!m_start) m_start = GetTickCount64();
            const double progress = std::min(1.0, (GetTickCount64() - m_start) / 160.0);
            if (!Draw(progress) || progress >= 1.0) Finish();
            return 0;
        }
        return __super::WindowProc(message, wParam, lParam);
    }
public:
    ~CNativeChromeSlide() override { Finish(); }
    bool Running() const { return m_running; }
    void Finish() {
        if (m_finishing) return;
        m_finishing = true; m_running = false;
        if (GetSafeHwnd()) KillTimer(1);
        for (auto& mask : m_masks) {
            if (::IsWindow(mask.window)) {
                if (::SetWindowRgn(mask.window, mask.original, FALSE)) mask.original = nullptr;
                ::RedrawWindow(mask.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
            }
            if (mask.original) ::DeleteObject(mask.original);
        }
        m_masks.clear();
        // Do not flush a frame containing both translucent copies: that would
        // deliberately present a dark double-background frame at handoff.
        if (GetSafeHwnd()) DestroyWindow();
        if (m_dc && m_old) ::SelectObject(m_dc, m_old);
        if (m_bitmap) ::DeleteObject(m_bitmap);
        if (m_dc) ::DeleteDC(m_dc);
        m_bitmap = nullptr; m_dc = nullptr; m_old = nullptr; m_bits = nullptr;
        m_image.Clear(); m_finishing = false;
    }
    bool Begin(CWnd* owner, NativeChromeImage image, bool entering, bool top,
            const std::vector<HWND>& liveWindows, bool retainMasks = false) {
        std::vector<Mask> prepared;
        if (retainMasks) prepared.swap(m_masks);
        Finish();
        m_masks.swap(prepared);
        if (!image.Valid()) { Finish(); return false; }
        m_image = std::move(image); m_entering = entering; m_top = top;
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = m_image.rect.Width(); info.bmiHeader.biHeight = -m_image.rect.Height();
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        m_dc = ::CreateCompatibleDC(nullptr);
        m_bitmap = ::CreateDIBSection(m_dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&m_bits), nullptr, 0);
        if (!m_dc || !m_bitmap || !m_bits) { Finish(); return false; }
        m_old = ::SelectObject(m_dc, m_bitmap);
        if (!CreateEx(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                AfxRegisterWndClass(0), L"Native chrome motion", WS_POPUP, m_image.rect, owner, 0)) {
            Finish(); return false;
        }
        if (!Draw(0)) { Finish(); return false; }
        if (!MaskWindows(liveWindows)) { Finish(); return false; }
        m_running = true; m_start = 0;
        SetWindowPos(&wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (!SetTimer(1, 15, nullptr)) { Finish(); return false; }
        return true;
    }
    bool MaskWindows(const std::vector<HWND>& liveWindows) {
        for (HWND window : liveWindows) {
            if (!::IsWindow(window)) continue;
            if (std::any_of(m_masks.begin(), m_masks.end(), [window](const Mask& mask) { return mask.window == window; })) continue;
            HRGN original = ::CreateRectRgn(0, 0, 0, 0);
            if (!original) { Finish(); return false; }
            if (::GetWindowRgn(window, original) == ERROR) { ::DeleteObject(original); original = nullptr; }
            HRGN empty = ::CreateRectRgn(0, 0, 0, 0);
            if (!empty) { if (original) ::DeleteObject(original); Finish(); return false; }
            if (!::SetWindowRgn(window, empty, FALSE)) {
                ::DeleteObject(empty); if (original) ::DeleteObject(original);
                Finish(); return false;
            }
            m_masks.push_back({window, original});
        }
        return true;
    }
};
