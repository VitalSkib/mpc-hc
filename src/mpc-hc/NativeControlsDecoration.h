/* Native-controls decoration only; no playback controls or hide timers. GPLv3+. */
#pragma once
#include <gdiplus.h>
#include "CMPCThemeToolTipCtrl.h"
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
#include "../DSUtil/GeometryDiagnostics.h"
#endif

class CNativeControlsDecoration final : public CWnd
{
    ULONG_PTR m_graphics = 0;
    bool m_header = false, m_hot = false, m_pressed = false;
    CString m_title;
    int m_scale = 96;
    CRect m_close;
    HRESULT m_lastDraw = S_OK;
    CMPCThemeToolTipCtrl m_tip;

    int Scale(int value) const { return MulDiv(value, m_scale, 96); }
    bool DrawSurface() {
        CRect bounds; GetWindowRect(bounds);
        const int width = bounds.Width(), height = bounds.Height();
        if (!m_graphics || width <= 0 || height <= 0) return false;
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP bitmap = ::CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap || !pixels) { if (bitmap) ::DeleteObject(bitmap); return false; }
        HDC memory = ::CreateCompatibleDC(nullptr);
        if (!memory) { ::DeleteObject(bitmap); return false; }
        const HGDIOBJ previous = ::SelectObject(memory, bitmap);
        {
            Gdiplus::Bitmap surface(width, height, width * 4, PixelFormat32bppPARGB,
                static_cast<BYTE*>(pixels));
            Gdiplus::Graphics graphics(&surface);
            graphics.Clear(Gdiplus::Color(128, 0, 0, 0));
            if (m_header) {
                graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
                Gdiplus::Font font(L"Segoe UI", float(Scale(14)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush text(Gdiplus::Color(240, 255, 255, 255));
                Gdiplus::StringFormat format;
                format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                const Gdiplus::RectF title(float(Scale(16)), 0,
                    float(std::max<LONG>(0, m_close.left - Scale(24))), float(height));
                graphics.DrawString(m_title, -1, &font, title, &format, &text);
                // Flat close glyph, without an individual background or bevel.
                Gdiplus::Pen pen(Gdiplus::Color(m_hot ? 255 : 210, 255, 255, 255),
                    float(std::max(1, Scale(1))));
                const float x = float(m_close.CenterPoint().x), y = float(m_close.CenterPoint().y);
                const float radius = float(Scale(6));
                graphics.DrawLine(&pen, x - radius, y - radius, x + radius, y + radius);
                graphics.DrawLine(&pen, x + radius, y - radius, x - radius, y + radius);
            }
        }
        POINT source = {};
        SIZE size = {width, height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        // Layout alone owns placement. A surface repaint must not relocate
        // this layered child, including when its parent is away from (0, 0).
        const bool success = !!::UpdateLayeredWindow(m_hWnd, nullptr, nullptr, &size,
            memory, &source, 0, &blend, ULW_ALPHA);
        m_lastDraw = success ? S_OK : HRESULT_FROM_WIN32(::GetLastError());
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
        GeometryDiagnostics::Record(m_header ? "decor.header-draw" : "decor.backdrop-draw",
            m_hWnd, nullptr, nullptr, m_lastDraw);
#endif
        ::SelectObject(memory, previous); ::DeleteDC(memory); ::DeleteObject(bitmap);
        return success;
    }

    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        if (message == WM_ERASEBKGND) return TRUE;
        if (message == WM_PAINT) { CPaintDC dc(this); DrawSurface(); return 0; }
        if (message == WM_NCHITTEST) {
            CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); ScreenToClient(&point);
            return m_header && m_close.PtInRect(point) ? HTCLIENT : HTTRANSPARENT;
        }
        if (message == WM_MOUSEMOVE && m_header) {
            CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            const bool hot = !!m_close.PtInRect(point);
            if (hot != m_hot) { m_hot = hot; DrawSurface(); }
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hWnd, 0};
            ::TrackMouseEvent(&track);
            return 0;
        }
        if (message == WM_MOUSELEAVE) {
            if (m_hot) { m_hot = false; DrawSurface(); }
            return 0;
        }
        if (message == WM_LBUTTONDOWN && m_header) {
            CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            m_pressed = !!m_close.PtInRect(point);
            if (m_pressed) SetCapture();
            return 0;
        }
        if (message == WM_LBUTTONUP && m_header) {
            CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            const bool close = m_pressed && m_close.PtInRect(point);
            m_pressed = false;
            if (::GetCapture() == m_hWnd) ::ReleaseCapture();
            if (close) ::PostMessage(::GetAncestor(m_hWnd, GA_ROOT), WM_CLOSE, 0, 0);
            return 0;
        }
        if (message == WM_CAPTURECHANGED) m_pressed = false;
        return __super::WindowProc(message, wParam, lParam);
    }
    BOOL PreTranslateMessage(MSG* message) override {
        if (m_tip.GetSafeHwnd()) m_tip.RelayEvent(message);
        return __super::PreTranslateMessage(message);
    }

public:
    CNativeControlsDecoration() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_graphics, &input, nullptr);
    }
    ~CNativeControlsDecoration() override {
        if (GetSafeHwnd()) DestroyWindow();
        if (m_graphics) Gdiplus::GdiplusShutdown(m_graphics);
    }
    void Hide() {
        m_hot = m_pressed = false;
        if (::GetCapture() == GetSafeHwnd() && GetSafeHwnd()) ::ReleaseCapture();
        if (m_tip.GetSafeHwnd()) m_tip.Pop();
        if (GetSafeHwnd() && (GetStyle() & WS_VISIBLE)) {
            ShowWindow(SW_HIDE);
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
            GeometryDiagnostics::Record(m_header ? "decor.header-hide" : "decor.backdrop-hide", m_hWnd);
#endif
        }
    }
    void Refresh(CWnd* view, const CRect& rectangle, bool header, const CString& title, int dpi) {
        if (rectangle.IsRectEmpty()) { Hide(); return; }
        if (!GetSafeHwnd()) {
            m_header = header;
            const DWORD extended = WS_EX_LAYERED | WS_EX_NOACTIVATE | (header ? 0 : WS_EX_TRANSPARENT);
            if (!CreateEx(extended, AfxRegisterWndClass(0), L"Native control decoration",
                    WS_CHILD, rectangle, view, 0)) return;
            if (header && m_tip.Create(this, TTS_ALWAYSTIP)) {
                m_tip.AddTool(this, L"Close", CRect(0, 0, 1, 1), 1);
            }
        }
        CRect current; GetWindowRect(current); view->ScreenToClient(current);
        const bool changed = current != rectangle || m_title != title || m_scale != dpi || !(GetStyle() & WS_VISIBLE);
        if (!changed) return;
        m_title = title; m_scale = dpi;
        m_close.SetRect(std::max(0, rectangle.Width() - Scale(40)), 0, rectangle.Width(), rectangle.Height());
        if (m_tip.GetSafeHwnd()) m_tip.SetToolRect(this, 1, m_close);
        SetWindowPos(&wndTop, rectangle.left, rectangle.top, rectangle.Width(), rectangle.Height(), SWP_NOACTIVATE);
        if (DrawSurface()) ShowWindow(SW_SHOWNOACTIVATE);
        else Hide(); // leave native controls usable if decoration cannot be rendered
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
        const RECT expected = rectangle;
        GeometryDiagnostics::Record(m_header ? "decor.header-layout" : "decor.backdrop-layout",
            m_hWnd, &expected, nullptr, m_lastDraw);
#endif
    }
};
