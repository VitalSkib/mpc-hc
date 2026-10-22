/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2017 see Authors.txt
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
#include <cmath>
#include <afxinet.h>
#include "mplayerc.h"
#include "MainFrm.h"
#include "DSUtil.h"
#include "SaveTextFileDialog.h"
#include "PlayerPlaylistBar.h"
#include "SettingsDefines.h"
#include "InternalFiltersConfig.h"
#include "PathUtils.h"
#include "WinAPIUtils.h"
#include "CMPCTheme.h"
#include "CMPCThemeUtil.h"
#include "CoverArt.h"
#include "FileHandle.h"
#include "MediaInfo/MediaInfoDLL.h"

#undef SubclassWindow


// CPlaylistListFrame — thin border container; forwards owner-draw and input messages to bar

BEGIN_MESSAGE_MAP(CPlaylistListFrame, CWnd)
    ON_WM_NCPAINT()
END_MESSAGE_MAP()

void CPlaylistListFrame::OnNcPaint()
{
    bool win11Style = AppIsThemeLoaded() && CMPCTheme::isWindows11Style;
    if (AppNeedsThemedControls() || win11Style) {
        CWindowDC dc(this);
        CRect wr;
        GetWindowRect(&wr);
        wr.OffsetRect(-wr.left, -wr.top);
        CPoint clientOffset = CMPCThemeUtil::GetClientRectOffset(this);
        CRect clip = wr;
        clip.DeflateRect(clientOffset.x, clientOffset.x);
        dc.ExcludeClipRect(clip);
        dc.FillSolidRect(wr, CMPCTheme::ContentBGColor);
        if (!win11Style) { //windows 11 panes don't frame their lists
            CBrush brush(CMPCTheme::WindowBorderColorLight);
            dc.FrameRect(wr, &brush);
        }
    } else {
        __super::OnNcPaint();
    }
}

LRESULT CPlaylistListFrame::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    if (nativeSurface && (message == WM_PAINT || message == WM_NCPAINT)) {
        auto popup = static_cast<CNativePlaylistPopup*>(GetParent());
        if (popup && popup->MaterialActive()) {
            // Composition owns the entire visible panel. This HWND supplies
            // layout/input only; even a translucent GDI fill here creates a
            // separate stationary rectangle when the panel moves away.
            if (message == WM_PAINT) { CPaintDC dc(this); }
            popup->QueueMaterialForeground();
            return 0;
        }
    }
    if (message == WM_ERASEBKGND && wParam && nativeSurface) {
        // The native list is briefly hidden by WM_SETREDRAW during layout.
        // Its container must clear to the same key, never an uninitialized black surface.
        auto popup = static_cast<CNativePlaylistPopup*>(GetParent());
        if (popup && popup->MaterialActive()) return TRUE;
        CRect rect; GetClientRect(rect);
        CDC::FromHandle(reinterpret_cast<HDC>(wParam))->FillSolidRect(rect, CNativePlaylistPopup::SurfaceColor());
        return TRUE;
    }
#endif
    if (CWnd* pBar = GetParent()) {
        switch (message) {
        case WM_NOTIFY:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
            return pBar->SendMessage(message, wParam, lParam);
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK: {
            CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            MapWindowPoints(pBar, &pt, 1);
            return pBar->SendMessage(message, wParam, MAKELPARAM(pt.x, pt.y));
        }
        }
    }
    return __super::WindowProc(message, wParam, lParam);
}


IMPLEMENT_DYNAMIC(CPlayerPlaylistBar, CMPCThemePlayerBar)

#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
static void RefreshChromeHeader(CMainFrame* frame)
{
    if (const auto view = DYNAMIC_DOWNCAST(CChildView, frame->GetDescendantWindow(AFX_IDW_PANE_FIRST)))
        view->UpdateNativeDecoration();
}
#endif
CPlayerPlaylistBar::CPlayerPlaylistBar(CMainFrame* pMainFrame)
    : CMPCThemePlayerBar(pMainFrame)
    , m_pMainFrame(pMainFrame)
    , m_list(true)
    , m_nTimeColWidth(0)
    , m_pDragImage(nullptr)
    , m_bDragging(FALSE)
    , m_nDragIndex(0)
    , m_nDropIndex(0)
    , m_bHiddenDueToFullscreen(false)
    , m_pl(AfxGetAppSettings().bShufflePlaylistItems)
    , createdWindow(false)
    , inlineEditXpos(0)
    , inlineEditItem(-1)
    , m_tcLastSave(0)
    , m_SaveDelayed(false)
    , m_insertingPos(nullptr)
{
    GetEventd().Connect(m_eventc, {
        MpcEvent::DPI_CHANGED,
    }, std::bind(&CPlayerPlaylistBar::EventCallback, this, std::placeholders::_1));
}

CPlayerPlaylistBar::~CPlayerPlaylistBar()
{
}

BOOL CPlayerPlaylistBar::Create(CWnd* pParentWnd, UINT defDockBarID)
{
    if (!__super::Create(ResStr(IDS_PLAYLIST_CAPTION), pParentWnd, ID_VIEW_PLAYLIST, defDockBarID, _T("Playlist"))) {
        return FALSE;
    }

    m_listFrame.CreateEx(
        WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME, AfxRegisterWndClass(0), nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        CRect(0, 0, 100, 100), this, 0);

    m_list.CreateEx(
        0,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP
        | LVS_OWNERDRAWFIXED
        | LVS_OWNERDATA
        | LVS_EDITLABELS
        | LVS_NOCOLUMNHEADER
        | LVS_REPORT | LVS_SINGLESEL | LVS_AUTOARRANGE | LVS_NOSORTHEADER, // TODO: remove LVS_SINGLESEL and implement multiple item repositioning (dragging is ready)
        CRect(0, 0, 100, 100), &m_listFrame, IDC_PLAYLIST);

    m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    ::SetWindowSubclass(m_list.m_hWnd, NativeListProcedure, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this));
#endif
    SetListBkColor();

    // The column titles don't have to be translated since they aren't displayed anyway
    m_list.InsertColumn(COL_NAME, _T("Name"), LVCFMT_LEFT);

    m_list.InsertColumn(COL_TIME, _T("Time"), LVCFMT_RIGHT);

    ScaleFont();

    m_fakeImageList.Create(1, 16, ILC_COLOR4, 10, 10);
    m_list.SetImageList(&m_fakeImageList, LVSIL_SMALL);

    m_dropTarget.Register(this);

	createdWindow = true;

    return TRUE;
}

BOOL CPlayerPlaylistBar::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!__super::PreCreateWindow(cs)) {
        return FALSE;
    }

    cs.dwExStyle |= WS_EX_ACCEPTFILES;

    return TRUE;
}

#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
#include "NativePlaylistPopupImpl.h"

bool CPlayerPlaylistBar::PrepareNativePresentation(bool enabled, const CRect& band)
{
    if (!GetSafeHwnd()) return false;
    const bool active = m_nativeDock.Update(*this, enabled);
    const int dockId = GetParent() ? GetParent()->GetDlgCtrlID() : 0;
    const bool side = active && (dockId == AFX_IDW_DOCKBAR_LEFT || dockId == AFX_IDW_DOCKBAR_RIGHT);
    CRect bounds; GetWindowRect(bounds);
    const int windowHeight = std::max(0, bounds.Height());
    const int top = side ? std::clamp<int>(band.top - bounds.top, 0, windowHeight) : 0;
    const int bottom = side ? std::clamp<int>(bounds.bottom - band.bottom, 0, windowHeight - top) : 0;
    if (top != m_nativeTopInset || bottom != m_nativeBottomInset) {
        m_nativeTopInset = top; m_nativeBottomInset = bottom;
        // Recalculate native client/nonclient areas, without moving the docked bar
        // or feeding a shorter outer size back into MFC's docking negotiation.
        SetWindowPos(nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        ResizeListColumn();
    }
    CRect clip(0, top, bounds.Width(), windowHeight - bottom);
    if (side) {
        if (!m_nativeClipped || clip != m_nativeClip) {
            HRGN region = ::CreateRectRgnIndirect(&clip);
            if (region) {
                if (::SetWindowRgn(m_hWnd, region, TRUE)) { m_nativeClip = clip; m_nativeClipped = true; }
                else ::DeleteObject(region);
            }
        }
    } else if (m_nativeClipped) {
        ::SetWindowRgn(m_hWnd, nullptr, TRUE); m_nativeClip.SetRectEmpty(); m_nativeClipped = false;
    }
    if (active) m_nativeDock.ClipTo(GetNativePresentationRect());
    UpdateNativeHover();
    return active;
}

LRESULT CPlayerPlaylistBar::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (UsesNativePopup() && message == WM_WINDOWPOSCHANGING) {
        auto position = reinterpret_cast<WINDOWPOS*>(lParam);
        if (position->flags & SWP_SHOWWINDOW) {
            position->flags &= ~SWP_SHOWWINDOW;
            position->flags |= SWP_HIDEWINDOW;
        }
    }
    if (m_nativeDock.Active()) {
        // The docked overlay is fixed; retain native list input, sizing and close.
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK
                || message == WM_NCLBUTTONDBLCLK) return 0;
        if (message == WM_NCPAINT) {
            CWindowDC dc(this);
            CRect window, client; GetWindowRect(window); GetClientRect(client);
            ClientToScreen(client); client.OffsetRect(-window.TopLeft());
            window.OffsetRect(-window.TopLeft());
            dc.ExcludeClipRect(client);
            dc.FillSolidRect(window, CMPCTheme::ContentBGColor);
            if (HasGripper()) paintHideButton(&dc, m_biHide);
            return 0;
        }
    }
    return __super::WindowProc(message, wParam, lParam);
}

