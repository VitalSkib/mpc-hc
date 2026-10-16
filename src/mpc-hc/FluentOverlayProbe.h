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
    bool m_playPressed = false;
    bool m_seeking = false;
    bool m_canPlay = false, m_playing = false, m_canSeek = false, m_controlsVisible = false;
    LONGLONG m_start = 0, m_stop = 0, m_position = 0;
    CString m_time;
    CRect PlayRect() const { return CRect(m_size.cx / 2 - 18, m_size.cy - 43, m_size.cx / 2 + 18, m_size.cy - 7); }
    CRect TimelineRect() const { return CRect(22, m_size.cy - 70, m_size.cx - 22, m_size.cy - 46); }
    bool IsControlPoint(CPoint point) const
    {
        return CloseRect().PtInRect(point) || (m_controlsVisible && (PlayRect().PtInRect(point) || TimelineRect().PtInRect(point)));
    }

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
            if (m_controlsVisible) graphics.FillRectangle(&backdrop, 0, height - 82, width, 82);
            graphics.FillRectangle(&backdrop, 0, 0, width, 58);
            Gdiplus::Font font(L"Segoe UI", 15.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush text(Gdiplus::Color(255, 235, 235, 235));
            Gdiplus::StringFormat format;
            format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
            format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
            const float textWidth = static_cast<float>(width - 86);
            graphics.DrawString(m_title, -1, &font, Gdiplus::RectF(18, 18, textWidth, 28), &format, &text);
            if (m_controlsVisible) {
                const CRect timeline = TimelineRect(), play = PlayRect();
                const float left = static_cast<float>(timeline.left), right = static_cast<float>(timeline.right - 1);
                const float y = static_cast<float>(timeline.CenterPoint().y);
                Gdiplus::Pen track(Gdiplus::Color(m_canSeek ? 140 : 60, 235, 235, 235), 3.0f);
                track.SetStartCap(Gdiplus::LineCapRound);
                track.SetEndCap(Gdiplus::LineCapRound);
                graphics.DrawLine(&track, left, y, right, y);
                if (m_canSeek && m_stop > m_start) {
                    const double progress = std::clamp(static_cast<double>(m_position - m_start) / static_cast<double>(m_stop - m_start), 0.0, 1.0);
                    const float x = left + static_cast<float>(progress) * (right - left);
                    Gdiplus::Pen elapsed(Gdiplus::Color(255, 235, 235, 235), 3.0f);
                    graphics.DrawLine(&elapsed, left, y, x, y);
                    Gdiplus::SolidBrush accent(Gdiplus::Color(255, 0, 120, 212));
                    graphics.FillEllipse(&accent, x - 5, y - 5, 10.0f, 10.0f);
                }
                Gdiplus::SolidBrush icon(Gdiplus::Color(m_canPlay ? 255 : 80, 235, 235, 235));
                const int x = play.left + 12, py = play.top + 10;
                if (m_playing) {
                    graphics.FillRectangle(&icon, x, py, 4, 16);
                    graphics.FillRectangle(&icon, x + 8, py, 4, 16);
                } else {
                    Gdiplus::Point triangle[] = {{x, py}, {x + 13, py + 8}, {x, py + 16}};
                    graphics.FillPolygon(&icon, triangle, 3);
                }
                graphics.DrawString(m_time, -1, &font,
                    Gdiplus::RectF(22, static_cast<float>(height - 34), static_cast<float>(width / 2 - 46), 24), &format, &text);
            }
            const CRect close = CloseRect();
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
            return IsControlPoint(point) ? HTCLIENT : HTTRANSPARENT;
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

    std::function<void()> playPause;
    std::function<bool(UINT, WPARAM, double)> seekMouse;
    std::function<bool()> seekActive;

    void SetControlState(bool canPlay, bool playing, bool canSeek, bool controlsVisible,
                         LONGLONG start, LONGLONG stop, LONGLONG position, const CString& time)
    {
        const bool changed = m_canPlay != canPlay || m_playing != playing || m_canSeek != canSeek
            || m_controlsVisible != controlsVisible || m_start != start || m_stop != stop || m_position != position || m_time != time;
        m_canPlay = canPlay; m_playing = playing; m_canSeek = canSeek; m_controlsVisible = controlsVisible;
        m_start = start; m_stop = stop; m_position = position; m_time = time;
        if (changed && GetSafeHwnd() && IsWindowVisible()) DrawSurface();
    }

    bool RouteInput(const MSG& message)
    {
        if (m_seeking && seekActive && !seekActive()) m_seeking = false;
        if (!GetSafeHwnd() || (!IsWindowVisible() && !m_seeking)) return false;
        CPoint point(message.pt);
        ScreenToClient(&point);
        const bool down = message.message == WM_LBUTTONDOWN || message.message == WM_LBUTTONDBLCLK;
        const bool up = message.message == WM_LBUTTONUP;
        if (down && CloseRect().PtInRect(point)) {
            m_pressed = true;
            ::SetCapture(m_view->GetSafeHwnd());
            return true;
        }
        if (down && m_controlsVisible && PlayRect().PtInRect(point)) {
            if (m_canPlay) {
                m_playPressed = true;
                ::SetCapture(m_view->GetSafeHwnd());
            }
            return true;
        }
        if ((down && m_controlsVisible && TimelineRect().PtInRect(point)) || m_seeking) {
            if (down || up || message.message == WM_MOUSEMOVE) {
                if (seekMouse && m_canSeek) {
                    const CRect timeline = TimelineRect();
                    const double position = std::clamp(static_cast<double>(point.x - timeline.left) / (timeline.Width() - 1), 0.0, 1.0);
                    m_seeking = seekMouse(down ? WM_LBUTTONDOWN : message.message, message.wParam, position);
                }
                return true;
            }
        }
        if (up && (m_pressed || m_playPressed)) {
            const bool close = m_pressed, play = m_playPressed;
            m_pressed = m_playPressed = false;
            if (::GetCapture() == m_view->GetSafeHwnd()) ::ReleaseCapture();
            if (close && CloseRect().PtInRect(point)) ::PostMessage(::GetAncestor(m_view->GetSafeHwnd(), GA_ROOT), WM_CLOSE, 0, 0);
            if (play && m_canPlay && PlayRect().PtInRect(point) && playPause) playPause();
            return true;
        }
        return false;
    }

    void CancelPress() { m_pressed = m_playPressed = false; }

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
        if (!IsWindowVisible() || oldSize != m_size) SetWindowPos(&wndTop, 0, 0, rc.Width(), rc.Height(), SWP_NOACTIVATE);
        if (repaint) DrawSurface();
        ShowWindow(SW_SHOWNOACTIVATE);
        if (trackingWindow && trackingWindow != m_hWnd) {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, trackingWindow, 0};
            ::TrackMouseEvent(&track);
        }
    }
};
