/* Preserve native text bars; key their background onto the shared backdrop. GPLv3+. */
#pragma once
#include "CMPCTheme.h"
namespace NativeTextBarPresentation {
inline bool Prepare(CWnd& bar) {
    if (!bar.GetSafeHwnd()) return false;
    const bool added = !(bar.GetExStyle() & WS_EX_LAYERED);
    if (added) bar.ModifyStyleEx(0, WS_EX_LAYERED);
    const COLORREF wanted = CMPCTheme::InfoBarBGColor;
    COLORREF key = 0; BYTE alpha = 0; DWORD flags = 0;
    if (::GetLayeredWindowAttributes(bar.GetSafeHwnd(), &key, &alpha, &flags)
            && flags == LWA_COLORKEY && key == wanted) return true;
    // Re-key only when necessary (e.g. palette changed). Do not change opacity
    // of glyphs, move windows or ask the renderer for an extra frame.
    if (::SetLayeredWindowAttributes(bar.GetSafeHwnd(), wanted, 255, LWA_COLORKEY)) {
        // Static labels must paint into the newly configured surface too;
        // changing values already invalidate themselves during playback.
        bar.RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
        return true;
    }
    if (added) bar.ModifyStyleEx(WS_EX_LAYERED, 0);
    return false;
}
}
