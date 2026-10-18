/* Native list flyout implementation, included by PlayerPlaylistBar.cpp. GPLv3+. */
#pragma once
#include <dwmapi.h>

#include <gdiplus.h>

bool CNativePlaylistPopup::Create(CWnd* owner, CPlayerPlaylistBar* bar)
{
    m_bar = bar;
    m_width = std::clamp<int>(AfxGetApp()->GetProfileInt(L"ToolBars\\Playlist", L"PopupWidth", 320), 120, 1200);
    if (!CreateEx(WS_EX_TOOLWINDOW, AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW)),
            L"Playlist", WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CRect(0, 0, 320, 200), owner, 0)) return false;
    Gdiplus::GdiplusStartupInput graphicsStartup;
    Gdiplus::GdiplusStartup(&m_graphicsToken, &graphicsStartup, nullptr);
    const DWORD corners = 2; const BOOL dark = TRUE;
    const COLORREF border = BorderColor();
    m_systemBorder = SUCCEEDED(DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(34), &border, sizeof(border)));
    DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(33), &corners, sizeof(corners));
    DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(20), &dark, sizeof(dark));
    return true;
}

void CNativePlaylistPopup::ConfigureSurface()
{
    // Opaque GDI children: no keyed surfaces, material, or transparent-input routing.
    const DWORD none = 1;
    const MARGINS solid = {};
    DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(38), &none, sizeof(none));
    DwmExtendFrameIntoClientArea(m_hWnd, &solid);
    m_bar->m_list.SetExtendedStyle(m_bar->m_list.GetExtendedStyle() & ~LVS_EX_DOUBLEBUFFER);
}

CRect CNativePlaylistPopup::CloseRect() const
{
    CRect client; GetClientRect(client);
    const int size = MulDiv(32, m_dpi, 96);
    return CRect(std::max<LONG>(0, client.right - size), 0, client.right, size);
}

