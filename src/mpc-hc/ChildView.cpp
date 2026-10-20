/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2015 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "mplayerc.h"
#include "ChildView.h"
#include "MainFrm.h"
#include "CMPCTheme.h"
#include "CMPCThemeUtil.h"
#include "ColorProfileUtil.h"
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
#include "../DSUtil/GeometryDiagnostics.h"
#endif

CChildView::CChildView(CMainFrame* pMainFrame)
    : CMouseWndWithArtView(pMainFrame)
    , m_bSwitchingFullscreen(false)
    , m_bFirstMedia(true)
{
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    m_nativeHeader.customIcon = [this](Gdiplus::Graphics& graphics, const CRect& rect, LPCWSTR name, bool hot) {
        return m_pMainFrame->m_wndToolBar.DrawThemeIcon(graphics, rect, name, hot);
    };
    m_nativeHeader.semiboldTitle = AfxGetApp()->GetProfileInt(L"Settings", L"FluentTitleSemibold", 1) != 0;
    m_nativeHeader.windowCommand = [this](int button) {
        if (button == 0) m_pMainFrame->PostMessage(WM_SYSCOMMAND, SC_MINIMIZE);
        else if (button == 1) {
            if (m_pMainFrame->IsFullScreenMode()) m_pMainFrame->PostMessage(WM_COMMAND, ID_VIEW_FULLSCREEN);
            else m_pMainFrame->PostMessage(WM_SYSCOMMAND, m_pMainFrame->IsZoomed() ? SC_RESTORE : SC_MAXIMIZE);
        } else if (button == 2) m_pMainFrame->PostMessage(WM_CLOSE);
    };
#endif
#ifdef MPC_EXPERIMENTAL_ALPHA_OVERLAY
    m_fluentProbe.timeClick = [this](bool right, CPoint point) {
        if (right) m_pMainFrame->m_wndStatusBar.ShowTimerOptionsMenu(this, point);
        else m_pMainFrame->m_wndStatusBar.ToggleOverlayTime();
        UpdateFluentControls();
    };
    m_fluentProbe.invokeButton = [this](UINT id, const CRect& anchor, bool right) {
        m_pMainFrame->m_wndToolBar.InvokeOverlayButton(id, anchor, right);
    };
    m_fluentProbe.volumeActive = [this]() {
        auto& volume = m_pMainFrame->m_wndToolBar.m_volctrl;
        return volume.OverlayDragging() && ::GetCapture() == volume.GetSafeHwnd();
    };
    m_fluentProbe.volumeMouse = [this](UINT message, WPARAM flags, double fraction) {
        const bool dragging = m_pMainFrame->m_wndToolBar.m_volctrl.ForwardOverlayMouse(message, flags, fraction);
        UpdateFluentControls();
        return dragging;
    };
    m_fluentProbe.seekActive = [this]() { return m_pMainFrame->m_wndSeekBar.DraggingThumb(); };
    m_fluentProbe.seekMouse = [this](UINT message, WPARAM flags, double fraction) {
        return m_pMainFrame->m_wndSeekBar.ForwardOverlayMouse(message, flags, fraction);
    };
#endif
    GetEventd().Connect(m_eventc, {
        MpcEvent::SWITCHING_TO_FULLSCREEN,
        MpcEvent::SWITCHED_TO_FULLSCREEN,
        MpcEvent::SWITCHING_FROM_FULLSCREEN,
        MpcEvent::SWITCHED_FROM_FULLSCREEN,
        MpcEvent::MEDIA_LOADED,
    }, std::bind(&CChildView::EventCallback, this, std::placeholders::_1));
}

CChildView::~CChildView()
{
}

