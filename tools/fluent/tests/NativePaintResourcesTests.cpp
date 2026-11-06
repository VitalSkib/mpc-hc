#include <windows.h>
#include <cstdio>
#include <new>
#include "NativePaintResources.h"

int main() {
    const DWORD initial = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 1000; ++i) {
        bool painting = false;
        try {
            HDC dc = CreateCompatibleDC(nullptr);
            HBITMAP bitmap = CreateBitmap(8, 8, 1, 32, nullptr);
            if (!dc || !bitmap) return 1;
            NativePaintResources::DibSelection dib(dc, bitmap, SelectObject(dc, bitmap));
            NativePaintResources::PaintingFlag flag(painting);
            throw std::bad_alloc();
        } catch (const std::bad_alloc&) {}
        if (painting) return 2;
    }
    if (GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) != initial) return 3;
    std::puts("1000 exception paint cleanup checks passed");
}
