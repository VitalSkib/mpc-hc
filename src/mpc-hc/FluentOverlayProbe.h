/* Experimental native alpha overlay. Distributed under MPC-HC's GPLv3+ license. */
#pragma once

#include <gdiplus.h>

// Header-only so the opt-in probe does not alter the upstream project files.
class CFluentOverlayProbe final : public CWnd
{
    ULONG_PTR m_gdiplus = 0;
    CWnd* m_view = nullptr;
    CString m_title;
    CSize m_size = CSize(0, 0);
    bool m_hot = false;
    bool m_pressed = false;

    CRect CloseRect() const { return CRect(m_size.cx - 46, 12, m_size.cx - 12, 46); }

    bool InsideView() const
    {
        CPoint point;
        ::GetCursorPos(&point);
        CRect rc;
        m_view->GetWindowRect(rc);
        return rc.PtInRect(point) != FALSE;
    }

    void DrawSurface()
    {
        const int width = m_size.cx, height = m_size.cy;
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HDC dc = ::CreateCompatibleDC(nullptr);
        HBITMAP bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dc || !bitmap || !bits) {
            if (bitmap) ::DeleteObject(bitmap);
            if (dc) ::DeleteDC(dc);
            return;
        }
        HGDIOBJ previous = ::SelectObject(dc, bitmap);
        {
            Gdiplus::Bitmap surface(width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
            Gdiplus::Graphics graphics(&surface);
            graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            // Straight video remains visible behind this per-pixel-alpha panel.
            Gdiplus::SolidBrush backdrop(Gdiplus::Color(64, 0, 0, 0));
            graphics.FillRectangle(&backdrop, 0, height - 64, width, 64);
            Gdiplus::Font font(L"Segoe UI", 15.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush text(Gdiplus::Color(255, 235, 235, 235));
            Gdiplus::StringFormat format;
            format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
            format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
            const float textWidth = static_cast<float>(width - 86);
            graphics.DrawString(m_title, -1, &font, Gdiplus::RectF(18, 18, textWidth, 28), &format, &text);
            graphics.DrawString(L"Native alpha overlay - 25% black", -1, &font,
                Gdiplus::RectF(18, static_cast<float>(height - 43), static_cast<float>(width - 36), 28), &format, &text);
            const CRect close = CloseRect();
            if (m_hot) graphics.FillRectangle(&backdrop, close.left, close.top, close.Width(), close.Height());
            Gdiplus::Pen cross(Gdiplus::Color(255, m_hot ? 255 : 235, m_hot ? 255 : 235, m_hot ? 255 : 235), 1.5f);
            const int x = close.left + 11, y = close.top + 11;
            graphics.DrawLine(&cross, x, y, x + 12, y + 12);
            graphics.DrawLine(&cross, x + 12, y, x, y + 12);
        }
        POINT origin = {0, 0};
        SIZE size = {width, height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        ::UpdateLayeredWindow(m_hWnd, nullptr, nullptr, &size, dc, &origin, 0, &blend, ULW_ALPHA);
        ::SelectObject(dc, previous);
        ::DeleteObject(bitmap);
        ::DeleteDC(dc);
    }

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_NCHITTEST) {
            CPoint point(lParam);
            ScreenToClient(&point);
            return CloseRect().PtInRect(point) ? HTCLIENT : HTTRANSPARENT;
        }
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (message == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hWnd, 0};
            ::TrackMouseEvent(&track);
            Refresh(m_view);
            return 0;
        }
        if (message == WM_MOUSELEAVE) {
            if (!InsideView()) Hide();
            else Refresh(m_view);
            return 0;
        }
        if (message == WM_LBUTTONDOWN) {
            m_pressed = true;
            SetCapture();
            return 0;
        }
        if (message == WM_LBUTTONUP) {
            const bool pressed = m_pressed;
            m_pressed = false;
            if (GetCapture() == this) ::ReleaseCapture();
            if (pressed && CloseRect().PtInRect(CPoint(lParam))) m_view->GetTopLevelParent()->PostMessage(WM_CLOSE);
            return 0;
        }
        if (message == WM_CAPTURECHANGED) m_pressed = false;
        return CWnd::WindowProc(message, wParam, lParam);
    }

public:
    CFluentOverlayProbe()
    {
        Gdiplus::GdiplusStartupInput startup;
        if (Gdiplus::GdiplusStartup(&m_gdiplus, &startup, nullptr) != Gdiplus::Ok) m_gdiplus = 0;
    }
    ~CFluentOverlayProbe()
    {
        if (GetSafeHwnd()) DestroyWindow();
        if (m_gdiplus) Gdiplus::GdiplusShutdown(m_gdiplus);
    }

    // Layered child windows can pass input to the video HWND. Route this one
    // hit region before the renderer or video mouse bindings consume the click.
    bool RouteCloseMessage(const MSG& message)
    {
        if (!GetSafeHwnd() || !IsWindowVisible()) return false;
        CPoint point(message.pt);
        ScreenToClient(&point);
        const bool inside = CloseRect().PtInRect(point) != FALSE;
        if (message.message == WM_LBUTTONDOWN && inside) {
            m_pressed = true;
            ::SetCapture(m_view->GetSafeHwnd());
            return true;
        }
        if (message.message == WM_LBUTTONUP && m_pressed) {
            m_pressed = false;
            if (::GetCapture() == m_view->GetSafeHwnd()) ::ReleaseCapture();
            if (inside) ::PostMessage(::GetAncestor(m_view->GetSafeHwnd(), GA_ROOT), WM_CLOSE, 0, 0);
            return true;
        }
        return false;
    }

    void CancelPress() { m_pressed = false; }

    void Hide() { if (GetSafeHwnd()) ShowWindow(SW_HIDE); }

    void Refresh(CWnd* view, HWND trackingWindow = nullptr)
    {
        if (!m_gdiplus) return;
        m_view = view;
        CRect rc;
        view->GetClientRect(rc);
        if (rc.Width() < 180 || rc.Height() < 120 || !view->IsWindowVisible() || !InsideView()) {
            Hide();
            return;
        }
        if (!GetSafeHwnd() && !CreateEx(WS_EX_LAYERED | WS_EX_NOACTIVATE,
                AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW)), L"Fluent alpha probe",
                WS_CHILD, rc, view, 0)) return;
        CString title;
        view->GetTopLevelParent()->GetWindowText(title);
        CPoint cursor;
        ::GetCursorPos(&cursor);
        ScreenToClient(&cursor);
        const bool oldHot = m_hot;
        const CSize oldSize = m_size;
        m_size = rc.Size();
        m_hot = CloseRect().PtInRect(cursor) != FALSE;
        const bool repaint = !IsWindowVisible() || oldSize != m_size || oldHot != m_hot || title != m_title;
        m_title = title;
        SetWindowPos(&wndTop, 0, 0, rc.Width(), rc.Height(), SWP_NOACTIVATE);
        if (repaint) DrawSurface();
        ShowWindow(SW_SHOWNOACTIVATE);
        if (trackingWindow && trackingWindow != m_hWnd) {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, trackingWindow, 0};
            ::TrackMouseEvent(&track);
        }
    }
};
