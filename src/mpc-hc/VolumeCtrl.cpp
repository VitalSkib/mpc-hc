/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2016 see Authors.txt
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
#include "VolumeCtrl.h"
#include "AppSettings.h"
#include "CMPCTheme.h"
#include "CMPCThemeUtil.h"
#include "MainFrm.h"
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
#include <gdiplus.h>
#endif
#undef SubclassWindow


// CVolumeCtrl

IMPLEMENT_DYNAMIC(CVolumeCtrl, CSliderCtrl)
CVolumeCtrl::CVolumeCtrl(bool fSelfDrawn)
    : m_fSelfDrawn(fSelfDrawn)
    , m_bDrag(false)
    , m_bHover(false)
    , modernStyle(AppIsThemeLoaded())
    , showPercentage(AfxGetAppSettings().bShowVolumePercentage)
{
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    Gdiplus::GdiplusStartupInput startup;
    Gdiplus::GdiplusStartup(&m_graphicsToken, &startup, nullptr);
    showPercentage = false; // value is presented by the native tooltip window
#endif
}

CVolumeCtrl::~CVolumeCtrl()
{
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    if (m_graphicsToken) Gdiplus::GdiplusShutdown(m_graphicsToken);
#endif
}

LRESULT CVolumeCtrl::OnMPCThemeChanged(WPARAM wParam, LPARAM lParam)
{
    modernStyle = AppIsThemeLoaded();
    CToolTipCtrl* pTip = GetToolTips();
    if (modernStyle && nullptr != pTip && nullptr == themedToolTip.m_hWnd) {
        themedToolTip.SubclassWindow(pTip->m_hWnd);
    } else if (!modernStyle && nullptr != themedToolTip.m_hWnd) {
        themedToolTip.UnsubclassWindow();
    }
    Invalidate();
    return 0;
}

bool CVolumeCtrl::Create(CWnd* pParentWnd)
{
    DWORD tooltipStyle = showPercentage && AppIsThemeLoaded() ? 0 : TBS_TOOLTIPS;
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    tooltipStyle = 0; // one themed tooltip for both hover and capture
#endif
    if (!CSliderCtrl::Create(WS_CHILD | WS_VISIBLE | TBS_NOTICKS | TBS_HORZ | tooltipStyle, CRect(0, 0, 0, 0), pParentWnd, IDC_SLIDER1)) {
        return false;
    }

    const CAppSettings& s = AfxGetAppSettings();
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    EnableToolTips(FALSE);
    m_volumeTip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX);
    m_volumeTipText.Format(L"Volume %d%%", s.nVolume);
    TOOLINFO ti = { sizeof(TOOLINFO) };
    ti.uFlags = TTF_TRACK | TTF_IDISHWND | TTF_ABSOLUTE;
    ti.hwnd = m_hWnd; ti.uId = (UINT_PTR)m_hWnd;
    ti.lpszText = m_volumeTipText.GetBuffer();
    m_volumeTip.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
    m_volumeTipText.ReleaseBuffer();
#else
    EnableToolTips(TRUE);
#endif
    SetRange(0, 100);
    SetPos(s.nVolume);
    SetPageSize(s.nVolumeStep);
    SetLineSize(0);

    if (AppIsThemeLoaded()) {
        CToolTipCtrl* pTip = GetToolTips();
        if (NULL != pTip) {
            themedToolTip.SubclassWindow(pTip->m_hWnd);
        }
    }

    return true;
}

#ifdef MPC_EXPERIMENTAL_ALPHA_OVERLAY
bool CVolumeCtrl::ForwardOverlayMouse(UINT message, WPARAM flags, double fraction)
{
    if (!IsWindowEnabled()) return false;
    CRect rect;
    GetChannelRect(rect);
    if (!(AppIsThemeLoaded() && modernStyle)) { rect.left += 3; rect.right -= 4; }
    if (rect.Width() <= 1) return false;
    const CPoint point(rect.left + static_cast<int>(lround(fraction * rect.Width())), rect.CenterPoint().y);
    m_overlayInput = true;
    // Existing handlers preserve volume step, notifications and capture behavior.
    if (message == WM_MOUSEWHEEL) {
        CPoint screen = point; ClientToScreen(&screen);
        SendMessage(message, flags, MAKELPARAM(screen.x, screen.y));
    } else SendMessage(message, flags, MAKELPARAM(point.x, point.y));
    m_overlayInput = false;
    return m_bDrag;
}
#endif

