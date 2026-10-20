/* Native list flyout implementation, included by PlayerPlaylistBar.cpp. GPLv3+. */
#pragma once
#include <dwmapi.h>
#include "MouseTouch.h"

#include <gdiplus.h>
#include <cmath>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

bool CNativePlaylistPopup::Create(CWnd* owner, CPlayerPlaylistBar* bar)
{
    m_bar = bar;
    m_materialMode = std::clamp(static_cast<int>(AfxGetApp()->GetProfileInt(L"Settings", L"FluentPlaylistMaterial", 3)), 1, 3);
    m_materialTint = std::clamp(static_cast<int>(AfxGetApp()->GetProfileInt(L"Settings", L"FluentPlaylistTint", 0)), 0, 255);
    m_bufferedPaintReady = SUCCEEDED(::BufferedPaintInit());
    // Give the native erase path the same background as our explicit paint path.
    static const HBRUSH background = ::CreateSolidBrush(SurfaceColor());
    if (!CreateEx(WS_EX_TOOLWINDOW, AfxRegisterWndClass(CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW), background),
            L"Playlist", WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CRect(0, 0, 320, 200), owner, 0)) return false;
    Gdiplus::GdiplusStartupInput graphicsStartup;
    Gdiplus::GdiplusStartup(&m_graphicsToken, &graphicsStartup, nullptr);
    const DWORD corners = 2; const BOOL dark = TRUE;
    const BOOL noTransitions = TRUE;
    DwmSetWindowAttribute(m_hWnd, DWMWA_TRANSITIONS_FORCEDISABLED, &noTransitions, sizeof(noTransitions));
    const COLORREF border = BorderColor();
    m_systemBorder = SUCCEEDED(DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(34), &border, sizeof(border)));
    DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(33), &corners, sizeof(corners));
    DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(20), &dark, sizeof(dark));
    return true;
}

void CNativePlaylistPopup::ConfigureSurface()
{
    EndLiveFade();
    KillTimer(3); m_foregroundPending = false;
    m_foregroundPixels.clear();
    // The backdrop and alpha-aware GDI client share this same HWND/redirection
    // surface. No keyed child, extra material window or new input routing.
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(20), &dark, sizeof(dark));
    HIGHCONTRAST contrast = {sizeof(contrast)};
    SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    DWORD transparency = 1, size = sizeof(transparency);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &size);
    const DWORD requested = m_bufferedPaintReady && transparency && !(contrast.dwFlags & HCF_HIGHCONTRASTON)
        ? m_materialMode : 1;
    EndLiveSlide(false);
    m_systemMaterial.Reset();
    // Composition owns every visible pixel. Native HWNDs retain their input,
    // layout and WM_PRINT paths, but have no second GDI redirection surface.
    ModifyStyleEx(WS_EX_COMPOSITED | WS_EX_NOREDIRECTIONBITMAP,
        requested != 1 ? WS_EX_NOREDIRECTIONBITMAP : 0);
    const DWORD none = 1;
    DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(38), &none, sizeof(none));
    m_materialActive = requested != 1 && m_systemMaterial.Start(m_hWnd, static_cast<int>(requested));
    const MARGINS margins{};
    if (FAILED(DwmExtendFrameIntoClientArea(m_hWnd, &margins))) m_materialActive = false;
    if (!m_materialActive) {
        m_systemMaterial.Reset();
        const DWORD none = 1; const MARGINS solid = {};
        DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(38), &none, sizeof(none));
        DwmExtendFrameIntoClientArea(m_hWnd, &solid);
    }
    // A material panel carries its border in the same foreground surface.
    // DWM's separate nonclient border must not precede that surface.
    const COLORREF border = m_materialActive ? 0xfffffffe : BorderColor();
    const bool borderSet = SUCCEEDED(DwmSetWindowAttribute(m_hWnd,
        static_cast<DWMWINDOWATTRIBUTE>(34), &border, sizeof(border)));
    m_systemBorder = !m_materialActive && borderSet;
    const DWMNCRENDERINGPOLICY nonclient = m_materialActive ? DWMNCRP_DISABLED : DWMNCRP_USEWINDOWSTYLE;
    DwmSetWindowAttribute(m_hWnd, DWMWA_NCRENDERING_POLICY, &nonclient, sizeof(nonclient));
    const DWORD corners = m_materialActive ? 1 : 2;
    DwmSetWindowAttribute(m_hWnd, static_cast<DWMWINDOWATTRIBUTE>(33), &corners, sizeof(corners));
    // Composition is the sole visible renderer in material mode.
    ModifyStyleEx(WS_EX_COMPOSITED | WS_EX_NOREDIRECTIONBITMAP,
        m_materialActive ? WS_EX_NOREDIRECTIONBITMAP : 0);
    // Native children remain input/WM_PRINT sources only. Their own redirected
    // GDI surfaces would remain stationary underneath the animated artwork.
    m_bar->m_listFrame.ModifyStyleEx(WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP,
        m_materialActive ? WS_EX_NOREDIRECTIONBITMAP : 0, SWP_FRAMECHANGED | SWP_NOACTIVATE);
    m_bar->m_list.ModifyStyleEx(WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP,
        m_materialActive ? WS_EX_NOREDIRECTIONBITMAP : 0, SWP_FRAMECHANGED | SWP_NOACTIVATE);
    QueueMaterialForeground();
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

