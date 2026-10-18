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
    bool m_material = false, m_bufferedPaint = false, m_systemBorder = false;
    CFont m_titleFont;
    CRect CloseRect() const;
    void Position();
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;
    BOOL PreTranslateMessage(MSG* message) override;
public:
    static COLORREF SurfaceColor() { return RGB(64, 64, 64); }
    static COLORREF BorderColor() { return RGB(69, 69, 69); }
    static COLORREF SelectionColor() { return RGB(76, 76, 76); }
    bool Create(CWnd* owner, CPlayerPlaylistBar* bar);
    void ConfigureMaterial();
    bool MaterialActive() const { return m_material; }
    bool Resizing() const { return m_resizing; }
    void RequestOpen();
    void Refresh(const CRect& band, bool show, int dpi, bool autoHide);
    void OnDropFiles(CAtlList<CStringW>& files, DROPEFFECT effect) override;
    DROPEFFECT OnDropAccept(COleDataObject* data, DWORD keys, CPoint point) override;
};