void CVolumeCtrl::SetPosInternal(int pos)
{
    SetPos(pos);
    const int currentPos = GetPos();
    // Post even when the clamped position is unchanged (e.g. Volume Up at 100):
    // the frame handler owes the user OSD feedback on every press and dedups the
    // renderer/API work itself.
    WPARAM scroll = MAKEWPARAM(static_cast<WORD>(currentPos), SB_THUMBPOSITION);
    GetParent()->PostMessage(WM_HSCROLL, scroll, reinterpret_cast<LPARAM>(m_hWnd)); // reflected back on us
    POINT p;
    ::GetCursorPos(&p);
    ScreenToClient(&p);
    checkHover(p);
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    Invalidate(FALSE);
    if (m_volumeTipVisible) UpdateVolumeTip(true);
#endif
}

void CVolumeCtrl::IncreaseVolume()
{
    // align volume up to step. recommend using steps 1, 2, 5 and 10
    SetPosInternal(GetPos() + GetPageSize() - GetPos() % GetPageSize());

}

void CVolumeCtrl::DecreaseVolume()
{
    // align volume down to step. recommend using steps 1, 2, 5 and 10
    int m = GetPos() % GetPageSize();
    SetPosInternal(GetPos() - (m ? m : GetPageSize()));
}

BEGIN_MESSAGE_MAP(CVolumeCtrl, CSliderCtrl)
    ON_MPCTHEMECHANGED()
    ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnNMCustomdraw)
    ON_WM_LBUTTONDOWN()
    ON_WM_SETFOCUS()
    ON_WM_HSCROLL_REFLECT()
    ON_WM_SETCURSOR()
    ON_NOTIFY_EX(TTN_NEEDTEXT, 0, OnToolTipNotify)
    ON_WM_MOUSEWHEEL()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSELEAVE()
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    ON_WM_TIMER()
    ON_WM_CAPTURECHANGED()
#endif
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

// CVolumeCtrl message handlers

void CVolumeCtrl::getCustomChannelRect(LPRECT rc)
{
    if (AppIsThemeLoaded()) {
        CRect channelRect;
        GetClientRect(channelRect);
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
        channelRect.DeflateRect(AfxGetMainFrame()->m_dpi.ScaleX(12), 0);
        CopyRect(rc, channelRect);
#else
        CopyRect(rc, CRect(channelRect.left, channelRect.top, channelRect.right - AfxGetMainFrame()->m_dpi.ScaleFloorX(2), channelRect.bottom));
#endif
    } else {
        CRect channelRect;
        GetChannelRect(channelRect);
        CRect thumbRect;
        GetThumbRect(thumbRect);

        CopyRect(rc, CRect(channelRect.left, thumbRect.top + 2, channelRect.right - 2, thumbRect.bottom - 2));
    }
}