#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
void CChildView::TraceGeometry(const char* tag)
{
    GeometryDiagnostics::Record(tag, m_hWnd);
    GeometryDiagnostics::Record("ui.video-target", m_pMainFrame->m_pVideoWnd ? m_pMainFrame->m_pVideoWnd->GetSafeHwnd() : nullptr);
    GeometryDiagnostics::Record("ui.toolbar", m_pMainFrame->m_wndToolBar.GetSafeHwnd());
    GeometryDiagnostics::Record("ui.seekbar", m_pMainFrame->m_wndSeekBar.GetSafeHwnd());
    GeometryDiagnostics::Record("ui.info", m_pMainFrame->m_wndInfoBar.GetSafeHwnd());
    GeometryDiagnostics::Record("ui.stats", m_pMainFrame->m_wndStatsBar.GetSafeHwnd());
    GeometryDiagnostics::Record("ui.status", m_pMainFrame->m_wndStatusBar.GetSafeHwnd());
    GeometryDiagnostics::Record("ui.playlist", m_pMainFrame->m_wndPlaylistBar.GetSafeHwnd());
    if (m_pMainFrame->m_wndPlaylistBar.GetSafeHwnd())
        GeometryDiagnostics::Record("ui.playlist-dock", ::GetParent(m_pMainFrame->m_wndPlaylistBar.m_hWnd));
}
#endif

void CChildView::EventCallback(MpcEvent ev)
{
    switch (ev) {
        case MpcEvent::SWITCHING_TO_FULLSCREEN:
        case MpcEvent::SWITCHING_FROM_FULLSCREEN:
            m_bSwitchingFullscreen = true;
            break;
        case MpcEvent::SWITCHED_TO_FULLSCREEN:
        case MpcEvent::SWITCHED_FROM_FULLSCREEN:
            m_bSwitchingFullscreen = false;
            break;
        case MpcEvent::MEDIA_LOADED:
            m_bFirstMedia = false;
            break;
        default:
            ASSERT(FALSE);
    }
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    UpdateNativeDecoration();
#endif
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
    char tag[64] = {};
    _snprintf_s(tag, sizeof(tag), _TRUNCATE, "ui.event.%u", static_cast<unsigned>(ev));
    TraceGeometry(tag);
#endif
}

BOOL CChildView::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CWnd::PreCreateWindow(cs)) {
        return FALSE;
    }

    cs.style &= ~WS_BORDER;
    cs.lpszClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
                                       ::LoadCursor(nullptr, IDC_ARROW), HBRUSH(COLOR_WINDOW + 1), nullptr);

    return TRUE;
}

BOOL CChildView::PreTranslateMessage(MSG* pMsg)
{
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    if (pMsg->message == WM_MOUSEMOVE || pMsg->message == WM_MOUSELEAVE)
        m_pMainFrame->m_wndPlaylistBar.UpdateNativeHover();
    if (m_pMainFrame->m_wndPlaylistBar.RouteNativePresentationInput(pMsg)) return TRUE;
#endif
    if (m_pMainFrame->m_wndToolBar.RouteTransparentButtonInput(pMsg)) return TRUE;
    // Keyed toolbar pixels can route movement to the video beneath them.
    // Recognize the complete native mute-button rectangle, not just its ink.
    if (pMsg->message == WM_MOUSEMOVE) m_pMainFrame->m_wndToolBar.UpdateVolumeFlyout(true);
#endif
#ifdef MPC_EXPERIMENTAL_ALPHA_OVERLAY
    UpdateFluentControls();
    if (pMsg->message == WM_MOUSEMOVE) {
        m_fluentProbe.Refresh(this, pMsg->hwnd);
    } else if (pMsg->message == WM_MOUSELEAVE) {
        m_fluentProbe.Refresh(this);
    }
#endif
    // filter interactive video controls mouse messages
    if (pMsg->hwnd != m_hWnd &&
            pMsg->message >= WM_MOUSEFIRST && pMsg->message <= WM_MOUSELAST &&
            m_pMainFrame->IsInteractiveVideo()) {
        switch (pMsg->message) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
                // let them through, interactive video controls will handle those
                break;
            case WM_MOUSEMOVE: {
                // duplicate those
                CPoint point(pMsg->lParam);
                ::MapWindowPoints(pMsg->hwnd, m_hWnd, &point, 1);
                VERIFY(PostMessage(pMsg->message, pMsg->wParam, MAKELPARAM(point.x, point.y)));
                break;
            }
            default: {
                // and handle others in this class
                CPoint point(pMsg->lParam);
                ::MapWindowPoints(pMsg->hwnd, m_hWnd, &point, 1);
                pMsg->lParam = MAKELPARAM(point.x, point.y);
                pMsg->hwnd = m_hWnd;
            }
        }
    }
    return CWnd::PreTranslateMessage(pMsg);
}

