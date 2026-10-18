/* Owned playlist flyout: native list and commands, independent video geometry. GPLv3+. */
#pragma once
#include "DropTarget.h"
class CPlayerPlaylistBar;
class CNativePlaylistPopup : public CWnd, public CDropClient {
    CPlayerPlaylistBar* m_bar = nullptr;
    CRect m_band = CRect(0, 0, 0, 0);
    int m_dpi = 96, m_width = 320;
    ULONGLONG m_revealUntil = 0;
    CPoint m_requestPoint = CPoint(0, 0);
    bool m_resizing = false, m_closeHot = false, m_closePressed = false;
    bool m_systemBorder = false, m_translucent = false;
    CSize m_regionSize = CSize(0, 0);
    int m_regionDpi = 0;
    void UpdateRoundedRegion();
    CFont m_titleFont;
    ULONG_PTR m_graphicsToken = 0;
    CRect CloseRect() const;
    void Position();
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;
    BOOL PreTranslateMessage(MSG* message) override;
public:
    static COLORREF SurfaceColor() { return RGB(43, 43, 43); }
    static COLORREF BorderColor() { return RGB(60, 60, 60); }
    static COLORREF SelectionColor() { return RGB(56, 56, 56); }
    bool Create(CWnd* owner, CPlayerPlaylistBar* bar);
    void ConfigureSurface();

    bool Resizing() const { return m_resizing; }
    void RequestOpen();
    void Refresh(const CRect& band, bool show, int dpi, bool autoHide);
    void OnDropFiles(CAtlList<CStringW>& files, DROPEFFECT effect) override;
    DROPEFFECT OnDropAccept(COleDataObject* data, DWORD keys, CPoint point) override;
};