void CVolumeCtrl::drawPercentage(CDC& dc, const CStringW& str, const CRect& rc)
{
    const UINT format = DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;

    // DT_VCENTER centres the line box, which is ascent + descent. The readout is only
    // digits and '%', none of which descend, so the descent below the baseline is dead
    // space while the font's internal leading pads the top, and the glyphs sit low in
    // the channel by (ascent - capHeight - descent) / 2. Centre the glyph ink instead,
    // measured from the font itself so it holds at every DPI and toolbar size.
    static const MAT2 identity = { { 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 } };
    TEXTMETRIC tm;
    int aboveBaseline = 0, belowBaseline = 0;
    if (dc.GetTextMetrics(&tm)) {
        for (int i = 0; i < str.GetLength(); i++) {
            GLYPHMETRICS gm;
            if (GDI_ERROR != ::GetGlyphOutlineW(dc.GetSafeHdc(), str[i], GGO_METRICS, &gm, 0, nullptr, &identity)) {
                aboveBaseline = std::max<int>(aboveBaseline, gm.gmptGlyphOrigin.y);
                belowBaseline = std::max<int>(belowBaseline, (int)gm.gmBlackBoxY - gm.gmptGlyphOrigin.y);
            }
        }
    }

    CRect textRect(rc);
    const int inkHeight = aboveBaseline + belowBaseline;
    if (inkHeight <= 0 || inkHeight > rc.Height()) {
        // bitmap font, or the glyphs do not fit anyway: nothing better than DT_VCENTER
        dc.DrawTextW(str, textRect, format | DT_VCENTER);
        return;
    }

    // equal space above and below the ink; any odd leftover pixel goes below, which
    // reads better than the other way round
    textRect.top = rc.top + (rc.Height() - inkHeight) / 2 + aboveBaseline - tm.tmAscent;
    textRect.bottom = textRect.top + tm.tmHeight;
    dc.DrawTextW(str, textRect, format | DT_TOP);
}

void CVolumeCtrl::OnNMCustomdraw(NMHDR* pNMHDR, LRESULT* pResult)
{
    LPNMCUSTOMDRAW pNMCD = reinterpret_cast<LPNMCUSTOMDRAW>(pNMHDR);

    LRESULT lr = CDRF_DODEFAULT;

    bool usetheme = AppIsThemeLoaded();

    if (m_fSelfDrawn)
        switch (pNMCD->dwDrawStage) {
            case CDDS_PREPAINT:
                lr = CDRF_NOTIFYITEMDRAW;
                break;

            case CDDS_ITEMPREPAINT:

                if (pNMCD->dwItemSpec == TBCD_CHANNEL) {
                    CDC dc;
                    dc.Attach(pNMCD->hdc);

                    if (usetheme) {
                        CRect rect;
                        GetClientRect(rect);
                        dc.FillSolidRect(&rect, CMPCTheme::PlayerBGColor);
                    }

                    getCustomChannelRect(&pNMCD->rc);

                    if (usetheme) {
                        DpiHelper dpiWindow;
                        dpiWindow.Override(GetSafeHwnd());

                        CRect r(pNMCD->rc);
                        r.DeflateRect(0, dpiWindow.ScaleFloorY(6), 0, dpiWindow.ScaleFloorY(6));
                        dc.FillSolidRect(r, CMPCTheme::ScrollBGColor);
                        CBrush fb;
                        fb.CreateSolidBrush(CMPCTheme::NoBorderColor);
                        dc.FrameRect(r, &fb);
                        fb.DeleteObject();
                    } else {
                        CPen shadow;
                        CPen light;
                        shadow.CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
                        light.CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DHILIGHT));
                        CPen* old = dc.SelectObject(&light);
                        dc.MoveTo(pNMCD->rc.right, pNMCD->rc.top);
                        dc.LineTo(pNMCD->rc.right, pNMCD->rc.bottom);
                        dc.LineTo(pNMCD->rc.left, pNMCD->rc.bottom);
                        dc.SelectObject(&shadow);
                        dc.LineTo(pNMCD->rc.right, pNMCD->rc.top);
                        dc.SelectObject(old);
                        shadow.DeleteObject();
                        light.DeleteObject();
                    }

                    dc.Detach();
                    lr = CDRF_SKIPDEFAULT;
                } else if (pNMCD->dwItemSpec == TBCD_THUMB) {
                    CDC dc;
                    dc.Attach(pNMCD->hdc);
                    pNMCD->rc.bottom--;
                    CRect r(pNMCD->rc);
                    r.DeflateRect(0, 0, 1, 0);

                    COLORREF shadow = GetSysColor(COLOR_3DSHADOW);
                    COLORREF light = GetSysColor(COLOR_3DHILIGHT);
                    if (usetheme) {
                        CBrush fb;
                        if (m_bDrag) {
                            dc.FillSolidRect(r, CMPCTheme::ScrollThumbDragColor);
                        } else if (m_bHover) {
                            dc.FillSolidRect(r, CMPCTheme::ScrollThumbHoverColor);
                        } else {
                            dc.FillSolidRect(r, CMPCTheme::ScrollThumbColor);
                        }
                        fb.CreateSolidBrush(CMPCTheme::NoBorderColor);
                        dc.FrameRect(r, &fb);
                        fb.DeleteObject();
                    } else {
                        dc.Draw3dRect(&r, light, 0);
                        r.DeflateRect(0, 0, 1, 1);
                        dc.Draw3dRect(&r, light, shadow);
                        r.DeflateRect(1, 1, 1, 1);
                        dc.FillSolidRect(&r, GetSysColor(COLOR_BTNFACE));
                        dc.SetPixel(r.left + 7, r.top - 1, GetSysColor(COLOR_BTNFACE));
                    }

                    dc.Detach();
                    lr = CDRF_SKIPDEFAULT;
                }

                break;
        };

    pNMCD->uItemState &= ~CDIS_FOCUS;

    *pResult = lr;
}

void CVolumeCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
    CRect r;
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    if (AppIsThemeLoaded() && modernStyle) getCustomChannelRect(&r);
    else GetChannelRect(&r);
#else
    GetChannelRect(&r);
#endif

    if (r.left >= r.right) {
        return;
    }

    int start, stop;
    GetRange(start, stop);

    if (!(AppIsThemeLoaded() && modernStyle)) {
        r.left += 3;
        r.right -= 4;
    }

    if (point.x < r.left) {
        SetPosInternal(start);
    } else if (point.x >= r.right) {
        SetPosInternal(stop);
    } else {
        int w = r.right - r.left;
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
        if (AppIsThemeLoaded() && modernStyle) w = std::max(1, w - 1);
#endif
        if (start < stop) {
            if (!(AppIsThemeLoaded() && modernStyle)) {
                SetPosInternal(start + ((stop - start) * (point.x - r.left) + (w / 2)) / w);
            } else {
                SetPosInternal(start + lround((stop - start) * float(point.x - r.left) / w));
            }
        }
    }
    m_bDrag = true;
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    m_thumbAnimation.State(*this, m_bHover, true);
    UpdateVolumeTip(true);
#endif
    if (AppIsThemeLoaded() && modernStyle) {
        if (themedToolTip.m_hWnd
#ifdef MPC_EXPERIMENTAL_ALPHA_OVERLAY
            && !m_overlayInput
#endif
        ) {
            TOOLINFO ti = { sizeof(TOOLINFO) };
            ti.uFlags = TTF_TRACK | TTF_IDISHWND | TTF_ABSOLUTE;
            ti.hwnd = m_hWnd;
            ti.uId = (UINT_PTR)m_hWnd;
            ti.hinst = AfxGetInstanceHandle();
            ti.lpszText = LPSTR_TEXTCALLBACK;

            themedToolTip.SendMessage(TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
        }

        updateModernVolCtrl(point);
        SetCapture();
    } else {
        invalidateThumb();
        CSliderCtrl::OnLButtonDown(nFlags, point);
    }
}

void CVolumeCtrl::OnSetFocus(CWnd* pOldWnd)
{
    CSliderCtrl::OnSetFocus(pOldWnd);

    AfxGetMainWnd()->SetFocus(); // don't focus on us, parents will take care of our positioning
}

void CVolumeCtrl::HScroll(UINT nSBCode, UINT nPos)
{
    auto &s = AfxGetAppSettings();
    auto oldVolume = s.nVolume;
    s.nVolume = GetPos();

    CFrameWnd* pFrame = GetParentFrame();
    if (pFrame && pFrame != GetParent()) {
        // Always forward so the frame shows the volume OSD on every press, including when
        // the value is clamped and unchanged (e.g. Volume Up at 100); the frame dedups the
        // renderer/LCD/API work itself. Only the text redraw is conditioned on a change.
        pFrame->PostMessage(WM_HSCROLL, MAKEWPARAM(static_cast<WORD>(nPos), static_cast<WORD>(nSBCode)), reinterpret_cast<LPARAM>(m_hWnd));
        if (s.nVolume != oldVolume) {
            CRect r;
            getCustomChannelRect(r);
            InvalidateRect(r); //needed to redraw the volume text
        }
    }
}

