/* Native list flyout implementation, included by PlayerPlaylistBar.cpp. GPLv3+. */
#pragma once
#include <dwmapi.h>

bool CNativePlaylistPopup::Create(CWnd* owner, CPlayerPlaylistBar* bar)
{
    m_bar = bar;
    m_width = std::clamp<int>(AfxGetApp()->GetProfileInt(L"ToolBars\\Playlist", L"PopupWidth", 320), 120, 1200);
    if (!CreateEx(WS_EX_TOOLWINDOW, AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW)),
            L"Playlist", WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CRect(0, 0, 320, 200), owner, 0)) return false;
    const DWORD corners = 2; const BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(33), &corners, sizeof(corners));
    DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(20), &dark, sizeof(dark));
    return true;
}

CRect CNativePlaylistPopup::CloseRect() const
{
    CRect client; GetClientRect(client);
    const int size = MulDiv(32, m_dpi, 96);
    return CRect(std::max<LONG>(0, client.right - size), 0, client.right, size);
}

void CNativePlaylistPopup::Position()
{
    if (m_band.IsRectEmpty()) return;
    const int maximum = std::max(1, m_band.Width() / 2);
    const int minimum = std::min(maximum, MulDiv(160, m_dpi, 96));
    const int width = std::clamp(MulDiv(m_width, m_dpi, 96), minimum, maximum);
    CRect rect(m_band.right - width, m_band.top, m_band.right, m_band.bottom), before;
    GetWindowRect(before);
    if (rect != before) SetWindowPos(nullptr, rect.left, rect.top, rect.Width(), rect.Height(), SWP_NOACTIVATE | SWP_NOZORDER);
}

