/* Native-controls decoration only; no playback controls or hide timers. GPLv3+. */
#pragma once
#include <gdiplus.h>
#include <functional>
#include "CMPCThemeToolTipCtrl.h"
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
#include "../DSUtil/GeometryDiagnostics.h"
#endif

class CNativeControlsDecoration final : public CWnd
{
    ULONG_PTR m_graphics = 0;
    bool m_header = false, m_restore = false;
    int m_hot = -1, m_pressed = -1;
    CString m_title;
    int m_scale = 96;
    CRect m_buttons[3] = {CRect(0, 0, 0, 0), CRect(0, 0, 0, 0), CRect(0, 0, 0, 0)};
    HRESULT m_lastDraw = S_OK;
    CMPCThemeToolTipCtrl m_tip;

    int Scale(int value) const { return MulDiv(value, m_scale, 96); }
    int HitButton(CPoint point) const {
        if (m_header) for (int i = 0; i < 3; ++i) if (m_buttons[i].PtInRect(point)) return i;
        return -1;
    }
    // Artwork is independent of hit testing and native window commands, so it
    // can later be replaced by custom resources without changing behaviour.
    void DrawWindowButton(Gdiplus::Graphics& graphics, int button) {
        const CRect& rect = m_buttons[button];
        const bool hot = m_hot == button;
        if (hot) {
            Gdiplus::SolidBrush background(Gdiplus::Color(m_pressed == button ? 60 : 40, 255, 255, 255));
            graphics.FillRectangle(&background, rect.left, rect.top, rect.Width(), rect.Height());
        }
        Gdiplus::Pen pen(Gdiplus::Color(hot ? 255 : 210, 255, 255, 255), float(std::max(1, Scale(1))));
        const float x = float(rect.CenterPoint().x), y = float(rect.CenterPoint().y);
        const float radius = float(Scale(6));
        if (button == 0) {
            graphics.DrawLine(&pen, x - radius, y, x + radius, y);
        } else if (button == 1) {
            if (m_restore) {
                const float step = float(Scale(3));
                graphics.DrawLine(&pen, x - radius + step, y - radius, x + radius, y - radius);
                graphics.DrawLine(&pen, x + radius, y - radius, x + radius, y + radius - step);
                graphics.DrawLine(&pen, x + radius, y + radius - step, x + radius - step, y + radius - step);
                graphics.DrawRectangle(&pen, x - radius, y - radius + step, 2 * radius - step, 2 * radius - step);
            } else graphics.DrawRectangle(&pen, x - radius, y - radius, 2 * radius, 2 * radius);
        } else {
            graphics.DrawLine(&pen, x - radius, y - radius, x + radius, y + radius);
            graphics.DrawLine(&pen, x + radius, y - radius, x - radius, y + radius);
        }
    }
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
                Gdiplus::Font font(L"Segoe UI", float(Scale(16)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush text(Gdiplus::Color(240, 255, 255, 255));
                Gdiplus::StringFormat format;
                format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                const Gdiplus::RectF title(float(Scale(16)), 0,
                    float(std::max<LONG>(0, m_buttons[0].left - Scale(24))), float(height));
                graphics.DrawString(m_title, -1, &font, title, &format, &text);
                for (int button = 0; button < 3; ++button) DrawWindowButton(graphics, button);
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
            return HitButton(point) >= 0 ? HTCLIENT : HTTRANSPARENT;
        }
        if (message == WM_MOUSEMOVE && m_header) {
            CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            const int hot = HitButton(point);
            if (hot != m_hot) { m_hot = hot; DrawSurface(); }
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hWnd, 0};
            ::TrackMouseEvent(&track);
            return 0;
        }
        if (message == WM_MOUSELEAVE) {
            if (m_hot != -1) { m_hot = -1; DrawSurface(); }
            return 0;
        }
        if (message == WM_LBUTTONDOWN && m_header) {
            CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            m_pressed = HitButton(point);
            if (m_pressed >= 0) { m_hot = m_pressed; SetCapture(); DrawSurface(); }
            return 0;
        }
        if (message == WM_LBUTTONUP && m_header) {
            CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            const int button = m_pressed >= 0 && m_pressed == HitButton(point) ? m_pressed : -1;
            m_pressed = -1;
            if (::GetCapture() == m_hWnd) ::ReleaseCapture();
            DrawSurface();
            if (button >= 0 && windowCommand) windowCommand(button);
            return 0;
        }
        if (message == WM_CAPTURECHANGED && m_pressed >= 0) { m_pressed = -1; m_hot = -1; DrawSurface(); }
        if (message == WM_LBUTTONDBLCLK && m_header) return 0;
        return __super::WindowProc(message, wParam, lParam);
    }
    BOOL PreTranslateMessage(MSG* message) override {
        if (m_tip.GetSafeHwnd()) m_tip.RelayEvent(message);
        return __super::PreTranslateMessage(message);
    }

public:
    std::function<void(int)> windowCommand;
    CNativeControlsDecoration() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_graphics, &input, nullptr);
    }
    ~CNativeControlsDecoration() override {
        if (GetSafeHwnd()) DestroyWindow();
        if (m_graphics) Gdiplus::GdiplusShutdown(m_graphics);
    }
    void Hide() {
        m_hot = m_pressed = -1;
        if (::GetCapture() == GetSafeHwnd() && GetSafeHwnd()) ::ReleaseCapture();
        if (m_tip.GetSafeHwnd()) m_tip.Pop();
        if (GetSafeHwnd() && (GetStyle() & WS_VISIBLE)) {
            ShowWindow(SW_HIDE);
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
            GeometryDiagnostics::Record(m_header ? "decor.header-hide" : "decor.backdrop-hide", m_hWnd);
#endif
        }
    }
    void Refresh(CWnd* view, const CRect& rectangle, bool header, const CString& title, int dpi, bool restore = false) {
        if (rectangle.IsRectEmpty()) { Hide(); return; }
        if (!GetSafeHwnd()) {
            m_header = header;
            const DWORD extended = WS_EX_LAYERED | WS_EX_NOACTIVATE | (header ? 0 : WS_EX_TRANSPARENT);
            if (!CreateEx(extended, AfxRegisterWndClass(0), L"Native control decoration",
                    WS_CHILD, rectangle, view, 0)) return;
            if (header && m_tip.Create(this, TTS_ALWAYSTIP)) {
                m_tip.AddTool(this, L"Minimize", CRect(0, 0, 1, 1), 1);
                m_tip.AddTool(this, L"Maximize", CRect(0, 0, 1, 1), 2);
                m_tip.AddTool(this, L"Close", CRect(0, 0, 1, 1), 3);
            }
        }
        CRect current; GetWindowRect(current); view->ScreenToClient(current);
        const bool changed = current != rectangle || m_title != title || m_scale != dpi || m_restore != restore || !(GetStyle() & WS_VISIBLE);
        if (!changed) return;
        m_title = title; m_scale = dpi; m_restore = restore;
        const int side = std::min(Scale(40), rectangle.Height());
        for (int button = 0; button < 3; ++button) {
            const int right = rectangle.Width() - (2 - button) * side;
            m_buttons[button].SetRect(std::max(0, right - side), 0, std::max(0, right), side);
            if (m_tip.GetSafeHwnd()) m_tip.SetToolRect(this, button + 1, m_buttons[button]);
        }
        if (m_tip.GetSafeHwnd()) m_tip.UpdateTipText(m_restore ? L"Restore" : L"Maximize", this, 2);
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