BOOL CVolumeCtrl::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    ::SetCursor(AfxGetApp()->LoadStandardCursor(IDC_HAND));
    return TRUE;
}

BOOL CVolumeCtrl::OnToolTipNotify(UINT id, NMHDR* pNMHDR, LRESULT* pResult)
{
    TOOLTIPTEXT* pTTT = reinterpret_cast<LPTOOLTIPTEXT>(pNMHDR);
    CString str;
    str.Format(IDS_VOLUME, GetPos());
    _tcscpy_s(pTTT->szText, str);
    pTTT->hinst = nullptr;

    *pResult = 0;

    return TRUE;
}

BOOL CVolumeCtrl::OnMouseWheel(UINT nFlags, short zDelta, CPoint point)
{
    if (zDelta > 0) {
        IncreaseVolume();
    } else if (zDelta < 0) {
        DecreaseVolume();
    } else {
        return FALSE;
    }
    return TRUE;
}

void CVolumeCtrl::invalidateThumb()
{
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    Invalidate(FALSE);
#endif
    if (!(AppIsThemeLoaded() && modernStyle)) {
        SetRangeMax(100, TRUE);
    }
}


void CVolumeCtrl::checkHover(CPoint point)
{
    CRect thumbRect;
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    if (AppIsThemeLoaded()) thumbRect = FluentThumbRect();
    else GetThumbRect(thumbRect);
#else
    GetThumbRect(thumbRect);
#endif
    bool oldHover = m_bHover;
    m_bHover = false;
    if (thumbRect.PtInRect(point)) {
        m_bHover = true;
    }

    if (m_bHover != oldHover) {
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
        m_thumbAnimation.State(*this, m_bHover, m_bDrag);
#endif
        invalidateThumb();
    }
}

void CVolumeCtrl::updateModernVolCtrl(CPoint point)
{
    //CSliderCtrl::OnMouseMove yields bad results due to assumption of thumb width
    //we must do all position calculation ourselves, and send correct position to tooltip

    CRect r;
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    if (AppIsThemeLoaded() && modernStyle) getCustomChannelRect(&r);
    else GetChannelRect(&r);
#else
    GetChannelRect(&r);
#endif

    int start, stop;
    GetRange(start, stop);
    int useX;
    if (point.x < r.left) {
        SetPosInternal(start);
        useX = r.left;
    } else if (point.x >= r.right) {
        SetPosInternal(stop);
        useX = r.right;
    } else {
        int w = r.right - r.left;
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
        if (AppIsThemeLoaded() && modernStyle) w = std::max(1, w - 1);
#endif
        if (start < stop) {
            SetPosInternal(start + lround((stop - start) * float(point.x - r.left) / w));
        }
        useX = point.x;
    }
    POINT p = { useX, point.y };
    ClientToScreen(&p);
    CRect ttRect;
    if (themedToolTip.m_hWnd
#ifdef MPC_EXPERIMENTAL_ALPHA_OVERLAY
        && !m_overlayInput
#endif
    ) {
        CRect cr = r;
        ClientToScreen(cr);
        themedToolTip.GetWindowRect(ttRect);
        p.y = cr.top - ttRect.Height();
        themedToolTip.SendMessage(TTM_TRACKPOSITION, 0, MAKELPARAM(p.x, p.y));
    }

    RECT ur;
    getCustomChannelRect(&ur);
    RedrawWindow(&ur, nullptr, RDW_INVALIDATE); //we must redraw the whole channel with the modern volume ctrl. by default only areas where thumb has been are invalidated
}


void CVolumeCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
    checkHover(point);
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hWnd, 0};
    ::TrackMouseEvent(&track);
    UpdateVolumeTip(true);
    if (flyoutActivity) flyoutActivity();
