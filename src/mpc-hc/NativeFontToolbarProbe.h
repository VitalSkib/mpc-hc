/* Diagnostic font artwork for the real native toolbar. GPLv3+. */
#pragma once
#include <gdiplus.h>

namespace NativeFontToolbarProbe {
class Renderer final {
    ULONG_PTR m_token = 0;
    std::unique_ptr<Gdiplus::FontFamily> m_family;
public:
    Renderer() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&m_token, &input, nullptr) != Gdiplus::Ok) return;
        m_family = std::make_unique<Gdiplus::FontFamily>(L"Segoe Fluent Icons");
        if (m_family->GetLastStatus() != Gdiplus::Ok)
            m_family = std::make_unique<Gdiplus::FontFamily>(L"Segoe MDL2 Assets");
    }
    ~Renderer() {
        m_family.reset();
        if (m_token) Gdiplus::GdiplusShutdown(m_token);
    }
    bool Draw(const NMTBCUSTOMDRAW& draw, int image) {
        if (!m_token || !m_family || m_family->GetLastStatus() != Gdiplus::Ok || image < 0 || image >= 52) return false;
        static const WCHAR symbols[26] = {
            0xe768, 0xe769, 0xe71a, 0xe892, 0xe893, 0xe8d6, 0xe7f0,
            0xec49, 0xec4a, 0xe768, 0xe8e5, 0xe713, 0xe92c, 0xe89f,
            0xe8b1, 0xe8ee, 0xeb9e, 0xeb9d, 0xe946, 0xe8bb,
            L'1', L'2', L'3', L'4', 0xe767, 0xe74f
        };
        const int column = image % 26;
        const bool alternate = image >= 26;
        WCHAR symbol[2] = {symbols[column], 0};
        if (column == 12 && alternate) symbol[0] = 0xe92d;
        if (column == 13 && alternate) symbol[0] = 0xe8a0;
        if (column == 24 && alternate) symbol[0] = 0xe993; // quiet volume
        if (column == 25 && alternate) symbol[0] = 0xe74f;
        const CRect rect(draw.nmcd.rc);
        if (rect.IsRectEmpty()) return false;
        const bool enabled = !(draw.nmcd.uItemState & CDIS_DISABLED);
        const bool hot = !!(draw.nmcd.uItemState & (CDIS_HOT | CDIS_SELECTED));
        const BYTE alpha = enabled ? (hot ? 255 : 225) : 85;
        Gdiplus::Graphics graphics(draw.nmcd.hdc);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const float size = float(std::max(8, rect.Height() - 8));
        Gdiplus::FontFamily digits(L"Segoe UI");
        Gdiplus::Font font(column >= 20 && column <= 23 ? &digits : m_family.get(),
            size, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush ink(Gdiplus::Color(alpha, 255, 255, 255));
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        const Gdiplus::RectF cell(float(rect.left), float(rect.top), float(rect.Width()), float(rect.Height()));
        const auto result = graphics.DrawString(symbol, 1, &font, cell, &format, &ink);
        if (result != Gdiplus::Ok) return false;
        // Frame-step must not look identical to Play/Next. Keep all command
        // routing, button state and rectangular native hit areas untouched.
        Gdiplus::Pen stroke(Gdiplus::Color(alpha, 255, 255, 255), 1.0f);
        if (column == 9) {
            const float x = float(rect.CenterPoint().x) + size * 0.34f;
            const float y = float(rect.CenterPoint().y), half = size * 0.30f;
            graphics.DrawLine(&stroke, x, y - half, x, y + half);
        }
        if ((column == 14 || column == 15) && alternate) {
            const float x = float(rect.CenterPoint().x), y = float(rect.CenterPoint().y), half = size * 0.32f;
            graphics.DrawLine(&stroke, x - half, y + half, x + half, y - half);
        }
        return true;
    }
};
inline bool Draw(const NMTBCUSTOMDRAW& draw, int image) {
    static Renderer renderer;
    return renderer.Draw(draw, image);
}
}
