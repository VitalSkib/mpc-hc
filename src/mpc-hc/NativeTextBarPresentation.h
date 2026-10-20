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
// Present the controls' own WM_PRINT output as one keyed surface. The native
// controls still own their text, fonts, layout, interaction and tooltip data.
inline bool Publish(CWnd& bar) {
    CRect bounds; bar.GetWindowRect(bounds);
    if (bounds.Width() <= 0 || bounds.Height() <= 0) return false;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.Width(); info.bmiHeader.biHeight = -bounds.Height();
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    HDC memory = ::CreateCompatibleDC(nullptr); void* bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memory || !bitmap || !bits) {
        if (bitmap) ::DeleteObject(bitmap);
        if (memory) ::DeleteDC(memory);
        return false;
    }
    const auto previous = ::SelectObject(memory, bitmap);
    CDC dc; dc.Attach(memory);
    dc.FillSolidRect(0, 0, bounds.Width(), bounds.Height(), CMPCTheme::InfoBarBGColor);
    dc.Detach(); // Native owner-draw handlers attach their own CDC wrappers.
    // No PRF_CHECKVISIBLE: prepare the complete frame while its ancestor is
    // hidden/clipped. DefWindowProc dispatches to the native child painters.
    bar.SendMessage(WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_CHILDREN);
    dc.Attach(memory);
    dc.FillSolidRect(0, 0, bounds.Width(), 1, RGB(48, 48, 48));
    dc.Detach();
    SIZE size = {bounds.Width(), bounds.Height()}; POINT origin = {};
    const bool result = !!::UpdateLayeredWindow(bar.m_hWnd, nullptr, nullptr, &size,
        memory, &origin, CMPCTheme::InfoBarBGColor, nullptr, ULW_COLORKEY);
    ::SelectObject(memory, previous); ::DeleteObject(bitmap); ::DeleteDC(memory);
    return result;
}

}