CRect CPlayerPlaylistBar::GetNativePresentationRect() const
{
    CRect rect; GetWindowRect(rect);
    if (m_nativeClipped) {
        const CPoint origin = rect.TopLeft(); rect = m_nativeClip; rect.OffsetRect(origin);
    }
    return rect;
}

void CPlayerPlaylistBar::NcCalcClient(LPRECT rect, UINT dockId)
{
    __super::NcCalcClient(rect, dockId);
    if (!m_nativeDock.Active()) return;
    rect->top = std::min(rect->bottom, rect->top + m_nativeTopInset);
    rect->bottom = std::max(rect->top, rect->bottom - m_nativeBottomInset);
    if (m_nativeTopInset && HasGripper()) {
        CRect close = m_biHide.GetRect(); close.OffsetRect(0, m_nativeTopInset);
        m_biHide.Move(close.TopLeft());
    }
}

void CPlayerPlaylistBar::UpdateNativeHover()
{
    if (!::IsWindow(m_list.GetSafeHwnd())) { m_nativeHotItem = -1; return; }
    int hot = -1;
    POINT cursor;
    const bool visible = UsesNativePopup() ? m_nativePopup.IsWindowVisible() : IsWindowVisible();
    CRect bounds;
    if (UsesNativePopup()) m_nativePopup.GetWindowRect(bounds);
    else bounds = GetNativePresentationRect();
    if ((UsesNativePopup() || m_nativeDock.Active()) && visible && ::GetCursorPos(&cursor)
            && bounds.PtInRect(cursor)) {
        m_list.ScreenToClient(&cursor);
        UINT flags = 0; hot = m_list.HitTest(cursor, &flags);
        if (!(flags & LVHT_ONITEM)) hot = -1;
    }
    if (hot == m_nativeHotItem) return;
    const int previous = m_nativeHotItem; m_nativeHotItem = hot;
    if (previous >= 0 && previous < m_list.GetItemCount()) m_list.RedrawItems(previous, previous);
    if (hot >= 0) m_list.RedrawItems(hot, hot);
}

LRESULT CALLBACK CPlayerPlaylistBar::NativeListProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR id, DWORD_PTR data)
{
    auto bar = reinterpret_cast<CPlayerPlaylistBar*>(data);
    if (message == WM_NCDESTROY) ::RemoveWindowSubclass(window, NativeListProcedure, id);
    else if ((message == WM_PAINT || message == WM_NCPAINT) && bar->UsesNativePopup()
            && bar->m_nativePopup.MaterialActive()) {
        if (bar->m_nativePopup.PaintMaterialList()) {
            if (message == WM_PAINT) {
                PAINTSTRUCT paint{};
                ::BeginPaint(window, &paint); ::EndPaint(window, &paint);
            }
            return 0;
        }
    }
    else if (message == WM_ERASEBKGND && bar->UsesNativePopup() && wparam) {
        if (bar->m_nativePopup.MaterialActive()) return TRUE;
        // The themed list's EraseBkgnd hardcodes ContentBGColor below the rows.
        // Paint this popup's actual background/key instead, including the empty area.
        RECT rect; ::GetClientRect(window, &rect);
        HBRUSH brush = ::CreateSolidBrush(CNativePlaylistPopup::SurfaceColor());
        ::FillRect(reinterpret_cast<HDC>(wparam), &rect, brush); ::DeleteObject(brush);
        return TRUE;
    } else if (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE) {
        bar->UpdateNativeHover();
        if (bar->UsesNativePopup() && bar->nativePopupActivity) bar->nativePopupActivity();
        if (message == WM_MOUSEMOVE && (bar->UsesNativePopup() || bar->m_nativeDock.Active())) {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0}; ::TrackMouseEvent(&track);
        }
    }
    const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
    // Read-only native queries (including dialog navigation and item queries)
    // must never enqueue a composition update. They also run while pumping it.
    if (bar->UsesNativePopup() && bar->m_nativePopup.MaterialActive()) {
        switch (message) {
        case WM_MOUSEMOVE: case WM_MOUSELEAVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
        case WM_MOUSEWHEEL: case WM_VSCROLL: case WM_HSCROLL:
        case WM_SIZE: case WM_SETFOCUS: case WM_KILLFOCUS:
        case LVM_SETITEMSTATE: case LVM_SETITEMCOUNT: case LVM_SETITEMTEXT:
        case LVM_SETITEM: case LVM_INSERTITEM: case LVM_DELETEITEM:
        case LVM_DELETEALLITEMS: case LVM_REDRAWITEMS: case LVM_ENSUREVISIBLE:
        case LVM_SCROLL:
            bar->m_nativePopup.QueueMaterialForeground(); break;
        }
    }
    if (message == WM_MOUSEWHEEL || message == WM_VSCROLL || message == WM_HSCROLL) bar->UpdateNativeHover();
    return result;
}

