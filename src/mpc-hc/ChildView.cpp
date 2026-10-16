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

CChildView::CChildView(CMainFrame* pMainFrame)
    : CMouseWndWithArtView(pMainFrame)
    , m_bSwitchingFullscreen(false)
    , m_bFirstMedia(true)
{
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
            const LPCWSTR tip = nativeToolbar.GetStringFromID(button.idCommand);
            buttons.push_back({static_cast<UINT>(button.idCommand), button.iBitmap, button.fsState, tip ? CString(tip) : CString()});
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
    m_fluentProbe.SetTimeParts(positionText, durationText);
    const CString time = positionText + L" / " + durationText;
    m_fluentProbe.SetControlState(m_pMainFrame->IsFullScreenMode(), false, seekbar.CanOverlaySeek(), toolbarSelected || seekSelected,
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
    ON_WM_NCHITTEST()
    ON_WM_NCLBUTTONDOWN()
END_MESSAGE_MAP()

void CChildView::OnPaint()
{
    CPaintDC dc(this);
    m_pMainFrame->RepaintVideo();
}

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
