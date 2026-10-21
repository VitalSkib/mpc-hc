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

#pragma once

#include "MouseWndWithArtView.h"
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
#include "NativeControlsDecoration.h"
#include "NativeChromeMotion.h"
#endif
#ifdef MPC_EXPERIMENTAL_ALPHA_OVERLAY
#include "FluentOverlayProbe.h"
#endif

class CChildView : public CMouseWndWithArtView
{
#ifdef MPC_EXPERIMENTAL_ALPHA_OVERLAY
    CFluentOverlayProbe m_fluentProbe;
    bool m_updatingFluent = false;
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;
#endif
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    CNativeControlsDecoration m_nativeHeader, m_nativeBackdrop;
    bool m_refreshingNativeDecoration = false;
    CNativeChromeHost m_topChromeHost, m_bottomChromeHost;
    bool m_chromeHosted = false;
#endif
    bool m_bSwitchingFullscreen;
    bool m_bFirstMedia;

    EventClient m_eventc;

    void EventCallback(MpcEvent ev);
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
    void TraceGeometry(const char* tag);
#endif

public:
#ifdef MPC_EXPERIMENTAL_NATIVE_DECORATION
    void UpdateNativeDecoration();
    bool EnsureNativeChromeHosts();
    bool NativeChromeHosted() const { return m_chromeHosted; }
    int NativeHeaderBottom() const;
    bool IsNativeHeaderWindow(HWND window) const {
        return window && (window == m_nativeHeader.GetSafeHwnd()
            || window == m_topChromeHost.GetSafeHwnd() || window == m_bottomChromeHost.GetSafeHwnd());
    }
#endif
#ifdef MPC_EXPERIMENTAL_ALPHA_OVERLAY
    void UpdateFluentControls();
    bool RouteFluentInput(const MSG& message);
#endif
    CChildView(CMainFrame* pMainFrm);
    virtual ~CChildView();

    DECLARE_DYNAMIC(CChildView)

protected:
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
    virtual BOOL PreTranslateMessage(MSG* pMsg);

    DECLARE_MESSAGE_MAP()

    afx_msg void OnPaint();
    afx_msg void OnSize(UINT nType, int cx, int cy);
#ifdef MPC_EXPERIMENTAL_NATIVE_SEEKBAR
    afx_msg void OnWindowPosChanging(WINDOWPOS* position);
#endif
    afx_msg LRESULT OnNcHitTest(CPoint point);
    afx_msg void OnNcLButtonDown(UINT nHitTest, CPoint point);
public:
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
};