#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
bool CChildView::EnsureNativeChromeHosts()
{
    if (m_chromeHosted) return true;
    if (!GetSafeHwnd() || !m_pMainFrame->m_wndSeekBar.GetSafeHwnd()
            || !m_pMainFrame->m_wndToolBar.GetSafeHwnd()
            || m_pMainFrame->HasDedicatedFSVideoWindow()) return false;
    if (!m_topChromeHost.Ensure(this, m_pMainFrame, true)
            || !m_bottomChromeHost.Ensure(this, m_pMainFrame, false)) return false;
    if (!m_bottomChromeHost.Adopt(m_pMainFrame->m_wndSeekBar, m_pMainFrame)
            || !m_bottomChromeHost.Adopt(m_pMainFrame->m_wndToolBar, m_pMainFrame)) {
        m_bottomChromeHost.RestoreControls(); return false;
    }
    if (m_nativeHeader.GetSafeHwnd()) m_nativeHeader.SetParent(m_topChromeHost.Content());
    if (m_nativeBackdrop.GetSafeHwnd()) m_nativeBackdrop.SetParent(m_bottomChromeHost.Content());
    m_chromeHosted = true;
    return true;
}

void CChildView::UpdateNativeDecoration()
{
    if (m_refreshingNativeDecoration || !GetSafeHwnd()) return;
    m_refreshingNativeDecoration = true;
    struct ResetFlag { bool& flag; ~ResetFlag() { flag = false; } } reset{m_refreshingNativeDecoration};
    CRect view; GetClientRect(view);
    const unsigned height = m_pMainFrame->m_controls.GetVisibleToolbarsHeight();
    const bool mediaLoaded = m_pMainFrame->GetLoadState() == MLS::LOADED;
    const bool videoLoaded = mediaLoaded && !m_pMainFrame->m_fAudioOnly;
    const bool available = !m_bSwitchingFullscreen && IsWindowVisible()
        && !m_pMainFrame->IsIconic() && !m_pMainFrame->HasDedicatedFSVideoWindow();
    const bool nativeVisible = m_pMainFrame->m_controls.ToolbarsCoverVideo() && height > 0;
    if (!available || view.IsRectEmpty()) {
        m_pMainFrame->m_wndPlaylistBar.PrepareNativePresentation(false);
        if (m_pMainFrame->m_wndPlaylistBar.UsesNativePopup())
            m_pMainFrame->m_wndPlaylistBar.RefreshNativePopup(CRect(0, 0, 0, 0), false, 96);
        if (m_chromeHosted) {
            m_topChromeHost.Present(false, false); m_bottomChromeHost.Present(false, false);
        }
        m_nativeHeader.Hide(); m_nativeBackdrop.Hide(); return;
    }
    CString title; m_pMainFrame->GetWindowText(title);
    const int dot = title.ReverseFind(L'.');
    if (dot >= 0) {
        CString suffix = title.Mid(dot); suffix.MakeLower();
        for (LPCWSTR extension : {L".mp4", L".mkv", L".avi", L".mov", L".wmv", L".webm", L".m4v",
                L".mpg", L".mpeg", L".ts", L".m2ts", L".mp3", L".flac", L".wav", L".aac", L".ogg", L".m4a"}) {
            if (suffix == extension) { title.Truncate(dot); break; }
        }
    }
    const int dpi = m_pMainFrame->m_dpi.ScaleY(96);
    CRect bottom(view); bottom.top = std::max(view.top, view.bottom - int(height));
    const bool bottomVisible = nativeVisible && (videoLoaded || m_pMainFrame->IsStatusBarForcedForMessage());
    auto& playlist = m_pMainFrame->m_wndPlaylistBar;
    CRect playlistArea(0, 0, 0, 0);
    const bool playlistVisible = playlist.GetSafeHwnd() && playlist.IsWindowVisible() && !playlist.IsFloating();
    // The view owns the actual screen band, including native docking borders.
    // Reserve the selected footer height even while its controls are hidden.
    CRect playlistBand(view);
    playlistBand.top = std::min(view.bottom, LONG(view.top + m_pMainFrame->m_dpi.ScaleY(40)));
    const unsigned footerHeight = std::max(height, m_pMainFrame->m_controls.GetToolbarsHeight());
    playlistBand.bottom = std::max(playlistBand.top, LONG(view.bottom - int(footerHeight)));
    ClientToScreen(playlistBand);
    const bool playlistTransparent = playlist.PrepareNativePresentation(!playlist.UsesNativePopup() && videoLoaded && AppIsThemeLoaded()
        && !playlist.IsFloating() && m_pMainFrame->m_controls.ToolbarsCoverVideo(), playlistBand);
    if (playlistVisible) {
        if (playlistTransparent) playlistArea = playlist.GetNativePresentationRect();
        else playlist.GetParent()->GetWindowRect(playlistArea);
        ScreenToClient(playlistArea);
        playlistArea.IntersectRect(playlistArea, view);
    }
    CRect top(view);
    const int headerHeight = m_pMainFrame->m_dpi.ScaleY(40);
    if (!playlistArea.IsRectEmpty()) {
        // Window commands remain in the unobscured video area, like before.
        if (playlistArea.Width() >= view.Width()) {
            if (playlistArea.top <= view.top) top.top = playlistArea.bottom;
        } else if (playlistArea.top < view.top + headerHeight) {
            if (playlistArea.left <= view.left) top.left = playlistArea.right;
            else if (playlistArea.right >= view.right) top.right = playlistArea.left;
        }
    }
    top.bottom = std::min(bottomVisible ? bottom.top : view.bottom, top.top + headerHeight);
    const bool headerVisible = !mediaLoaded || nativeVisible;
    if (m_chromeHosted) {
        BOOL systemMotion = TRUE; SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &systemMotion, 0);
        const bool animate = mediaLoaded && systemMotion
            && AfxGetApp()->GetProfileInt(L"Settings", L"FluentChromeAnimation", 1) != 0;
        auto& controls = m_pMainFrame->m_controls;
        const int seekHeight = controls.ControlChecked(CMainFrameControls::Toolbar::SEEKBAR)
            ? static_cast<CControlBar&>(m_pMainFrame->m_wndSeekBar).CalcFixedLayout(TRUE, TRUE).cy : 0;
        const int buttonsHeight = controls.ControlChecked(CMainFrameControls::Toolbar::CONTROLS)
            ? static_cast<CControlBar&>(m_pMainFrame->m_wndToolBar).CalcFixedLayout(TRUE, TRUE).cy : 0;
        const unsigned visibleHosted = controls.GetVisibleHostedToolbarsHeight();
        const int otherHeight = static_cast<int>(height - std::min(height, visibleHosted));
        CRect footer(view); footer.bottom -= otherHeight;
        footer.top = std::max(footer.top, footer.bottom - seekHeight - buttonsHeight);
        m_topChromeHost.Layout(top); m_bottomChromeHost.Layout(footer);
        const auto place = [](CWnd& window, const CRect& rect) {
            CRect before; window.GetWindowRect(before);
            window.GetParent()->ScreenToClient(before);
            if (before != rect) window.SetWindowPos(nullptr, rect.left, rect.top, rect.Width(), rect.Height(),
                SWP_NOZORDER | SWP_NOACTIVATE);
        };
        if (seekHeight > 0) place(m_pMainFrame->m_wndSeekBar, CRect(0, 0, footer.Width(), seekHeight));
        if (buttonsHeight > 0) place(m_pMainFrame->m_wndToolBar,
            CRect(0, seekHeight, footer.Width(), seekHeight + buttonsHeight));
        const CRect footerClient(0, 0, footer.Width(), footer.Height());
        m_nativeBackdrop.shadedAreas = {footerClient}; m_nativeBackdrop.headerExclusion.SetRectEmpty();
        if (!footerClient.IsRectEmpty()) {
            m_nativeBackdrop.Refresh(m_bottomChromeHost.Content(), footerClient, false, CString(), dpi);
            if (::GetWindow(m_nativeBackdrop.m_hWnd, GW_HWNDNEXT))
                m_nativeBackdrop.SetWindowPos(&wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        m_nativeHeader.Refresh(m_topChromeHost.Content(), CRect(0, 0, top.Width(), top.Height()), true,
            mediaLoaded ? title : CString(), dpi, m_pMainFrame->IsFullScreenMode() || m_pMainFrame->IsZoomed());
        m_topChromeHost.Present(headerVisible, animate);
        m_bottomChromeHost.Present(bottomVisible && visibleHosted > 0, animate);
    } else {
        m_nativeBackdrop.shadedAreas.clear();
        if (bottomVisible) m_nativeBackdrop.shadedAreas.push_back(bottom);
        if (playlistTransparent && !playlistArea.IsRectEmpty()) m_nativeBackdrop.shadedAreas.push_back(playlistArea);
        m_nativeBackdrop.headerExclusion = headerVisible ? top : CRect(0, 0, 0, 0);
        if (!m_nativeBackdrop.shadedAreas.empty()) m_nativeBackdrop.Refresh(this, view, false, CString(), dpi);
        else m_nativeBackdrop.Hide();
        if (headerVisible) m_nativeHeader.Refresh(this, top, true,
            mediaLoaded ? title : CString(), dpi, m_pMainFrame->IsFullScreenMode() || m_pMainFrame->IsZoomed());
        else m_nativeHeader.Hide();
    }
    if (playlist.UsesNativePopup()) {
        // Fill the stable band between caption and full footer, even while
        // those controls are hidden. Position adds the same 12-DIP margins.
        CRect flyoutBand(playlistBand);
        playlist.nativePopupActivity = [this]() { m_pMainFrame->m_controls.UpdateToolbarsVisibility(); };
        playlist.RefreshNativePopup(flyoutBand, mediaLoaded, dpi, nativeVisible);
    }
}
#endif

#ifdef MPC_EXPERIMENTAL_ALPHA_OVERLAY
void CChildView::UpdateFluentControls()
{
    if (m_updatingFluent || !GetSafeHwnd() || !m_pMainFrame->m_wndToolBar.GetSafeHwnd()
            || !m_pMainFrame->m_wndSeekBar.GetSafeHwnd()) return;
    m_updatingFluent = true;
    struct ResetFlag { bool& flag; ~ResetFlag() { flag = false; } } reset{m_updatingFluent};
    if (!m_pMainFrame->m_OSD.overlayMessage) {
        m_pMainFrame->m_OSD.overlayMessage = [this](LPCWSTR message, int duration) {
            return m_fluentProbe.ShowFeedback(this, message, duration);
        };
    }
    auto& nativeToolbar = m_pMainFrame->m_wndToolBar;
    auto& toolbar = nativeToolbar.GetToolBarCtrl();
    auto& seekbar = m_pMainFrame->m_wndSeekBar;
    // Keep hidden native input geometry usable without allowing it to cover video.
    CRect viewRect; GetClientRect(viewRect);
    for (CWnd* backend : {static_cast<CWnd*>(&nativeToolbar), static_cast<CWnd*>(&seekbar)}) {
        CRect rect; backend->GetClientRect(rect);
        if (rect.Width() != viewRect.Width() || rect.Height() < 24)
            backend->SetWindowPos(nullptr, 0, 0, viewRect.Width(), std::max(32, rect.Height()), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    nativeToolbar.UpdateOverlayCommandState();
    const bool toolbarSelected = m_pMainFrame->m_controls.ControlChecked(CMainFrameControls::Toolbar::CONTROLS);
    const bool seekSelected = m_pMainFrame->m_controls.ControlChecked(CMainFrameControls::Toolbar::SEEKBAR);
    std::vector<CFluentOverlayProbe::Button> buttons;
    if (toolbarSelected) {
        for (int i = 0; i < toolbar.GetButtonCount(); ++i) {
            TBBUTTON button = {};
            if (!toolbar.GetButton(i, &button) || (button.fsState & TBSTATE_HIDDEN) || (button.fsStyle & BTNS_SEP)) continue;
            if (button.idCommand == ID_VOLUME_MUTE) button.iBitmap = nativeToolbar.OverlayVolumeImage();
            const CString tip = nativeToolbar.OverlayButtonTip(button.idCommand);
            buttons.push_back({static_cast<UINT>(button.idCommand), button.iBitmap, button.fsState, tip});
        }
    }
    auto& volume = nativeToolbar.m_volctrl;
    int minimum = 0, maximum = 100; volume.GetRange(minimum, maximum);
    CImageList* images = toolbar.GetImageList();
    const bool volumeSelected = toolbarSelected && volume.GetSafeHwnd() && (volume.GetStyle() & WS_VISIBLE);
    m_fluentProbe.SetNativeLayout(std::move(buttons), images ? images->GetSafeHandle() : nullptr,
        toolbarSelected, seekSelected, volumeSelected, volume.GetPos() - minimum, maximum - minimum, AfxGetAppSettings().nToolbarAlignment);
    REFERENCE_TIME start, stop; seekbar.GetRange(start, stop);
    CString positionText, durationText;
    m_pMainFrame->m_wndStatusBar.GetOverlayTimeParts(positionText, durationText);
    m_fluentProbe.SetTimeParts(positionText, durationText, AfxGetAppSettings().fRemainingTime);
    const CString time = positionText + L" / " + durationText;
    m_fluentProbe.SetControlState(m_pMainFrame->IsFullScreenMode(), AfxGetAppSettings().fShowOSD, seekbar.CanOverlaySeek(), toolbarSelected || seekSelected,
        start, stop, seekbar.GetPos(), time);
    m_fluentProbe.Refresh(this);
}

bool CChildView::RouteFluentInput(const MSG& message)
{
    if (message.message == WM_MOUSELEAVE || message.message == WM_NCMOUSELEAVE
            || message.message == WM_NCMOUSEMOVE) {
        UpdateFluentControls();
        return false;
    }
    if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
        m_fluentProbe.Activity(message);
        UpdateFluentControls();
        return false;
    }
    if (message.message < WM_MOUSEFIRST || message.message > WM_MOUSELAST) return false;
    // Include movement over the original toolbar: leaving video is still a leave.
    UpdateFluentControls();
    const HWND seekbar = m_pMainFrame->m_wndSeekBar.GetSafeHwnd();
    const HWND volume = m_pMainFrame->m_wndToolBar.m_volctrl.GetSafeHwnd();
    if (message.hwnd != m_hWnd && !::IsChild(m_hWnd, message.hwnd)
            && !(message.hwnd == seekbar && m_pMainFrame->m_wndSeekBar.DraggingThumb())
            && !(message.hwnd == volume && m_pMainFrame->m_wndToolBar.m_volctrl.OverlayDragging())) return false;
    m_fluentProbe.Activity(message);
    UpdateFluentControls();
    return m_fluentProbe.RouteInput(message);
}

LRESULT CChildView::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_CAPTURECHANGED) m_fluentProbe.CancelPress();
    if (message == WM_MOUSELEAVE) {
        m_fluentProbe.Refresh(this);
    } else if (message == WM_SIZE || message == WM_DESTROY || message == WM_SHOWWINDOW) {
        m_fluentProbe.Hide();
    }
    return __super::WindowProc(message, wParam, lParam);
}
#endif