// GDI fills and text do not preserve meaningful alpha. Render the native
// artwork into a top-down DIB first, then assign premultiplied alpha explicitly.
// Buffered painting copies the complete surface; repeated invalidations cannot
// accumulate a translucent tint over an earlier frame.
bool CNativePlaylistPopup::PaintMaterialBuffer(HDC target, const RECT& rect,
    const std::function<void(HDC)>& draw, BYTE tint)
{
    if (rect.right <= rect.left || rect.bottom <= rect.top) return false;
    BP_PAINTPARAMS params = {sizeof(params)};
    params.dwFlags = BPPF_ERASE;
    HDC buffer = nullptr;
    HPAINTBUFFER paint = BeginBufferedPaint(target, &rect, BPBF_TOPDOWNDIB, &params, &buffer);
    if (!paint || !buffer) return false;
    draw(buffer);
    RGBQUAD* pixels = nullptr; int stride = 0;
    if (FAILED(GetBufferedPaintBits(paint, &pixels, &stride)) || !pixels) {
        EndBufferedPaint(paint, FALSE); return false;
    }
    GdiFlush();
    for (int y = 0; y < rect.bottom - rect.top; ++y) {
        RGBQUAD* row = pixels + static_cast<size_t>(y) * stride;
        for (int x = 0; x < rect.right - rect.left; ++x) {
            auto& px = row[x];
            // Only the panel material is translucent. Text, selection, accent,
            // separators and scroll thumb retain their accepted solid artwork.
            const BYTE alpha = px.rgbRed == 43 && px.rgbGreen == 43 && px.rgbBlue == 43 ? tint : 255;
            px.rgbRed = static_cast<BYTE>(px.rgbRed * alpha / 255);
            px.rgbGreen = static_cast<BYTE>(px.rgbGreen * alpha / 255);
            px.rgbBlue = static_cast<BYTE>(px.rgbBlue * alpha / 255);
            px.rgbReserved = alpha;
        }
    }
    return SUCCEEDED(EndBufferedPaint(paint, TRUE));
}

