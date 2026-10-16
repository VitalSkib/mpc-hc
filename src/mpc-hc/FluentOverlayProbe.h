/* Experimental native alpha overlay. Distributed under MPC-HC's GPLv3+ license. */
#pragma once
#include <gdiplus.h>
#include "CMPCThemeToolTipCtrl.h"

class CFluentOverlayProbe final : public CWnd
{
public:
    struct Button {
        UINT id;
        int image;
        BYTE state;
        CString tip;
        bool operator==(const Button& other) const {
            return id == other.id && image == other.image && state == other.state && tip == other.tip;
        }
    };
    std::function<void(UINT, const CRect&, bool)> invokeButton;
    std::function<void(bool, CPoint)> timeClick;
    std::function<bool(UINT, WPARAM, double)> seekMouse, volumeMouse;
    std::function<bool()> seekActive, volumeActive;

private:
    ULONG_PTR m_gdiplus = 0;
    std::unique_ptr<Gdiplus::Font> m_smallIcons, m_largeIcons, m_frameIcons;
    int m_hotTime = -1;
    bool m_remainingTime = false;
    float m_seekHover = 0, m_volumeHover = 0, m_seekTarget = 0, m_volumeTarget = 0;
    ULONGLONG m_animationTick = 0, m_timerDue = 0, m_frameTick = 0;
    static constexpr UINT_PTR PositionTool = 0xfffffff0, DurationTool = 0xfffffff1;
    bool AnimatingSliders() const { return m_seekHover != m_seekTarget || m_volumeHover != m_volumeTarget; }
    void AdvanceSliders(ULONGLONG now) {
        const float step = std::min(1.0f, static_cast<float>(now - m_animationTick) / 120.0f);
        const auto advance = [step](float value, float target) {
            return value < target ? std::min(target, value + step) : std::max(target, value - step);
        };
        m_seekHover = advance(m_seekHover, m_seekTarget); m_volumeHover = advance(m_volumeHover, m_volumeTarget);
        m_animationTick = now;
    }
    void SliderTargets(float seek, float volume) {
        if (seek == m_seekTarget && volume == m_volumeTarget) return;
        const ULONGLONG now = GetTickCount64(); AdvanceSliders(now);
        m_seekTarget = seek; m_volumeTarget = volume; m_animationTick = now;
    }
    CWnd* m_view = nullptr;
    CMPCThemeToolTipCtrl m_tip, m_volumeTip;
    CString m_volumeTipText;
    bool m_volumeTipActive = false;
    static constexpr UINT_PTR VolumeTool = 0xfffffff2, CloseTool = 0xfffffff3;
    void HideVolumeTip() {
        if (!m_volumeTip.GetSafeHwnd() || !m_volumeTipActive) return;
        TOOLINFO tool = {}; tool.cbSize = sizeof(tool); tool.hwnd = m_hWnd; tool.uId = VolumeTool;
        m_volumeTipActive = false;
        m_volumeTip.SendMessage(TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
    }
    void UpdateVolumeTip(CPoint cursor) {
        if (!m_volumeTip.GetSafeHwnd()) return;
        const bool dragging = m_volumeDragging && volumeActive && volumeActive();
        if (!IsWindowVisible() || !m_panelsVisible || !m_volumeVisible || (!HitVolume(cursor) && !dragging)) {
            HideVolumeTip(); return;
        }
        TOOLINFO tool = {}; tool.cbSize = sizeof(tool); tool.hwnd = m_hWnd; tool.uId = VolumeTool;
        tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        CString text; text.Format(L"Volume %d%%", MulDiv(m_volume, 100, std::max(1, m_volumeMaximum)));
        if (text != m_volumeTipText) {
            m_volumeTipText = text; tool.lpszText = const_cast<LPWSTR>(m_volumeTipText.GetString());
            m_volumeTip.SendMessage(TTM_UPDATETIPTEXT, 0, reinterpret_cast<LPARAM>(&tool));
            m_volumeTip.Update();
        }
        if (!m_volumeTipActive) {
            m_tip.Pop();
            m_volumeTipActive = true;
            m_volumeTip.SendMessage(TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
        }
        CRect tip; m_volumeTip.GetWindowRect(tip);
        CPoint anchor(m_volumeRect.CenterPoint().x, m_volumeRect.top - 7); ClientToScreen(&anchor);
        m_volumeTip.SendMessage(TTM_TRACKPOSITION, 0, MAKELPARAM(anchor.x - tip.Width() / 2, anchor.y - tip.Height()));
    }
    CString m_title, m_time, m_positionText, m_durationText, m_feedback;
    bool m_fullscreen = false, m_idleHidden = false, m_panelsVisible = true;
    ULONGLONG m_lastInput = GetTickCount64(), m_fadeStart = 0, m_feedbackUntil = 0;
    CPoint m_lastCursor = CPoint(INT_MIN, INT_MIN);
    BYTE m_opacity = 255;
    static constexpr UINT_PTR DeadlineTimer = 1;
    void ArmDeadline() {
        if (!GetSafeHwnd()) return;
        if (!IsWindowVisible()) { KillTimer(DeadlineTimer); m_timerDue = 0; return; }
        const ULONGLONG now = GetTickCount64();
        ULONGLONG due = m_feedbackUntil > now ? m_feedbackUntil : 0;
        if (m_fullscreen && m_panelsVisible && !m_idleHidden && !CapturedInput()) {
            const ULONGLONG idleDue = m_fadeStart ? m_frameTick + 40 : m_lastInput + 4000;
            if (!due || idleDue < due) due = idleDue;
        }
        if (AnimatingSliders() && (!due || m_animationTick + 16 < due)) due = m_animationTick + 16;
        // Repeated native UI refreshes must not postpone an animation deadline.
        if (due && due == m_timerDue) return;
        KillTimer(DeadlineTimer); m_timerDue = due;
        if (due) SetTimer(DeadlineTimer, static_cast<UINT>(std::max<ULONGLONG>(1, due > now ? due - now : 1)), nullptr);
    }
    void WakeControls() {
        m_lastInput = GetTickCount64(); m_fadeStart = 0; m_idleHidden = false;
        const bool faded = m_opacity != 255; m_opacity = 255;
        if (faded && GetSafeHwnd() && IsWindowVisible()) DrawSurface();
        ArmDeadline();
    }
    static CString DisplayTitle(CString title) {
        // Only known media suffixes: a dot in a film/program title is meaningful.
        title.Trim();
        const int dot = title.ReverseFind(L'.');
        if (dot >= 0) {
            CString suffix = title.Mid(dot); suffix.MakeLower();
            for (LPCWSTR extension : {L".mp4", L".mkv", L".avi", L".mov", L".wmv", L".webm", L".m4v", L".mpg", L".mpeg", L".ts", L".m2ts", L".mp3", L".flac", L".wav", L".aac", L".ogg", L".m4a"}) {
                if (suffix == extension) { title.Truncate(dot); break; }
            }
        }
        return title;
    }
    CSize m_size = CSize(0, 0), m_layoutSize = CSize(0, 0);
    std::vector<Button> m_buttons;
    std::vector<CRect> m_buttonRects;
    HIMAGELIST m_images = nullptr;
    bool m_toolbarVisible = false, m_seekVisible = false, m_volumeVisible = false;
    bool m_controlsVisible = false, m_canSeek = false, m_dirty = true;
    bool m_seeking = false, m_volumeDragging = false;
    int m_volume = 0, m_volumeMaximum = 100, m_alignment = 0;
    bool m_timePressed = false, m_timeRight = false, m_blockedPress = false;
    int m_hotButton = -1, m_pressedButton = -1;
    bool m_closeHot = false, m_closePressed = false, m_rightPressed = false;
    LONGLONG m_start = 0, m_stop = 0, m_position = 0;
    int m_bottomHeight = 0, m_overflowStart = -1;
    CRect m_overflowRect;
    UINT m_pressedId = 0;
    bool Compact() const { return m_size.cx < 480 || m_size.cy < 300; }
    CRect m_timeline, m_volumeRect, m_timeRect, m_durationRect;
    int HeaderHeight() const { return Compact() ? 40 : 44; }
    bool HitTime(CPoint point) const { return m_seekVisible && (m_timeRect.PtInRect(point) || m_durationRect.PtInRect(point)); }

    CRect CloseRect() const { return CRect(m_size.cx - 34, (HeaderHeight() - 28) / 2, m_size.cx - 6, (HeaderHeight() + 28) / 2); }
    bool CapturedInput() const { return m_seeking || m_volumeDragging || m_closePressed || m_pressedButton != -1 || m_timePressed || m_blockedPress; }
    int HitButton(CPoint point) const {
        for (size_t i = 0; i < m_buttonRects.size(); ++i) if (m_buttonRects[i].PtInRect(point)) return static_cast<int>(i);
        return m_overflowRect.PtInRect(point) ? -2 : -1;
    }
    bool HitSeek(CPoint point) const {
        CRect hit = m_timeline; hit.InflateRect(7, 4);
        return m_seekVisible && hit.PtInRect(point);
    }
    static bool HitThumb(const CRect& rect, double fraction, CPoint point) {
        const int x = rect.left + static_cast<int>(std::clamp(fraction, 0.0, 1.0) * std::max(1, rect.Width() - 1));
        return std::abs(point.x - x) <= 10 && std::abs(point.y - rect.CenterPoint().y) <= 10;
    }
    bool HitVolume(CPoint point) const {
        CRect hit = m_volumeRect;
        hit.InflateRect(7, 5);
        return m_volumeVisible && hit.PtInRect(point);
    }
    bool IsControlPoint(CPoint point) const {
        return m_panelsVisible && (CloseRect().PtInRect(point) || (m_controlsVisible &&
            (HitButton(point) != -1 || HitTime(point) || HitSeek(point)
                || HitVolume(point))));
    }
    bool InsideView() const {
        CPoint point;
        ::GetCursorPos(&point);
        CRect rc;
        m_view->GetWindowRect(rc);
        if (!rc.PtInRect(point)) return false;
        const HWND window = ::WindowFromPoint(point);
        return window && (window == m_tip.GetSafeHwnd() || window == m_volumeTip.GetSafeHwnd() || window == m_view->GetSafeHwnd() || ::IsChild(m_view->GetSafeHwnd(), window));
    }
    void EnsureGeometry() {
        if (!m_dirty && m_layoutSize == m_size) return;
        if (m_tip.GetSafeHwnd()) {
            for (size_t i = 0; i < m_buttonRects.size(); ++i) m_tip.DelTool(this, static_cast<UINT_PTR>(i + 1));
            m_tip.DelTool(this, PositionTool); m_tip.DelTool(this, DurationTool); m_tip.DelTool(this, CloseTool);
        }
        m_buttonRects.clear();
        m_timeline.SetRectEmpty(); m_volumeRect.SetRectEmpty(); m_timeRect.SetRectEmpty(); m_durationRect.SetRectEmpty();
        const int margin = Compact() ? 12 : 16;
        const int left = margin, right = m_size.cx - margin, cell = Compact() ? 24 : 28;
        const int volumeWidth = m_volumeVisible ? (Compact() ? 60 : 84) : 0;
        const int gap = m_volumeVisible ? 8 : 0;
        const int spaceForButtons = right - left - volumeWidth - gap;
        int capacity = std::max(0, spaceForButtons / cell);
        const bool overflow = m_toolbarVisible && static_cast<int>(m_buttons.size()) > capacity;
        if (overflow) capacity = std::max(0, capacity - 1);
        const int visibleCount = m_toolbarVisible ? std::min(static_cast<int>(m_buttons.size()), capacity) : 0;
        const int rows = m_toolbarVisible && (!m_buttons.empty() || m_volumeVisible) ? 1 : 0;
        const int seekHeight = m_seekVisible ? (Compact() ? 24 : 28) : 0;
        m_bottomHeight = rows * cell + seekHeight + 8;
        const int top = m_size.cy - m_bottomHeight;
        if (m_seekVisible) {
            const int maxLabel = std::max(0, (right - left - 48) / 2);
            const int charWidth = Compact() ? 7 : 8;
            const int leftWidth = m_positionText.IsEmpty() ? 0 : std::min(maxLabel, charWidth * m_positionText.GetLength() + 8);
            const int rightWidth = m_durationText.IsEmpty() ? 0 : std::min(maxLabel, charWidth * m_durationText.GetLength() + 8);
            m_timeRect = CRect(left, top + 2, left + leftWidth, top + seekHeight + 2);
            m_durationRect = CRect(right - rightWidth, top + 2, right, top + seekHeight + 2);
            m_timeline = CRect(left + leftWidth + 4, top + 4, right - rightWidth - 4, top + seekHeight);
        }
        m_buttonRects.resize(m_buttons.size());
        const int groupWidth = (visibleCount + (overflow ? 1 : 0)) * cell;
        const int totalWidth = groupWidth + volumeWidth + gap;
        const int groupLeft = m_alignment == 1 ? left + std::max(0, (right - left - totalWidth) / 2)
            : m_alignment == 2 ? std::max(left, right - totalWidth) : left;
        int x = groupLeft;
        const int y = top + seekHeight + 2;
        for (int i = 0; i < visibleCount; ++i) {
            m_buttonRects[i] = CRect(x, y, x + cell, y + cell);
            x += cell;
        }
        m_overflowRect.SetRectEmpty(); m_overflowStart = -1;
        if (overflow) {
            m_overflowStart = visibleCount;
            m_overflowRect = CRect(x, y, x + cell, y + cell);
        }
        if (m_volumeVisible) {
            const int volumeLeft = m_alignment == 0 ? right - volumeWidth : groupLeft + groupWidth + gap;
            m_volumeRect = CRect(volumeLeft + 7, y + 3, volumeLeft + volumeWidth - 3, y + cell - 3);
        }
        if (m_tip.GetSafeHwnd()) {
            for (size_t i = 0; i < m_buttonRects.size(); ++i) {
                if (!m_buttonRects[i].IsRectEmpty()) m_tip.AddTool(this, m_buttons[i].tip, m_buttonRects[i], static_cast<UINT_PTR>(i + 1));
            }
            m_tip.AddTool(this, L"Close", CloseRect(), CloseTool);
            if (!m_timeRect.IsRectEmpty()) m_tip.AddTool(this, m_remainingTime ? L"Time remaining" : L"Time elapsed", m_timeRect, PositionTool);
            if (!m_durationRect.IsRectEmpty()) m_tip.AddTool(this, L"Total duration", m_durationRect, DurationTool);
        }
        m_layoutSize = m_size;
        m_dirty = false;
    }
    static std::unique_ptr<Gdiplus::Bitmap> DecodeIcon(HICON icon) {
        // Bitmap(HICON) can flatten the icon against black. Read its actual
        // premultiplied BGRA pixels, falling back to the native AND mask.
        struct IconBits {
            ICONINFO info = {};
            ~IconBits() {
                if (info.hbmColor) ::DeleteObject(info.hbmColor);
                if (info.hbmMask) ::DeleteObject(info.hbmMask);
            }
        } source;
        if (!::GetIconInfo(icon, &source.info) || !source.info.hbmColor) return nullptr;
        BITMAP bitmap = {};
        if (!::GetObject(source.info.hbmColor, sizeof(bitmap), &bitmap)) return nullptr;
        const int width = bitmap.bmWidth, height = std::abs(bitmap.bmHeight);
        if (width <= 0 || height <= 0) return nullptr;
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        std::vector<DWORD> pixels(static_cast<size_t>(width) * height);
        HDC dc = ::CreateCompatibleDC(nullptr);
        if (!dc) return nullptr;
        const bool read = ::GetDIBits(dc, source.info.hbmColor, 0, height, pixels.data(), &info, DIB_RGB_COLORS) == height;
        if (!read) { ::DeleteDC(dc); return nullptr; }
        const bool alpha = std::any_of(pixels.begin(), pixels.end(), [](DWORD pixel) { return (pixel >> 24) != 0; });
        if (!alpha) {
            std::vector<DWORD> mask(pixels.size());
            if (!source.info.hbmMask || ::GetDIBits(dc, source.info.hbmMask, 0, height, mask.data(), &info, DIB_RGB_COLORS) != height) {
                ::DeleteDC(dc); return nullptr;
            }
            for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = (mask[i] & 0x00ffffff) ? 0 : (pixels[i] | 0xff000000);
        }
        ::DeleteDC(dc);
        auto image = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
        Gdiplus::Rect rect(0, 0, width, height);
        Gdiplus::BitmapData target = {};
        if (image->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &target) != Gdiplus::Ok) return nullptr;
        for (int y = 0; y < height; ++y)
            memcpy(static_cast<BYTE*>(target.Scan0) + static_cast<ptrdiff_t>(y) * target.Stride,
                pixels.data() + static_cast<size_t>(y) * width, static_cast<size_t>(width) * 4);
        image->UnlockBits(&target);
        return image;
    }
    bool DrawSystemIcon(Gdiplus::Graphics& graphics, const Button& button, const CRect& rect) {
        const auto* font = Compact() ? m_smallIcons.get() : m_largeIcons.get();
        if (!font || !m_images || ImageList_GetImageCount(m_images) != 52 || button.image < 0 || button.image >= 52) return false;
        // MPC's two 26-slot banks are selected by the native toolbar itself.
        // This is an artwork table, not a second implementation of command states.
        static constexpr WCHAR glyphs[2][26] = {
            {0xe768,0xe769,0xe71a,0xe892,0xe893,0xe8d6,0xe7f0,0xec49,0xec4a,0xe768,0xe8e5,0xe713,0xe92c,0xe89f,0xe8b1,0xe8ee,0xeb9e,0xeb9d,0xe946,0xe8bb,0,0,0,0,0xe767,0xe74f},
            {0xe768,0xe769,0xe71a,0xe892,0xe893,0xe8d6,0xe7f0,0xec49,0xec4a,0xe768,0xe8e5,0xe713,0xe92d,0xe8a0,0xe8b1,0xf5e7,0xeb9e,0xeb9d,0xe946,0xe8bb,0,0,0,0,0xe767,0xe74f}
        };
        const WCHAR glyph = glyphs[button.image / 26][button.image % 26];
        if (!glyph) return false;
        const bool enabled = (button.state & TBSTATE_ENABLED) != 0;
        const bool hot = enabled && m_hotButton >= 0 && static_cast<size_t>(m_hotButton) < m_buttons.size() && m_buttons[m_hotButton].id == button.id;
        const BYTE alpha = enabled ? (hot ? 255 : 220) : 85;
        Gdiplus::SolidBrush ink(Gdiplus::Color(alpha, 255, 255, 255));
        Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
        format.SetAlignment(Gdiplus::StringAlignmentCenter); format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        if (button.image % 26 == 9 && m_frameIcons) {
            const int cx = rect.CenterPoint().x, cy = rect.CenterPoint().y;
            Gdiplus::Pen frame(Gdiplus::Color(alpha, 255, 255, 255), 1.0f);
            graphics.DrawRectangle(&frame, cx - 7.5f, cy - 5.5f, 15.0f, 11.0f);
            graphics.DrawString(&glyph, 1, m_frameIcons.get(),
                Gdiplus::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(rect.Width()), static_cast<float>(rect.Height())), &format, &ink);
            return true;
        }
        graphics.DrawString(&glyph, 1, font,
            Gdiplus::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(rect.Width()), static_cast<float>(rect.Height())), &format, &ink);
        if (button.image / 26 == 1 && button.image % 26 == 14) {
            // The font has Shuffle but no ShuffleOff glyph; retain the off-bank slash.
            Gdiplus::Pen slash(Gdiplus::Color(alpha, 255, 255, 255), 1.0f);
            const int cx = rect.CenterPoint().x, cy = rect.CenterPoint().y;
            graphics.DrawLine(&slash, cx - 7.0f, cy - 7.0f, cx + 7.0f, cy + 7.0f);
        }
        return true;
    }
    void DrawSurface() {
        EnsureGeometry();
        const int width = m_size.cx, height = m_size.cy;
        if (width <= 0 || height <= 0) return;
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HDC dc = ::CreateCompatibleDC(nullptr);
        HBITMAP bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dc || !bitmap || !bits) {
            if (bitmap) ::DeleteObject(bitmap);
            if (dc) ::DeleteDC(dc);
            return;
        }
        HGDIOBJ previous = ::SelectObject(dc, bitmap);
        {
            Gdiplus::Bitmap surface(width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
            Gdiplus::Graphics graphics(&surface);
            graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            Gdiplus::SolidBrush backdrop(Gdiplus::Color(64, 0, 0, 0));
            if (m_panelsVisible) graphics.FillRectangle(&backdrop, 0, 0, width, HeaderHeight());
            if (m_panelsVisible && m_controlsVisible) graphics.FillRectangle(&backdrop, 0, height - m_bottomHeight, width, m_bottomHeight);
            Gdiplus::Font font(L"Segoe UI", Compact() ? 12.0f : 14.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush text(Gdiplus::Color(255, 235, 235, 235));
            Gdiplus::StringFormat format;
            format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
            format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
            Gdiplus::Font titleFont(L"Segoe UI", Compact() ? 15.0f : 17.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            if (m_panelsVisible) graphics.DrawString(m_title, -1, &titleFont, Gdiplus::RectF(14, static_cast<float>((HeaderHeight() - 22) / 2), static_cast<float>(width - 60), 22), &format, &text);
            if (m_panelsVisible && m_controlsVisible) {
                if (m_seekVisible) {
                    DrawSlider(graphics, m_timeline, m_canSeek && m_stop > m_start
                        ? static_cast<double>(m_position - m_start) / static_cast<double>(m_stop - m_start) : 0, m_canSeek, m_seekHover);
                    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                    Gdiplus::SolidBrush positionInk(Gdiplus::Color(255, m_hotTime == 0 ? 255 : 220, m_hotTime == 0 ? 255 : 220, m_hotTime == 0 ? 255 : 220));
                    Gdiplus::SolidBrush durationInk(Gdiplus::Color(255, m_hotTime == 1 ? 255 : 220, m_hotTime == 1 ? 255 : 220, m_hotTime == 1 ? 255 : 220));
                    graphics.DrawString(m_positionText, -1, &font,
                        Gdiplus::RectF(static_cast<float>(m_timeRect.left), static_cast<float>(m_timeRect.top),
                            static_cast<float>(m_timeRect.Width()), static_cast<float>(m_timeRect.Height())), &format, &positionInk);
                    format.SetAlignment(Gdiplus::StringAlignmentFar);
                    graphics.DrawString(m_durationText, -1, &font,
                        Gdiplus::RectF(static_cast<float>(m_durationRect.left), static_cast<float>(m_durationRect.top),
                            static_cast<float>(m_durationRect.Width()), static_cast<float>(m_durationRect.Height())), &format, &durationInk);
                    format.SetAlignment(Gdiplus::StringAlignmentNear);
                    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
                }
                Gdiplus::SolidBrush highlight(Gdiplus::Color(40, 255, 255, 255));
                for (size_t i = 0; i < m_buttonRects.size(); ++i) {
                    const auto& button = m_buttons[i];
                    const auto& rect = m_buttonRects[i];
                    if (rect.IsRectEmpty()) continue;
                    if (m_hotButton == static_cast<int>(i))
                        graphics.FillRectangle(&highlight, rect.left, rect.top, rect.Width(), rect.Height());
                    if (DrawSystemIcon(graphics, button, rect)) continue;
                    HICON icon = m_images ? ::ImageList_GetIcon(m_images, button.image, ILD_TRANSPARENT) : nullptr;
                    if (icon) {
                        auto image = DecodeIcon(icon);
                        if (!image) { ::DestroyIcon(icon); continue; }
                        const float opacity = (button.state & TBSTATE_ENABLED)
                            ? (m_hotButton == static_cast<int>(i) ? 1.0f : 0.78f) : 0.28f;
                        // Unknown/custom artwork retains its colors and alternate image.
                        Gdiplus::ColorMatrix matrix = {{{1,0,0,0,0},{0,1,0,0,0},{0,0,1,0,0},
                            {0,0,0,opacity,0},{0,0,0,0,1}}};
                        Gdiplus::ImageAttributes attrs;
                        attrs.SetColorMatrix(&matrix);
                        const int iconSize = Compact() ? 16 : 20;
                        const int inset = (rect.Width() - iconSize) / 2;
                        graphics.DrawImage(image.get(), Gdiplus::Rect(rect.left + inset, rect.top + inset, iconSize, iconSize),
                            0, 0, image->GetWidth(), image->GetHeight(), Gdiplus::UnitPixel, &attrs);
                        ::DestroyIcon(icon);
                    }
                }
                if (!m_overflowRect.IsRectEmpty()) {
                    if (m_hotButton == -2) graphics.FillRectangle(&highlight, m_overflowRect.left, m_overflowRect.top, m_overflowRect.Width(), m_overflowRect.Height());
                    const auto center = m_overflowRect.CenterPoint();
                    for (int offset : {-5, 0, 5}) graphics.FillEllipse(&text, center.x + offset - 1, center.y - 1, 2, 2);
                }
                if (m_toolbarVisible && m_volumeVisible) {
                    DrawSlider(graphics, m_volumeRect, static_cast<double>(m_volume) / std::max(1, m_volumeMaximum), true, m_volumeHover);
                }
            }
            if (m_panelsVisible) {
            const CRect close = CloseRect();
            if (m_closeHot) {
                Gdiplus::SolidBrush hover(Gdiplus::Color(48, 255, 255, 255));
                graphics.FillRectangle(&hover, close.left, close.top, close.Width(), close.Height());
            }
            const BYTE shade = m_closeHot ? 255 : 210;
            Gdiplus::Pen cross(Gdiplus::Color(255, shade, shade, shade), 1.5f);
            const int x = close.left + 8, y = close.top + 8;
            graphics.DrawLine(&cross, x, y, x + 12, y + 12);
            graphics.DrawLine(&cross, x + 12, y, x, y + 12);
            }
            if (!m_feedback.IsEmpty() && m_feedbackUntil > GetTickCount64()) {
                // Compact status is separate from, and below, the file title.
                const int top = HeaderHeight() + 8;
                Gdiplus::Font feedbackFont(L"Segoe UI", 14.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::RectF bounds;
                graphics.MeasureString(m_feedback, -1, &feedbackFont, Gdiplus::PointF(0, 0), &bounds);
                const float boxWidth = std::min(static_cast<float>(width - 28), bounds.Width + 20.0f);
                Gdiplus::SolidBrush statusBG(Gdiplus::Color(90, 0, 0, 0));
                graphics.FillRectangle(&statusBG, 14.0f, static_cast<float>(top), boxWidth, 30.0f);
                format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                graphics.DrawString(m_feedback, -1, &feedbackFont,
                    Gdiplus::RectF(24.0f, static_cast<float>(top), boxWidth - 20.0f, 30.0f), &format, &text);
            }
        }
        POINT origin = {0, 0}; SIZE size = {width, height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, m_opacity, AC_SRC_ALPHA};
        ::UpdateLayeredWindow(m_hWnd, nullptr, nullptr, &size, dc, &origin, 0, &blend, ULW_ALPHA);
        ::SelectObject(dc, previous); ::DeleteObject(bitmap); ::DeleteDC(dc);
    }
    void DrawSlider(Gdiplus::Graphics& graphics, const CRect& rect, double value, bool enabled, float hover) {
        value = std::clamp(value, 0.0, 1.0);
        const float left = static_cast<float>(rect.left), right = static_cast<float>(rect.right - 1);
        const float y = static_cast<float>(rect.CenterPoint().y), x = left + static_cast<float>(value) * (right - left);
        Gdiplus::Pen track(Gdiplus::Color(enabled ? 120 : 45, 235, 235, 235), 3.0f);
        track.SetStartCap(Gdiplus::LineCapRound); track.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawLine(&track, left, y, right, y);
        if (enabled) {
            Gdiplus::Pen elapsed(Gdiplus::Color(255, 0, 120, 212), 3.0f);
            elapsed.SetStartCap(Gdiplus::LineCapRound); elapsed.SetEndCap(Gdiplus::LineCapRound);
            graphics.DrawLine(&elapsed, left, y, x, y);
            Gdiplus::SolidBrush accent(Gdiplus::Color(255, 0, 120, 212));
            Gdiplus::SolidBrush shell(Gdiplus::Color(255, 242, 242, 242));
            graphics.FillEllipse(&shell, x - 6.5f, y - 6.5f, 13.0f, 13.0f);
            const float radius = 3.0f + 1.5f * hover;
            graphics.FillEllipse(&accent, x - radius, y - radius, radius * 2, radius * 2);
        }
    }
protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        if (message == WM_TIMER && wParam == DeadlineTimer) {
            KillTimer(DeadlineTimer); m_timerDue = 0;
            const ULONGLONG now = GetTickCount64(); m_frameTick = now;
            AdvanceSliders(now);
            if (m_feedbackUntil && now >= m_feedbackUntil) { m_feedback.Empty(); m_feedbackUntil = 0; }
            if (m_fullscreen && m_panelsVisible && !CapturedInput() && now - m_lastInput >= 4000) {
                if (!m_fadeStart) m_fadeStart = now;
                const ULONGLONG elapsed = now - m_fadeStart;
                m_opacity = static_cast<BYTE>(255 - std::min<ULONGLONG>(255, elapsed * 255 / 240));
                if (elapsed >= 240) { m_idleHidden = true; m_fadeStart = 0; }
            }
            Refresh(m_view);
            if (IsWindowVisible()) DrawSurface();
            ArmDeadline(); return 0;
        }
        if (message == WM_NCHITTEST) {
            CPoint point(lParam); ScreenToClient(&point);
            return IsControlPoint(point) ? HTCLIENT : HTTRANSPARENT;
        }
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (message == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, m_hWnd, 0};
            ::TrackMouseEvent(&track); Refresh(m_view); return 0;
        }
        if (message == WM_MOUSELEAVE) { Refresh(m_view); return 0; }
        return CWnd::WindowProc(message, wParam, lParam);
    }
public:
    CFluentOverlayProbe() {
        Gdiplus::GdiplusStartupInput startup;
        if (Gdiplus::GdiplusStartup(&m_gdiplus, &startup, nullptr) != Gdiplus::Ok) m_gdiplus = 0;
        if (m_gdiplus) {
            Gdiplus::FontFamily system(L"Segoe Fluent Icons");
            if (system.GetLastStatus() == Gdiplus::Ok) {
                m_smallIcons = std::make_unique<Gdiplus::Font>(&system, 16.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                m_largeIcons = std::make_unique<Gdiplus::Font>(&system, 16.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                m_frameIcons = std::make_unique<Gdiplus::Font>(&system, 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            }
        }
    }
    ~CFluentOverlayProbe() {
        if (m_volumeTip.GetSafeHwnd()) m_volumeTip.DestroyWindow();
        if (m_tip.GetSafeHwnd()) m_tip.DestroyWindow();
        if (GetSafeHwnd()) DestroyWindow();
        m_smallIcons.reset(); m_largeIcons.reset(); m_frameIcons.reset();
        if (m_gdiplus) Gdiplus::GdiplusShutdown(m_gdiplus);
    }
    void Activity(const MSG& message) {
        if (message.message == WM_MOUSEMOVE) {
            if (message.pt.x == m_lastCursor.x && message.pt.y == m_lastCursor.y) return;
            m_lastCursor = message.pt;
        }
        WakeControls();
    }
    bool ShowFeedback(CWnd* view, LPCWSTR message, int duration) {
        if (!view->GetSafeHwnd() || ::GetCurrentThreadId() != ::GetWindowThreadProcessId(view->GetSafeHwnd(), nullptr)) return false;
        if (m_idleHidden) m_opacity = 255;
        m_feedback = message; m_feedbackUntil = GetTickCount64() + std::clamp(duration, 500, 3000);
        Refresh(view);
        if (GetSafeHwnd() && IsWindowVisible()) DrawSurface();
        ArmDeadline(); return true;
    }
    void SetNativeLayout(std::vector<Button> buttons, HIMAGELIST images, bool toolbar, bool seek,
                         bool volume, int volumeValue, int volumeMaximum, int alignment) {
        if (buttons != m_buttons || images != m_images || toolbar != m_toolbarVisible
                || seek != m_seekVisible || volume != m_volumeVisible || alignment != m_alignment) m_dirty = true;
        // Remove tooltip text references before replacing the snapshot.
        if (m_dirty && m_tip.GetSafeHwnd()) {
            for (size_t i = 0; i < m_buttonRects.size(); ++i) m_tip.DelTool(this, static_cast<UINT_PTR>(i + 1));
            m_tip.DelTool(this, PositionTool); m_tip.DelTool(this, DurationTool); m_tip.DelTool(this, CloseTool);
        }
        if (buttons != m_buttons) m_buttons = std::move(buttons);
        m_images = images; m_toolbarVisible = toolbar;
        m_seekVisible = seek; m_volumeVisible = volume;
        const bool volumeChanged = m_volume != volumeValue || m_volumeMaximum != volumeMaximum;
        m_volume = volumeValue; m_volumeMaximum = volumeMaximum; m_alignment = alignment;
        if (volumeChanged && GetSafeHwnd() && IsWindowVisible()) {
            DrawSurface();
            CPoint cursor; ::GetCursorPos(&cursor); ScreenToClient(&cursor); UpdateVolumeTip(cursor);
        }
    }
    void SetTimeParts(const CString& position, const CString& duration, bool remaining) {
        if (remaining != m_remainingTime) m_dirty = true;
        m_remainingTime = remaining;
        if (m_positionText.GetLength() != position.GetLength() || m_durationText.GetLength() != duration.GetLength()) m_dirty = true;
        m_positionText = position; m_durationText = duration;
    }
    void SetControlState(bool fullscreen, bool feedbackEnabled, bool canSeek, bool controlsVisible,
                         LONGLONG start, LONGLONG stop, LONGLONG position, const CString& time) {
        if (m_fullscreen != fullscreen) { m_fullscreen = fullscreen; WakeControls(); }
        const bool feedbackCleared = !feedbackEnabled && !m_feedback.IsEmpty();
        if (feedbackCleared) { m_feedback.Empty(); m_feedbackUntil = 0; }
        const bool changed = feedbackCleared || m_dirty || m_canSeek != canSeek || m_controlsVisible != controlsVisible
            || m_start != start || m_stop != stop || m_position != position || m_time != time;
        m_canSeek = canSeek; m_controlsVisible = controlsVisible;
        m_start = start; m_stop = stop; m_position = position; m_time = time;
        if (changed && GetSafeHwnd() && IsWindowVisible()) DrawSurface();
    }
    bool RouteInput(const MSG& message) {
        if (m_seeking && seekActive && !seekActive()) m_seeking = false;
        if (m_volumeDragging && volumeActive && !volumeActive()) m_volumeDragging = false;
        if (!GetSafeHwnd() || (!IsWindowVisible() && !CapturedInput())) return false;
        CPoint point(message.pt); ScreenToClient(&point);
        if (m_tip.GetSafeHwnd()) {
            MSG tooltipMessage = message;
            tooltipMessage.hwnd = m_hWnd;
            tooltipMessage.lParam = MAKELPARAM(point.x, point.y);
            m_tip.RelayEvent(&tooltipMessage);
        }
        const bool leftDown = message.message == WM_LBUTTONDOWN || message.message == WM_LBUTTONDBLCLK;
        const bool rightDown = message.message == WM_RBUTTONDOWN || message.message == WM_RBUTTONDBLCLK;
        const bool up = message.message == WM_LBUTTONUP || message.message == WM_RBUTTONUP;
        const int hit = HitButton(point);
        if (leftDown && CloseRect().PtInRect(point)) {
            m_closePressed = true; ::SetCapture(m_view->GetSafeHwnd()); return true;
        }
        if ((leftDown || rightDown) && m_controlsVisible && HitTime(point)) {
            m_timePressed = true; m_timeRight = rightDown;
            ::SetCapture(m_view->GetSafeHwnd()); return true;
        }
        if ((leftDown || rightDown) && m_controlsVisible && hit != -1) {
            m_pressedId = hit >= 0 ? m_buttons[hit].id : 0;
            m_pressedButton = hit; m_rightPressed = rightDown; ::SetCapture(m_view->GetSafeHwnd());
            return true;
        }
        if (up && (m_timePressed || m_blockedPress)) {
            const bool activateTime = m_timePressed && HitTime(point)
                && (m_timeRight ? message.message == WM_RBUTTONUP : message.message == WM_LBUTTONUP);
            const bool right = m_timeRight;
            m_timePressed = m_blockedPress = false;
            if (::GetCapture() == m_view->GetSafeHwnd()) ::ReleaseCapture();
            if (activateTime && timeClick) timeClick(right, message.pt);
            return true;
        }
        const bool move = message.message == WM_MOUSEMOVE;
        if ((leftDown && m_controlsVisible && HitSeek(point)) || m_seeking) {
            if (leftDown || message.message == WM_LBUTTONUP || move) {
                if (leftDown && !m_canSeek) {
                    m_blockedPress = true; ::SetCapture(m_view->GetSafeHwnd());
                }
                if (seekMouse && m_canSeek) m_seeking = seekMouse(leftDown ? WM_LBUTTONDOWN : message.message,
                    message.wParam, std::clamp(static_cast<double>(point.x - m_timeline.left) / std::max(1, m_timeline.Width() - 1), 0.0, 1.0));
                return true;
            }
        }
        if ((leftDown && m_controlsVisible && HitVolume(point)) || m_volumeDragging) {
            if (leftDown || message.message == WM_LBUTTONUP || move) {
                if (volumeMouse) m_volumeDragging = volumeMouse(leftDown ? WM_LBUTTONDOWN : message.message,
                    message.wParam, std::clamp(static_cast<double>(point.x - m_volumeRect.left) / std::max(1, m_volumeRect.Width() - 1), 0.0, 1.0));
                if (leftDown && !m_volumeDragging) {
                    m_blockedPress = true; ::SetCapture(m_view->GetSafeHwnd());
                }
                return true;
            }
        }
        if (message.message == WM_MOUSEWHEEL && HitVolume(point)) {
            if (volumeMouse) volumeMouse(WM_MOUSEWHEEL, message.wParam, 0);
            return true;
        }
        if (up && (m_closePressed || m_pressedButton != -1)) {
            const bool close = m_closePressed, right = m_rightPressed;
            const int pressed = m_pressedButton;
            m_closePressed = false; m_pressedButton = -1;
            if (::GetCapture() == m_view->GetSafeHwnd()) ::ReleaseCapture();
            if (close && message.message == WM_LBUTTONUP && CloseRect().PtInRect(point))
                ::PostMessage(::GetAncestor(m_view->GetSafeHwnd(), GA_ROOT), WM_CLOSE, 0, 0);
            if (pressed == -2 && hit == -2) { ShowOverflow(); return true; }
            if (pressed >= 0 && pressed == hit && pressed < static_cast<int>(m_buttons.size())
                    && (right ? message.message == WM_RBUTTONUP : message.message == WM_LBUTTONUP)
                    && m_buttons[pressed].id == m_pressedId
                    && (m_buttons[pressed].state & TBSTATE_ENABLED) && invokeButton) {
                CRect anchor = m_buttonRects[pressed]; ClientToScreen(&anchor);
                invokeButton(m_buttons[pressed].id, anchor, right);
            }
            return true;
        }
        return false;
    }
    void ShowOverflow() {
        if (m_overflowStart < 0) return;
        HMENU menu = ::CreatePopupMenu();
        // Native Windows popup, with exactly the configured buttons that did not fit.
        const auto buttons = m_buttons;
        for (size_t i = m_overflowStart; i < buttons.size(); ++i) {
            UINT flags = MF_STRING | ((buttons[i].state & TBSTATE_ENABLED) ? MF_ENABLED : MF_GRAYED);
            if (buttons[i].state & TBSTATE_CHECKED) flags |= MF_CHECKED;
            ::AppendMenu(menu, flags, static_cast<UINT_PTR>(1000 + i), buttons[i].tip);
        }
        CRect anchor = m_overflowRect; ClientToScreen(&anchor);
        const UINT selected = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
            anchor.left, anchor.top, 0, GetTopLevelParent()->GetSafeHwnd(), nullptr);
        ::DestroyMenu(menu);
        if (selected >= 1000 && selected - 1000 < buttons.size() && invokeButton)
            invokeButton(buttons[selected - 1000].id, anchor, false);
    }
    void CancelPress() { WakeControls(); m_closePressed = m_timePressed = m_blockedPress = false; m_pressedButton = -1; }
    void Hide() {
        HideVolumeTip();
        m_seekHover = m_seekTarget = m_volumeHover = m_volumeTarget = 0;
        if (m_tip.GetSafeHwnd()) m_tip.Pop();
        if (GetSafeHwnd()) { KillTimer(DeadlineTimer); m_timerDue = 0; ShowWindow(SW_HIDE); }
    }
    void Refresh(CWnd* view, HWND trackingWindow = nullptr) {
        if (!m_gdiplus) return;
        m_view = view;
        CRect rc; view->GetClientRect(rc);
        if (rc.Width() < 180 || rc.Height() < 120 || !view->IsWindowVisible()) { Hide(); return; }
        const bool feedbackExpired = m_feedbackUntil && GetTickCount64() >= m_feedbackUntil;
        if (feedbackExpired) { m_feedback.Empty(); m_feedbackUntil = 0; }
        const bool panels = InsideView() && !m_idleHidden;
        const bool feedback = m_feedbackUntil > GetTickCount64();
        if (!panels && !feedback) { Hide(); return; }
        const bool panelChanged = panels != m_panelsVisible; m_panelsVisible = panels;
        if (!GetSafeHwnd()) {
            if (!CreateEx(WS_EX_LAYERED | WS_EX_NOACTIVATE, AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW)),
                    L"Fluent native overlay", WS_CHILD, rc, view, 0)) return;
            m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX); m_tip.Activate(TRUE);
            m_volumeTip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX);
            TOOLINFO tool = {}; tool.cbSize = sizeof(tool); tool.hwnd = m_hWnd; tool.uId = VolumeTool;
            tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            m_volumeTipText = L"Volume"; tool.lpszText = const_cast<LPWSTR>(m_volumeTipText.GetString());
            m_volumeTip.SendMessage(TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&tool));
        }
        CString title; view->GetTopLevelParent()->GetWindowText(title); title = DisplayTitle(title);
        CPoint cursor; ::GetCursorPos(&cursor); ScreenToClient(&cursor);
        const CSize oldSize = m_size; m_size = rc.Size(); EnsureGeometry();
        const bool hot = CloseRect().PtInRect(cursor) != FALSE;
        const int hotButton = HitButton(cursor);
        const int hotTime = m_timeRect.PtInRect(cursor) ? 0 : m_durationRect.PtInRect(cursor) ? 1 : -1;
        SliderTargets(m_panelsVisible && m_canSeek && (m_seeking || HitThumb(m_timeline, m_stop > m_start ? static_cast<double>(m_position - m_start) / (m_stop - m_start) : 0, cursor)) ? 1.0f : 0.0f,
            m_panelsVisible && m_volumeVisible && (m_volumeDragging || HitThumb(m_volumeRect, static_cast<double>(m_volume) / std::max(1, m_volumeMaximum), cursor)) ? 1.0f : 0.0f);
        const bool repaint = feedbackExpired || panelChanged || !IsWindowVisible() || oldSize != m_size || hot != m_closeHot || hotButton != m_hotButton || hotTime != m_hotTime || title != m_title;
        m_closeHot = hot; m_hotButton = hotButton; m_hotTime = hotTime; m_title = title;
        if (!IsWindowVisible() || oldSize != m_size || ::GetWindow(m_hWnd, GW_HWNDPREV))
            SetWindowPos(&wndTop, 0, 0, rc.Width(), rc.Height(), SWP_NOACTIVATE);
        if (repaint) DrawSurface();
        ShowWindow(SW_SHOWNOACTIVATE);
        UpdateVolumeTip(cursor);
        ArmDeadline();
        if (trackingWindow && trackingWindow != m_hWnd) {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, trackingWindow, 0}; ::TrackMouseEvent(&track);
        }
    }
};