IMPLEMENT_DYNAMIC(CChildView, CMouseWndWithArtView)

BEGIN_MESSAGE_MAP(CChildView, CMouseWndWithArtView)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    ON_WM_WINDOWPOSCHANGING()
#endif
    ON_WM_NCHITTEST()
    ON_WM_NCLBUTTONDOWN()
END_MESSAGE_MAP()

void CChildView::OnPaint()
{
    CPaintDC dc(this);
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    // The initial frame layout may run before the child is visible. Ensure
    // empty-window commands are present at its first real paint as well.
    if (m_pMainFrame->GetLoadState() != MLS::LOADED) UpdateNativeDecoration();
#endif
    m_pMainFrame->RepaintVideo();
}

#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
void CChildView::OnWindowPosChanging(WINDOWPOS* position)
{
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
    const RECT requested = {position->x, position->y,
        position->x + position->cx, position->y + position->cy};
#endif
    if (m_pMainFrame->m_nativeVideoLayout && !(position->flags & SWP_NOSIZE)) {
        unsigned top = 0, left = 0, right = 0, bottom = 0;
        unsigned toolbarHeight = 0;
        unsigned hostedOverlap = 0;
        if (!m_pMainFrame->HasDedicatedFSVideoWindow()
                && m_pMainFrame->m_controls.ToolbarsCoverVideo()) {
            toolbarHeight = m_pMainFrame->m_controls.GetVisibleToolbarsHeight();
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
            // Hosted bars are no longer direct frame children and therefore
            // consume no native client layout space that the view must cover.
            if (m_chromeHosted) {
                hostedOverlap = std::min(toolbarHeight, m_pMainFrame->m_controls.GetVisibleHostedToolbarsHeight());
                toolbarHeight -= hostedOverlap;
            }
#endif
            bottom = toolbarHeight;
            const auto& playlist = m_pMainFrame->m_wndPlaylistBar;
            const bool playlistVisible = playlist.GetSafeHwnd() && playlist.IsWindowVisible()
                && !playlist.IsFloating() && m_pMainFrame->GetLoadState() == MLS::LOADED;
            if (m_pMainFrame->m_controls.PanelsCoverVideo() || playlistVisible) {
                m_pMainFrame->m_controls.GetVisibleDockZones(top, left, right, bottom);
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
                if (m_chromeHosted) bottom -= std::min(bottom,
                    m_pMainFrame->m_controls.GetVisibleHostedToolbarsHeight());
#endif
            }
        }
        // Extend the real video HWND across dock zones in this native layout
        // transaction, before WM_SIZE updates the renderer. No stale strip is
        // left beneath full-width controls when a side panel consumes width.
        position->x -= left; position->y -= top;
        position->cx += left + right; position->cy += top + bottom;
        // Both already-covered areas count for video/aspect calculations:
        // hosted controls are inside the view; other bars use the extension.
        m_pMainFrame->m_nativeToolbarOverlapHeight = toolbarHeight + hostedOverlap;
        m_pMainFrame->m_nativeDockOverlap = CRect(left, top, right, bottom + hostedOverlap);
        if (left || top) position->flags &= ~SWP_NOMOVE;
        if (left || top || right || bottom) {
            position->hwndInsertAfter = HWND_BOTTOM;
            position->flags &= ~SWP_NOZORDER;
        }
    }
    // Do not preserve pixels from the old-sized video surface.
    CRect before;
    GetWindowRect(before);
    if (CWnd* parent = GetParent()) parent->ScreenToClient(before);
    const bool resized = !(position->flags & SWP_NOSIZE)
        && (position->cx != before.Width() || position->cy != before.Height());
    const bool moved = !(position->flags & SWP_NOMOVE)
        && (position->x != before.left || position->y != before.top);
    if (resized || moved) position->flags |= SWP_NOCOPYBITS;
    __super::OnWindowPosChanging(position);
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
    if (!(position->flags & SWP_NOSIZE)) {
        const RECT applied = {position->x, position->y,
            position->x + position->cx, position->y + position->cy};
        GeometryDiagnostics::Record(m_pMainFrame->m_nativeVideoLayout
            ? "layout.pane" : "window.pane", m_hWnd, &requested, &applied);
    }
#endif
}
#endif

