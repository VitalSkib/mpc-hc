/* Native playlist artwork only: keep docking, list input and commands. GPLv3+. */
#pragma once
#include "CMPCTheme.h"
namespace NativePlaylistPresentation {
inline bool Active(HWND window) { return !!::GetPropW(window, L"MPC.NativePlaylist"); }
class DockSurface {
    HWND m_dock = nullptr, m_playlist = nullptr;
    COLORREF m_key = CLR_INVALID;
    CRect m_clip = CRect(0, 0, 0, 0);
    bool m_clipped = false;
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR id, DWORD_PTR data) {
        auto self = reinterpret_cast<DockSurface*>(data);
        if (message == WM_NCDESTROY) {
            ::RemoveWindowSubclass(window, Procedure, id); self->m_dock = nullptr;
        } else if (message == WM_NCPAINT) {
            HDC dc = ::GetWindowDC(window);
            if (dc) {
                RECT bounds, client; ::GetWindowRect(window, &bounds); ::GetClientRect(window, &client);
                ::MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&client), 2);
                ::OffsetRect(&client, -bounds.left, -bounds.top);
                ::OffsetRect(&bounds, -bounds.left, -bounds.top);
                ::ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
                HBRUSH brush = ::CreateSolidBrush(CMPCTheme::ContentBGColor);
                ::FillRect(dc, &bounds, brush); ::DeleteObject(brush); ::ReleaseDC(window, dc);
            }
            return 0;
        } else if (message == WM_ERASEBKGND || message == WM_PAINT) {
            PAINTSTRUCT paint = {};
            HDC dc = message == WM_PAINT ? ::BeginPaint(window, &paint) : reinterpret_cast<HDC>(wparam);
            if (dc) {
                const int saved = ::SaveDC(dc);
                for (HWND child = ::GetWindow(window, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT)) {
                    if (!::IsWindowVisible(child)) continue;
                    RECT childRect; ::GetWindowRect(child, &childRect);
                    ::MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&childRect), 2);
                    HRGN region = ::CreateRectRgn(0, 0, 0, 0);
                    if (region && ::GetWindowRgn(child, region) != ERROR) {
                        // A clipped playlist no longer covers the caption/footer bands.
                        ::OffsetRgn(region, childRect.left, childRect.top);
                        ::ExtSelectClipRgn(dc, region, RGN_DIFF);
                    } else ::ExcludeClipRect(dc, childRect.left, childRect.top, childRect.right, childRect.bottom);
                    if (region) ::DeleteObject(region);
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
            if (m_clipped) ::SetWindowRgn(m_dock, nullptr, TRUE);
            ::RemoveWindowSubclass(m_dock, Procedure, reinterpret_cast<UINT_PTR>(this));
            ::SetWindowLongPtrW(m_dock, GWL_EXSTYLE, ::GetWindowLongPtrW(m_dock, GWL_EXSTYLE) & ~WS_EX_LAYERED);
            ::RedrawWindow(m_dock, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        }
        m_dock = m_playlist = nullptr; m_key = CLR_INVALID;
        m_clip.SetRectEmpty(); m_clipped = false;
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
    void ClipTo(const CRect& screenRect) {
        if (!Active()) return;
        RECT bounds; ::GetWindowRect(m_dock, &bounds);
        CRect clip(screenRect); clip.OffsetRect(-bounds.left, -bounds.top);
        if (m_clipped && clip == m_clip) return;
        HRGN region = ::CreateRectRgnIndirect(&clip);
        if (region) {
            if (::SetWindowRgn(m_dock, region, TRUE)) { m_clip = clip; m_clipped = true; }
            else ::DeleteObject(region);
        }
    }
    bool Active() const { return ::IsWindow(m_dock) && NativePlaylistPresentation::Active(m_playlist); }
};
}
