/* Native list flyout implementation, included by PlayerPlaylistBar.cpp. GPLv3+. */
#pragma once
#include <dwmapi.h>

#include <gdiplus.h>

bool CNativePlaylistPopup::Create(CWnd* owner, CPlayerPlaylistBar* bar)
{
    m_bar = bar;
    // Give the native erase path the same background as our explicit paint path.
    static const HBRUSH background = ::CreateSolidBrush(SurfaceColor());
    if (!CreateEx(WS_EX_TOOLWINDOW, AfxRegisterWndClass(CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW), background),
            L"Playlist", WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CRect(0, 0, 320, 200), owner, 0)) return false;
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
    m_bar->m_listFrame.nativeSurface = true;
    ::SetPropW(m_bar->m_list.m_hWnd, L"MPC.FluentPlaylistScroll", reinterpret_cast<HANDLE>(1));
    // Match the upstream non-layered floating playlist and native list buffering.
    m_bar->m_list.SetExtendedStyle(m_bar->m_list.GetExtendedStyle() | LVS_EX_DOUBLEBUFFER);
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
    const int gap = MulDiv(12, m_dpi, 96);
    CRect available(m_band);
    const int insetX = std::min(gap, std::max(0, (available.Width() - 1) / 2));
    const int insetY = std::min(gap, std::max(0, (available.Height() - 1) / 2));
    available.DeflateRect(insetX, insetY);
    const int width = std::min(MulDiv(360, m_dpi, 96), available.Width());
    const int height = std::min(MulDiv(440, m_dpi, 96), available.Height());
    CRect rect(available.right - width, available.bottom - height, available.right, available.bottom), before;
    GetWindowRect(before);
    if (rect != before) SetWindowPos(nullptr, rect.left, rect.top, rect.Width(), rect.Height(),
        SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
}

void CNativePlaylistPopup::RequestOpen()
{
    m_entered = false;
    m_forceReveal = true;
}

CRect CNativePlaylistPopup::HoverRect() const
{
    CRect rect; GetWindowRect(rect);
    rect.right = m_band.right; // bridge the gap to the player right edge
    return rect;
}

bool CNativePlaylistPopup::WantsReveal() const
{
    if (!m_bar->m_nativePopupSelected || m_band.IsRectEmpty()) return false;
    CPoint cursor; ::GetCursorPos(&cursor);
    return m_forceReveal || (!IsWindowVisible() && cursor != m_hiddenPoint && HoverRect().PtInRect(cursor));
}

void CNativePlaylistPopup::HideEnabled()
{
    if (IsWindowVisible()) {
        ::GetCursorPos(&m_hiddenPoint);
        ShowWindow(SW_HIDE);
    }
    m_entered = false;
    m_closeHot = false;
    m_forceReveal = false;
}

void CNativePlaylistPopup::Dismiss()
{
    HideEnabled();
    m_bar->m_nativePopupSelected = false;
    KillTimer(1);
}