#endif

    if (AppIsThemeLoaded() && modernStyle && m_bDrag) {
        updateModernVolCtrl(point);
    } else {
        CSliderCtrl::OnMouseMove(nFlags, point);
    }
}


void CVolumeCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (AppIsThemeLoaded() && modernStyle) {
        if (m_bDrag) {
            ReleaseCapture();
        }
        m_bDrag = false;
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
        checkHover(point);
        m_thumbAnimation.State(*this, m_bHover, false);
#endif
        if (themedToolTip.m_hWnd) {
            themedToolTip.SendMessage(TTM_TRACKACTIVATE, FALSE, 0);
        }
    } else {
        m_bDrag = false;
        invalidateThumb();
        checkHover(point);
        CSliderCtrl::OnLButtonUp(nFlags, point);
    }
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    if (flyoutActivity) flyoutActivity();
#endif
}


void CVolumeCtrl::OnMouseLeave()
{
    checkHover(CPoint(-1, -1));
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    if (!m_bDrag) UpdateVolumeTip(false);
#endif
    CSliderCtrl::OnMouseLeave();
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    if (flyoutActivity) flyoutActivity();
#endif
}


#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
void CVolumeCtrl::HideFlyoutFeedback()
{
    if (::GetCapture() == m_hWnd) ::ReleaseCapture();
    UpdateVolumeTip(false);
}

int CVolumeCtrl::DisplayedPosition() const
{
    // Mute suppresses the effective volume, but must preserve the native gain
    // position so unmuting restores the user's previous level.
    return AfxGetAppSettings().fMute ? 0 : GetPos();
}

CRect CVolumeCtrl::FluentThumbRect()
{
    CRect channel; getCustomChannelRect(channel);
    const int x = channel.left + MulDiv(std::max(0, channel.Width() - 1), DisplayedPosition(), 100);
    const int y = channel.CenterPoint().y;
    const int radius = AfxGetMainFrame()->m_dpi.ScaleX(12);
    return CRect(x - radius, y - radius, x + radius + 1, y + radius + 1);
}

