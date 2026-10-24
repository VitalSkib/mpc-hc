/* Native-controls decoration only; no playback controls or hide timers. GPLv3+. */
#pragma once
#include <gdiplus.h>
#include <functional>
#include <cmath>
#include <vector>
#include "CMPCThemeToolTipCtrl.h"
#include "NativePlaylistMaterial.h"
#include "NativeHeaderInput.h"
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
#include "../DSUtil/GeometryDiagnostics.h"
#endif

class CNativeControlsDecoration final : public CWnd
{
    ULONG_PTR m_graphics = 0;
    bool m_header = false, m_restore = false;
    int m_hot = -1, m_pressed = -1;
    CString m_title;
    CString m_paintedTip;
    std::vector<CRect> m_paintedAreas;
    std::vector<int> m_paintedSeparators;
    CRect m_paintedExclusion = CRect(0, 0, 0, 0);
    int m_scale = 96;
    int m_headerTint = 0;
    CRect m_buttons[3] = {CRect(0, 0, 0, 0), CRect(0, 0, 0, 0), CRect(0, 0, 0, 0)};
    HRESULT m_lastDraw = S_OK;
    CMPCThemeToolTipCtrl m_tip;
    CNativePlaylistMaterial m_material;
    bool m_materialActive = false;

    int Scale(int value) const { return MulDiv(value, m_scale, 96); }
    int HitButton(CPoint point) const {
        if (m_header) for (int i = 0; i < 3; ++i) if (m_buttons[i].PtInRect(point)) return i;
        return -1;
    }
    // Artwork is independent of hit testing and native window commands, so it
    // can later be replaced by custom resources without changing behaviour.
    void DrawWindowButton(Gdiplus::Graphics& graphics, int button) {
        const CRect& rect = m_buttons[button];
        const bool hot = m_hot == button;
        if (hot) {
            const Gdiplus::Color fill = button == 2 ? Gdiplus::Color(255, 196, 43, 28)
                : Gdiplus::Color(m_pressed == button ? 65 : 85, 255, 255, 255);
            Gdiplus::SolidBrush background(fill);
            const int side = std::min({rect.Width(), rect.Height(), Scale(36)});
            const float left = float(rect.CenterPoint().x - side / 2), top = float(rect.CenterPoint().y - side / 2);
            const float diameter = float(Scale(10));
            Gdiplus::GraphicsPath shape;
            shape.AddArc(left, top, diameter, diameter, 180, 90);
            shape.AddArc(left + side - diameter, top, diameter, diameter, 270, 90);
            shape.AddArc(left + side - diameter, top + side - diameter, diameter, diameter, 0, 90);
            shape.AddArc(left, top + side - diameter, diameter, diameter, 90, 90);
            shape.CloseFigure(); graphics.FillPath(&background, &shape);
        }
        // 66% ink coverage gives the previous 12px glyph span at 100% DPI.
        const int iconSize = Scale(18);
        const CRect artwork(rect.CenterPoint().x - iconSize / 2, rect.CenterPoint().y - iconSize / 2,
            rect.CenterPoint().x - iconSize / 2 + iconSize, rect.CenterPoint().y - iconSize / 2 + iconSize);
        const LPCWSTR resource = button == 0 ? L"minimize" : button == 2 ? L"close"
            : m_restore ? L"restore" : L"maximize";
        if (customIcon && customIcon(graphics, artwork, resource, hot)) return;
        Gdiplus::Pen pen(Gdiplus::Color(hot ? 255 : 210, 255, 255, 255), float(std::max(1, Scale(1))));
        const float x = float(rect.CenterPoint().x), y = float(rect.CenterPoint().y);
        const float radius = float(Scale(6));
        if (button == 0) {
            graphics.DrawLine(&pen, x - radius, y, x + radius, y);
        } else if (button == 1) {
            if (m_restore) {
                const float step = float(Scale(3));
                graphics.DrawLine(&pen, x - radius + step, y - radius, x + radius, y - radius);
                graphics.DrawLine(&pen, x + radius, y - radius, x + radius, y + radius - step);
                graphics.DrawLine(&pen, x + radius, y + radius - step, x + radius - step, y + radius - step);
                graphics.DrawRectangle(&pen, x - radius, y - radius + step, 2 * radius - step, 2 * radius - step);
            } else graphics.DrawRectangle(&pen, x - radius, y - radius, 2 * radius, 2 * radius);
        } else {
            graphics.DrawLine(&pen, x - radius, y - radius, x + radius, y + radius);
            graphics.DrawLine(&pen, x + radius, y - radius, x - radius, y + radius);
        }
    }
    bool DrawSurface() {
        CRect bounds; GetWindowRect(bounds);
        const int width = bounds.Width(), height = bounds.Height();
        if (!m_graphics || width <= 0 || height <= 0) return false;
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP bitmap = ::CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap || !pixels) { if (bitmap) ::DeleteObject(bitmap); return false; }
        HDC memory = ::CreateCompatibleDC(nullptr);
        if (!memory) { ::DeleteObject(bitmap); return false; }
        const HGDIOBJ previous = ::SelectObject(memory, bitmap);
        {
            Gdiplus::Bitmap surface(width, height, width * 4, PixelFormat32bppPARGB,
                static_cast<BYTE*>(pixels));
            Gdiplus::Graphics graphics(&surface);
            graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
            if (m_header) {
                // Temporary test value controls our extra dark overlay only.
                // The controller's own Acrylic tint stays at system defaults.
                graphics.Clear(Gdiplus::Color(m_materialActive ? BYTE(MulDiv(m_headerTint, 255, 100)) : BYTE(128), 0, 0, 0));
            }
            else {
                // Union all shaded areas before filling once. Intersections
                // retain 50% opacity instead of accumulating another layer.
                Gdiplus::Region region;
                region.MakeEmpty();
                for (const auto& area : shadedAreas) {
                    region.Union(Gdiplus::Rect(area.left, area.top, area.Width(), area.Height()));
                }
                if (!headerExclusion.IsRectEmpty()) region.Exclude(Gdiplus::Rect(headerExclusion.left,
                    headerExclusion.top, headerExclusion.Width(), headerExclusion.Height()));
                Gdiplus::SolidBrush shade(Gdiplus::Color(153, 0, 0, 0));
                graphics.FillRegion(&shade, &region);
                Gdiplus::Pen separator(Gdiplus::Color(255, 60, 60, 60), 1.0f);
                for (int y : separators) graphics.DrawLine(&separator, 0.0f, float(y), float(width), float(y));
            }
            if (m_header) {
                graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
                Gdiplus::FontFamily requested(semiboldTitle ? L"Segoe UI Semibold" : L"Segoe UI");
                Gdiplus::FontFamily fallback(L"Segoe UI");
                Gdiplus::Font font(requested.GetLastStatus() == Gdiplus::Ok ? &requested : &fallback,
                    float(Scale(16)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush text(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::StringFormat format;
                format.SetLineAlignment(Gdiplus::StringAlignmentNear);
                format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
                format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
                // Avoid fractional vertical centring of the layout rectangle.
                // Keep the same font size and smoothing for a weight-only A/B.
                const float top = std::max(0.0f, std::floor((float(height) - font.GetHeight(&graphics)) * 0.5f + 0.5f));
                int titleLeft = Scale(16);
                // Paint app artwork into this same surface, so the background,
                // icon, title and caption buttons remain one animated header.
                const int iconSize = std::min(Scale(28), height);
                // The SVG has equal internal padding on both axes. Matching
                // its canvas inset to the vertical inset also aligns its
                // visible circle equally from the left, top and bottom.
                const int iconInset = (height-iconSize)/2;
                const CRect icon(iconInset, iconInset,
                    iconInset+iconSize, iconInset+iconSize);
                if (customIcon && customIcon(graphics, icon, L"top-icon", false))
                    titleLeft = icon.right + Scale(8);
                const Gdiplus::RectF title(float(titleLeft), top,
                    float(std::max<LONG>(0, m_buttons[0].left - Scale(8) - titleLeft)), float(height) - top);
                graphics.DrawString(m_title, -1, &font, title, &format, &text);
                for (int button = 0; button < 3; ++button) DrawWindowButton(graphics, button);
            }
        }
        POINT source = {};
        SIZE size = {width, height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        // Layout alone owns placement. A surface repaint must not relocate
        // this layered child, including when its parent is away from (0, 0).
        bool success = false;
        if (m_materialActive) {
            success = m_material.UpdateForeground(pixels, width, height);
            if (success) m_material.SetVisible(true);
            else {
                m_material.Reset(); m_materialActive = false;
                ModifyStyleEx(WS_EX_NOREDIRECTIONBITMAP, WS_EX_LAYERED);
            }
        }
        if (!m_materialActive) success = !!::UpdateLayeredWindow(m_hWnd, nullptr, nullptr, &size,
            memory, &source, 0, &blend, ULW_ALPHA);
        m_lastDraw = success ? S_OK : HRESULT_FROM_WIN32(::GetLastError());
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
        GeometryDiagnostics::Record(m_header ? "decor.header-draw" : "decor.backdrop-draw",
            m_hWnd, nullptr, nullptr, m_lastDraw);
#endif
        ::SelectObject(memory, previous); ::DeleteDC(memory); ::DeleteObject(bitmap);
        return success;
    }

    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        if (message == WM_DESTROY) { m_material.Reset(); m_materialActive = false; }
        if (message == WM_ERASEBKGND) return TRUE;
        if (message == WM_PAINT) { CPaintDC dc(this); DrawSurface(); return 0; }
        if (message == WM_NCHITTEST) {
            if (!m_header) return HTTRANSPARENT;
            CPoint screen(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)), client(screen);
            ScreenToClient(&client);
            const LRESULT hit=frameHitTest?frameHitTest(screen):HTCLIENT;
            return NativeHeaderInput::Hit(hit,HitButton(client)>=0);
        }
        if (m_header && frameTarget && NativeHeaderInput::Action(message)) {
            if (m_tip.GetSafeHwnd()) m_tip.Pop();
            return NativeHeaderInput::Forward(frameTarget->GetSafeHwnd(),message,wParam,lParam);
        }
        if (m_header && message==WM_NCMOUSEMOVE) {
            if (wParam!=HTCAPTION) {
                if (m_tip.GetSafeHwnd()) m_tip.Pop();
                return 0;
            }
            // Native caption/border input is nonclient; tooltip tools use client coordinates.
            CPoint screen(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)), client(screen);
            ScreenToClient(&client);
            MSG hover{}; hover.hwnd=m_hWnd; hover.message=WM_MOUSEMOVE; hover.time=::GetMessageTime();
            hover.lParam=MAKELPARAM(client.x,client.y); hover.pt=screen;
            if (m_tip.GetSafeHwnd()) m_tip.RelayEvent(&hover);
            if (m_hot!=-1) {m_hot=-1;DrawSurface();}
            TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE|TME_NONCLIENT,m_hWnd,0};
            ::TrackMouseEvent(&track);
            return 0;
        }
        if (m_header && message==WM_NCMOUSELEAVE) {
            if (m_tip.GetSafeHwnd()) m_tip.Pop();
            return 0;
        }
        if (message == WM_MOUSEMOVE && m_header) {
            CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            const int hot = HitButton(point);
            if (hot != m_hot) { m_hot = hot; DrawSurface(); }
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hWnd, 0};
            ::TrackMouseEvent(&track);
            return 0;
        }
        if (message == WM_MOUSELEAVE) {
            if (m_hot != -1) { m_hot = -1; DrawSurface(); }
            return 0;
        }
        if (message == WM_LBUTTONDOWN && m_header) {
            CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            m_pressed = HitButton(point);
            // Own the entire click, including release outside the header.
            // Window-button clicks retain their capture and command handling.
            SetCapture();
            if (m_pressed >= 0) { m_hot = m_pressed; DrawSurface(); }
            return 0;
        }
        if (message == WM_LBUTTONUP && m_header) {
            CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            const int button = m_pressed >= 0 && m_pressed == HitButton(point) ? m_pressed : -1;
            m_pressed = -1;
            if (::GetCapture() == m_hWnd) ::ReleaseCapture();
            DrawSurface();
            if (button >= 0 && windowCommand) windowCommand(button);
            return 0;
        }
        if (message == WM_CAPTURECHANGED && m_pressed >= 0) { m_pressed = -1; m_hot = -1; DrawSurface(); }
        if (message == WM_LBUTTONDBLCLK && m_header) {
            CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (HitButton(point) < 0 && windowCommand) windowCommand(1);
            return 0;
        }
        return __super::WindowProc(message, wParam, lParam);
    }
    BOOL PreTranslateMessage(MSG* message) override {
        if (m_tip.GetSafeHwnd()) m_tip.RelayEvent(message);
        return __super::PreTranslateMessage(message);
    }