void CNativePlaylistPopup::Refresh(const CRect& band, bool show, int dpi)
{
    m_band = band; m_dpi = dpi;
    if (!show || band.IsRectEmpty()) { if (IsWindowVisible()) ShowWindow(SW_HIDE); return; }
    Position();
    if (!IsWindowVisible()) SetWindowPos(&wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

BOOL CNativePlaylistPopup::PreTranslateMessage(MSG* message)
{
    if (m_bar && m_bar->PreTranslateMessage(message)) return TRUE;
    return __super::PreTranslateMessage(message);
}

void CNativePlaylistPopup::OnDropFiles(CAtlList<CStringW>& files, DROPEFFECT effect)
{
    if (m_bar) static_cast<CDropClient*>(m_bar)->OnDropFiles(files, effect);
}

DROPEFFECT CNativePlaylistPopup::OnDropAccept(COleDataObject* data, DWORD keys, CPoint point)
{
    return m_bar ? static_cast<CDropClient*>(m_bar)->OnDropAccept(data, keys, point) : DROPEFFECT_NONE;
}

LRESULT CNativePlaylistPopup::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!m_bar) return __super::WindowProc(message, wParam, lParam);
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        CPaintDC dc(this); CRect client; GetClientRect(client);
        dc.FillSolidRect(client, CMPCTheme::MenuBGColor);
        CBrush border(CMPCTheme::MenuSeparatorColor); dc.FrameRect(client, &border);
        CRect close = CloseRect();
        if (m_closeHot) dc.FillSolidRect(close, CMPCTheme::MenuSelectedColor);
        dc.SetBkMode(TRANSPARENT); dc.SetTextColor(CMPCTheme::TextFGColor);
        CFont* old = dc.SelectObject(&m_bar->m_font);
        CRect title(client); title.left += MulDiv(12, m_dpi, 96); title.right = close.left; title.bottom = close.bottom;
        CString caption; m_bar->GetWindowText(caption);
        dc.DrawText(caption, title, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        dc.SelectObject(old);
        CPen pen(PS_SOLID, 1, CMPCTheme::TextFGColor); CPen* previous = dc.SelectObject(&pen);
        const CPoint center = close.CenterPoint(); const int half = MulDiv(4, m_dpi, 96);
        dc.MoveTo(center.x-half, center.y-half); dc.LineTo(center.x+half+1, center.y+half+1);
        dc.MoveTo(center.x+half, center.y-half); dc.LineTo(center.x-half-1, center.y+half+1);
        dc.SelectObject(previous); return 0;
    }
    case WM_SIZE: m_bar->ResizeListColumn(); return 0;
    case WM_SETCURSOR: {
        POINT point; ::GetCursorPos(&point); ScreenToClient(&point);
        if (point.x < MulDiv(6, m_dpi, 96)) { ::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE)); return TRUE; }
        break;
    }
    case WM_LBUTTONDOWN: {
        CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (point.x < MulDiv(6, m_dpi, 96)) { m_resizing = true; SetCapture(); }
        else if (CloseRect().PtInRect(point)) { m_closePressed = true; SetCapture(); }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (m_resizing) {
            POINT cursor; ::GetCursorPos(&cursor);
            const int maximum = std::max(1, m_band.Width()/2);
            const int minimum = std::min(maximum, MulDiv(160, m_dpi, 96));
            m_width = MulDiv(std::clamp<int>(m_band.right-cursor.x, minimum, maximum), 96, m_dpi);
            Position();
        }
        const bool hot = CloseRect().PtInRect(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        if (hot != m_closeHot) { m_closeHot = hot; Invalidate(FALSE); }
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hWnd, 0}; ::TrackMouseEvent(&track);
        if (m_bar->nativePopupActivity) m_bar->nativePopupActivity();
        return 0;
    }
    case WM_LBUTTONUP: {
        const bool close = m_closePressed && CloseRect().PtInRect(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        const bool resized = m_resizing;
        m_resizing = m_closePressed = false;
        if (::GetCapture() == m_hWnd) ::ReleaseCapture();
        if (resized) AfxGetApp()->WriteProfileInt(L"ToolBars\\Playlist", L"PopupWidth", m_width);
        if (!close) return 0;
        [[fallthrough]];
    }
    case WM_CLOSE:
        m_bar->m_nativePopupSelected = false; ShowWindow(SW_HIDE);
        m_bar->m_pMainFrame->SetFocus();
        if (m_bar->nativePopupActivity) m_bar->nativePopupActivity();
        return 0;
    case WM_CAPTURECHANGED: m_resizing = m_closePressed = false; break;
    case WM_MOUSELEAVE:
        m_closeHot = false; Invalidate(FALSE); m_bar->UpdateNativeHover();
        if (m_bar->nativePopupActivity) m_bar->nativePopupActivity();
        return 0;
    case WM_NOTIFY: case WM_DRAWITEM: case WM_MEASUREITEM: case WM_COMMAND: case WM_CONTEXTMENU:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        return m_bar->SendMessage(message, wParam, lParam);
    }
    return __super::WindowProc(message, wParam, lParam);
}

bool CPlayerPlaylistBar::NativePopupInputActive() const
{
    if (!UsesNativePopup() || !m_nativePopup.IsWindowVisible()) return false;
    if (HasActivePopup() || m_bDragging) return true;
    HWND capture = ::GetCapture();
    if (capture && (capture == m_hWnd || capture == m_nativePopup.m_hWnd || ::IsChild(m_nativePopup.m_hWnd, capture))) return true;
    POINT point; ::GetCursorPos(&point); CRect bounds; m_nativePopup.GetWindowRect(bounds);
    const HWND target = ::WindowFromPoint(point);
    return bounds.PtInRect(point) && ::GetAncestor(target, GA_ROOT) == m_nativePopup.m_hWnd;
}

void CPlayerPlaylistBar::RefreshNativePopup(const CRect& band, bool show, int dpi)
{
    if (!UsesNativePopup()) return;
    const bool hiddenFullscreen = m_pMainFrame->IsFullScreenMode() && AfxGetAppSettings().bHidePlaylistFullScreen;
    m_nativePopup.Refresh(band, show && m_nativePopupSelected && !hiddenFullscreen, dpi);
    UpdateNativeHover();
}
