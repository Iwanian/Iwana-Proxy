// Theme.h — Colors ported verbatim (same hex values) from the Windows build's
// Theme.h, which itself came from the Android app's ui/theme/Color.kt.
// Cairo wants doubles in [0,1] instead of COLORREF, so we store both a hex
// value (for reference / debugging) and precomputed r/g/b doubles.
#pragma once

namespace Theme {

    struct Color {
        double r, g, b;
        constexpr Color(unsigned hex)
            : r(((hex >> 16) & 0xFF) / 255.0),
              g(((hex >> 8) & 0xFF) / 255.0),
              b((hex & 0xFF) / 255.0) {}
    };

    struct Palette {
        Color background;
        Color surface;
        Color surfaceVariant;
        Color primary;
        Color secondary;
        Color tertiary;      // border/accent tone
        Color onSurface;
        Color onSurfaceVariant;
        Color onPrimary;
        Color errorContainer;
        Color error;
        Color success;       // "Online" chip green family
    };

    // Dark Theme Palette (Elevated Dark Professional Polish) — from Color.kt
    inline constexpr Palette Dark = {
        /*background*/      0x0F172A,
        /*surface*/         0x141E30,
        /*surfaceVariant*/  0x1B273D,
        /*primary*/         0x5AB6E5,
        /*secondary*/       0x1E293B,
        /*tertiary*/        0x2E3D52,
        /*onSurface*/       0xF8FAFC,
        /*onSurfaceVariant*/0xC7D1DE,
        /*onPrimary*/       0xFFFFFF,
        /*errorContainer*/  0x4A2222,
        /*error*/           0xFF1744,
        /*success*/         0x00E676,
    };

    // Light Theme Palette (Professional Polish) — from Color.kt
    inline constexpr Palette Light = {
        /*background*/      0xFDFBFF,
        /*surface*/         0xFFFFFF,
        /*surfaceVariant*/  0xF3F4F9,
        /*primary*/         0x005FB0,
        /*secondary*/       0x001D35,
        /*tertiary*/        0xE1E2EC,
        /*onSurface*/       0x1B1B1F,
        /*onSurfaceVariant*/0x4A4A52,
        /*onPrimary*/       0xFFFFFF,
        /*errorContainer*/  0xFBE4E4,
        /*error*/           0xB3261E,
        /*success*/         0x1B5E20,
    };

    // ---- Metrics (identical to the Windows build's desktop-scaled values) ----
    constexpr int CornerRadiusLarge = 18; // disclaimer/outer card radius
    constexpr int CornerRadiusCard  = 16; // proxy card radius
    constexpr int CornerRadiusPill  = 14; // buttons/badges
    constexpr int SpacingSmall   = 8;
    constexpr int SpacingMedium  = 16;
    constexpr int SpacingLarge   = 24;
    constexpr int TopBarHeight   = 78;
    constexpr int BottomBarHeight = 78;

    // ---- Fonts ----
    // Pango font family lists (comma-separated fallback chain) so a missing
    // font degrades gracefully instead of the "invisible text" bug documented
    // in PORT_SPEC.md §7.5. Must cover Latin + Persian(Arabic) + Cyrillic.
    constexpr const char* FontFamily =
        "Cantarell, Noto Sans, Noto Sans Arabic, Ubuntu, DejaVu Sans, sans-serif";
    constexpr const char* MonoFontFamily =
        "Noto Sans Mono, DejaVu Sans Mono, Ubuntu Mono, monospace";

    // Ping color coding (PORT_SPEC.md §4)
    inline constexpr Color PingGood   = 0x00E676; // 0-150ms
    inline constexpr Color PingMedium = 0xFFD600; // 151-300ms
    inline constexpr Color PingBad    = 0xFF1744; // >300ms

}