bool CPlayerPlaylistBar::RouteNativePresentationInput(MSG* message)
{
    // Only the legacy transparent dock needs geometry-based input routing.
    if (!m_nativeDock.Active() || !IsWindowVisible() || ::GetCapture()
            || message->hwnd == m_hWnd || ::IsChild(m_hWnd, message->hwnd)) return false;
    const UINT event = message->message;
    if (event != WM_MOUSEMOVE && event != WM_LBUTTONDOWN && event != WM_LBUTTONUP
            && event != WM_LBUTTONDBLCLK && event != WM_RBUTTONDOWN && event != WM_RBUTTONUP
            && event != WM_MOUSEWHEEL) return false;
    CRect bounds;
    bounds = GetNativePresentationRect();
    if (!bounds.PtInRect(message->pt)) return false;
    HWND target = m_hWnd;
    while (true) {
        POINT point = message->pt; ::ScreenToClient(target, &point);
        RECT client; ::GetClientRect(target, &client);
        if (!::PtInRect(&client, point)) break;
        HWND child = ::ChildWindowFromPointEx(target, point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
        if (!child || child == target) break;
        target = child;
    }
    if (target == message->hwnd) return false;
    const LRESULT hit = ::SendMessage(target, WM_NCHITTEST, 0, MAKELPARAM(message->pt.x, message->pt.y));
    if (event == WM_MOUSEWHEEL) ::SendMessage(target, event, message->wParam, message->lParam);
    else if (hit == HTCLIENT) {
        POINT point = message->pt; ::ScreenToClient(target, &point);
        ::SendMessage(target, event, message->wParam, MAKELPARAM(point.x, point.y));
    } else {
        UINT nonclient = event == WM_MOUSEMOVE ? WM_NCMOUSEMOVE : event == WM_LBUTTONDOWN ? WM_NCLBUTTONDOWN
            : event == WM_LBUTTONUP ? WM_NCLBUTTONUP : event == WM_LBUTTONDBLCLK ? WM_NCLBUTTONDBLCLK
            : event == WM_RBUTTONDOWN ? WM_NCRBUTTONDOWN : WM_NCRBUTTONUP;
        ::SendMessage(target, nonclient, hit, MAKELPARAM(message->pt.x, message->pt.y));
    }
    return true;
}
#endif

BOOL CPlayerPlaylistBar::PreTranslateMessage(MSG* pMsg)
{
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    if (pMsg->message == WM_MOUSEMOVE || pMsg->message == WM_MOUSELEAVE) UpdateNativeHover();
#endif
    bool nativeVisible = IsVisible();
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    nativeVisible = nativeVisible || (UsesNativePopup() && m_nativePopup.IsWindowVisible());
#endif
    if (IsWindow(pMsg->hwnd) && nativeVisible && pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST) {
        if (GetKeyState(VK_MENU) & 0x8000) {
            if (TranslateAccelerator(m_pMainFrame->GetSafeHwnd(), m_pMainFrame->GetDefaultAccelerator(), pMsg)) {
                return TRUE;
            }
        }
        if (
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
            UsesNativePopup() ? m_nativePopup.IsDialogMessage(pMsg) :
#endif
            IsDialogMessage(pMsg)) {
            return TRUE;
        }
    }

    return __super::PreTranslateMessage(pMsg);
}

void CPlayerPlaylistBar::ReloadTranslatableResources()
{
    SetWindowText(ResStr(IDS_PLAYLIST_CAPTION));
}

void CPlayerPlaylistBar::LoadState(CFrameWnd* pParent)
{
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    if (m_nativePopup.Create(pParent, this)) {
        CString section = _T("ToolBars\\") + m_strSettingName;
        m_nativePopupSelected = !!AfxGetApp()->GetProfileInt(section, _T("Visible"), FALSE);
        if (m_nativePopupSelected) m_nativePopup.RequestOpen();
        SetAutohidden(false);
        pParent->ShowControlBar(this, FALSE, TRUE);
        m_listFrame.ModifyStyleEx(WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME, 0, SWP_FRAMECHANGED);
        m_listFrame.SetParent(&m_nativePopup);
        m_nativePopup.ConfigureSurface();
        ScaleFont();
        SetListBkColor();
        m_dropTarget.Revoke();
        m_dropTarget.Register(&m_nativePopup);
        return;
    }
#endif
    CString section = _T("ToolBars\\") + m_strSettingName;

    if (AfxGetApp()->GetProfileInt(section, _T("Visible"), FALSE)) {
        SetAutohidden(true);
    }

    __super::LoadState(pParent);
}

void CPlayerPlaylistBar::SaveState()
{
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    if (UsesNativePopup()) {
        CString section = _T("ToolBars\\") + m_strSettingName;
        AfxGetApp()->WriteProfileInt(section, _T("Visible"), m_nativePopupSelected);
        return;
    }
#endif
    __super::SaveState();

    CString section = _T("ToolBars\\") + m_strSettingName;

    AfxGetApp()->WriteProfileInt(section, _T("Visible"),
                                 IsWindowVisible() || (AfxGetAppSettings().bHidePlaylistFullScreen && m_bHiddenDueToFullscreen) || IsAutohidden());
}

bool CPlayerPlaylistBar::IsHiddenDueToFullscreen() const
{
    return m_bHiddenDueToFullscreen;
}

void CPlayerPlaylistBar::SetHiddenDueToFullscreen(bool bHiddenDueToFullscreen, bool returningFromFullScreen /* = false */)
{
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    if (UsesNativePopup()) { m_bHiddenDueToFullscreen = false; return; }
#endif
    if (bHiddenDueToFullscreen) {
        SetAutohidden(false);
    } else if (returningFromFullScreen) { //it was already hidden, but now we will flag it as autohidden vs. hiddenDueToFullscreen
        SetAutohidden(true);
    }
    m_bHiddenDueToFullscreen = bHiddenDueToFullscreen;
}

void CPlayerPlaylistBar::LoadDuration(POSITION pos) {
    if (AfxGetAppSettings().bUseMediainfoLoadFileDuration) {

        CPlaylistItem& pli = m_pl.GetAt(pos);

        MediaInfoDLL::MediaInfo mi;
        if (mi.IsReady()) {
            auto fn = pli.m_fns.GetHead();
            if (!PathUtils::IsURL(fn)) {
                auto fnString = fn.GetBuffer();
                size_t fp = mi.Open(fnString);
                fn.ReleaseBuffer();
                if (fp > 0) {
                    MediaInfoDLL::String info = mi.Get(MediaInfoDLL::Stream_General, 0, L"Duration");
                    if (!info.empty()) {
                        try {
                            int duration = std::stoi(info);
                            if (duration > 0) {
                                pli.m_duration = duration * 10000LL;
                                RefreshItem(pos);
                            }
                        } catch (...) {
                        }
                    }
                }
            }
            mi.Close();
        }
    }
}

void CPlayerPlaylistBar::AddItem(CString fn, bool insertAtCurrent /*= false*/)
{
    if (fn.IsEmpty()) {
        return;
    }

    CPlaylistItem pli;
    pli.m_fns.AddTail(fn);
    pli.AutoLoadFiles();

    POSITION pos;
    if (insertAtCurrent && m_insertingPos != nullptr) {
        pos = m_insertingPos = m_pl.InsertAfter(m_insertingPos, pli);
    } else {
        pos = m_pl.AddTail(pli);
    }
    LoadDuration(pos);
}

void CPlayerPlaylistBar::AddItem(CString fn, CAtlList<CString>* subs)
{
    CAtlList<CString> sl;
    sl.AddTail(fn);
    AddItem(sl, subs);
}

void CPlayerPlaylistBar::AddItem(CAtlList<CString>& fns, CAtlList<CString>* subs, CString label, CString ydl_src, CString ydl_ua, CString cue, CAtlList<CYoutubeDLInstance::YDLSubInfo>* ydl_subs)
{
    CPlaylistItem pli;

    POSITION pos = fns.GetHeadPosition();
    while (pos) {
        CString fn = fns.GetNext(pos);
        if (!fn.Trim().IsEmpty()) {
            pli.m_fns.AddTail(fn);
        }
    }

    if (pli.m_fns.IsEmpty()) {
        return;
    }

    if (subs) {
        POSITION posSub = subs->GetHeadPosition();
        while (posSub) {
            CString fn = subs->GetNext(posSub);
            if (!fn.Trim().IsEmpty()) {
                pli.m_subs.AddTail(fn);
            }
        }
    }

    pli.AutoLoadFiles();
    if (label) {
        pli.m_label = label;
    } else {
        ASSERT(false);
    }
    if (!ydl_src.IsEmpty()) {
        pli.m_ydlSourceURL = ydl_src;
        pli.m_useragent = ydl_ua;
        pli.m_bYoutubeDL = true;
    }

    if (!cue.IsEmpty()) {
        pli.m_cue = true;
        pli.m_cue_filename = cue;
    }

    if (ydl_subs) {
        pli.m_ydl_subs.AddTailList(ydl_subs);
    }

    POSITION ipos = m_pl.AddTail(pli);

    if (m_pl.GetCount() > 1) {
        LoadDuration(ipos);
    }
}

void CPlayerPlaylistBar::ReplaceCurrentItem(CAtlList<CString>& fns, CAtlList<CString>* subs, CString label, CString ydl_src, CString ydl_ua, CString cue, CAtlList<CYoutubeDLInstance::YDLSubInfo>* ydl_subs)
{
    CAutoLock pledit(&m_plEditLock);

    CPlaylistItem* pli = GetCur();
    if (pli == nullptr) {
        AddItem(fns, subs, label, ydl_src, ydl_ua, cue);
    } else {
        pli->m_fns.RemoveAll();
        pli->m_fns.AddTailList(&fns);
        pli->m_subs.RemoveAll();
        if (subs) {
            pli->m_subs.AddTailList(subs);
        }
        pli->m_label = label;
        pli->m_ydlSourceURL = ydl_src;
        pli->m_useragent = ydl_ua;
        pli->m_bYoutubeDL = !ydl_src.IsEmpty();
        pli->m_cue = !cue.IsEmpty();
        pli->m_cue_filename = cue;
        pli->m_ydl_subs.RemoveAll();
        if (ydl_subs) {
            pli->m_ydl_subs.AddTailList(ydl_subs);
        }

        Refresh();
        SavePlaylist();
    }
}

void CPlayerPlaylistBar::AddSubtitleToCurrent(CString fn)
{
    CPlaylistItem* pli = GetCur();
    if (pli != nullptr) {
        if (!pli->m_subs.Find(fn)) {
            pli->m_subs.AddHead(fn);
        }
    }
}

void CPlayerPlaylistBar::ParsePlayList(CString fn, CAtlList<CString>* subs, int redir_count)
{
    TRACE(fn + _T("\n"));
    CAtlList<CString> sl;
    sl.AddTail(fn);
    ParsePlayList(sl, subs, redir_count);
}

void CPlayerPlaylistBar::ResolveLinkFiles(CAtlList<CString>& fns)
{
    // resolve .lnk files

    POSITION pos = fns.GetHeadPosition();
    while (pos) {
        CString& fn = fns.GetNext(pos);

        if (PathUtils::IsLinkFile(fn)) {
            CString fnResolved = PathUtils::ResolveLinkFile(fn);
            if (!fnResolved.IsEmpty()) {
                fn = fnResolved;
            }
        }
    }
}

bool CPlayerPlaylistBar::AddItemNoDuplicate(CString fn, bool insertAtCurrent /*= false*/)
{
    POSITION pos = m_pl.GetHeadPosition();
    CString fnLower = CString(fn).MakeLower();
    while (pos) {
        const CPlaylistItem& pli = m_pl.GetNext(pos);
        POSITION subpos = pli.m_fns.GetHeadPosition();
        while (subpos) {
            CString cur = pli.m_fns.GetNext(subpos);
            if (cur.MakeLower() == fnLower) {
                // duplicate
                return false;
            }
        }
    }

    AddItem(fn, insertAtCurrent);
    return true;
}

bool CPlayerPlaylistBar::AddFromFilemask(CString mask, bool recurse_dirs, bool insertAtCurrent /*= false*/)
{
    ASSERT(ContainsWildcard(mask));

    bool added = false;

    std::set<CString, CStringUtils::LogicalLess> filelist;
    if (m_pMainFrame->WildcardFileSearch(mask, filelist, recurse_dirs)) {
        auto it = filelist.begin();
        while (it != filelist.end()) {
            if (AddItemNoDuplicate(*it, insertAtCurrent)) {
                added = true;
            }
            it++;
        }
    }

    return added;
}

bool CPlayerPlaylistBar::AddItemsInFolder(CString pathname, bool insertAtCurrent /*= false*/)
{
    CString mask = CString(pathname).TrimRight(_T("\\/")) + _T("\\*.*");
    return AddFromFilemask(mask, false, insertAtCurrent);
}

void CPlayerPlaylistBar::ExternalPlayListLoaded(CStringW fn) {
    auto& s = AfxGetAppSettings();
    if (!PathUtils::IsURL(fn)) {
        s.externalPlayListPath = fn;
        m_ExternalPlayListFNCopy = m_pl.GetIDs();
    } else {
        s.externalPlayListPath = L"";
        m_ExternalPlayListFNCopy.clear();
    }
}

//convenience function to clear the saved playlist if it has been invalidated
void CPlayerPlaylistBar::ClearExternalPlaylistIfInvalid() {
    CStringW discard;
    IsExternalPlayListActive(discard);
}

bool CPlayerPlaylistBar::IsExternalPlayListActive(CStringW& playlistPath) {
    auto& s = AfxGetAppSettings();
    if (!s.externalPlayListPath.IsEmpty() && m_pl.GetIDs() == m_ExternalPlayListFNCopy) {
        playlistPath = s.externalPlayListPath;
        return true;
    } else {
        s.externalPlayListPath = L"";
        m_ExternalPlayListFNCopy.clear();
        return false;
    }
}

void CPlayerPlaylistBar::ParsePlayList(CAtlList<CString>& fns, CAtlList<CString>* subs, int redir_count, CString label, CString ydl_src, CString ydl_ua, CString cue, CAtlList<CYoutubeDLInstance::YDLSubInfo>* ydl_subs)
{
    if (fns.IsEmpty()) {
        return;
    }
    if (redir_count >= 5) {
        ASSERT(FALSE);
        return;
    }

    if (redir_count == 0) {
        ResolveLinkFiles(fns);

        // single entry can be a directory or file mask -> search for files
        // multiple filenames means video file plus audio dub
        if (fns.GetCount() == 1) {
            CString fname = fns.GetHead();
            if (!PathUtils::IsURL(fname)) {
                if (ContainsWildcard(fname)) {
                    AddFromFilemask(fname, true);
                    return;
                } else if (PathUtils::IsDir(fname)) {
                    AddItemsInFolder(fname);
                    return;
                }
            }
        }
    }

    CAtlList<CString> redir;
    CString ct = GetContentType(fns.GetHead(), &redir);
    if (!redir.IsEmpty()) {
        POSITION pos = redir.GetHeadPosition();
        while (pos) {
            ParsePlayList(redir.GetNext(pos), subs, redir_co…15890 tokens truncated… = static_cast<Gdiplus::REAL>(tile.left);
            const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(tile.top);
            const Gdiplus::REAL w = static_cast<Gdiplus::REAL>(tile.Width());
            const Gdiplus::REAL h = static_cast<Gdiplus::REAL>(tile.Height());
            const Gdiplus::REAL diameter = static_cast<Gdiplus::REAL>(m_pMainFrame->m_dpi.ScaleY(12));
            Gdiplus::GraphicsPath path;
            path.AddArc(x, y, diameter, diameter, 180.0f, 90.0f);
            path.AddArc(x + w - diameter, y, diameter, diameter, 270.0f, 90.0f);
            path.AddArc(x + w - diameter, y + h - diameter, diameter, diameter, 0.0f, 90.0f);
            path.AddArc(x, y + h - diameter, diameter, diameter, 90.0f, 90.0f);
            path.CloseFigure();
            Gdiplus::SolidBrush fill(Gdiplus::Color(255, 56, 56, 56));
            graphics.FillPath(&fill, &path);
        }
        if (itemSelected) {
            const int pillWidth = m_pMainFrame->m_dpi.ScaleX(3), pillHeight = m_pMainFrame->m_dpi.ScaleY(15);
            CRect pill(CPoint(rcItem.left, rcItem.CenterPoint().y - pillHeight/2), CSize(pillWidth, pillHeight));
            Gdiplus::Graphics graphics(pDC->GetSafeHdc());
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(pill.left);
            const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(pill.top);
            const Gdiplus::REAL w = static_cast<Gdiplus::REAL>(pill.Width());
            const Gdiplus::REAL h = static_cast<Gdiplus::REAL>(pill.Height());
            Gdiplus::GraphicsPath path;
            path.AddArc(x, y, w, w, 180.0f, 180.0f);
            path.AddArc(x, y + h - w, w, w, 0.0f, 180.0f);
            path.CloseFigure();
            Gdiplus::SolidBrush accent(Gdiplus::Color(255, 76, 194, 255));
            graphics.FillPath(&accent, &path);
        }
    } else
#endif
    if (win11Style) {
        //fluent list selection: a neutral fill across the whole row, with an accent indicator at the left edge
        bgColor = rowHighlighted ? CMPCTheme::PlaylistSelectedColor : contentBGColor;
        FillRect(pDC->m_hDC, rcItem, CBrush(bgColor));
        if (itemSelected) {
            int pillWidth = m_pMainFrame->m_dpi.ScaleX(3);
            int pillHeight = rcItem.Height() / 2;
            CRect pill(CPoint(rcItem.left + m_pMainFrame->m_dpi.ScaleX(2), rcItem.top + (rcItem.Height() - pillHeight) / 2), CSize(pillWidth, pillHeight));
            CBrush pillBrush(CMPCTheme::PlaylistIndicatorColor);
            CBrush* oldBrush = pDC->SelectObject(&pillBrush);
            CPen* oldPen = (CPen*)pDC->SelectStockObject(NULL_PEN);
            pDC->RoundRect(pill, CPoint(pillWidth, pillWidth));
            pDC->SelectObject(oldPen);
            pDC->SelectObject(oldBrush);
        }
    } else if (rowHighlighted) {
        if (AppNeedsThemedControls()) {
            bgColor = CMPCTheme::ContentSelectedColor;
        } else {
            bgColor = 0xf1dacc;
            FrameRect(pDC->m_hDC, rcItem, CBrush(0xc56a31));
        }
        FillRect(pDC->m_hDC, fileRect, CBrush(bgColor));
        FillRect(pDC->m_hDC, seqRect, CBrush(contentBGColor));
    } else {
        bgColor = contentBGColor;
        FillRect(pDC->m_hDC, rcItem, CBrush(bgColor));
    }

    COLORREF textColor, sequenceColor;

    if (themedList) {
        if (pli.m_fInvalid) {
            textColor = CMPCTheme::ContentTextDisabledFGColorFade2;
            sequenceColor = textColor;
        } else if (itemPlaying) {
            textColor = itemSelected ? CMPCTheme::ActivePlayListItemHLColor : CMPCTheme::ActivePlayListItemColor;
            sequenceColor = CMPCTheme::ActivePlayListItemColor;
        } else {
            textColor = CMPCTheme::TextFGColor;
            sequenceColor = textColor;
        }
    } else {
        textColor = itemPlaying ? 0xff : 0;
        if (pli.m_fInvalid) {
            textColor |= 0xA0A0A0;
        }
        sequenceColor = textColor;
    }

#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    if ((m_nativeDock.Active() || UsesNativePopup()) && !pli.m_fInvalid) {
        textColor = sequenceColor = UsesNativePopup() ? RGB(255, 255, 255)
            : itemSelected ? RGB(76, 194, 255) : RGB(255, 255, 255);
    }
#endif

    CString time = !pli.m_fInvalid ? pli.GetLabel(1) : CString(_T("Invalid"));
    CPoint timept(rcItem.right, 0);
    if (!time.IsEmpty()) {
        CSize timesize = pDC->GetTextExtent(time);
        if ((dpi3 + timesize.cx + dpi3) < rcItem.Width() / 2) {
            timept = CPoint(rcItem.right - (dpi3 + timesize.cx + dpi3), (rcItem.top + rcItem.bottom - timesize.cy) / 2);

            pDC->SetTextColor(textColor);
            pDC->SetBkColor(bgColor);
            pDC->TextOut(timept.x, timept.y, time);
        }
    }
    pli.inlineEditMaxWidth = std::max(0, (int)timept.x - dpi6 - inlineEditXpos); //leave the same gap before the time as the truncated filename

    file = pli.GetLabel(0);
    CSize filesize = pDC->GetTextExtent(file);
    while (dpi3 + numWidth.cx + filesize.cx + dpi6 > timept.x && file.GetLength() > 3) {
        file = file.Left(file.GetLength() - 4) + _T("...");
        filesize = pDC->GetTextExtent(file);
    }

    if (nItem != inlineEditItem) { //don't draw the filename of the item being edited (visually distracting while editing)
        pDC->SetTextColor(textColor);
        pDC->SetBkColor(bgColor);
        pDC->TextOut(rcItem.left + dpi3 + numWidth.cx, (rcItem.top + rcItem.bottom - filesize.cy) / 2, file);
    }

    if (!itemPlaying) {
        pDC->SetTextColor(CMPCTheme::ContentTextDisabledFGColorFade);
    } else {
        pDC->SetTextColor(sequenceColor);
    }
    pDC->SetBkColor(win11Style ? bgColor : contentBGColor);
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    if (UsesNativePopup() && m_numberFont.GetSafeHandle()) pDC->SelectObject(&m_numberFont);
#endif
    pDC->TextOut(rcItem.left + dpi3 + numOffset, (rcItem.top + rcItem.bottom - filesize.cy) / 2, num);


    pDC->RestoreDC(oldDC);
}

BOOL CPlayerPlaylistBar::OnPlayPlay(UINT nID)
{
    m_list.Invalidate();
    return FALSE;
}

DROPEFFECT CPlayerPlaylistBar::OnDropAccept(COleDataObject*, DWORD, CPoint)
{
    return DROPEFFECT_COPY;
}

void CPlayerPlaylistBar::OnDropFiles(CAtlList<CString>& slFiles, DROPEFFECT)
{
    SetForegroundWindow();
    m_list.SetFocus();

    if ((slFiles.GetCount() == 1) && m_pMainFrame->CanSendToYoutubeDL(slFiles.GetHead())) {
        if (m_pMainFrame->ProcessYoutubeDLURL(slFiles.GetHead(), true)) {
            return;
        } else if (m_pMainFrame->IsOnYDLWhitelist(slFiles.GetHead())) {
            // don't bother trying to open this website URL directly
            return;
        }
    }

    PathUtils::ParseDirs(slFiles);

    Append(slFiles, true);
}

void CPlayerPlaylistBar::OnBeginDrag(NMHDR* pNMHDR, LRESULT* pResult)
{
    ModifyStyle(WS_EX_ACCEPTFILES, 0);

    m_nDragIndex = ((LPNMLISTVIEW)pNMHDR)->iItem;

    CPoint p(0, 0);
    m_pDragImage = m_list.CreateDragImageEx(&p);

    CPoint p2 = ((LPNMLISTVIEW)pNMHDR)->ptAction;

    m_pDragImage->BeginDrag(0, p2 - p);
    m_pDragImage->DragEnter(GetDesktopWindow(), ((LPNMLISTVIEW)pNMHDR)->ptAction);

    m_bDragging = TRUE;
    m_nDropIndex = -1;

    SetCapture();
}

void CPlayerPlaylistBar::OnMouseMove(UINT nFlags, CPoint point)
{
    if (m_bDragging) {
        m_ptDropPoint = point;
        ClientToScreen(&m_ptDropPoint);

        m_pDragImage->DragMove(m_ptDropPoint);
        m_pDragImage->DragShowNolock(FALSE);

        WindowFromPoint(m_ptDropPoint)->ScreenToClient(&m_ptDropPoint);

        m_pDragImage->DragShowNolock(TRUE);

        {
            int iOverItem = m_list.HitTest(m_ptDropPoint);
            int iTopItem = m_list.GetTopIndex();
            int iBottomItem = m_list.GetBottomIndex();

            if (iOverItem == iTopItem && iTopItem != 0) { // top of list
                SetTimer(1, 100, nullptr);
            } else {
                KillTimer(1);
            }

            if (iOverItem >= iBottomItem && iBottomItem != (m_list.GetItemCount() - 1)) { // bottom of list
                SetTimer(2, 100, nullptr);
            } else {
                KillTimer(2);
            }
        }
    }

    __super::OnMouseMove(nFlags, point);
}

void CPlayerPlaylistBar::OnTimer(UINT_PTR nIDEvent)
{
    int iTopItem = m_list.GetTopIndex();
    int iBottomItem = iTopItem + m_list.GetCountPerPage() - 1;

    if (m_bDragging) {
        m_pDragImage->DragShowNolock(FALSE);

        if (nIDEvent == 1) {
            m_list.EnsureVisible(iTopItem - 1, false);
            m_list.UpdateWindow();
            if (m_list.GetTopIndex() == 0) {
                KillTimer(1);
            }
        } else if (nIDEvent == 2) {
            m_list.EnsureVisible(iBottomItem + 1, false);
            m_list.UpdateWindow();
            if (m_list.GetBottomIndex() == (m_list.GetItemCount() - 1)) {
                KillTimer(2);
            }
        }

        m_pDragImage->DragShowNolock(TRUE);
    }

    __super::OnTimer(nIDEvent);
}

void CPlayerPlaylistBar::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (m_bDragging) {
        ::ReleaseCapture();

        m_bDragging = FALSE;
        m_pDragImage->DragLeave(GetDesktopWindow());
        m_pDragImage->EndDrag();

        delete m_pDragImage;
        m_pDragImage = nullptr;

        KillTimer(1);
        KillTimer(2);

        CPoint pt(point);
        ClientToScreen(&pt);

        if (WindowFromPoint(pt) == &m_list) {
            DropItemOnList();
        }
    }

    ModifyStyle(0, WS_EX_ACCEPTFILES);

    __super::OnLButtonUp(nFlags, point);
}