public:
    std::function<bool(Gdiplus::Graphics&, const CRect&, LPCWSTR, bool)> customIcon;
    std::function<void(int)> windowCommand;
    CWnd* frameTarget = nullptr;
    std::function<LRESULT(CPoint)> frameHitTest;
    bool semiboldTitle = true;
    CString titleTip;
    CWnd* titleTarget = nullptr;
    std::vector<CRect> shadedAreas;
    std::vector<int> separators;
    CRect headerExclusion = CRect(0, 0, 0, 0);
    CNativeControlsDecoration() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_graphics, &input, nullptr);
    }
    ~CNativeControlsDecoration() override {
        if (GetSafeHwnd()) DestroyWindow();
        if (m_graphics) Gdiplus::GdiplusShutdown(m_graphics);
    }
    void Hide() {
        m_hot = m_pressed = -1;
        if (::GetCapture() == GetSafeHwnd() && GetSafeHwnd()) ::ReleaseCapture();
        if (m_tip.GetSafeHwnd()) m_tip.Pop();
        if (m_tip.GetSafeHwnd() && titleTarget) m_tip.SetToolRect(titleTarget, 4, CRect(0, 0, 0, 0));
        if (GetSafeHwnd() && (GetStyle() & WS_VISIBLE)) {
            ShowWindow(SW_HIDE);
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
            GeometryDiagnostics::Record(m_header ? "decor.header-hide" : "decor.backdrop-hide", m_hWnd);
#endif
        }
    }
    void Refresh(CWnd* view, const CRect& rectangle, bool header, const CString& title, int dpi, bool restore = false) {
        if (rectangle.IsRectEmpty()) { Hide(); return; }
        if (!GetSafeHwnd()) {
            m_header = header;
            const DWORD extended = WS_EX_NOACTIVATE | (header ? WS_EX_NOREDIRECTIONBITMAP
                : WS_EX_LAYERED | WS_EX_TRANSPARENT);
            if (!CreateEx(extended, AfxRegisterWndClass(header ? CS_DBLCLKS : 0), L"Native control decoration",
                    WS_CHILD, rectangle, view, 0)) return;
            if (header) {
                m_materialActive = m_material.Start(m_hWnd, 3, false);
                if (!m_materialActive) ModifyStyleEx(WS_EX_NOREDIRECTIONBITMAP, WS_EX_LAYERED);
            }
            if (header && m_tip.Create(this, TTS_ALWAYSTIP)) {
                m_tip.AddTool(this, L"Minimize", CRect(0, 0, 1, 1), 1);
                m_tip.AddTool(this, L"Maximize", CRect(0, 0, 1, 1), 2);
                m_tip.AddTool(this, L"Close", CRect(0, 0, 1, 1), 3);
                CWnd* target = titleTarget ? titleTarget : this;
                // The tooltip follows the same native HWND that receives title input.
                TOOLINFOW tool = {}; tool.cbSize = sizeof(tool); tool.hwnd = target->GetSafeHwnd(); tool.uId = 4;
                tool.uFlags = TTF_SUBCLASS; tool.rect = {0, 0, 1, 1}; tool.lpszText = const_cast<LPWSTR>(L"");
                m_tip.SendMessage(TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
                m_tip.SetMaxTipWidth(800);
            }
        }
        CRect current; GetWindowRect(current); view->ScreenToClient(current);
        const int tint = header ? std::clamp(int(AfxGetApp()->GetProfileInt(L"Settings", L"FluentHeaderAcrylicTint", 0)), 0, 50) : 0;
        if (header && m_tip.GetSafeHwnd()) {
            CWnd* target = titleTarget ? titleTarget : this;
            if (m_paintedTip != titleTip) {
                m_tip.Pop(); m_tip.UpdateTipText(titleTip, target, 4); m_paintedTip = titleTip;
            }
            // rectangle is the new header position in its content parent's coordinates.
            CRect titleRect(Scale(42), 0, std::max(Scale(42), rectangle.Width()-Scale(128)), rectangle.Height());
            if (target != this) {
                titleRect.OffsetRect(rectangle.TopLeft());
                view->ClientToScreen(titleRect); target->ScreenToClient(titleRect);
            }
            if (titleTip.IsEmpty()) titleRect.SetRectEmpty();
            m_tip.SetToolRect(target, 4, titleRect);
        }
        const bool changed = current != rectangle || m_title != title || m_scale != dpi || m_restore != restore
            || m_headerTint != tint
            || m_paintedAreas != shadedAreas || m_paintedSeparators != separators || m_paintedExclusion != headerExclusion || !(GetStyle() & WS_VISIBLE);
        if (!changed) return;
        m_title = title; m_scale = dpi; m_restore = restore;
        m_headerTint = tint;
        m_paintedAreas = shadedAreas; m_paintedSeparators = separators; m_paintedExclusion = headerExclusion;
        const int side = std::min(Scale(40), rectangle.Height());
        for (int button = 0; button < 3; ++button) {
            const int right = rectangle.Width() - (2 - button) * side;
            m_buttons[button].SetRect(std::max(0, right - side), 0, std::max(0, right), side);
            if (m_tip.GetSafeHwnd()) m_tip.SetToolRect(this, button + 1, m_buttons[button]);
        }
        if (m_tip.GetSafeHwnd()) m_tip.UpdateTipText(m_restore ? L"Restore" : L"Maximize", this, 2);
        SetWindowPos(&wndTop, rectangle.left, rectangle.top, rectangle.Width(), rectangle.Height(), SWP_NOACTIVATE);
        if (DrawSurface()) ShowWindow(SW_SHOWNOACTIVATE);
        else Hide(); // leave native controls usable if decoration cannot be rendered
#ifdef MPC_EXPERIMENTAL_GEOMETRY_DIAGNOSTICS
        const RECT expected = rectangle;
        GeometryDiagnostics::Record(m_header ? "decor.header-layout" : "decor.backdrop-layout",
            m_hWnd, &expected, nullptr, m_lastDraw);
#endif
    }
};