void CVolumeCtrl::UpdateVolumeTip(bool visible)
{
    if (!m_volumeTip.GetSafeHwnd()) return;
    visible = visible && !showValueInFlyout;
    TOOLINFO ti = {sizeof(ti)};
    ti.uFlags = TTF_TRACK | TTF_IDISHWND | TTF_ABSOLUTE;
    ti.hwnd = m_hWnd; ti.uId = (UINT_PTR)m_hWnd;
    m_volumeTipText.Format(L"Volume %d%%", DisplayedPosition());
    ti.lpszText = m_volumeTipText.GetBuffer();
    m_volumeTip.SendMessage(TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
    m_volumeTipText.ReleaseBuffer();
    if (visible) {
        CRect thumb = FluentThumbRect();
        CPoint anchor(thumb.CenterPoint().x, thumb.top - AfxGetMainFrame()->m_dpi.ScaleY(24));
        ClientToScreen(&anchor);
        m_volumeTip.SendMessage(TTM_TRACKPOSITION, 0, MAKELPARAM(anchor.x, anchor.y));
    }
    if (m_volumeTipVisible != visible) {
        m_volumeTip.SendMessage(TTM_TRACKACTIVATE, visible, (LPARAM)&ti);
        m_volumeTipVisible = visible;
    }
}

void CVolumeCtrl::OnTimer(UINT_PTR id)
{
    if (!m_thumbAnimation.Tick(*this, id)) CSliderCtrl::OnTimer(id);
}

void CVolumeCtrl::OnCaptureChanged(CWnd* wnd)
{
    m_bDrag = false;
    m_thumbAnimation.State(*this, m_bHover, false);
    UpdateVolumeTip(false);
    CSliderCtrl::OnCaptureChanged(wnd);
}
#endif

void CVolumeCtrl::OnPaint() {
    if (m_fSelfDrawn && AppIsThemeLoaded() && modernStyle) {
        DpiHelper dpiWindow;
        dpiWindow.Override(GetSafeHwnd());

        CPaintDC dc(this);
        CRect r, cr;
        GetClientRect(cr);

        CDC dcMem;
        CBitmap bmMem;
        CRect memRect = { 0, 0, cr.right, cr.bottom };
        CMPCThemeUtil::initMemDC(&dc, dcMem, bmMem, memRect);

        dcMem.FillSolidRect(&memRect, CMPCTheme::PlayerBGColor);
        getCustomChannelRect(r);
        //r.DeflateRect(0, dpiWindow.ScaleFloorY(3), 0, dpiWindow.ScaleFloorY(2));
        r.OffsetRect(-cr.TopLeft());

#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
        if (m_graphicsToken && r.Width() > 0) {
            Gdiplus::Graphics graphics(dcMem.GetSafeHdc());
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const float y = float(r.CenterPoint().y);
            const float x = float(r.left) + float(std::max(0, r.Width() - 1)) * DisplayedPosition() / 100.0f;
            const float stroke = float(dpiWindow.ScaleY(3));
            Gdiplus::Pen track(Gdiplus::Color(255, 155, 155, 155), stroke);
            Gdiplus::Pen played(Gdiplus::Color(255, 76, 194, 255), stroke);
            track.SetStartCap(Gdiplus::LineCapRound); track.SetEndCap(Gdiplus::LineCapRound);
            played.SetStartCap(Gdiplus::LineCapRound); played.SetEndCap(Gdiplus::LineCapRound);
            graphics.DrawLine(&track, float(r.left), y, float(r.right - 1), y);
            if (DisplayedPosition() > 0) graphics.DrawLine(&played, float(r.left), y, x, y);
            const float outer = float(std::min(dpiWindow.ScaleY(int(NativeThumbAnimation::OuterDiameter)), std::max(1, cr.Height() - 2)));
            const float core = outer * m_thumbAnimation.Value() / NativeThumbAnimation::OuterDiameter;
            Gdiplus::SolidBrush ring(Gdiplus::Color(255, 69, 69, 69));
            Gdiplus::SolidBrush dot(Gdiplus::Color(255, 76, 194, 255));
            graphics.FillEllipse(&ring, x - outer / 2, y - outer / 2, outer, outer);
            graphics.FillEllipse(&dot, x - core / 2, y - core / 2, core, core);
        }
#else
        CRect filledRect, unfilledRect;
        filledRect = r;
        filledRect.right = r.left + lround(r.Width() * float(GetPos()) / 100);
        dcMem.FillSolidRect(&filledRect, CMPCTheme::ScrollProgressColor);

        if (filledRect.right < r.right) { //do not fill bg if already full
            unfilledRect = r;
            unfilledRect.left = filledRect.right;
            dcMem.FillSolidRect(&unfilledRect, CMPCTheme::ScrollBGColor);
        }

        CBrush fb;
        fb.CreateSolidBrush(CMPCTheme::NoBorderColor);
        dcMem.FrameRect(r, &fb);
        fb.DeleteObject();

        if (showPercentage) {
            dcMem.SetTextColor(CMPCTheme::TextFGColor);
            CFont f;
            LOGFONT lf = { 0 };
            lf.lfHeight = r.Height();
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, L"Calibri");
            f.CreateFontIndirectW(&lf);
            CFont* oldFont = (CFont*)dcMem.SelectObject(&f);
            int oldMode = dcMem.SetBkMode(TRANSPARENT);
            CStringW str;
            str.Format(IDS_VOLUME, GetPos());
            drawPercentage(dcMem, str, r);
            dcMem.SelectObject(oldFont);
            dcMem.SetBkMode(oldMode);
        }

#endif
        CMPCThemeUtil::flushMemDC(&dc, dcMem, memRect);
    } else {
        __super::OnPaint(); //can trigger OnNMCustomdraw
    }
}

BOOL CVolumeCtrl::OnEraseBkgnd(CDC* pDC) {
    if (m_fSelfDrawn && AppIsThemeLoaded() && modernStyle) {
        return TRUE;
    } else {
        return CSliderCtrl::OnEraseBkgnd(pDC);
    }
}