void CChildView::OnSize(UINT nType, int cx, int cy)
{
    __super::OnSize(nType, cx, cy);
    if (!m_bSwitchingFullscreen) {
        m_pMainFrame->MoveVideoWindow();
    }
    // bitmap OSD draws into the renderer, so it never gets its own WM_SIZE
    if (m_pMainFrame->m_OSD.GetOSDType() == OSD_TYPE_BITMAP && m_pMainFrame->m_pVideoWnd == this) {
        m_pMainFrame->m_OSD.OnSize(nType, cx, cy);
    }
    m_pMainFrame->UpdateThumbnailClip();
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    UpdateNativeDecoration();
#endif
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
    TraceGeometry("ui.after-size");
#endif
}

LRESULT CChildView::OnNcHitTest(CPoint point)
{
    LRESULT ret = CWnd::OnNcHitTest(point);
    if (!m_pMainFrame->IsFullScreenMainFrame() && m_pMainFrame->IsFrameLessWindow()) {
        CRect rcFrame;
        GetWindowRect(&rcFrame);
        CRect rcClient(rcFrame);
        rcClient.InflateRect(-GetSystemMetrics(SM_CXSIZEFRAME), -GetSystemMetrics(SM_CYSIZEFRAME));

        if (rcFrame.PtInRect(point)) {
            if (point.x > rcClient.right) {
                if (point.y < rcClient.top) {
                    ret = HTTOPRIGHT;
                } else if (point.y > rcClient.bottom) {
                    ret = HTBOTTOMRIGHT;
                } else {
                    ret = HTRIGHT;
                }
            } else if (point.x < rcClient.left) {
                if (point.y < rcClient.top) {
                    ret = HTTOPLEFT;
                } else if (point.y > rcClient.bottom) {
                    ret = HTBOTTOMLEFT;
                } else {
                    ret = HTLEFT;
                }
            } else if (point.y < rcClient.top) {
                ret = HTTOP;
            } else if (point.y > rcClient.bottom) {
                ret = HTBOTTOM;
            }
        }
    }
    return ret;
}

void CChildView::OnNcLButtonDown(UINT nHitTest, CPoint point)
{
    BYTE flag = 0;
    switch (nHitTest) {
        case HTTOP:
            flag = WMSZ_TOP;
            break;
        case HTTOPLEFT:
            flag = WMSZ_TOPLEFT;
            break;
        case HTTOPRIGHT:
            flag = WMSZ_TOPRIGHT;
            break;
        case HTLEFT:
            flag = WMSZ_LEFT;
            break;
        case HTRIGHT:
            flag = WMSZ_RIGHT;
            break;
        case HTBOTTOM:
            flag = WMSZ_BOTTOM;
            break;
        case HTBOTTOMLEFT:
            flag = WMSZ_BOTTOMLEFT;
            break;
        case HTBOTTOMRIGHT:
            flag = WMSZ_BOTTOMRIGHT;
            break;
    }
    if (flag) {
        m_pMainFrame->SendMessage(WM_SYSCOMMAND, SC_SIZE | flag, MAKELPARAM(point.x, point.y));
    }
}

BOOL CChildView::OnEraseBkgnd(CDC* pDC) {
    return __super::OnEraseBkgnd(pDC);
}
