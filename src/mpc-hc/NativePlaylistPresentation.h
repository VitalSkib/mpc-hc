/* Native playlist artwork only: keep docking, list input and commands. GPLv3+. */
#pragma once
#include "CMPCTheme.h"
namespace NativePlaylistPresentation {
inline bool Active(HWND window) { return !!::GetPropW(window, L"MPC.NativePlaylist"); }
class DockSurface {
    HWND m_dock = nullptr, m_playlist = nullptr;
    COLORREF m_key = CLR_INVALID;
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR id, DWORD_PTR data) {
        auto self = reinterpret_cast<DockSurface*>(data);
        if (message == WM_NCDESTROY) {
            ::RemoveWindowSubclass(window, Procedure, id); self->m_dock = nullptr;
        } else if (message == WM_ERASEBKGND || message == WM_PAINT) {
            PAINTSTRUCT paint = {};
            HDC dc = message == WM_PAINT ? ::BeginPaint(window, &paint) : reinterpret_cast<HDC>(wparam);
            if (dc) {
                const int saved = ::SaveDC(dc);
                for (HWND child = ::GetWindow(window, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT)) {
                    if (!::IsWindowVisible(child)) continue;
                    RECT childRect; ::GetWindowRect(child, &childRect);
                    ::MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&childRect), 2);
                    ::ExcludeClipRect(dc, childRect.left, childRect.top, childRect.right, childRect.bottom);
                }
                RECT rect; ::GetClientRect(window, &rect);
                HBRUSH brush = ::CreateSolidBrush(CMPCTheme::ContentBGColor);
                ::FillRect(dc, &rect, brush); ::DeleteObject(brush); ::RestoreDC(dc, saved);
            }
            if (message == WM_PAINT) ::EndPaint(window, &paint);
            return message == WM_ERASEBKGND ? 1 : 0;
        }
        return ::DefSubclassProc(window, message, wparam, lparam);
    }
public:
    ~DockSurface() { Reset(); }
    void Reset() {
        if (::IsWindow(m_playlist)) ::RemovePropW(m_playlist, L"MPC.NativePlaylist");
        if (::IsWindow(m_dock)) {
            ::RemoveWindowSubclass(m_dock, Procedure, reinterpret_cast<UINT_PTR>(this));
            ::SetWindowLongPtrW(m_dock, GWL_EXSTYLE, ::GetWindowLongPtrW(m_dock, GWL_EXSTYLE) & ~WS_EX_LAYERED);
            ::RedrawWindow(m_dock, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        m_dock = m_playlist = nullptr; m_key = CLR_INVALID;
    }
    bool Update(CWnd& playlist, bool enabled) {
        const HWND parent = playlist.GetSafeHwnd() ? ::GetParent(playlist.m_hWnd) : nullptr;
        const int id = parent ? ::GetDlgCtrlID(parent) : 0;
        enabled = enabled && (id == AFX_IDW_DOCKBAR_LEFT || id == AFX_IDW_DOCKBAR_RIGHT
            || id == AFX_IDW_DOCKBAR_TOP || id == AFX_IDW_DOCKBAR_BOTTOM);
        // A dock bar shared by other visible panels keeps its native palette.
        if (enabled) for (HWND child = ::GetWindow(parent, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT)) {
            if (child != playlist.m_hWnd && ::IsWindowVisible(child)) { enabled = false; break; }
        }
        if (!enabled || m_dock != parent) Reset();
        if (!enabled) return false;
        if (!m_dock) {
            // Do not take ownership of layering belonging to another feature.
            if (::GetWindowLongPtrW(parent, GWL_EXSTYLE) & WS_EX_LAYERED) return false;
            if (!::SetWindowSubclass(parent, Procedure, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this))) return false;
            m_dock = parent; m_playlist = playlist.m_hWnd;
            ::SetPropW(m_playlist, L"MPC.NativePlaylist", reinterpret_cast<HANDLE>(1));
            ::SetWindowLongPtrW(parent, GWL_EXSTYLE, ::GetWindowLongPtrW(parent, GWL_EXSTYLE) | WS_EX_LAYERED);
        }
        if (m_key != CMPCTheme::ContentBGColor) {
            if (!::SetLayeredWindowAttributes(m_dock, CMPCTheme::ContentBGColor, 255, LWA_COLORKEY)) { Reset(); return false; }
            m_key = CMPCTheme::ContentBGColor;
            ::RedrawWindow(m_dock, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        return true;
    }
    bool Active() const { return ::IsWindow(m_dock) && NativePlaylistPresentation::Active(m_playlist); }
};
}