void CPlayerPlaylistBar::DropItemOnList()
{
    m_ptDropPoint.y += 10;
    m_nDropIndex = m_list.HitTest(CPoint(10, m_ptDropPoint.y));

    if (m_nDropIndex < 0) {
        m_nDropIndex = (int)m_pl.GetCount();
    }

    ASSERT(m_indexToPos.size() == (size_t)m_pl.GetCount());

    POSITION dragPos = FindPos(m_nDragIndex);
    if (!dragPos || m_nDragIndex == m_nDropIndex) {
        return;
    }

    // Build the desired order by moving dragPos within m_indexToPos
    int dropIdx = m_nDropIndex;
    if (m_nDropIndex > m_nDragIndex) {
        dropIdx--;
    }
    m_indexToPos.erase(m_indexToPos.begin() + m_nDragIndex);
    if (dropIdx > (int)m_indexToPos.size()) {
        dropIdx = (int)m_indexToPos.size();
    }
    m_indexToPos.insert(m_indexToPos.begin() + dropIdx, dragPos);

    // Reorder m_pl to match the new visual order (preserves POSITIONs)
    for (size_t i = 0; i < m_indexToPos.size(); i++) {
        m_pl.MoveToTail(m_indexToPos[i]);
    }

    RebuildPosMap();
    m_list.Invalidate();
    ResizeListColumn();

    int newIndex = FindItem(dragPos);
    if (newIndex >= 0) {
        // In virtual list mode, explicitly clear old states and set new ones
        m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        m_list.SetItemState(newIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        m_list.SetSelectionMark(newIndex);
    }
}

BOOL CPlayerPlaylistBar::OnToolTipNotify(UINT id, NMHDR* pNMHDR, LRESULT* pResult)
{
    TOOLTIPTEXT* pTTT = (TOOLTIPTEXT*)pNMHDR;

    if ((HWND)pTTT->lParam != m_list.m_hWnd) {
        return FALSE;
    }

    int row = ((pNMHDR->idFrom - 1) >> 10) & 0x3fffff;
    int col = (pNMHDR->idFrom - 1) & 0x3ff;

    if (row < 0 || size_t(row) >= m_pl.GetCount()) {
        return FALSE;
    }

    CPlaylistItem& pli = m_pl.GetAt(FindPos(row));

    static CString strTipText; // static string
    strTipText.Empty();

    if (col == COL_NAME) {
        POSITION pos = pli.m_fns.GetHeadPosition();
        while (pos) {
            strTipText += _T("\n") + pli.m_fns.GetNext(pos);
        }
        strTipText.Trim();

        if (pli.m_type == CPlaylistItem::device) {
            if (pli.m_vinput >= 0) {
                strTipText.AppendFormat(_T("\nVideo Input %d"), pli.m_vinput);
            }
            if (pli.m_vchannel >= 0) {
                strTipText.AppendFormat(_T("\nVideo Channel %d"), pli.m_vchannel);
            }
            if (pli.m_ainput >= 0) {
                strTipText.AppendFormat(_T("\nAudio Input %d"), pli.m_ainput);
            }
        }

        ::SendMessage(pNMHDR->hwndFrom, TTM_SETMAXTIPWIDTH, 0, 1000);
    } else if (col == COL_TIME) {
        return FALSE;
    }

    pTTT->lpszText = (LPWSTR)(LPCWSTR)strTipText;

    *pResult = 0;

    return TRUE;    // message was handled
}

void CPlayerPlaylistBar::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
    LVHITTESTINFO lvhti;
    POSITION pos = nullptr;
    UINT selId = 0;

    CMPCThemeMenu m;

    enum {
        M_OPEN = 1,
        M_ADD,
        M_REMOVE,
        M_CLEAR,
        M_CLIPBOARD,
        M_SHOWFOLDER,
        M_ADDFOLDER,
        M_RECYCLE,
        M_SAVE,
        M_SAVEAS,
        M_SORTBYNAME,
        M_SORTBYPATH,
        M_SORTBYDATENEWEST,
        M_SORTBYDATEOLDEST,
        M_RANDOMIZE,
        M_SORTBYID,
        M_SHUFFLE,
        M_HIDEFULLSCREEN,
        M_POSITION_LEFT,
        M_POSITION_TOP,
        M_POSITION_RIGHT,
        M_POSITION_BOTTOM,
        M_POSITION_FLOAT,
        M_HEADER_TINT_0 = 100,
        M_HEADER_TINT_10,
        M_HEADER_TINT_20,
        M_HEADER_TINT_30,
        M_HEADER_TINT_40,
        M_HEADER_TINT_50,
    };

    CAppSettings& s = AfxGetAppSettings();

    {
        CAutoLock pledit(&m_plEditLock);

        bool bOnItem;
        if (point.x == -1 && point.y == -1) {
            lvhti.iItem = m_list.GetSelectionMark();

            if (lvhti.iItem == -1 && m_pl.GetCount() == 1) {
                lvhti.iItem = 0;
            }

            CRect r;
            if (!!m_list.GetItemRect(lvhti.iItem, r, LVIR_BOUNDS)) {
                point.SetPoint(r.left, r.bottom);
            } else {
                point.SetPoint(0, 0);
            }
            m_list.ClientToScreen(&point);
            bOnItem = lvhti.iItem != -1;
        } else {
            lvhti.pt = point;
            m_list.ScreenToClient(&lvhti.pt);
            m_list.SubItemHitTest(&lvhti);
            bOnItem = lvhti.iItem >= 0 && !!(lvhti.flags & LVHT_ONITEM);
            if (!bOnItem && m_pl.GetCount() == 1) {
                bOnItem = true;
                lvhti.iItem = 0;
            }
        }

        pos = FindPos(lvhti.iItem);
        bool bIsLocalFile = bOnItem ? PathUtils::Exists(m_pl.GetAt(pos).m_fns.GetHead()) : false;
        selId = (bOnItem && pos) ? m_pl.GetAt(pos).m_id : 0;

        m.CreatePopupMenu();

        UINT styleOnItem       = MF_STRING | (!bOnItem ? (MF_DISABLED | MF_GRAYED) : MF_ENABLED);
        UINT styleOnItemLocal  = MF_STRING | ((!bOnItem || !bIsLocalFile) ? (MF_DISABLED | MF_GRAYED) : MF_ENABLED);
        UINT styleListNotEmpty = MF_STRING | (!m_pl.GetCount() ? (MF_DISABLED | MF_GRAYED) : MF_ENABLED);

        m.AppendMenu(styleOnItem, M_OPEN, ResStr(IDS_PLAYLIST_OPEN));
        if (m_pMainFrame->GetPlaybackMode() == PM_ANALOG_CAPTURE) {
            m.AppendMenu(MF_STRING | MF_ENABLED, M_ADD, ResStr(IDS_PLAYLIST_ADD));
        }
        m.AppendMenu(styleOnItem, M_REMOVE, ResStr(IDS_PLAYLIST_REMOVE));
        m.AppendMenu(MF_SEPARATOR);
        m.AppendMenu(styleListNotEmpty, M_CLEAR, ResStr(IDS_PLAYLIST_CLEAR));
        m.AppendMenu(MF_SEPARATOR);
        m.AppendMenu(styleOnItem, M_CLIPBOARD, ResStr(IDS_PLAYLIST_COPYTOCLIPBOARD));
        m.AppendMenu(styleOnItemLocal, M_SHOWFOLDER, ResStr(IDS_PLAYLIST_SHOWFOLDER));
        m.AppendMenu(styleOnItemLocal, M_RECYCLE, ResStr(IDS_FILE_RECYCLE));
        m.AppendMenu(styleOnItemLocal, M_ADDFOLDER, ResStr(IDS_PLAYLIST_ADDFOLDER));
        m.AppendMenu(MF_SEPARATOR);
        if (!m_playListPath.IsEmpty()) {
            m.AppendMenu(styleListNotEmpty, M_SAVE, ResStr(IDS_PLAYLIST_SAVE));
        }
        m.AppendMenu(styleListNotEmpty, M_SAVEAS, ResStr(IDS_PLAYLIST_SAVEAS));
        m.AppendMenu(MF_SEPARATOR);
        //the submenus are plain menus: m themes them when they are appended and owns what that allocates. a local
        //CMPCThemeMenu would free its item data here while m still shows the items, which crashed once the freed
        //addresses were reused
        {
            CMenu sortMenu;
            sortMenu.CreatePopupMenu();
            UINT styleListNotEmptyPopup = MF_POPUP | (!m_pl.GetCount() ? (MF_DISABLED | MF_GRAYED) : MF_ENABLED);
            sortMenu.AppendMenu(styleListNotEmpty, M_SORTBYNAME, ResStr(IDS_PLAYLIST_SORTBYLABEL));
            sortMenu.AppendMenu(styleListNotEmpty, M_SORTBYPATH, ResStr(IDS_PLAYLIST_SORTBYPATH));
            sortMenu.AppendMenu(styleListNotEmpty, M_SORTBYDATENEWEST, ResStr(IDS_PLAYLIST_SORTBYDATE_NEWEST));
            sortMenu.AppendMenu(styleListNotEmpty, M_SORTBYDATEOLDEST, ResStr(IDS_PLAYLIST_SORTBYDATE_OLDEST));
            sortMenu.AppendMenu(styleListNotEmpty, M_RANDOMIZE, ResStr(IDS_PLAYLIST_RANDOMIZE));
            sortMenu.AppendMenu(MF_SEPARATOR);
            sortMenu.AppendMenu(styleListNotEmpty, M_SORTBYID, ResStr(IDS_PLAYLIST_RESTORE));
            m.AppendMenu(styleListNotEmptyPopup, (UINT_PTR)sortMenu.GetSafeHmenu(), ResStr(IDS_PLAYLIST_SORT));
            sortMenu.Detach();
        }
        m.AppendMenu(MF_SEPARATOR);
        m.AppendMenu(MF_STRING | MF_ENABLED | (s.bShufflePlaylistItems ? MF_CHECKED : MF_UNCHECKED), M_SHUFFLE, ResStr(IDS_PLAYLIST_SHUFFLE));
        m.AppendMenu(MF_SEPARATOR);
        m.AppendMenu(MF_STRING | MF_ENABLED | (s.bHidePlaylistFullScreen ? MF_CHECKED : MF_UNCHECKED), M_HIDEFULLSCREEN, ResStr(IDS_PLAYLIST_HIDEFS));
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
        if (!UsesNativePopup())
#endif
        {
            m.AppendMenu(MF_SEPARATOR);
            const UINT dockBarID = GetParent()->GetDlgCtrlID();
            CMenu positionMenu;
            positionMenu.CreatePopupMenu();
            positionMenu.AppendMenu(MF_STRING | MF_ENABLED | (dockBarID == AFX_IDW_DOCKBAR_LEFT ? MF_CHECKED : MF_UNCHECKED), M_POSITION_LEFT, ResStr(IDS_PLAYLIST_POSITION_LEFT));
            positionMenu.AppendMenu(MF_STRING | MF_ENABLED | (dockBarID == AFX_IDW_DOCKBAR_TOP ? MF_CHECKED : MF_UNCHECKED), M_POSITION_TOP, ResStr(IDS_PLAYLIST_POSITION_TOP));
            positionMenu.AppendMenu(MF_STRING | MF_ENABLED | (dockBarID == AFX_IDW_DOCKBAR_RIGHT ? MF_CHECKED : MF_UNCHECKED), M_POSITION_RIGHT, ResStr(IDS_PLAYLIST_POSITION_RIGHT));
            positionMenu.AppendMenu(MF_STRING | MF_ENABLED | (dockBarID == AFX_IDW_DOCKBAR_BOTTOM ? MF_CHECKED : MF_UNCHECKED), M_POSITION_BOTTOM, ResStr(IDS_PLAYLIST_POSITION_BOTTOM));
            positionMenu.AppendMenu(MF_STRING | MF_ENABLED | (dockBarID == AFX_IDW_DOCKBAR_FLOAT ? MF_CHECKED : MF_UNCHECKED), M_POSITION_FLOAT, ResStr(IDS_PLAYLIST_POSITION_FLOAT));
            m.AppendMenu(MF_STRING | MF_POPUP | MF_ENABLED, (UINT_PTR)positionMenu.GetSafeHmenu(), ResStr(IDS_PLAYLIST_POSITION));
            positionMenu.Detach();
        }
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
        {
            CMenu tintMenu;
            tintMenu.CreatePopupMenu();
            const int tint = std::clamp(int(AfxGetApp()->GetProfileInt(L"Settings", L"FluentHeaderAcrylicTint", 0)), 0, 50);
            for (int i = 0; i < 6; ++i) {
                CString label; label.Format(L"%d%%", i*10);
                tintMenu.AppendMenu(MF_STRING, M_HEADER_TINT_0+i, label);
            }
            tintMenu.CheckMenuRadioItem(M_HEADER_TINT_0, M_HEADER_TINT_50,
                M_HEADER_TINT_0+std::min(tint,50)/10, MF_BYCOMMAND);
            m.AppendMenu(MF_SEPARATOR);
            m.AppendMenu(MF_POPUP | MF_ENABLED, reinterpret_cast<UINT_PTR>(tintMenu.GetSafeHmenu()), L"Titlebar Acrylic tint (test)");
            tintMenu.Detach(); // The themed parent owns submenu item data.
        }
#endif
        if (AppIsThemeLoaded()) {
            m.fulfillThemeReqs();
        }
    }

    SetHasActivePopup(true);
    //use mainframe as parent to take advantage of measure redirect (was 'this' but text was not printed)
    //adipose: note this will bypass CPlayerBar::OnEnterMenuLoop, so we set the flag directly here
    int nID = (int)m.TrackPopupMenu(TPM_LEFTBUTTON | TPM_RETURNCMD, point.x, point.y, m_pMainFrame);
    SetHasActivePopup(false);
    m_list.SetFocus();

#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    if (nID >= M_HEADER_TINT_0 && nID <= M_HEADER_TINT_50) {
        AfxGetApp()->WriteProfileInt(L"Settings", L"FluentHeaderAcrylicTint", (nID-M_HEADER_TINT_0)*10);
        RefreshChromeHeader(m_pMainFrame);
        return;
    }
