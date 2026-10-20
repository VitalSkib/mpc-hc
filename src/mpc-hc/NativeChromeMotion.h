/* Persistent native chrome viewport; GPLv3+. */
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

class CNativeChromeContent final : public CWnd {
public:
    CWnd* frame = nullptr;
protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        if (message == WM_ERASEBKGND) return TRUE;
        if (message == WM_PAINT) { CPaintDC dc(this); return 0; }
        // Keep native command and seek notifications at their original frame.
        if (frame && frame->GetSafeHwnd()) switch (message) {
            case WM_HSCROLL: case WM_VSCROLL: case WM_NOTIFY: case WM_COMMAND:
            case WM_DRAWITEM: case WM_MEASUREITEM: case WM_CONTEXTMENU:
                return frame->SendMessage(message, wParam, lParam);
        }
        return __super::WindowProc(message, wParam, lParam);
    }
};

class CNativeChromeHost final : public CWnd {
    struct Hosted { HWND window, parent; };
    std::vector<Hosted> m_hosted;
    CNativeChromeContent m_content;
    CRect m_rect = CRect(0, 0, 0, 0);
    bool m_top = false, m_known = false, m_target = false, m_running = false;
    double m_position = 0, m_from = 0;
    ULONGLONG m_start = 0;

    void Apply() {
        if (!m_content.GetSafeHwnd() || m_rect.IsRectEmpty()) return;
        const int offset = static_cast<int>(std::lround((1.0 - m_position) * m_rect.Height())) * (m_top ? -1 : 1);
        CRect before; m_content.GetWindowRect(before); ScreenToClient(before);
        const CRect after(0, offset, m_rect.Width(), offset + m_rect.Height());
        if (before != after) m_content.SetWindowPos(nullptr, after.left, after.top, after.Width(), after.Height(),
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void DetachControls() {
        for (const auto& item : m_hosted) if (::IsWindow(item.window) && ::IsWindow(item.parent))
            ::SetParent(item.window, item.parent);
        m_hosted.clear();
    }
protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        if (message == WM_ERASEBKGND) return TRUE;
        if (message == WM_PAINT) { CPaintDC dc(this); return 0; }
        if (message == WM_DESTROY) { KillTimer(1); DetachControls(); }
        if (message == WM_TIMER && wParam == 1) {
            if (!m_running) return 0;
            if (!m_start) m_start = GetTickCount64();
            const double p = std::min(1.0, (GetTickCount64() - m_start) / 160.0);
            const double eased = m_target ? 1.0 - std::pow(1.0 - p, 3.0) : p * p * p;
            m_position = m_from + ((m_target ? 1.0 : 0.0) - m_from) * eased;
            Apply();
            if (p >= 1.0) {
                KillTimer(1); m_running = false;
                if (!m_target) ShowWindow(SW_HIDE);
            }
            return 0;
        }
        return __super::WindowProc(message, wParam, lParam);
    }
public:
    ~CNativeChromeHost() override { DetachControls(); }
    bool Ensure(CWnd* view, CWnd* frame, bool top) {
        if (GetSafeHwnd()) return m_content.GetSafeHwnd() != nullptr;
        m_top = top;
        const auto cls = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW));
        if (!CreateEx(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, cls, L"Native chrome viewport",
                WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CRect(0, 0, 0, 0), view, 0)) return false;
        m_content.frame = frame;
        if (!m_content.CreateEx(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, cls, L"Native chrome content",
                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                CRect(0, 0, 0, 0), this, 0)) { DestroyWindow(); return false; }
        return true;
    }
    CWnd* Content() { return &m_content; }
    void RestoreControls() { DetachControls(); }
    bool Adopt(CWnd& window, CWnd* frame) {
        if (::GetParent(window.m_hWnd) == m_content.m_hWnd) return true;
        const HWND parent = ::GetParent(window.m_hWnd);
        window.SetParent(&m_content);
        if (::GetParent(window.m_hWnd) != m_content.m_hWnd) return false;
        window.SetOwner(frame);
        m_hosted.push_back({window.m_hWnd, parent});
        return true;
    }
    void Layout(const CRect& rectangle) {
        if (rectangle.IsRectEmpty() || m_rect == rectangle) return;
        m_rect = rectangle;
        SetWindowPos(&wndTop, rectangle.left, rectangle.top, rectangle.Width(), rectangle.Height(), SWP_NOACTIVATE);
        if (m_known) { KillTimer(1); m_running = false; m_position = m_target ? 1.0 : 0.0; }
        Apply();
        if (m_known && !m_target) ShowWindow(SW_HIDE);
    }
    void Present(bool visible, bool animate) {
        if (m_rect.IsRectEmpty()) visible = false;
        if (m_known && m_target == visible && (animate || !m_running)) return;
        m_known = true; m_target = visible;
        KillTimer(1); m_running = false;
        if (!animate) {
            m_position = visible ? 1.0 : 0.0; Apply();
            ShowWindow(visible ? SW_SHOWNOACTIVATE : SW_HIDE); return;
        }
        m_from = m_position; m_start = 0;
        Apply();
        if (visible || m_position > 0) ShowWindow(SW_SHOWNOACTIVATE);
        if (m_position == (visible ? 1.0 : 0.0)) return;
        m_running = SetTimer(1, 15, nullptr) != 0;
        if (!m_running) {
            m_position = visible ? 1.0 : 0.0; Apply();
            ShowWindow(visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        }
    }
};