void CNativePlaylistPopup::Position()
{
    if (m_band.IsRectEmpty() || m_resizing) return;
    const int maximum = std::max(1, m_band.Width() / 2);
    const int minimum = std::min(maximum, MulDiv(160, m_dpi, 96));
    const int width = std::clamp(MulDiv(m_width, m_dpi, 96), minimum, maximum);
    CRect rect(m_band.right - width, m_band.top, m_band.right, m_band.bottom), before;
    GetWindowRect(before);
    if (rect != before) SetWindowPos(nullptr, rect.left, rect.top, rect.Width(), rect.Height(), SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
}

void CNativePlaylistPopup::RequestOpen()
{
    ::GetCursorPos(&m_requestPoint);
    m_revealUntil = GetTickCount64() + std::max(250u, AfxGetAppSettings().uHideFullscreenControlsDelay);
}

void CNativePlaylistPopup::Refresh(const CRect& band, bool show, int dpi, bool autoHide)
{
    m_band = band;
    if (m_dpi != dpi || !m_titleFont.GetSafeHandle()) {
        m_titleFont.DeleteObject();
        m_titleFont.CreateFont(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    m_dpi = dpi;
    m_band.right = std::max(m_band.left, m_band.right - 1); // one physical pixel inside the view
    if (!show || m_band.IsRectEmpty()) { if (IsWindowVisible()) ShowWindow(SW_HIDE); return; }
    Position();
    if (autoHide) {
        CPoint cursor; ::GetCursorPos(&cursor);
        if (cursor != m_requestPoint || GetTickCount64() >= m_revealUntil) m_revealUntil = 0;
        CRect bounds; GetWindowRect(bounds);
        const HWND target = ::GetAncestor(::WindowFromPoint(cursor), GA_ROOT);
        const HWND owner = ::GetWindow(m_hWnd, GW_OWNER);
        CRect hotZone(bounds); hotZone.right = band.right;
        const bool hovered = hotZone.PtInRect(cursor) && (target == m_hWnd || target == owner);
        if (!hovered && !m_bar->NativePopupInputActive() && !m_revealUntil) {
            if (IsWindowVisible()) ShowWindow(SW_HIDE);
            return;
        }
    }
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
    case WM_NCCALCSIZE: return wParam ? WVR_REDRAW : 0;
    case WM_NCPAINT: return 0; // DWM owns the rounded border; no classic sizing-frame paint
    case WM_NCACTIVATE: return DefWindowProc(message, wParam, -1);
    case WM_NCHITTEST: {
        CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); ScreenToClient(&point);
        return point.x >= 0 && point.x < MulDiv(6, m_dpi, 96) ? HTLEFT : HTCLIENT;
    }
    case WM_GETMINMAXINFO:
        if (!m_band.IsRectEmpty()) {
            auto sizes = reinterpret_cast<MINMAXINFO*>(lParam);
            const int maximum = std::max(1, m_band.Width()/2);
            sizes->ptMinTrackSize = CPoint(std::min(maximum, MulDiv(160, m_dpi, 96)), m_band.Height());
            sizes->ptMaxTrackSize = CPoint(maximum, m_band.Height());
            return 0;
        }
        break;
    case WM_ENTERSIZEMOVE: m_resizing = true; return 0;
    case WM_SIZING: {
        auto rect = reinterpret_cast<RECT*>(lParam);
        const int maximum = std::max(1, m_band.Width()/2);
        const int minimum = std::min(maximum, MulDiv(160, m_dpi, 96));
        const int width = std::clamp<int>(m_band.right - rect->left, minimum, maximum);
        *rect = CRect(m_band.right - width, m_band.top, m_band.right, m_band.bottom);
        m_width = MulDiv(width, 96, m_dpi);
        return TRUE;
    }
    case WM_EXITSIZEMOVE: {
        CRect finalRect; GetWindowRect(finalRect);
        m_width = MulDiv(finalRect.Width(), 96, m_dpi);
        m_resizing = false;
        AfxGetApp()->WriteProfileInt(L"ToolBars\\Playlist", L"PopupWidth", m_width);
        if (m_bar->nativePopupActivity) m_bar->nativePopupActivity();
        return 0;
    }
    case WM_NCLBUTTONDBLCLK: return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        CPaintDC paint(this); CRect client; GetClientRect(client);
        CDC& dc = paint;
        dc.FillSolidRect(client, SurfaceColor());
        CRect header(client); header.bottom = std::min<LONG>(client.bottom, MulDiv(32, m_dpi, 96));
        dc.FillSolidRect(0, header.bottom - 1, client.Width(), 1, BorderColor());
        CBrush border(BorderColor()); if (!m_systemBorder) dc.FrameRect(client, &border);
        CRect close = CloseRect();
        if (m_closeHot) dc.FillSolidRect(close, SelectionColor());
        dc.SetBkMode(TRANSPARENT); dc.SetTextColor(RGB(210, 210, 210));
        CFont* old = dc.SelectObject(m_titleFont.GetSafeHandle() ? &m_titleFont : &m_bar->m_font);
        CRect title(client); title.left += MulDiv(12, m_dpi, 96); title.right = close.left; title.bottom = close.bottom;
        CString caption; m_bar->GetWindowText(caption);
        dc.DrawText(caption, title, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        dc.SelectObject(old);
        CPen pen(PS_SOLID, 1, CMPCTheme::TextFGColor); CPen* previous = dc.SelectObject(&pen);
        const CPoint center = close.CenterPoint(); const int half = MulDiv(4, m_dpi, 96);
        dc.MoveTo(center.x-half, center.y-half); dc.LineTo(center.x+half+1, center.y+half+1);
        dc.MoveTo(center.x+half, center.y-half); dc.LineTo(center.x-half-1, center.y+half+1);
        dc.SelectObject(previous);
        return 0;
    }
    case WM_SIZE: {
        m_bar->ResizeListColumn();
        // Paint the small header during the native resize transaction, rather than
        // letting queued WM_PAINT leave its old close-button position visible.
        CRect header; GetClientRect(header);
        header.bottom = std::min<LONG>(header.bottom, MulDiv(32, m_dpi, 96));
        RedrawWindow(header, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
        return 0;
    }
    case WM_SETCURSOR: {
        POINT point; ::GetCursorPos(&point); ScreenToClient(&point);
        if (point.x < MulDiv(6, m_dpi, 96)) { ::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE)); return TRUE; }
        break;
    }
    case WM_LBUTTONDOWN: {
        CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (CloseRect().PtInRect(point)) { m_closePressed = true; SetCapture(); }
        return 0;
    }
    case WM_MOUSEMOVE: {
        const bool hot = CloseRect().PtInRect(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        if (hot != m_closeHot) { m_closeHot = hot; Invalidate(FALSE); }
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hWnd, 0}; ::TrackMouseEvent(&track);
        if (m_bar->nativePopupActivity) m_bar->nativePopupActivity();
        return 0;
    }
    case WM_LBUTTONUP: {
        const bool close = m_closePressed && CloseRect().PtInRect(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        m_closePressed = false;
        if (::GetCapture() == m_hWnd) ::ReleaseCapture();
        if (!close) return 0;
        [[fallthrough]];
    }
    case WM_CLOSE:
        m_bar->m_nativePopupSelected = false; ShowWindow(SW_HIDE);
        m_bar->m_pMainFrame->SetFocus();
        if (m_bar->nativePopupActivity) m_bar->nativePopupActivity();
        return 0;
    case WM_NCDESTROY:
        if (m_graphicsToken) { Gdiplus::GdiplusShutdown(m_graphicsToken); m_graphicsToken = 0; }
        break;
    case WM_CAPTURECHANGED: m_closePressed = false; break;
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
    if (HasActivePopup() || m_bDragging || m_nativePopup.Resizing()) return true;
    HWND capture = ::GetCapture();
    if (capture && (capture == m_hWnd || capture == m_nativePopup.m_hWnd || ::IsChild(m_nativePopup.m_hWnd, capture))) return true;
    POINT point; ::GetCursorPos(&point); CRect bounds; m_nativePopup.GetWindowRect(bounds);
    const HWND target = ::WindowFromPoint(point);
    return bounds.PtInRect(point) && ::GetAncestor(target, GA_ROOT) == m_nativePopup.m_hWnd;
}

void CPlayerPlaylistBar::RefreshNativePopup(const CRect& band, bool show, int dpi)
{
    if (!UsesNativePopup()) return;
    const auto& settings = AfxGetAppSettings();
    const bool fullscreen = m_pMainFrame->IsFullScreenMode();
    const bool neverShow = fullscreen && settings.bHideFullscreenControls
        && settings.eHideFullscreenControlsPolicy == CAppSettings::HideFullscreenControlsPolicy::SHOW_NEVER;
    const bool hiddenFullscreen = fullscreen && settings.bHidePlaylistFullScreen;
    const bool autoHide = settings.bHideFullscreenControls && (fullscreen || settings.bHideWindowedControls);
    m_nativePopup.Refresh(band, show && m_nativePopupSelected && !hiddenFullscreen && !neverShow, dpi, autoHide);
    UpdateNativeHover();
}
