#pragma once
#include <windows.h>

namespace NativePaintResources {
// Stack-only owners: also restore native state when painting throws.
class DibSelection {
    HDC dc; HBITMAP bitmap; HGDIOBJ previous;
public:
    DibSelection(HDC d, HBITMAP b, HGDIOBJ p) : dc(d), bitmap(b), previous(p) {}
    DibSelection(const DibSelection&) = delete;
    DibSelection& operator=(const DibSelection&) = delete;
    ~DibSelection() {
        if (dc && previous && previous != HGDI_ERROR) SelectObject(dc, previous);
        if (dc) DeleteDC(dc);
        if (bitmap) DeleteObject(bitmap);
    }
};
class PaintingFlag {
    bool& flag; bool previous;
public:
    explicit PaintingFlag(bool& f) : flag(f), previous(f) { flag = true; }
    ~PaintingFlag() { flag = previous; }
};
class WindowProperty {
    HWND window; const wchar_t* name;
public:
    WindowProperty(HWND w, const wchar_t* n) : window(w), name(n) {}
    ~WindowProperty() { if (IsWindow(window)) RemovePropW(window, name); }
};
}