#endif

    //the lock is not held while the menu is up, so the playlist may have changed meanwhile.
    //find the clicked item again by its id and drop the command when it is gone.
    {
        CAutoLock pledit(&m_plEditLock);
        pos = selId ? FindPosById(selId) : nullptr;
        lvhti.iItem = pos ? FindItem(pos) : -1;
    }
    if (!pos && (nID == M_OPEN || nID == M_REMOVE || nID == M_RECYCLE || nID == M_CLIPBOARD || nID == M_SHOWFOLDER || nID == M_ADDFOLDER)) {
        return;
    }

    switch (nID) {
        case M_OPEN:
            {
                CAutoLock pledit(&m_plEditLock);
                m_pl.SetPos(pos);
            }
            m_list.Invalidate();
            m_pMainFrame->PostMessage(WM_MPC_OPENCURPLAYLIST, 0, 0);
            break;
        case M_ADD:
            m_pMainFrame->AddCurDevToPlaylist();
            {
                CAutoLock pledit(&m_plEditLock);
                m_pl.SetPos(m_pl.GetTailPosition());
            }
            break;
        case M_REMOVE:
            RemoveItemAt(lvhti.iItem);
            break;
        case M_RECYCLE:
            DeleteFileInPlaylist(pos, true);
            break;
        case M_CLEAR:
            if (Empty()) {
                m_pMainFrame->PostMessage(WM_COMMAND, ID_FILE_CLOSEMEDIA);
            }
            SavePlaylist();
            break;
        case M_SORTBYID: {
            {
                CAutoLock pledit(&m_plEditLock);
                POSITION selPos = FindPos(m_list.GetSelectionMark());
                m_pl.SortById();
                SetupList();
                SyncSelectionToPos(selPos ? selPos : m_pl.GetPos());
            }
            SavePlaylist();
            break;
        }
        case M_SORTBYNAME: {
            {
                CAutoLock pledit(&m_plEditLock);
                POSITION selPos = FindPos(m_list.GetSelectionMark());
                m_pl.SortByName();
                SetupList();
                SyncSelectionToPos(selPos ? selPos : m_pl.GetPos());
            }
            SavePlaylist();
            break;
        }
        case M_SORTBYPATH: {
            {
                CAutoLock pledit(&m_plEditLock);
                POSITION selPos = FindPos(m_list.GetSelectionMark());
                m_pl.SortByPath();
                SetupList();
                SyncSelectionToPos(selPos ? selPos : m_pl.GetPos());
            }
            SavePlaylist();
            break;
        }
        case M_SORTBYDATENEWEST:
        case M_SORTBYDATEOLDEST: {
            {
                CAutoLock pledit(&m_plEditLock);
                POSITION selPos = FindPos(m_list.GetSelectionMark());
                m_pl.SortByDate(nID == M_SORTBYDATEOLDEST);
                SetupList();
                SyncSelectionToPos(selPos ? selPos : m_pl.GetPos());
            }
            SavePlaylist();
            break;
        }
        case M_RANDOMIZE:
            Randomize();
            break;
        case M_CLIPBOARD: {
                CString str;
                {
                    CAutoLock pledit(&m_plEditLock);
                    CPlaylistItem& pli = m_pl.GetAt(pos);
                    POSITION pos2 = pli.m_fns.GetHeadPosition();
                    while (pos2) {
                        str += _T("\r\n") + pli.m_fns.GetNext(pos2);
                    }
                }
                str.Trim();

                CClipboard clipboard(this);
                VERIFY(clipboard.SetText(str));
            }
            break;
        case M_SHOWFOLDER: {
            CString fn;
            {
                CAutoLock pledit(&m_plEditLock);
                fn = m_pl.GetAt(pos).m_fns.GetHead();
            }
            ExploreToFile(fn);
            break;
        }
        case M_ADDFOLDER: {
            // add all media files in current playlist item folder that are not yet in the playlist
            CString fn;
            {
                CAutoLock pledit(&m_plEditLock);
                fn = m_pl.GetAt(pos).m_fns.GetHead();
            }
            const CString dirName = PathUtils::DirName(fn);
            if (PathUtils::IsDir(dirName)) {
                m_insertingPos = pos;
                if (AddItemsInFolder(dirName, true)) {
                    Refresh();
                    SavePlaylist();
                }
            }
            break;
        }
        case M_SAVE: {
            SaveMPCPlayList(m_playListPath, CTextFile::UTF8);
            break;
        }
        case M_SAVEAS: {
            CSaveTextFileDialog fd(
                CTextFile::DEFAULT_ENCODING, nullptr, nullptr,
                _T("MPC-HC playlist (*.mpcpl)|*.mpcpl|Playlist (*.pls)|*.pls|Winamp playlist (*.m3u)|*.m3u|Windows Media playlist (*.asx)|*.asx||"),
                this);

            if (fd.DoModal() != IDOK) {
                break;
            }

            CTextFile::enc encoding = (CTextFile::enc)fd.GetEncoding();

            int idx = fd.m_pOFN->nFilterIndex;

            if (encoding == CTextFile::DEFAULT_ENCODING) {
                if (idx == 1 || idx == 3) {
                    encoding = CTextFile::UTF8;
                } else {
                    encoding = CTextFile::ANSI;
                }
            }

            CLongPath path(fd.GetPathName());

            switch (idx) {
                case 1:
                    path.AddExtension(_T(".mpcpl"));
                    break;
                case 2:
                    path.AddExtension(_T(".pls"));
                    break;
                case 3:
                    path.AddExtension(_T(".m3u"));
                    break;
                case 4:
                    path.AddExtension(_T(".asx"));
                    break;
                default:
                    break;
            }

            if (idx == 1) {
                SaveMPCPlayList(path, encoding);
                m_playListPath = (LPCTSTR)path;
                break;
            }

            CTextFile f;
            if (!f.Save(path, encoding)) {
                break;
            }

            if (idx == 2) {
                f.WriteString(_T("[playlist]\n"));
            } else if (idx == 4) {
                f.WriteString(_T("<ASX version = \"3.0\">\n"));
            }

            POSITION savePos = m_pl.GetHeadPosition();
            CString str;
            int i;
            for (i = 0; savePos; i++) {
                CPlaylistItem& pli = m_pl.GetNext(savePos);

                if (pli.m_type != CPlaylistItem::file) {
                    continue;
                }

                CString fn = pli.m_fns.GetHead();

                /*
                if (idx != 4 && PlaylistCanStripPath(path))
                {
                    CLongPath p(path);
                    p.StripPath();
                    fn = (LPCTSTR)p;
                }
                */

                switch (idx) {
                    case 2:
                        str.Format(_T("File%d=%s\n"), i + 1, fn.GetString());
                        break;
                    case 3:
                        str.Format(_T("%s\n"), fn.GetString());
                        break;
                    case 4:
                        str.Format(_T("<Entry><Ref href = \"%s\"/></Entry>\n"), fn.GetString());
                        break;
                    default:
                        break;
                }
                f.WriteString(str);
            }

            if (idx == 2) {
                str.Format(_T("NumberOfEntries=%d\n"), i);
                f.WriteString(str);
                f.WriteString(_T("Version=2\n"));
            } else if (idx == 4) {
                f.WriteString(_T("</ASX>\n"));
            }
        }
        break;
        case M_SHUFFLE:
            s.bShufflePlaylistItems = !s.bShufflePlaylistItems;
            {
                CAutoLock pledit(&m_plEditLock);
                m_pl.SetShuffle(s.bShufflePlaylistItems);
            }
            break;
        case M_HIDEFULLSCREEN:
            s.bHidePlaylistFullScreen = !s.bHidePlaylistFullScreen;
            m_pMainFrame->HidePlaylistFullScreen();
            break;
        case M_POSITION_LEFT:
            if (GetParent()->GetDlgCtrlID() != AFX_IDW_DOCKBAR_LEFT) {
                m_pMainFrame->DockControlBar(this, AFX_IDW_DOCKBAR_LEFT);
            }
            break;
        case M_POSITION_TOP:
            if (GetParent()->GetDlgCtrlID() != AFX_IDW_DOCKBAR_TOP) {
                m_pMainFrame->DockControlBar(this, AFX_IDW_DOCKBAR_TOP);
            }
            break;
        case M_POSITION_RIGHT:
            if (GetParent()->GetDlgCtrlID() != AFX_IDW_DOCKBAR_RIGHT) {
                m_pMainFrame->DockControlBar(this, AFX_IDW_DOCKBAR_RIGHT);
            }
            break;
        case M_POSITION_BOTTOM:
            if (GetParent()->GetDlgCtrlID() != AFX_IDW_DOCKBAR_BOTTOM) {
                m_pMainFrame->DockControlBar(this, AFX_IDW_DOCKBAR_BOTTOM);
            }
            break;
        case M_POSITION_FLOAT:
            if (GetParent()->GetDlgCtrlID() != AFX_IDW_DOCKBAR_FLOAT) {
                CRect rectMain;
                m_pMainFrame->GetWindowRect(rectMain);
                CPoint fp(rectMain.right, rectMain.top);

                CRect rectDesktop;
                GetDesktopWindow()->GetWindowRect(&rectDesktop);

                CRect rect;
                GetWindowRect(rect);

                if (fp.x < rectDesktop.left) {
                    fp.x = rectDesktop.left;
                }
                if (fp.y < rectDesktop.top) {
                    fp.y = rectDesktop.top;
                }
                if (fp.x + rect.Width() > rectDesktop.right) {
                    fp.x = rectDesktop.left;
                }
                if (fp.y + rect.Height() > rectDesktop.bottom) {
                    fp.y = rectDesktop.top;
                }

                m_pMainFrame->FloatControlBar(this, fp);
            }
            break;
        default:
            break;
    }
}

