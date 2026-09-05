// Theme.h — Colors extracted verbatim from the Android app's ui/theme/Color.kt
// and Theme.kt, so the Windows build matches the real app's palette exactly.
#pragma once
#include <windows.h>

namespace Theme {

    struct Palette {
        COLORREF background;
        COLORREF surface;
        COLORREF surfaceVariant;
        COLORREF primary;
        COLORREF secondary;
        COLORREF tertiary;      // border/accent tone
        COLORREF onSurface;
        COLORREF onSurfaceVariant;
        COLORREF onPrimary;
        COLORREF errorContainer;
        COLORREF error;
        COLORREF success;       // "Online" chip green family
    };

    // Dark Theme Palette (Elevated Dark Professional Polish) — from Color.kt
    inline const Palette Dark = {
        /*background*/      RGB(0x0F, 0x17, 0x2A), // DarkBackground
        /*surface*/         RGB(0x14, 0x1E, 0x30), // DarkSurface
        /*surfaceVariant*/  RGB(0x1B, 0x27, 0x3D), // DarkSurface, slightly lifted
        /*primary*/         RGB(0x5A, 0xB6, 0xE5), // DarkPrimary
        /*secondary*/       RGB(0x1E, 0x29, 0x3B), // DarkSecondary
        /*tertiary*/        RGB(0x2E, 0x3D, 0x52), // DarkTertiary
        /*onSurface*/       RGB(0xF8, 0xFA, 0xFC), // DarkOnSurface
        /*onSurfaceVariant*/RGB(0xC7, 0xD1, 0xDE),
        /*onPrimary*/       RGB(0xFF, 0xFF, 0xFF),
        /*errorContainer*/  RGB(0x4A, 0x22, 0x22),
        /*error*/           RGB(0xFF, 0x17, 0x44),
        /*success*/         RGB(0x00, 0xE6, 0x76),
    };

    // Light Theme Palette (Professional Polish) — from Color.kt
    inline const Palette Light = {
        /*background*/      RGB(0xFD, 0xFB, 0xFF), // LightBackground
        /*surface*/         RGB(0xFF, 0xFF, 0xFF), // LightSurface
        /*surfaceVariant*/  RGB(0xF3, 0xF4, 0xF9), // proxy card fill in light mode
        /*primary*/         RGB(0x00, 0x5F, 0xB0), // LightPrimary
        /*secondary*/       RGB(0x00, 0x1D, 0x35), // LightSecondary
        /*tertiary*/        RGB(0xE1, 0xE2, 0xEC), // LightTertiary (borders)
        /*onSurface*/       RGB(0x1B, 0x1B, 0x1F), // LightOnSurface
        /*onSurfaceVariant*/RGB(0x4A, 0x4A, 0x52),
        /*onPrimary*/       RGB(0xFF, 0xFF, 0xFF),
        /*errorContainer*/  RGB(0xFB, 0xE4, 0xE4),
        /*error*/           RGB(0xB3, 0x26, 0x1E),
        /*success*/         RGB(0x1B, 0x5E, 0x20),
    };

    // ---- Metrics (scaled from the mobile dp values to comfortable desktop px) ----
    constexpr int CornerRadiusLarge = 18; // disclaimer/outer card radius (20dp)
    constexpr int CornerRadiusCard  = 16; // proxy card radius (24dp scaled down)
    constexpr int CornerRadiusPill  = 14; // buttons/badges
    constexpr int SpacingSmall   = 8;
    constexpr int SpacingMedium  = 16;
    constexpr int SpacingLarge   = 24;
    constexpr int TopBarHeight   = 78;
    constexpr int BottomBarHeight = 78;

    // ---- Fonts ----
    constexpr const wchar_t* FontFamily = L"Segoe UI";
    constexpr const wchar_t* MonoFontFamily = L"Consolas";

}