void CNativePlaylistPopup::SetMaterialTint(int tint)
{
    EndReveal(false);
    m_materialTint = std::clamp(tint, 0, 255);
    AfxGetApp()->WriteProfileInt(L"Settings", L"FluentPlaylistTint", m_materialTint);
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void CNativePlaylistPopup::SetMaterialMode(int mode)
{
    if (mode < 1 || mode > 3) return;
    EndReveal(false);
    m_materialMode = mode;
    AfxGetApp()->WriteProfileInt(L"Settings", L"FluentPlaylistMaterial", mode);
    ConfigureSurface();
    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void CNativePlaylistPopup::QueueMaterialForeground()
{
    if (!m_materialActive || m_materialPainting || m_foregroundPending || m_liveSliding || m_liveFading) return;
    // A posted update can starve input/WM_PAINT when native queries schedule
    // another update. A one-shot low-priority timer coalesces actual damage.
    m_foregroundPending = !!SetTimer(3, 16, nullptr);
}

void CNativePlaylistPopup::UpdateMaterialForeground()
{
    if (!m_materialActive || m_materialPainting || m_liveSliding || m_liveFading) return;
    CRect area; GetClientRect(area);
    const int width = area.Width(), height = area.Height();
    if (width <= 0 || height <= 0 || !m_bar->m_list.GetSafeHwnd()) return;
    CRect origin(area); ClientToScreen(origin);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    HDC memory = ::CreateCompatibleDC(nullptr);
    DWORD* pixels = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(memory, &info, DIB_RGB_COLORS,
        reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (!memory || !bitmap || !pixels) {
        if (bitmap) ::DeleteObject(bitmap);
        if (memory) ::DeleteDC(memory);
        return;
    }
    HGDIOBJ old = ::SelectObject(memory, bitmap);
    CDC* dc = CDC::FromHandle(memory);
    m_materialPainting = true;

    PaintSurface(*dc);
    // Print only the native list, at its exact final location. Do not ask the
    // window manager to separately animate the live client or its DWM frame.
    CRect list; m_bar->m_list.GetWindowRect(list);
    list.OffsetRect(-origin.TopLeft());
    const int saved = dc->SaveDC();
    dc->SetViewportOrg(list.TopLeft());
    dc->IntersectClipRect(0, 0, list.Width(), list.Height());
    m_bar->m_list.SendMessage(WM_PRINT, reinterpret_cast<WPARAM>(memory),
        PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
    dc->RestoreDC(saved);
    const int scrollbarClip = dc->SaveDC();
    dc->SetViewportOrg(list.TopLeft());
    dc->IntersectClipRect(0, 0, list.Width(), list.Height());
    // Native WM_PRINT uses an unthemed scrollbar. Compose the very same narrow
    // scrollbar renderer used by the live list over it before taking the image.
    CRect listWindow, listClient;
    m_bar->m_list.GetWindowRect(listWindow); m_bar->m_list.GetClientRect(listClient);
    m_bar->m_list.ClientToScreen(listClient); listClient.OffsetRect(-listWindow.TopLeft());
    const LONG_PTR listStyle = ::GetWindowLongPtr(m_bar->m_list.m_hWnd, GWL_STYLE);
    const bool hasVertical = !!(listStyle & WS_VSCROLL), hasHorizontal = !!(listStyle & WS_HSCROLL);
    // GetScrollBarInfo reports a hidden parent's bar as invisible. Use its
    // layout/style here so the print image still gets the dark scrollbar.
    CRect vertical(listClient.right, listClient.top, listWindow.Width() - listClient.left, listClient.bottom);
    CRect horizontal(listClient.left, listClient.bottom, listClient.right, listWindow.Height() - listClient.top);
    m_bar->m_list.DrawThemedScrollBars(dc, m_bar->m_list.m_hWnd,
        vertical, horizontal, hasVertical, hasHorizontal, hasVertical && hasHorizontal);
    dc->RestoreDC(scrollbarClip);
    // Explicitly retain the full header even if a child print path changes its clip.
    const int headerClip = dc->SaveDC();
    dc->IntersectClipRect(0, 0, width, MulDiv(32, m_dpi, 96));
    PaintSurface(*dc); dc->RestoreDC(headerClip);

    ::GdiFlush();
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        const DWORD rgb = pixels[i] & 0xffffff;
        const DWORD alpha = rgb == 0x2b2b2b ? m_materialTint : 255;
        pixels[i] = (alpha << 24) | (((rgb >> 16 & 255) * alpha / 255) << 16)
            | (((rgb >> 8 & 255) * alpha / 255) << 8) | ((rgb & 255) * alpha / 255);
    }
    const size_t count = static_cast<size_t>(width) * height;
    const bool changed = m_foregroundWidth != width || m_foregroundHeight != height
        || m_foregroundPixels.size() != count
        || memcmp(m_foregroundPixels.data(), pixels, count * sizeof(DWORD));
    // Composition commits can themselves cause paint notifications. Stop that
    // feedback at the pixel boundary instead of recommitting an identical frame.
    const bool updated = !changed || m_systemMaterial.UpdateForeground(pixels, width, height);
    if (updated && changed) {
        m_foregroundPixels.assign(pixels, pixels + count);
        m_foregroundWidth = width; m_foregroundHeight = height;
    }
    ::SelectObject(memory, old); ::DeleteObject(bitmap); ::DeleteDC(memory);
    m_materialPainting = false;
    if (!updated) {
        // An unsupported/device-lost foreground must leave a readable Solid UI.
        m_systemMaterial.Reset(); m_materialActive = false;
        const MARGINS solid{}; DwmExtendFrameIntoClientArea(m_hWnd, &solid);
        ModifyStyleEx(WS_EX_COMPOSITED | WS_EX_NOREDIRECTIONBITMAP, 0);
        m_bar->m_listFrame.ModifyStyleEx(WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP, 0);
        m_bar->m_list.ModifyStyleEx(WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP, 0);
        RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
}

bool CNativePlaylistPopup::PaintMaterialList()
{
    if (!m_materialActive || m_materialPainting) return false;
    QueueMaterialForeground();
    return true;
}

void CNativePlaylistPopup::Position()
{
    if (m_band.IsRectEmpty()) return;
    const int gap = MulDiv(12, m_dpi, 96);
    CRect available(m_band);
    const int insetX = std::min(gap, std::max(0, (available.Width() - 1) / 2));
    const int insetY = std::min(gap, std::max(0, (available.Height() - 1) / 2));
    available.DeflateRect(insetX, insetY);
    const int preferredWidth = m_bar->m_pMainFrame->IsFullScreenMode() ? 540 : 360;
    const int width = std::min(MulDiv(preferredWidth, m_dpi, 96), available.Width());
    const int height = available.Height();
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
    if (!m_bar->m_pMainFrame->CanHoverControls()
            || !m_bar->m_nativePopupSelected || m_band.IsRectEmpty()) return false;
    CPoint cursor; ::GetCursorPos(&cursor);
    return m_forceReveal || (!IsWindowVisible() && cursor != m_hiddenPoint && HoverRect().PtInRect(cursor)
        && CMouse::CursorOnRootWindow(cursor, *m_bar->m_pMainFrame));
}

void CNativePlaylistPopup::HideEnabled(bool animate)
{
    if (m_liveFading) { if (!animate) EndLiveFade(); return; }
    if (m_fading) { if (!animate) EndReveal(false); return; }
    const bool visible = !!IsWindowVisible();
    if (visible || m_revealing) ::GetCursorPos(&m_hiddenPoint);
    m_entered = false; m_closeHot = false; m_forceReveal = false;
    BOOL animations = TRUE;
    ::SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    if (visible && !m_revealing && m_materialActive && animate && animations && BeginLiveFade()) return;
    if (m_liveSliding) EndLiveSlide(false);
    if (visible && !m_revealing && !MaterialLive() && animate && animations && BeginReveal(true)) return;
    EndReveal(false);
    m_systemMaterial.SetVisible(false);
    if (IsWindowVisible()) ShowWindow(SW_HIDE);
}

void CNativePlaylistPopup::Dismiss()
{
    HideEnabled();
    m_bar->m_nativePopupSelected = false;
    KillTimer(1);
}

void CNativePlaylistPopup::Refresh(const CRect& band, bool show, int dpi, bool controlsVisible)
{
    if (!m_bar->m_pMainFrame->CanHoverControls()) { HideEnabled(false); return; }
    const bool sameLayout = m_band == band && m_dpi == dpi;
    m_band = band;
    if (m_dpi != dpi || !m_titleFont.GetSafeHandle()) {
        m_titleFont.DeleteObject();
        m_titleFont.CreateFont(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    m_dpi = dpi;
    if (!show || m_band.IsRectEmpty()) {
        HideEnabled(!m_band.IsRectEmpty() && !m_bar->m_pMainFrame->IsIconic());
        KillTimer(1);
        return;
    }
    if (m_liveFading) {
        if (!m_forceReveal) return;
        EndLiveFade(); RequestOpen();
    }
    if (m_liveSliding) {
        if (sameLayout) return;
        EndLiveSlide(true);
    }
    Position();
    SetTimer(1, 100, nullptr); // hover/controls observation only; no independent hide timeout
    if (m_fading) {
        if (!WantsReveal()) return;
        EndReveal(false); RequestOpen();
    }
    if (!IsWindowVisible() && !m_revealing && WantsReveal()) RequestOpen();
    if (!IsWindowVisible() && !m_forceReveal && !m_revealing) return;
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
    if (m_revealing) return;
    const bool opening = !IsWindowVisible();
    // The controller supplies its initial brush asynchronously. Keep the HWND
    // hidden until that brush is routed under our clip; never expose a separate
    // full-size backdrop while the foreground is still being prepared.
    if (m_materialActive) {
        if (!m_systemMaterial.Ready()) return;
        if (opening) UpdateMaterialForeground();
    }
    BOOL animations = TRUE;
    ::SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    // A menu command can still hold the menu activity lock on first invocation.
    // It protects visibility, but must not suppress the entrance animation.
    const bool dragBusy = m_bar->m_bDragging || GetTickCount64() < m_dropUntil
        || (capture && (capture == m_hWnd || ::IsChild(m_hWnd, capture)));
    if (opening && !dragBusy && animations
            && (m_materialActive ? BeginLiveSlide() : BeginReveal())) {
        m_forceReveal = false;
        return;
    }
    // Do not repeatedly raise an already visible owned popup over its menu.
    // Fullscreen makes the owner/top-level popup topmost, so HWND_TOP here
    // can cover the native menu on each hover observation tick.
    if (!opening && m_materialActive) m_systemMaterial.SetVisible(true);
    if (opening) {
        m_systemMaterial.SetVisible(true);
        SetWindowPos(&wndTop, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
    if (!m_systemBorder && client.Width() > 1 && client.Height() > 1) {
        Gdiplus::Graphics graphics(dc.GetSafeHdc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const float w = float(client.Width()-1), h = float(client.Height()-1);
        const float diameter = std::min(float(MulDiv(16, m_dpi, 96)), std::min(w, h));
        Gdiplus::GraphicsPath path;
        path.AddArc(.5f, .5f, diameter, diameter, 180, 90);
        path.AddArc(w-diameter+.5f, .5f, diameter, diameter, 270, 90);
        path.AddArc(w-diameter+.5f, h-diameter+.5f, diameter, diameter, 0, 90);
        path.AddArc(.5f, h-diameter+.5f, diameter, diameter, 90, 90);
        path.CloseFigure();
        Gdiplus::Pen border(Gdiplus::Color(255, 60, 60, 60), 1.f);
        graphics.DrawPath(&border, &path);
    }
}

bool CNativePlaylistPopup::BeginLiveFade()
{
    if (!m_systemMaterial.StartFade()) return false;
    // A hide during entrance fades the same moving group without snapping it
    // to its final position. The compositor continues its existing Offset.
    KillTimer(4); m_liveSliding = false;
    m_liveFading = true; m_liveFadeStart = GetTickCount64();
    m_bar->m_listFrame.EnableWindow(FALSE);
    if (!SetTimer(5, 10, nullptr)) EndLiveFade();
    return true;
}

void CNativePlaylistPopup::TickLiveFade()
{
    if (m_liveFading && GetTickCount64() - m_liveFadeStart >= CNativePlaylistMaterial::FadeDurationMs + 20)
        EndLiveFade();
}

void CNativePlaylistPopup::EndLiveFade()
{
    if (!m_liveFading) return;
    KillTimer(5);
    m_systemMaterial.SetVisible(false);
    ShowWindow(SW_HIDE);
    m_liveFading = false;
    if (m_bar->m_listFrame.GetSafeHwnd()) m_bar->m_listFrame.EnableWindow(TRUE);
}

bool CNativePlaylistPopup::BeginLiveSlide()
{
    // The HWND stays at its final position and size for the entire reveal.
    // Prepare native artwork and the material subtree before exposing it.
    UpdateMaterialForeground();
    if (!m_materialActive) return BeginReveal();
    if (!m_systemMaterial.PrepareSlide(static_cast<float>(MulDiv(8, m_dpi, 96)))) return false;
    m_liveSliding = true;
    m_bar->m_listFrame.EnableWindow(FALSE);
    // Submit the initial transform, animation and visibility together before
    // exposing the HWND. Never force a presentation between Show and Start.
    if (!m_systemMaterial.StartSlide()) { EndLiveSlide(false); return false; }
    SetWindowPos(&wndTop, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    m_liveSlideStart = GetTickCount64();
    // The compositor animates independently of WM_TIMER. This timer only
    // restores native interaction after the composition animation.
    if (!SetTimer(4, 16, nullptr)) { EndLiveSlide(true); return false; }
    return true;
}

void CNativePlaylistPopup::TickLiveSlide()
{
    if (m_liveSliding && GetTickCount64() - m_liveSlideStart >= CNativePlaylistMaterial::SlideDurationMs + 20) EndLiveSlide(true);
}

void CNativePlaylistPopup::EndLiveSlide(bool show)
{
    if (!m_liveSliding) return;
    KillTimer(4);
    if (!show) { m_systemMaterial.SetVisible(false); ShowWindow(SW_HIDE); }
    m_systemMaterial.FinishSlide();
    m_liveSliding = false;
    m_bar->m_listFrame.EnableWindow(TRUE);
    if (show) QueueMaterialForeground();
}

bool CNativePlaylistPopup::BeginReveal(bool fading)
{
    GetWindowRect(m_revealRect);
    const int width = m_revealRect.Width(), height = m_revealRect.Height();
    if (width <= 0 || height <= 0) return false;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    m_revealDC = ::CreateCompatibleDC(nullptr);
    m_revealBitmap = ::CreateDIBSection(m_revealDC, &info, DIB_RGB_COLORS,
        reinterpret_cast<void**>(&m_revealBits), nullptr, 0);
    if (!m_revealDC || !m_revealBitmap || !m_revealBits) { EndReveal(false); return false; }
    m_revealOldBitmap = static_cast<HBITMAP>(::SelectObject(m_revealDC, m_revealBitmap));
    CDC* dc = CDC::FromHandle(m_revealDC);
    PaintSurface(*dc);
    // Print only the native list, at its exact final location. Do not ask the
    // window manager to separately animate the live client or its DWM frame.
    CRect list; m_bar->m_list.GetWindowRect(list);
    list.OffsetRect(-m_revealRect.TopLeft());
    const int saved = dc->SaveDC();
    dc->SetViewportOrg(list.TopLeft());
    dc->IntersectClipRect(0, 0, list.Width(), list.Height());
    m_bar->m_list.SendMessage(WM_PRINT, reinterpret_cast<WPARAM>(m_revealDC),
        PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
    dc->RestoreDC(saved);
    const int scrollbarClip = dc->SaveDC();
    dc->SetViewportOrg(list.TopLeft());
    dc->IntersectClipRect(0, 0, list.Width(), list.Height());
    // Native WM_PRINT uses an unthemed scrollbar. Compose the very same narrow
    // scrollbar renderer used by the live list over it before taking the image.
    CRect listWindow, listClient;
    m_bar->m_list.GetWindowRect(listWindow); m_bar->m_list.GetClientRect(listClient);
    m_bar->m_list.ClientToScreen(listClient); listClient.OffsetRect(-listWindow.TopLeft());
    const LONG_PTR listStyle = ::GetWindowLongPtr(m_bar->m_list.m_hWnd, GWL_STYLE);
    const bool hasVertical = !!(listStyle & WS_VSCROLL), hasHorizontal = !!(listStyle & WS_HSCROLL);
    // GetScrollBarInfo reports a hidden parent's bar as invisible. Use its
    // layout/style here so the print image still gets the dark scrollbar.
    CRect vertical(listClient.right, listClient.top, listWindow.Width() - listClient.left, listClient.bottom);
    CRect horizontal(listClient.left, listClient.bottom, listClient.right, listWindow.Height() - listClient.top);
    m_bar->m_list.DrawThemedScrollBars(dc, m_bar->m_list.m_hWnd,
        vertical, horizontal, hasVertical, hasHorizontal, hasVertical && hasHorizontal);
    dc->RestoreDC(scrollbarClip);
    // Explicitly retain the full header even if a child print path changes its clip.
    const int headerClip = dc->SaveDC();
    dc->IntersectClipRect(0, 0, width, MulDiv(32, m_dpi, 96));
    PaintSurface(*dc); dc->RestoreDC(headerClip);
    const double radius = std::min<double>(MulDiv(8, m_dpi, 96), std::min(width, height) / 2.0);
    // Include the border in the image; the live DWM frame stays hidden until handoff.
    {
        Gdiplus::Graphics graphics(m_revealDC);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        const float diameter = static_cast<float>(radius * 2.0);
        const float w = static_cast<float>(width - 1), h = static_cast<float>(height - 1);
        path.AddArc(.5f, .5f, diameter, diameter, 180, 90);
        path.AddArc(w-diameter+.5f, .5f, diameter, diameter, 270, 90);
        path.AddArc(w-diameter+.5f, h-diameter+.5f, diameter, diameter, 0, 90);
        path.AddArc(.5f, h-diameter+.5f, diameter, diameter, 90, 90);
        path.CloseFigure();
        const COLORREF color = BorderColor();
        Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)), 1.f);
        graphics.DrawPath(&pen, &path);
    }
    ::GdiFlush();
    const size_t count = static_cast<size_t>(width) * height;
    m_revealPixels.resize(count); m_revealMask.resize(count);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const double px = x + .5, py = y + .5;
        const double dx = px - std::clamp(px, radius, width - radius);
        const double dy = py - std::clamp(py, radius, height - radius);
        const unsigned alpha = static_cast<unsigned>(255.0 * std::clamp(radius - std::sqrt(dx*dx + dy*dy) + .5, 0.0, 1.0));
        const size_t index = static_cast<size_t>(y) * width + x;
        const DWORD rgb = m_revealBits[index];
        m_revealMask[index] = static_cast<BYTE>(alpha);
        m_revealPixels[index] = (alpha << 24) | (((rgb >> 16 & 255) * alpha / 255) << 16)
            | (((rgb >> 8 & 255) * alpha / 255) << 8) | ((rgb & 255) * alpha / 255);
    }
    if (!m_revealWindow.CreateEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW), nullptr), L"Playlist reveal",
            WS_POPUP, m_revealRect, m_bar->m_pMainFrame, 0)) {
        EndReveal(false); return false;
    }
    m_revealing = true; m_fading = fading; m_revealStart = 0;
    if (!DrawReveal(0.0)) { EndReveal(false); return false; }
    m_revealWindow.SetWindowPos(&wndTop, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (!SetTimer(2, 15, nullptr)) { EndReveal(false); return false; }
    if (fading) {
        // The composed image covers the live frame before it is hidden.
        ::DwmFlush(); ShowWindow(SW_HIDE);
    }
    return true;
}

bool CNativePlaylistPopup::DrawReveal(double progress)
{
    const int width = m_revealRect.Width(), height = m_revealRect.Height();
    const double eased = 1.0 - std::pow(1.0 - std::clamp(progress, 0.0, 1.0), 3.0);
    const int offset = m_fading ? 0 : static_cast<int>(height * (1.0 - eased));
    ::ZeroMemory(m_revealBits, static_cast<size_t>(width) * height * sizeof(DWORD));
    for (int y = offset; y < height; ++y) for (int x = 0; x < width; ++x) {
        const size_t dest = static_cast<size_t>(y) * width + x;
        const DWORD source = m_revealPixels[static_cast<size_t>(y - offset) * width + x];
        const unsigned mask = m_revealMask[dest];
        const unsigned sourceAlpha = source >> 24;
        if (!sourceAlpha) continue;
        const unsigned alpha = std::min(sourceAlpha, mask);
        m_revealBits[dest] = (alpha << 24)
            | (((source >> 16 & 255) * alpha / sourceAlpha) << 16)
            | (((source >> 8 & 255) * alpha / sourceAlpha) << 8) | ((source & 255) * alpha / sourceAlpha);
    }
    POINT dest = m_revealRect.TopLeft(), source = {};
    SIZE size = {width, height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, static_cast<BYTE>(m_fading ? 255.0 * (1.0 - progress) : 255.0), AC_SRC_ALPHA};
    return !!::UpdateLayeredWindow(m_revealWindow.m_hWnd, nullptr, &dest, &size,
        m_revealDC, &source, 0, &blend, ULW_ALPHA);
}

void CNativePlaylistPopup::TickReveal()
{
    if (!m_revealing) return;
    if (!m_revealStart) m_revealStart = GetTickCount64();
    CRect current; GetWindowRect(current);
    if (current != m_revealRect) { EndReveal(!m_fading); return; }
    const double progress = std::min(1.0, (GetTickCount64() - m_revealStart)
        / (m_fading ? static_cast<double>(CNativePlaylistMaterial::FadeDurationMs)
            : static_cast<double>(CNativePlaylistMaterial::SlideDurationMs)));
    if (!DrawReveal(progress) || progress >= 1.0) EndReveal(!m_fading);
}

void CNativePlaylistPopup::EndReveal(bool showLive)
{
    KillTimer(2); m_revealing = false; m_fading = false;
    if (showLive) {
        // Paint the real list while the complete image still covers it, then
        // remove the image. No partially prepared live scrollbar is exposed.
        const CWnd* after = m_revealWindow.GetSafeHwnd() ? &m_revealWindow : &wndTop;
        SetWindowPos(after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        ::DwmFlush();
    }
    if (m_revealWindow.GetSafeHwnd()) m_revealWindow.DestroyWindow();
    if (m_revealDC && m_revealOldBitmap) ::SelectObject(m_revealDC, m_revealOldBitmap);
    if (m_revealBitmap) ::DeleteObject(m_revealBitmap);
    if (m_revealDC) ::DeleteDC(m_revealDC);
    m_revealDC = nullptr; m_revealBitmap = m_revealOldBitmap = nullptr; m_revealBits = nullptr;
    m_revealPixels.clear(); m_revealMask.clear();
}

LRESULT CNativePlaylistPopup::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!m_bar) return __super::WindowProc(message, wParam, lParam);
    switch (message) {
    case WM_NCCALCSIZE: return wParam ? WVR_REDRAW : 0;
    case WM_NCPAINT: return 0;
    case WM_NCACTIVATE: return DefWindowProc(message, wParam, -1);
    case WM_NCHITTEST: return (m_liveSliding || m_liveFading) ? HTTRANSPARENT : HTCLIENT;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_MOVE || (wParam & 0xfff0) == SC_SIZE) return 0;
        break;
    case WM_TIMER:
        if (!m_bar->m_pMainFrame->CanHoverControls()) { HideEnabled(false); return 0; }
        if (wParam == 5) { TickLiveFade(); return 0; }
        if (wParam == 4) { TickLiveSlide(); return 0; }
        if (wParam == 3) {
            KillTimer(3); m_foregroundPending = false;
            if (IsWindowVisible()) UpdateMaterialForeground();
            return 0;
        }
        if (wParam == 2) { TickReveal(); return 0; }
        if (wParam == 1) {
            if (m_bar->nativePopupActivity) m_bar->nativePopupActivity();
            return 0;
        }
        break;
    case WM_NCLBUTTONDBLCLK: return 0;
    case WM_ERASEBKGND: {
        if (m_materialActive) return TRUE;
        if (!wParam) return 0;
        CRect client; GetClientRect(client);
        CDC::FromHandle(reinterpret_cast<HDC>(wParam))->FillSolidRect(client, SurfaceColor());
        return TRUE;
    }
    case WM_PAINT: {
        CPaintDC dc(this);
        if (m_materialActive) { QueueMaterialForeground(); return 0; }
        CRect client; GetClientRect(client);
        if (!m_materialActive || !PaintMaterialBuffer(dc, client,
                [this](HDC buffer) { PaintSurface(*CDC::FromHandle(buffer)); }, static_cast<BYTE>(m_materialTint))) PaintSurface(dc);
        return 0;
    }
    case WM_SIZE:
        if (m_liveSliding) return 0; // retain the prepared full-size native layout
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
    case WM_DWMCOMPOSITIONCHANGED:
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
        ConfigureSurface();
        RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        break;
    case WM_NCDESTROY:
        EndLiveFade();
        EndLiveSlide(false);
        KillTimer(3); m_foregroundPending = false; m_foregroundPixels.clear();
        EndReveal(false);
        m_systemMaterial.Reset();
        if (m_bufferedPaintReady) { ::BufferedPaintUnInit(); m_bufferedPaintReady = false; }
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
    if (!UsesNativePopup() || !m_pMainFrame->CanHoverControls()) return false;
    if (!m_nativePopup.IsWindowVisible()) return m_nativePopup.Revealing() || m_nativePopup.WantsReveal();
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