void CNativePlaylistPopup::Refresh(const CRect& band, bool show, int dpi, bool controlsVisible)
{
    m_band = band;
    if (m_dpi != dpi || !m_titleFont.GetSafeHandle()) {
        m_titleFont.DeleteObject();
        m_titleFont.CreateFont(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    m_dpi = dpi;
    if (!show || m_band.IsRectEmpty()) {
        HideEnabled();
        KillTimer(1);
        return;
    }
    Position();
    SetTimer(1, 100, nullptr); // hover/controls observation only; no independent hide timeout
    if (!IsWindowVisible() && WantsReveal()) RequestOpen();
    if (!IsWindowVisible() && !m_forceReveal) return;
    CPoint cursor; ::GetCursorPos(&cursor);
    const bool hovered = HoverRect().PtInRect(cursor);
    if (hovered) m_entered = true;
    const HWND capture = ::GetCapture();
    const bool busy = m_bar->HasActivePopup() || m_bar->m_bDragging || GetTickCount64() < m_dropUntil
        || (capture && (capture == m_hWnd || ::IsChild(m_hWnd, capture)));
    const HWND owner = ::GetWindow(m_hWnd, GW_OWNER);
    CRect ownerBounds; ::GetWindowRect(owner, ownerBounds);
    if (!busy && ((!ownerBounds.PtInRect(cursor) && !hovered)
            || (!controlsVisible && !m_forceReveal)
            || (m_entered && !hovered))) {
        HideEnabled();
        return;
    }
    const bool opening = !IsWindowVisible();
    SetWindowPos(&wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (opening) {
        BOOL animations = TRUE;
        ::SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
        // Use the system slide/mask animation, without moving or resizing the
        // live list. Drag/drop reveals stay immediate for uninterrupted input.
        if (busy || !animations || !::AnimateWindow(m_hWnd, 140, AW_SLIDE | AW_VER_NEGATIVE)) {
            ShowWindow(SW_SHOWNOACTIVATE);
        }
        RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    m_forceReveal = false;
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
    m_dropUntil = GetTickCount64() + 500;
    return m_bar ? static_cast<CDropClient*>(m_bar)->OnDropAccept(data, keys, point) : DROPEFFECT_NONE;
}

void CNativePlaylistPopup::PaintSurface(CDC& dc)
{
    CRect client; GetClientRect(client);
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
}

LRESULT CNativePlaylistPopup::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!m_bar) return __super::WindowProc(message, wParam, lParam);
    switch (message) {
    case WM_NCCALCSIZE: return wParam ? WVR_REDRAW : 0;
    case WM_NCPAINT: return 0;
    case WM_NCACTIVATE: return DefWindowProc(message, wParam, -1);
    case WM_NCHITTEST: return HTCLIENT;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_MOVE || (wParam & 0xfff0) == SC_SIZE) return 0;
        break;
    case WM_TIMER:
        if (wParam == 1) {
            if (m_bar->nativePopupActivity) m_bar->nativePopupActivity();
            return 0;
        }
        break;
    case WM_NCLBUTTONDBLCLK: return 0;
    case WM_ERASEBKGND: {
        if (!wParam) return 0;
        CRect client; GetClientRect(client);
        CDC::FromHandle(reinterpret_cast<HDC>(wParam))->FillSolidRect(client, SurfaceColor());
        return TRUE;
    }
    case WM_PAINT: {
        CPaintDC dc(this);
        PaintSurface(dc);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (wParam) PaintSurface(*CDC::FromHandle(reinterpret_cast<HDC>(wParam)));
        return 0;
    case WM_SIZE:
        m_bar->ResizeListColumn();
        RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return 0;
    case WM_SETCURSOR:
        ::SetCursor(::LoadCursor(nullptr, IDC_ARROW)); return TRUE;
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
        Dismiss();
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
    if (!UsesNativePopup()) return false;
    if (!m_nativePopup.IsWindowVisible()) return m_nativePopup.WantsReveal();
    if (HasActivePopup() || m_bDragging) return true;
    HWND capture = ::GetCapture();
    if (capture && (capture == m_hWnd || capture == m_nativePopup.m_hWnd || ::IsChild(m_nativePopup.m_hWnd, capture))) return true;
    POINT point; ::GetCursorPos(&point); CRect bounds; m_nativePopup.GetWindowRect(bounds);
    const HWND target = ::WindowFromPoint(point);
    return bounds.PtInRect(point) && ::GetAncestor(target, GA_ROOT) == m_nativePopup.m_hWnd;
}

void CPlayerPlaylistBar::RefreshNativePopup(const CRect& band, bool show, int dpi, bool controlsVisible)
{
    if (!UsesNativePopup()) return;
    const auto& settings = AfxGetAppSettings();
    const bool fullscreen = m_pMainFrame->IsFullScreenMode();
    const bool neverShow = fullscreen && settings.bHideFullscreenControls
        && settings.eHideFullscreenControlsPolicy == CAppSettings::HideFullscreenControlsPolicy::SHOW_NEVER;
    const bool hiddenFullscreen = fullscreen && settings.bHidePlaylistFullScreen;
    m_nativePopup.Refresh(band, show && m_nativePopupSelected && !hiddenFullscreen && !neverShow, dpi, controlsVisible);
    UpdateNativeHover();
}
