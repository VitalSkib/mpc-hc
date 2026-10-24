/* Owned playlist flyout: native list and commands, independent video geometry. GPLv3+. */
#pragma once
#include "DropTarget.h"
#include "NativePlaylistMaterial.h"
#include <vector>
#include <functional>
class CPlayerPlaylistBar;
class CNativePlaylistPopup : public CWnd, public CDropClient {
    CPlayerPlaylistBar* m_bar = nullptr;
    CRect m_band = CRect(0, 0, 0, 0);
    int m_dpi = 96;
    ULONGLONG m_dropUntil = 0;
    CPoint m_hiddenPoint = CPoint(0, 0);
    bool m_forceReveal = false, m_entered = false, m_closeHot = false, m_closePressed = false;
    bool m_systemBorder = false;
    bool m_materialActive = false, m_materialPainting = false, m_bufferedPaintReady = false;
    CNativePlaylistMaterial m_systemMaterial;
    int m_materialTint = 128;
    int m_materialMode = 3; // documented DWM: 1 = solid, 2 = Mica, 3 = Acrylic
    CFont m_titleFont;
    ULONG_PTR m_graphicsToken = 0;
    CRect CloseRect() const;
    void Position();
    bool EnsureSurfaceMode(bool composition);
    void PaintSurface(CDC& dc);
    CWnd m_revealWindow;
    HDC m_revealDC = nullptr;
    HBITMAP m_revealBitmap = nullptr, m_revealOldBitmap = nullptr;
    DWORD* m_revealBits = nullptr;
    std::vector<DWORD> m_revealPixels;
    std::vector<BYTE> m_revealMask;
    CRect m_revealRect = CRect(0, 0, 0, 0);
    ULONGLONG m_revealStart = 0;
    bool m_revealing = false, m_fading = false;
    bool m_liveSliding = false;
    bool m_liveFading = false;
    ULONGLONG m_liveFadeStart = 0;
    bool BeginLiveFade();
    void TickLiveFade();
    void EndLiveFade();
    ULONGLONG m_liveSlideStart = 0;
    bool BeginLiveSlide();
    void TickLiveSlide();
    void EndLiveSlide(bool show);
    bool BeginReveal(bool fading = false);
    bool DrawReveal(double progress);
    void TickReveal();
    void EndReveal(bool showLive);
    void Dismiss();
    void HideEnabled(bool animate = true);
    CRect HoverRect() const;
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;
    BOOL PreTranslateMessage(MSG* message) override;
public:
    static COLORREF SurfaceColor() { return RGB(43, 43, 43); }
    static COLORREF BorderColor() { return RGB(60, 60, 60); }
    static COLORREF SelectionColor() { return RGB(56, 56, 56); }
    bool Create(CWnd* owner, CPlayerPlaylistBar* bar, bool composition = true);
    void ConfigureSurface();
    bool MaterialActive() const { return m_materialActive; }
    bool MaterialLive() const { return m_materialActive && m_systemMaterial.Active(); }
    bool PaintMaterialList();
    void QueueMaterialForeground();
    void UpdateMaterialForeground();
    bool m_foregroundPending = false;
    std::vector<DWORD> m_foregroundPixels;
    int m_foregroundWidth = 0, m_foregroundHeight = 0;
    static bool PaintMaterialBuffer(HDC target, const RECT& rect, const std::function<void(HDC)>& draw, BYTE tint);

    bool Revealing() const { return (m_revealing && !m_fading) || m_liveSliding; }
    bool WantsReveal() const;
    void RequestOpen();
    void Refresh(const CRect& band, bool show, int dpi, bool controlsVisible);
    void OnDropFiles(CAtlList<CStringW>& files, DROPEFFECT effect) override;
    DROPEFFECT OnDropAccept(COleDataObject* data, DWORD keys, CPoint point) override;
};