void CPlayerPlaylistBar::OnLvnGetDispInfoList(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVDISPINFO* pDispInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
    LVITEM& item = pDispInfo->item;

    POSITION pos = FindPos(item.iItem);
    if (!pos) {
        return;
    }
    CPlaylistItem& pli = m_pl.GetAt(pos);

    if (item.mask & LVIF_TEXT) {
        CString text = pli.GetLabel(item.iSubItem);
        _tcsncpy_s(item.pszText, item.cchTextMax, text, _TRUNCATE);
    }

    *pResult = 0;
}

void CPlayerPlaylistBar::OnLvnBeginlabeleditList(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVDISPINFO* pDispInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
    if (pDispInfo->item.iSubItem != COL_NAME) {
        *pResult = 1; // cancel
        return;
    }
    HWND hEdit = (HWND)m_list.SendMessage(LVM_GETEDITCONTROL);
    if (::IsWindow(m_edit.m_hWnd)) {
        m_edit.UnsubclassWindow();
    }
    if (hEdit) {
        m_edit.SubclassWindow(hEdit);
        m_list.m_fInPlaceDirty = false;
        POSITION pos = FindPos(pDispInfo->item.iItem);
        int maxW = pos ? m_pl.GetAt(pos).inlineEditMaxWidth : -1;
        m_edit.setOverridePos(inlineEditXpos, maxW);
        inlineEditItem = pDispInfo->item.iItem;
    }
    *pResult = 0; // allow
}

