// Theme.swift — Colors ported verbatim (same hex values, same source
// comment) from the Windows build's Theme.h, which itself was extracted from
// the Android app's ui/theme/Color.kt and Theme.kt, so the macOS build
// matches the real app's palette exactly.
import SwiftUI

struct Palette {
    let background: Color
    let surface: Color
    let surfaceVariant: Color
    let primary: Color
    let secondary: Color
    let tertiary: Color        // border/accent tone
    let onSurface: Color
    let onSurfaceVariant: Color
    let onPrimary: Color
    let errorContainer: Color
    let error: Color
    let success: Color         // "Online" chip green family
}

private func rgb(_ r: Int, _ g: Int, _ b: Int) -> Color {
    Color(red: Double(r) / 255.0, green: Double(g) / 255.0, blue: Double(b) / 255.0)
}

enum Theme {

    // Dark Theme Palette (Elevated Dark Professional Polish) — from Color.kt
    static let dark = Palette(
        background:       rgb(0x0F, 0x17, 0x2A), // DarkBackground
        surface:          rgb(0x14, 0x1E, 0x30), // DarkSurface
        surfaceVariant:   rgb(0x1B, 0x27, 0x3D), // DarkSurface, slightly lifted
        primary:          rgb(0x5A, 0xB6, 0xE5), // DarkPrimary
        secondary:        rgb(0x1E, 0x29, 0x3B), // DarkSecondary
        tertiary:         rgb(0x2E, 0x3D, 0x52), // DarkTertiary
        onSurface:        rgb(0xF8, 0xFA, 0xFC), // DarkOnSurface
        onSurfaceVariant: rgb(0xC7, 0xD1, 0xDE),
        onPrimary:        rgb(0xFF, 0xFF, 0xFF),
        errorContainer:   rgb(0x4A, 0x22, 0x22),
        error:            rgb(0xFF, 0x17, 0x44),
        success:          rgb(0x00, 0xE6, 0x76)
    )

    // Light Theme Palette (Professional Polish) — from Color.kt
    static let light = Palette(
        background:       rgb(0xFD, 0xFB, 0xFF), // LightBackground
        surface:          rgb(0xFF, 0xFF, 0xFF), // LightSurface
        surfaceVariant:   rgb(0xF3, 0xF4, 0xF9), // proxy card fill in light mode
        primary:          rgb(0x00, 0x5F, 0xB0), // LightPrimary
        secondary:        rgb(0x00, 0x1D, 0x35), // LightSecondary
        tertiary:         rgb(0xE1, 0xE2, 0xEC), // LightTertiary (borders)
        onSurface:        rgb(0x1B, 0x1B, 0x1F), // LightOnSurface
        onSurfaceVariant: rgb(0x4A, 0x4A, 0x52),
        onPrimary:        rgb(0xFF, 0xFF, 0xFF),
        errorContainer:   rgb(0xFB, 0xE4, 0xE4),
        error:            rgb(0xB3, 0x26, 0x1E),
        success:          rgb(0x1B, 0x5E, 0x20)
    )

    // ---- Metrics (same scale the Windows build used from the mobile dp values) ----
    static let cornerRadiusLarge: CGFloat = 18 // disclaimer/outer card radius
    static let cornerRadiusCard: CGFloat = 16  // proxy card radius
    static let cornerRadiusPill: CGFloat = 14  // buttons/badges
    static let spacingSmall: CGFloat = 8
    static let spacingMedium: CGFloat = 16
    static let spacingLarge: CGFloat = 24
    static let topBarHeight: CGFloat = 78
    static let bottomBarHeight: CGFloat = 78
}