void CPlayerPlaylistBar::OnLvnEndlabeleditList(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVDISPINFO* pDispInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
    inlineEditItem = -1;
    // pszText is nullptr when the edit was cancelled (ESC)
    if (pDispInfo->item.pszText != nullptr) {
        POSITION pos = FindPos(pDispInfo->item.iItem);
        if (pos) {
            CString text(pDispInfo->item.pszText);
            text.Trim();
            m_pl.GetAt(pos).m_label = text;
            RefreshItem(pos);
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
            if (pos == m_pl.GetPos()) RefreshChromeHeader(m_pMainFrame);
#endif
        }
    }
    *pResult = 0;
}

void CPlayerPlaylistBar::OnLvnFinditem(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMLVFINDITEM pFindItem = reinterpret_cast<LPNMLVFINDITEM>(pNMHDR);

    int firstfoundidx = -1;
    if (pFindItem) {
        int startidx = pFindItem->iStart;
        CString findstr = pFindItem->lvfi.psz;
        int len = findstr.GetLength();
        int idx = -1;
        POSITION pos = m_pl.GetHeadPosition();
        while (pos) {
            CPlaylistItem& pli = m_pl.GetNext(pos);
            CString label = pli.GetLabel(0).MakeLower();
            idx++;
            if (len > 2) { // search whole label
                if (label.Find(findstr) >= 0) {
                    if (idx >= startidx) {
                        *pResult = idx;
                        return;
                    } else if (firstfoundidx == -1) {
                        firstfoundidx = idx;
                    }
                }
            } else { // compare left side
                if (findstr == label.Left(len)) {
                    if (idx >= startidx) {
                        *pResult = idx;
                        return;
                    } else if (firstfoundidx == -1) {
                        firstfoundidx = idx;
                    }
                }
            }
        }
    }

    *pResult = firstfoundidx;
}

void CPlayerPlaylistBar::OnXButtonDown(UINT nFlags, UINT nButton, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    UNREFERENCED_PARAMETER(point);
    if (m_pMainFrame->GetPlaybackMode() == PM_FILE && GetCount() > 1) {
        switch (nButton) {
            case XBUTTON1:
                if (SetPrev()) {
                    m_pMainFrame->PostMessage(WM_MPC_OPENCURPLAYLIST, 0, 0);
                }
                break;
            case XBUTTON2:
                if (SetNext()) {
                    m_pMainFrame->PostMessage(WM_MPC_OPENCURPLAYLIST, 0, 0);
                }
                break;
        }
    }
}

void CPlayerPlaylistBar::OnXButtonUp(UINT nFlags, UINT nButton, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    UNREFERENCED_PARAMETER(nButton);
    UNREFERENCED_PARAMETER(point);
    // do nothing
}

void CPlayerPlaylistBar::OnXButtonDblClk(UINT nFlags, UINT nButton, CPoint point)
{
    OnXButtonDown(nFlags, nButton, point);
}
