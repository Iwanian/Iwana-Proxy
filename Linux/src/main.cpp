// main.cpp — Iwana Proxy, Linux port (GTK4 + Cairo + Pango).
//
// Architecture mirrors the Windows build's main.cpp: one GtkDrawingArea per
// window, a single Paint()-equivalent draw function that switches on the
// current Screen, hand-drawn vector icons (no icon fonts/emoji per
// PORT_SPEC.md §9), hit-testing via rectangles recorded during the draw pass
// (GTK4/Cairo has no per-widget-per-icon click target primitive the way a
// native-widget toolkit would, so we do the same "record rects while
// painting, hit-test on click" approach the Windows GDI+ build used).
//
// Networking (fetch/ping/speed-test) runs on background std::threads and
// posts results back to the GTK main loop via g_idle_add, mirroring the
// Windows build's PostMessage hand-off.
#include <gtk/gtk.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <unordered_set>

#include "Theme.h"
#include "Loc.h"
#include "Config.h"
#include "Storage.h"
#include "ProxyItem.h"
#include "ProxyParser.h"
#include "ProxySource.h"
#include "PingService.h"
#include "SpeedTester.h"
#include "TelegramLauncher.h"
#include "BannerSlideshow.h"

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------
enum class Screen { Home, Saved, SpeedTest, Settings, Support };

// A rectangle recorded during painting, tagged with an action id, so the
// click handler can hit-test without re-computing layout.
struct HitRect {
    double x, y, w, h;
    std::string action;   // e.g. "connect:3", "copy:3", "fav:3", "nav:saved"
};

// Live per-frame banner slide state.
struct BannerState {
    std::vector<BannerSlideshow::BannerItem> items;
    std::vector<GdkPixbuf*> pixbufs; // parallel to items; may contain nullptr while loading
    int current = 0;
    bool loaded = false;
    bool loading = false;
    guint timerId = 0;
};

struct SpeedTestState {
    std::string inputText;
    bool running = false;
    bool hasResult = false;
    SpeedTester::Result result;
    std::string resolvedHost, resolvedPort;
    double fileSizeMb = 100.0;
    int cursorPos = 0; // caret position within inputText (byte offset)
};

struct AppState {
    GtkWidget* window = nullptr;
    GtkWidget* drawingArea = nullptr;
    double speedInputRectX = 0, speedInputRectY = 0, speedInputRectW = 0, speedInputRectH = 0;
    bool speedInputFocused = false;   // hand-drawn text input focus state
    bool caretVisible = true;         // blinks on a timer
    double lastPaintedWidth = 480, lastPaintedHeight = 800;

    Screen screen = Screen::Home;
    Screen settingsReturnTo = Screen::Home;

    Config::AppSettings settings;
    bool darkMode = true; // resolved effective mode (system probed once at startup)

    std::vector<ProxyItem> proxies;
    std::mutex proxiesMutex;
    bool isScanning = false;
    bool isOffline = false;   // true if serving from cache
    bool offlineNoticeDismissed = false;
    bool loadError = false;

    double scrollY = 0.0;
    double contentHeight = 0.0; // measured last paint, used to clamp scroll
    double currentBarH = 0.0;   // top-bar height for the screen currently being painted
    double autoScanSliderX = 0, autoScanSliderW = 0, autoScanSliderY = 0; // recorded each paint for drag hit-testing
    bool draggingAutoScanSlider = false;

    std::vector<HitRect> hitRects; // rebuilt every paint

    BannerState banner;
    SpeedTestState speedTest;

    std::string clearFavConfirmPending; // non-empty => showing confirm dialog via GTK native dialog instead

    guint autoScanTimerId = 0;
};

static AppState* app_new() {
    auto* app = new AppState();
    app->settings = Config::Load();
    if (app->settings.themeMode == "dark") app->darkMode = true;
    else if (app->settings.themeMode == "light") app->darkMode = false;
    else app->darkMode = true; // "system": default dark (no reliable portal probe kept minimal)

    auto favKeys = Storage::LoadFavoriteKeys();
    (void)favKeys; // applied to proxies once fetched (see ApplyFavorites)
    return app;
}

static const Theme::Palette& Pal(const AppState* app) {
    return app->darkMode ? Theme::Dark : Theme::Light;
}

static bool IsRtl(const AppState* app) { return app->settings.language == "fa"; }

static std::string L(const AppState* app, Loc::Key k) { return Loc::T(app->settings.language, k); }

// ---------------------------------------------------------------------------
// Small drawing helpers
// ---------------------------------------------------------------------------
static void SetColor(cairo_t* cr, const Theme::Color& c, double alpha = 1.0) {
    cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
}

static void RoundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    double deg = M_PI / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -90 * deg, 0 * deg);
    cairo_arc(cr, x + w - r, y + h - r, r, 0 * deg, 90 * deg);
    cairo_arc(cr, x + r, y + h - r, r, 90 * deg, 180 * deg);
    cairo_arc(cr, x + r, y + r, r, 180 * deg, 270 * deg);
    cairo_close_path(cr);
}

// Draws text with Pango, honoring RTL alignment automatically (UAX#9 via
// Pango's own bidi handling — see PORT_SPEC.md §7, this is the whole reason
// GTK4/Pango was chosen over hand-rolled text layout).
struct TextStyle {
    double size = 14.0;
    bool bold = false;
    bool mono = false;
    Theme::Color color = Theme::Color(0xFFFFFF);
    PangoAlignment align = PANGO_ALIGN_LEFT;
};

static void DrawText(cairo_t* cr, const AppState* app, const std::string& text,
                      double x, double y, double maxWidth, const TextStyle& style,
                      double* outHeight = nullptr) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, text.c_str(), -1);

    PangoFontDescription* desc = pango_font_description_from_string(
        style.mono ? Theme::MonoFontFamily : Theme::FontFamily);
    pango_font_description_set_size(desc, (int)(style.size * PANGO_SCALE));
    pango_font_description_set_weight(desc, style.bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    if (maxWidth > 0) pango_layout_set_width(layout, (int)(maxWidth * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);

    PangoAlignment align = style.align;
    // Auto-detect base direction from the text itself so mixed Persian/English
    // strings align correctly without a manual RTL flag on LTR text.
    // (pango_find_base_dir is deprecated in favor of context-based direction
    // resolution, but remains fully functional; the suggested replacement
    // requires a live PangoContext text-walk that isn't warranted here.)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    PangoDirection dir = pango_find_base_dir(text.c_str(), -1);
#pragma GCC diagnostic pop
    if (dir == PANGO_DIRECTION_RTL && align == PANGO_ALIGN_LEFT) align = PANGO_ALIGN_RIGHT;
    pango_layout_set_alignment(layout, align);
    pango_layout_set_auto_dir(layout, TRUE);

    SetColor(cr, style.color);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);

    if (outHeight) {
        int w, h;
        pango_layout_get_pixel_size(layout, &w, &h);
        *outHeight = h;
    }
    g_object_unref(layout);
}

static double TextHeight(cairo_t* cr, const std::string& text, double maxWidth, const TextStyle& style) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, text.c_str(), -1);
    PangoFontDescription* desc = pango_font_description_from_string(
        style.mono ? Theme::MonoFontFamily : Theme::FontFamily);
    pango_font_description_set_size(desc, (int)(style.size * PANGO_SCALE));
    pango_font_description_set_weight(desc, style.bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    if (maxWidth > 0) pango_layout_set_width(layout, (int)(maxWidth * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    int w, h;
    pango_layout_get_pixel_size(layout, &w, &h);
    g_object_unref(layout);
    return h;
}

// Draws a short, single-line label (button/pill/badge text) horizontally
// centered within [x, x+w], with NO word-wrap regardless of measured width —
// used anywhere a fixed-height pill/button must never grow to two lines.
static void DrawLabelCentered(cairo_t* cr, const std::string& text, double x, double y, double w,
                               double size, bool bold, const Theme::Color& color) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, text.c_str(), -1);
    PangoFontDescription* desc = pango_font_description_from_string(Theme::FontFamily);
    pango_font_description_set_size(desc, (int)(size * PANGO_SCALE));
    pango_font_description_set_weight(desc, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);
    SetColor(cr, color);
    cairo_move_to(cr, x + (w - tw) / 2.0, y - th / 2.0);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}

// Measures a short single-line label's pixel width (for auto-sizing pills).
static double MeasureLabelWidth(cairo_t* cr, const std::string& text, double size, bool bold) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, text.c_str(), -1);
    PangoFontDescription* desc = pango_font_description_from_string(Theme::FontFamily);
    pango_font_description_set_size(desc, (int)(size * PANGO_SCALE));
    pango_font_description_set_weight(desc, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);
    g_object_unref(layout);
    return tw;
}

// Draws a short, single-line label anchored to one edge (never word-wrapped,
// regardless of how wide the text turns out to be on the system's actual
// installed fonts) — e.g. right-edge-anchored "155 ms" that must never
// break into "155" / "ms" on two lines just because a 3-digit number is
// wider than a 1-digit one was during testing.
static double DrawLabelEdgeAligned(cairo_t* cr, const std::string& text, double edgeX, double y,
                                    double size, bool bold, const Theme::Color& color, bool alignRight) {
    double tw = MeasureLabelWidth(cr, text, size, bold);
    double x = alignRight ? edgeX - tw : edgeX;
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, text.c_str(), -1);
    PangoFontDescription* desc = pango_font_description_from_string(Theme::FontFamily);
    pango_font_description_set_size(desc, (int)(size * PANGO_SCALE));
    pango_font_description_set_weight(desc, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    SetColor(cr, color);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    return tw;
}

// ---------------------------------------------------------------------------
// Hand-drawn vector icons (PORT_SPEC.md §9) — small (~18-22px), ~1.5-2px
// stroke, drawn with Cairo paths. Each function draws centered in the given
// box.
// ---------------------------------------------------------------------------
namespace Icon {

    static void SetStroke(cairo_t* cr, const Theme::Color& c, double width = 1.8) {
        SetColor(cr, c);
        cairo_set_line_width(cr, width);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    }

    // Gear/settings icon: filled 6-tooth cog with a true center hole via
    // even-odd fill (outer gear polygon + inner circle in the same path) —
    // not a ring-with-ticks. See PORT_SPEC.md §5.4.
    static void Gear(cairo_t* cr, double cx, double cy, double r, const Theme::Color& c) {
        cairo_save(cr);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        SetColor(cr, c);
        int teeth = 6;
        double outerR = r, innerR = r * 0.62, toothR = r * 1.22;
        cairo_new_path(cr);
        for (int i = 0; i < teeth * 2; ++i) {
            double ang = i * M_PI / teeth;
            double rr = (i % 2 == 0) ? toothR : outerR;
            double px = cx + rr * cos(ang), py = cy + rr * sin(ang);
            if (i == 0) cairo_move_to(cr, px, py); else cairo_line_to(cr, px, py);
        }
        cairo_close_path(cr);
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, innerR * 0.45, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // Bookmark ribbon (not a star — PORT_SPEC.md §5.1).
    static void Bookmark(cairo_t* cr, double x, double y, double s, const Theme::Color& c, bool filled) {
        cairo_save(cr);
        double w = s * 0.62, h = s;
        cairo_new_path(cr);
        cairo_move_to(cr, x, y);
        cairo_line_to(cr, x + w, y);
        cairo_line_to(cr, x + w, y + h);
        cairo_line_to(cr, x + w / 2, y + h * 0.72);
        cairo_line_to(cr, x, y + h);
        cairo_close_path(cr);
        if (filled) { SetColor(cr, c); cairo_fill(cr); }
        else { SetStroke(cr, c, 1.6); cairo_stroke(cr); }
        cairo_restore(cr);
    }

    // Solid arrowhead "connect" icon (->), not a font glyph. Pass flip=true
    // to mirror it into a <- shape for RTL layouts.
    static void Arrow(cairo_t* cr, double x, double y, double s, const Theme::Color& c, bool flip = false) {
        cairo_save(cr);
        SetColor(cr, c);
        double dir = flip ? -1.0 : 1.0;
        cairo_move_to(cr, x, y - s * 0.45);
        cairo_line_to(cr, x + dir * s * 0.9, y);
        cairo_line_to(cr, x, y + s * 0.45);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // Lightning bolt, optionally mirrored vertically (Upload vs Download in
    // the Speed Test stat grid use the SAME shape, just flipped — PORT_SPEC §5.3).
    static void Bolt(cairo_t* cr, double cx, double cy, double s, const Theme::Color& c, bool flip = false) {
        cairo_save(cr);
        cairo_translate(cr, cx, cy);
        if (flip) cairo_scale(cr, 1, -1);
        SetColor(cr, c);
        double h = s;
        cairo_move_to(cr, 0.12 * h, -0.5 * h);
        cairo_line_to(cr, -0.28 * h, 0.08 * h);
        cairo_line_to(cr, 0.0, 0.08 * h);
        cairo_line_to(cr, -0.12 * h, 0.5 * h);
        cairo_line_to(cr, 0.32 * h, -0.12 * h);
        cairo_line_to(cr, 0.02 * h, -0.12 * h);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // Checkmark-in-circle (Stability stat).
    static void CheckCircle(cairo_t* cr, double cx, double cy, double r, const Theme::Color& c) {
        cairo_save(cr);
        SetStroke(cr, c, 1.8);
        cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
        cairo_stroke(cr);
        cairo_move_to(cr, cx - r * 0.45, cy);
        cairo_line_to(cr, cx - r * 0.12, cy + r * 0.35);
        cairo_line_to(cr, cx + r * 0.5, cy - r * 0.35);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    // Wifi/signal bars (Avg ping stat).
    static void SignalBars(cairo_t* cr, double x, double y, double s, const Theme::Color& c) {
        cairo_save(cr);
        SetColor(cr, c);
        double barW = s * 0.16;
        double gap = s * 0.08;
        for (int i = 0; i < 4; ++i) {
            double h = s * (0.3 + 0.23 * i);
            double bx = x + i * (barW + gap);
            cairo_rectangle(cr, bx, y + s - h, barW, h);
            cairo_fill(cr);
        }
        cairo_restore(cr);
    }

    // Circular arrow / refresh with a REAL solid triangular arrowhead
    // (a bare ring with no arrowhead was explicitly rejected — PORT_SPEC §5.3).
    static void RefreshArrow(cairo_t* cr, double cx, double cy, double r, const Theme::Color& c) {
        cairo_save(cr);
        SetStroke(cr, c, 1.8);
        cairo_arc(cr, cx, cy, r, -200 * M_PI / 180.0, 150 * M_PI / 180.0);
        cairo_stroke(cr);
        double ang = 150 * M_PI / 180.0;
        double hx = cx + r * cos(ang), hy = cy + r * sin(ang);
        double tang = ang + M_PI / 2;
        SetColor(cr, c);
        cairo_move_to(cr, hx + r * 0.35 * cos(tang), hy + r * 0.35 * sin(tang));
        cairo_line_to(cr, hx - r * 0.35 * cos(tang), hy - r * 0.35 * sin(tang));
        cairo_line_to(cr, hx + r * 0.5 * cos(ang), hy + r * 0.5 * sin(ang));
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // Small zigzag wave (Jitter stat).
    static void Zigzag(cairo_t* cr, double x, double y, double s, const Theme::Color& c) {
        cairo_save(cr);
        SetStroke(cr, c, 1.8);
        cairo_move_to(cr, x, y + s * 0.5);
        cairo_line_to(cr, x + s * 0.25, y);
        cairo_line_to(cr, x + s * 0.5, y + s * 0.7);
        cairo_line_to(cr, x + s * 0.75, y + s * 0.15);
        cairo_line_to(cr, x + s, y + s * 0.5);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    // Cloud with down arrow (Real Telegram speed card).
    static void CloudDown(cairo_t* cr, double cx, double cy, double s, const Theme::Color& c) {
        cairo_save(cr);
        SetColor(cr, c);
        cairo_arc(cr, cx - s * 0.22, cy, s * 0.22, 0, 2 * M_PI);
        cairo_arc(cr, cx + s * 0.05, cy - s * 0.08, s * 0.28, 0, 2 * M_PI);
        cairo_arc(cr, cx + s * 0.32, cy + s * 0.02, s * 0.18, 0, 2 * M_PI);
        cairo_fill(cr);
        SetStroke(cr, c, 1.8);
        cairo_move_to(cr, cx, cy + s * 0.15);
        cairo_line_to(cr, cx, cy + s * 0.55);
        cairo_move_to(cr, cx - s * 0.15, cy + s * 0.38);
        cairo_line_to(cr, cx, cy + s * 0.58);
        cairo_line_to(cr, cx + s * 0.15, cy + s * 0.38);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    // Crescent moon via true region subtraction (two circles, path-based
    // subtraction) — NOT even-odd fill of two overlapping circles, which
    // leaves both leftover slivers visible. See PORT_SPEC §5.4.
    static void Moon(cairo_t* cr, double cx, double cy, double r, const Theme::Color& c) {
        cairo_save(cr);
        cairo_push_group(cr);
        SetColor(cr, c);
        cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
        cairo_set_source_rgba(cr, 0, 0, 0, 1);
        cairo_arc(cr, cx + r * 0.55, cy - r * 0.2, r * 0.85, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_pop_group_to_source(cr);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    static void Sun(cairo_t* cr, double cx, double cy, double r, const Theme::Color& c) {
        cairo_save(cr);
        SetColor(cr, c);
        cairo_arc(cr, cx, cy, r * 0.55, 0, 2 * M_PI);
        cairo_fill(cr);
        SetStroke(cr, c, 1.6);
        for (int i = 0; i < 8; ++i) {
            double ang = i * M_PI / 4;
            double x1 = cx + r * 0.75 * cos(ang), y1 = cy + r * 0.75 * sin(ang);
            double x2 = cx + r * 1.05 * cos(ang), y2 = cy + r * 1.05 * sin(ang);
            cairo_move_to(cr, x1, y1);
            cairo_line_to(cr, x2, y2);
        }
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    static void HalfDarkLight(cairo_t* cr, double cx, double cy, double r,
                               const Theme::Color& dark, const Theme::Color& light) {
        cairo_save(cr);
        SetColor(cr, light);
        cairo_arc(cr, cx, cy, r, -M_PI / 2, M_PI / 2);
        cairo_fill(cr);
        SetColor(cr, dark);
        cairo_arc(cr, cx, cy, r, M_PI / 2, 3 * M_PI / 2);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    static void Heart(cairo_t* cr, double cx, double cy, double s, const Theme::Color& c) {
        cairo_save(cr);
        SetColor(cr, c);
        double r = s * 0.28;
        cairo_arc(cr, cx - r, cy - r * 0.4, r, 0, 2 * M_PI);
        cairo_arc(cr, cx + r, cy - r * 0.4, r, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_move_to(cr, cx - s * 0.5, cy - r * 0.2);
        cairo_line_to(cr, cx, cy + s * 0.55);
        cairo_line_to(cr, cx + s * 0.5, cy - r * 0.2);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    static void WarningTriangle(cairo_t* cr, double cx, double cy, double s, const Theme::Color& c) {
        cairo_save(cr);
        SetStroke(cr, c, 1.8);
        cairo_move_to(cr, cx, cy - s * 0.5);
        cairo_line_to(cr, cx + s * 0.5, cy + s * 0.4);
        cairo_line_to(cr, cx - s * 0.5, cy + s * 0.4);
        cairo_close_path(cr);
        cairo_stroke(cr);
        cairo_move_to(cr, cx, cy - s * 0.15);
        cairo_line_to(cr, cx, cy + s * 0.08);
        cairo_stroke(cr);
        cairo_arc(cr, cx, cy + s * 0.25, 1.4, 0, 2 * M_PI);
        SetColor(cr, c);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // Speedometer (gauge with needle + center dot) — Settings "Speed Test"
    // nav row icon; not a checkmark. PORT_SPEC §5.4.
    static void Speedometer(cairo_t* cr, double cx, double cy, double r, const Theme::Color& c) {
        cairo_save(cr);
        SetStroke(cr, c, 1.8);
        cairo_arc(cr, cx, cy, r, M_PI * 0.75, M_PI * 2.25);
        cairo_stroke(cr);
        double ang = M_PI * 1.65;
        cairo_move_to(cr, cx, cy);
        cairo_line_to(cr, cx + r * 0.7 * cos(ang), cy + r * 0.7 * sin(ang));
        cairo_stroke(cr);
        SetColor(cr, c);
        cairo_arc(cr, cx, cy, r * 0.12, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // ---- Generic SVG path renderer -----------------------------------
    // Parses a subset of the SVG path mini-language (M/L/H/V/C/S/Q/T/A/Z,
    // upper or lower case) and issues the equivalent Cairo path commands.
    // Used to render the *exact* official brand marks below from their
    // real SVG path data (simple-icons, 24x24 viewBox) instead of a
    // hand-approximated silhouette.
    struct SvgPathParser {
        const char* p;
        double curX = 0, curY = 0;        // current point
        double startX = 0, startY = 0;    // subpath start (for Z)
        double prevCtrlX = 0, prevCtrlY = 0; // for S/T reflection
        char prevCmd = 0;
        cairo_t* cr;
        double ox, oy, scale;             // origin + scale to map SVG units -> screen px

        double tx(double x) const { return ox + x * scale; }
        double ty(double y) const { return oy + y * scale; }

        void SkipSep() { while (*p && (isspace((unsigned char)*p) || *p == ',')) ++p; }

        bool ReadNumber(double& out) {
            SkipSep();
            const char* start = p;
            if (*p == '+' || *p == '-') ++p;
            bool anyDigit = false;
            while (isdigit((unsigned char)*p)) { ++p; anyDigit = true; }
            if (*p == '.') { ++p; while (isdigit((unsigned char)*p)) { ++p; anyDigit = true; } }
            if (!anyDigit) { p = start; return false; }
            if (*p == 'e' || *p == 'E') {
                const char* save = p; ++p;
                if (*p == '+' || *p == '-') ++p;
                if (isdigit((unsigned char)*p)) { while (isdigit((unsigned char)*p)) ++p; }
                else p = save;
            }
            out = atof(std::string(start, p - start).c_str());
            return true;
        }

        // Arc flags are a single '0' or '1' digit, sometimes with no
        // separator before the next number (e.g. "01.171" = flag(0),
        // flag(1)? no: large-arc=0, sweep=1, then x=.171) — so these must
        // be read one character at a time rather than via ReadNumber.
        bool ReadFlag(double& out) {
            SkipSep();
            if (*p == '0' || *p == '1') { out = (*p == '1') ? 1 : 0; ++p; return true; }
            return false;
        }

        void ArcToBezier(double rx, double ry, double rot, bool largeArc, bool sweep, double x2, double y2) {
            // Standard SVG arc -> center parameterization -> cubic bezier
            // approximation (split into <= 90deg segments).
            double x1 = curX, y1 = curY;
            if (rx == 0 || ry == 0) { cairo_line_to(cr, tx(x2), ty(y2)); return; }
            rx = fabs(rx); ry = fabs(ry);
            double phi = rot * M_PI / 180.0;
            double dx2 = (x1 - x2) / 2.0, dy2 = (y1 - y2) / 2.0;
            double x1p = cos(phi) * dx2 + sin(phi) * dy2;
            double y1p = -sin(phi) * dx2 + cos(phi) * dy2;
            double rxs = rx * rx, rys = ry * ry;
            double x1ps = x1p * x1p, y1ps = y1p * y1p;
            double lambda = x1ps / rxs + y1ps / rys;
            if (lambda > 1) { double s = sqrt(lambda); rx *= s; ry *= s; rxs = rx*rx; rys = ry*ry; }
            double sign = (largeArc != sweep) ? 1.0 : -1.0;
            double num = rxs*rys - rxs*y1ps - rys*x1ps;
            if (num < 0) num = 0;
            double co = sign * sqrt(num / (rxs*y1ps + rys*x1ps + 1e-9));
            double cxp = co * (rx * y1p / ry);
            double cyp = co * (-ry * x1p / rx);
            double cxCtr = cos(phi)*cxp - sin(phi)*cyp + (x1+x2)/2.0;
            double cyCtr = sin(phi)*cxp + cos(phi)*cyp + (y1+y2)/2.0;
            auto angle = [](double ux, double uy, double vx, double vy) {
                double dot = ux*vx + uy*vy;
                double len = sqrt((ux*ux+uy*uy)*(vx*vx+vy*vy));
                double a = acos(std::clamp(dot/len, -1.0, 1.0));
                if (ux*vy - uy*vx < 0) a = -a;
                return a;
            };
            double theta1 = angle(1,0,(x1p-cxp)/rx,(y1p-cyp)/ry);
            double dtheta = angle((x1p-cxp)/rx,(y1p-cyp)/ry,(-x1p-cxp)/rx,(-y1p-cyp)/ry);
            if (!sweep && dtheta > 0) dtheta -= 2*M_PI;
            if (sweep && dtheta < 0) dtheta += 2*M_PI;
            int segs = (int)ceil(fabs(dtheta) / (M_PI/2));
            if (segs < 1) segs = 1;
            double delta = dtheta / segs;
            double t = theta1;
            for (int i = 0; i < segs; ++i) {
                double t2 = t + delta;
                double alpha = sin(delta) * (sqrt(4 + 3*tan(delta/2)*tan(delta/2)) - 1) / 3.0;
                double cosT = cos(t), sinT = sin(t), cosT2 = cos(t2), sinT2 = sin(t2);
                double e1x = -rx*cosT*0 - rx*sinT, e1y = ry*cosT; // d/dtheta of ellipse point (unrotated)
                // Ellipse point + derivative, then rotate by phi and translate.
                auto ellipsePt = [&](double ang, double& ex, double& ey) {
                    double ux = rx*cos(ang), uy = ry*sin(ang);
                    ex = cxCtr + cos(phi)*ux - sin(phi)*uy;
                    ey = cyCtr + sin(phi)*ux + cos(phi)*uy;
                };
                auto ellipseDeriv = [&](double ang, double& dx, double& dy) {
                    double ux = -rx*sin(ang), uy = ry*cos(ang);
                    dx = cos(phi)*ux - sin(phi)*uy;
                    dy = sin(phi)*ux + cos(phi)*uy;
                };
                (void)e1x; (void)e1y; (void)cosT; (void)sinT; (void)cosT2; (void)sinT2;
                double p1x, p1y, p2x, p2y, d1x, d1y, d2x, d2y;
                ellipsePt(t, p1x, p1y); ellipsePt(t2, p2x, p2y);
                ellipseDeriv(t, d1x, d1y); ellipseDeriv(t2, d2x, d2y);
                double c1x = p1x + alpha*d1x, c1y = p1y + alpha*d1y;
                double c2x = p2x - alpha*d2x, c2y = p2y - alpha*d2y;
                cairo_curve_to(cr, tx(c1x), ty(c1y), tx(c2x), ty(c2y), tx(p2x), ty(p2y));
                t = t2;
            }
        }

        void Run(const char* d, cairo_t* cr_, double ox_, double oy_, double scale_) {
            cr = cr_; ox = ox_; oy = oy_; scale = scale_;
            p = d;
            curX = curY = startX = startY = prevCtrlX = prevCtrlY = 0;
            prevCmd = 0;
            char cmd = 0;
            while (*p) {
                SkipSep();
                if (!*p) break;
                if (isalpha((unsigned char)*p)) { cmd = *p; ++p; }
                bool rel = islower((unsigned char)cmd);
                char C = toupper(cmd);
                double n1,n2,n3,n4,n5,n6,n7;
                switch (C) {
                    case 'M':
                        if (!ReadNumber(n1) || !ReadNumber(n2)) return;
                        curX = rel ? curX+n1 : n1; curY = rel ? curY+n2 : n2;
                        startX = curX; startY = curY;
                        cairo_move_to(cr, tx(curX), ty(curY));
                        break;
                    case 'L':
                        if (!ReadNumber(n1) || !ReadNumber(n2)) return;
                        curX = rel ? curX+n1 : n1; curY = rel ? curY+n2 : n2;
                        cairo_line_to(cr, tx(curX), ty(curY));
                        break;
                    case 'H':
                        if (!ReadNumber(n1)) return;
                        curX = rel ? curX+n1 : n1;
                        cairo_line_to(cr, tx(curX), ty(curY));
                        break;
                    case 'V':
                        if (!ReadNumber(n1)) return;
                        curY = rel ? curY+n1 : n1;
                        cairo_line_to(cr, tx(curX), ty(curY));
                        break;
                    case 'C': {
                        if (!ReadNumber(n1)||!ReadNumber(n2)||!ReadNumber(n3)||!ReadNumber(n4)||!ReadNumber(n5)||!ReadNumber(n6)) return;
                        double x1 = rel?curX+n1:n1, y1 = rel?curY+n2:n2;
                        double x2 = rel?curX+n3:n3, y2 = rel?curY+n4:n4;
                        double ex = rel?curX+n5:n5, ey = rel?curY+n6:n6;
                        cairo_curve_to(cr, tx(x1),ty(y1), tx(x2),ty(y2), tx(ex),ty(ey));
                        prevCtrlX = x2; prevCtrlY = y2;
                        curX = ex; curY = ey;
                        break;
                    }
                    case 'S': {
                        if (!ReadNumber(n1)||!ReadNumber(n2)||!ReadNumber(n3)||!ReadNumber(n4)) return;
                        double x1, y1;
                        if (prevCmd=='C'||prevCmd=='S') { x1 = 2*curX-prevCtrlX; y1 = 2*curY-prevCtrlY; }
                        else { x1 = curX; y1 = curY; }
                        double x2 = rel?curX+n1:n1, y2 = rel?curY+n2:n2;
                        double ex = rel?curX+n3:n3, ey = rel?curY+n4:n4;
                        cairo_curve_to(cr, tx(x1),ty(y1), tx(x2),ty(y2), tx(ex),ty(ey));
                        prevCtrlX = x2; prevCtrlY = y2;
                        curX = ex; curY = ey;
                        break;
                    }
                    case 'Q': {
                        if (!ReadNumber(n1)||!ReadNumber(n2)||!ReadNumber(n3)||!ReadNumber(n4)) return;
                        double x1 = rel?curX+n1:n1, y1 = rel?curY+n2:n2;
                        double ex = rel?curX+n3:n3, ey = rel?curY+n4:n4;
                        // Quadratic -> cubic
                        double c1x = curX + 2.0/3.0*(x1-curX), c1y = curY + 2.0/3.0*(y1-curY);
                        double c2x = ex + 2.0/3.0*(x1-ex), c2y = ey + 2.0/3.0*(y1-ey);
                        cairo_curve_to(cr, tx(c1x),ty(c1y), tx(c2x),ty(c2y), tx(ex),ty(ey));
                        prevCtrlX = x1; prevCtrlY = y1;
                        curX = ex; curY = ey;
                        break;
                    }
                    case 'A': {
                        if (!ReadNumber(n1)||!ReadNumber(n2)||!ReadNumber(n3)||!ReadFlag(n4)||!ReadFlag(n5)||!ReadNumber(n6)||!ReadNumber(n7)) return;
                        double ex = rel?curX+n6:n6, ey = rel?curY+n7:n7;
                        ArcToBezier(n1, n2, n3, n4!=0, n5!=0, ex, ey);
                        curX = ex; curY = ey;
                        break;
                    }
                    case 'Z':
                        cairo_close_path(cr);
                        curX = startX; curY = startY;
                        break;
                    default:
                        return; // unsupported command — bail rather than mis-render
                }
                prevCmd = C;
            }
        }
    };

    // Draws an SVG path (24x24 viewBox assumed) filled solid, centered at
    // (cx,cy) and scaled so its viewBox fills a box of size s.
    static void DrawSvgIcon24(cairo_t* cr, const char* pathData, double cx, double cy, double s, const Theme::Color& c) {
        cairo_save(cr);
        cairo_new_path(cr);
        double scale = s / 24.0;
        SvgPathParser parser;
        parser.Run(pathData, cr, cx - s/2, cy - s/2, scale);
        SetColor(cr, c);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // Official GitHub mark — exact path from simple-icons (24x24 viewBox).
    static void GitHubLogo(cairo_t* cr, double cx, double cy, double s, const Theme::Color& c) {
        static const char* kPath =
            "M12 .297c-6.63 0-12 5.373-12 12 0 5.303 3.438 9.8 8.205 11.385.6.113.82-.258.82-.577 0-.285-.01-1.04-.015-2.04-3.338.724-4.042-1.61-4.042-1.61C4.422 18.07 3.633 17.7 3.633 17.7c-1.087-.744.084-.729.084-.729 1.205.084 1.838 1.236 1.838 1.236 1.07 1.835 2.809 1.305 3.495.998.108-.776.417-1.305.76-1.605-2.665-.3-5.466-1.332-5.466-5.93 0-1.31.465-2.38 1.235-3.22-.135-.303-.54-1.523.105-3.176 0 0 1.005-.322 3.3 1.23.96-.267 1.98-.399 3-.405 1.02.006 2.04.138 3 .405 2.28-1.552 3.285-1.23 3.285-1.23.645 1.653.24 2.873.12 3.176.765.84 1.23 1.91 1.23 3.22 0 4.61-2.805 5.625-5.475 5.92.42.36.81 1.096.81 2.22 0 1.606-.015 2.896-.015 3.286 0 .315.21.69.825.57C20.565 22.092 24 17.592 24 12.297c0-6.627-5.373-12-12-12";
        DrawSvgIcon24(cr, kPath, cx, cy, s, c);
    }

    // Official Telegram mark — exact path from simple-icons (24x24 viewBox,
    // circle + paper-plane combined in one path).
    static void TelegramLogo(cairo_t* cr, double cx, double cy, double s, const Theme::Color& c) {
        static const char* kPath =
            "M11.944 0A12 12 0 000 12a12 12 0 0012 12 12 12 0 0012-12A12 12 0 0012 0a12 12 0 00-.056 0zm4.962 7.224c.1-.002.321.023.465.14a.506.506 0 01.171.325c.016.093.036.306.02.472-.18 1.898-.962 6.502-1.36 8.627-.168.9-.499 1.201-.82 1.23-.696.065-1.225-.46-1.9-.902-1.056-.693-1.653-1.124-2.678-1.8-1.185-.78-.417-1.21.258-1.91.177-.184 3.247-2.977 3.307-3.23.007-.032.014-.15-.056-.212s-.174-.041-.249-.024c-.106.024-1.793 1.14-5.061 3.345-.48.33-.913.49-1.302.48-.428-.008-1.252-.241-1.865-.44-.752-.245-1.349-.374-1.297-.789.027-.216.325-.437.893-.663 3.498-1.524 5.83-2.529 6.998-3.014 3.332-1.386 4.025-1.627 4.476-1.635";
        DrawSvgIcon24(cr, kPath, cx, cy, s, c);
    }

    // Real (if simplified) national flags, drawn as vectors so no external
    // image asset/path resolution is needed inside the AppImage. Sized to
    // fill a w x h box with rounded corners + a thin border for definition
    // against either theme's background.
    static void FlagUS(cairo_t* cr, double x, double y, double w, double h) {
        cairo_save(cr);
        RoundedRect(cr, x, y, w, h, 2);
        cairo_clip(cr);
        SetColor(cr, Theme::Color(0xFFFFFF));
        cairo_paint(cr);
        int stripes = 7;
        double stripeH = h / stripes;
        SetColor(cr, Theme::Color(0xB22234));
        for (int i = 0; i < stripes; i += 2) {
            cairo_rectangle(cr, x, y + i * stripeH, w, stripeH);
            cairo_fill(cr);
        }
        double cantonW = w * 0.42, cantonH = stripeH * 4;
        SetColor(cr, Theme::Color(0x3C3B6E));
        cairo_rectangle(cr, x, y, cantonW, cantonH);
        cairo_fill(cr);
        SetColor(cr, Theme::Color(0xFFFFFF));
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 3; ++col)
                cairo_arc(cr, x + cantonW * (0.2 + col * 0.3), y + cantonH * (0.28 + row * 0.44), w * 0.03, 0, 2 * M_PI),
                cairo_fill(cr);
        cairo_restore(cr);
        RoundedRect(cr, x, y, w, h, 2);
        SetColor(cr, Theme::Color(0x000000), 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    }

    static void FlagRU(cairo_t* cr, double x, double y, double w, double h) {
        cairo_save(cr);
        RoundedRect(cr, x, y, w, h, 2);
        cairo_clip(cr);
        double bandH = h / 3.0;
        SetColor(cr, Theme::Color(0xFFFFFF));
        cairo_rectangle(cr, x, y, w, bandH);
        cairo_fill(cr);
        SetColor(cr, Theme::Color(0x0039A6));
        cairo_rectangle(cr, x, y + bandH, w, bandH);
        cairo_fill(cr);
        SetColor(cr, Theme::Color(0xD52B1E));
        cairo_rectangle(cr, x, y + 2 * bandH, w, h - 2 * bandH);
        cairo_fill(cr);
        cairo_restore(cr);
        RoundedRect(cr, x, y, w, h, 2);
        SetColor(cr, Theme::Color(0x000000), 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    }

}

// ---------------------------------------------------------------------------
// Ping color coding (PORT_SPEC.md §4)
// ---------------------------------------------------------------------------
static Theme::Color PingColor(int ms) {
    if (ms < 0) return Theme::PingBad;
    if (ms <= 150) return Theme::PingGood;
    if (ms <= 300) return Theme::PingMedium;
    return Theme::PingBad;
}

static const char* QualityKey(SpeedTester::Quality q, const AppState* app, std::string& out) {
    switch (q) {
        case SpeedTester::Quality::Excellent: out = L(app, Loc::Key::QualityExcellent); break;
        case SpeedTester::Quality::Good:      out = L(app, Loc::Key::QualityGood); break;
        case SpeedTester::Quality::Fair:      out = L(app, Loc::Key::QualityFair); break;
        case SpeedTester::Quality::Poor:      out = L(app, Loc::Key::QualityPoor); break;
        default:                               out = L(app, Loc::Key::QualityOffline); break;
    }
    return out.c_str();
}

static Theme::Color QualityColor(SpeedTester::Quality q) {
    switch (q) {
        case SpeedTester::Quality::Excellent: return Theme::PingGood;
        case SpeedTester::Quality::Good:      return Theme::Color(0x8BC34A);
        case SpeedTester::Quality::Fair:      return Theme::PingMedium;
        default:                               return Theme::PingBad;
    }
}

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
static constexpr double kWindowWidth = 480;
static constexpr double kPad = Theme::SpacingMedium;

// Raw hit-rect registration, bypassing the visibility filter below — used
// ONLY by the top bar's own back/gear buttons, which must stay clickable at
// their fixed screen position regardless of how the content beneath is
// scrolled.
static void AddHit(AppState* app, double x, double y, double w, double h, const std::string& action) {
    app->hitRects.push_back({x, y, w, h, action});
}

// Hit-rect registration for scrollable screen CONTENT. Rejects anything
// that's entirely above the visible content area (y+h <= currentBarH).
// Without this, a card's remembered click target — computed from its
// scrolled Y position, which can coincide with the fixed top-bar's screen
// coordinates once scrolled far enough — would still register as clickable
// even though it's invisible (clipped away), and since content hits are
// pushed after the top bar's and OnClick checks most-recently-pushed first,
// that invisible hit could silently swallow clicks meant for the back
// button. This is why "back" sometimes didn't work after scrolling.
static void AddContentHit(AppState* app, double x, double y, double w, double h, const std::string& action) {
    if (y + h <= app->currentBarH) return;
    app->hitRects.push_back({x, y, w, h, action});
}

// ---------------------------------------------------------------------------
// Top bar (shared by every screen)
// ---------------------------------------------------------------------------
static double DrawTopBar(cairo_t* cr, AppState* app, double width, const std::string& title,
                          bool showBack, bool showGear, const std::string& subtitle = "") {
    const auto& pal = Pal(app);
    bool rtl = IsRtl(app);
    double barH = Theme::TopBarHeight;
    SetColor(cr, pal.surface);
    cairo_rectangle(cr, 0, 0, width, barH);
    cairo_fill(cr);

    // Proper RTL mirroring: gear moves to the left and the back button to
    // the right (pointing right) in Persian, with the title/subtitle block
    // right-aligned and starting from the right edge — the traditional RTL
    // arrangement, per product direction.
    double reservedLeft = rtl ? (showGear ? 44.0 : 0.0) : (showBack ? 44.0 : 0.0);
    double reservedRight = rtl ? (showBack ? 44.0 : 0.0) : (showGear ? 44.0 : 0.0);
    double titleX = kPad + reservedLeft;
    double titleMax = width - kPad * 2 - reservedLeft - reservedRight;

    if (showBack) {
        cairo_save(cr);
        Icon::SetStroke(cr, pal.onSurface, 2.0);
        double bx = rtl ? width - kPad - 22 : kPad;
        double by = barH / 2;
        if (rtl) {
            cairo_move_to(cr, bx + 6, by - 8); cairo_line_to(cr, bx + 16, by); cairo_line_to(cr, bx + 6, by + 8);
        } else {
            cairo_move_to(cr, bx + 16, by - 8); cairo_line_to(cr, bx + 6, by); cairo_line_to(cr, bx + 16, by + 8);
        }
        cairo_stroke(cr);
        cairo_restore(cr);
        AddHit(app, bx - 14, by - 20, 44, 40, "nav:back");
    }

    // Gear icon: larger and tinted primary-blue to match the reference
    // design (it reads as a clear settings affordance instead of a faint
    // gray dot in the corner).
    if (showGear) {
        double gx = rtl ? kPad + 16 : width - kPad - 16;
        Icon::Gear(cr, gx, barH / 2, 15, pal.primary);
        AddHit(app, gx - 18, barH / 2 - 18, 36, 36, "nav:settings");
    }

    // Title in the accent blue (not plain onSurface) — matches the
    // reference's "Iwana Proxy" wordmark treatment.
    TextStyle titleStyle; titleStyle.size = 19; titleStyle.bold = true; titleStyle.color = pal.primary;
    titleStyle.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
    double titleH = 0;
    DrawText(cr, app, title, titleX, subtitle.empty() ? (barH - 24) / 2 : 13, titleMax, titleStyle, &titleH);

    if (!subtitle.empty()) {
        // Small green status dot, level with the subtitle text (on the same
        // side it starts from), given its own reserved width so the two
        // never overlap.
        TextStyle subStyle; subStyle.size = 11; subStyle.bold = true; subStyle.color = pal.onSurfaceVariant;
        subStyle.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
        double dotR = 3.5;
        double subY = 13 + titleH + 6;
        double dotX = rtl ? titleX + titleMax - dotR : titleX + dotR;
        cairo_save(cr);
        SetColor(cr, pal.success);
        cairo_arc(cr, dotX, subY + 6, dotR, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_restore(cr);
        double textX = rtl ? titleX : titleX + dotR * 2 + 8;
        double textW = titleMax - dotR * 2 - 8;
        DrawText(cr, app, subtitle, textX, subY, textW, subStyle);
    }

    SetColor(cr, pal.tertiary, 0.5);
    cairo_rectangle(cr, 0, barH - 1, width, 1);
    cairo_fill(cr);
    return barH;
}

// ---------------------------------------------------------------------------
// Proxy card (Home + Saved screens) — layout matches the reference Windows
// app screenshot: "Proxy N" + inline ONLINE pill on the top-left, a bold
// colored latency number with a small caption underneath on the top-right,
// the host:port line below, then a bottom row with copy/bookmark icons on
// the leading side and the CONNECT pill button on the trailing side.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Proxy card (Home + Saved screens) — ALWAYS laid out left-to-right
// (name/host/copy/bookmark on the left, latency/connect on the right),
// regardless of app language. Per explicit product direction, Persian mode
// keeps this exact arrangement rather than mirroring it — only the pill
// text content (ONLINE/FOR DOWNLOAD/RUSSIAN) actually translates.
// ---------------------------------------------------------------------------
static double DrawProxyCard(cairo_t* cr, AppState* app, double x, double y, double w,
                             const ProxyItem& p, int index) {
    const auto& pal = Pal(app);
    double pad = 16;
    double cardH = 150; // status/download/russian badges are now always on their own row

    RoundedRect(cr, x, y, w, cardH, Theme::CornerRadiusCard);
    SetColor(cr, pal.surfaceVariant);
    cairo_fill(cr);

    double leftX = x + pad;
    double rightX = x + w - pad;

    // --- Row 1: "Proxy N" + inline ONLINE/OFFLINE pill on the left,
    //            latency number + "LATENCY" caption on the right ---
    std::string name = "Proxy " + std::to_string(p.id);
    double rowTopY = y + 16;

    // Reserve space on the right for the latency block so the name area
    // never runs into it. Sized generously (worst case "3000 ms" at 17px
    // bold) since PingService's timeout caps latency at 3000ms.
    double latencyBlockW = 100;
    double nameAreaW = w - pad * 2 - latencyBlockW;

    {
        TextStyle nameStyle; nameStyle.size = 15; nameStyle.bold = true; nameStyle.color = pal.onSurface;
        nameStyle.align = PANGO_ALIGN_LEFT;
        DrawText(cr, app, name, leftX, rowTopY, nameAreaW, nameStyle);
    }

    if (p.isScanned && p.IsAlive()) {
        Theme::Color latColor = PingColor(p.pingMs);
        std::string latText = std::to_string(p.pingMs) + " ms";
        double numY = rowTopY - 3;
        // Edge-aligned, never-wraps (see DrawLabelEdgeAligned) — a fixed
        // wrapped box here previously broke "155 ms" into "155" / "ms" on
        // two separate lines for any 3-digit latency, since the box was
        // only ever tested against single-digit values.
        DrawLabelEdgeAligned(cr, latText, rightX, numY, 17, true, latColor, true);
        // "LATENCY" is deliberately never translated (kept in English in
        // every language) per product direction.
        DrawLabelEdgeAligned(cr, "LATENCY", rightX, numY + 24, 8.5, true, pal.onSurfaceVariant, true);
    }

    double rowY = rowTopY + 30;

    // --- Row 2: ALL badges together as one group — status (Online/Offline/
    //     Scanning) plus For-Download/Russian, in that order — instead of
    //     splitting the status pill onto the name's row while For-Download/
    //     Russian sat on a separate row below the host line.
    {
        double pillX = leftX;
        auto drawPill = [&](const std::string& text, Theme::Color color) {
            double tw = MeasureLabelWidth(cr, text, 10, true) + 16;
            double bh = 20;
            RoundedRect(cr, pillX, rowY, tw, bh, 10);
            SetColor(cr, color, 0.16);
            cairo_fill(cr);
            DrawLabelCentered(cr, text, pillX, rowY + bh / 2, tw, 10, true, color);
            pillX += tw + 6;
        };
        if (p.isScanned) {
            bool alive = p.IsAlive();
            drawPill(alive ? L(app, Loc::Key::Online) : L(app, Loc::Key::Offline), alive ? pal.success : pal.error);
        } else {
            drawPill(L(app, Loc::Key::Scanning), pal.onSurfaceVariant);
        }
        if (p.isForDownload) drawPill(L(app, Loc::Key::ForDownloadBadge), pal.primary);
        if (p.isRussian) drawPill(L(app, Loc::Key::RussianBadge), Theme::Color(0xB3261E));
        rowY += 20 + 10;
    }

    // --- Row 3: host:port (single line, ellipsized) ---
    {
        TextStyle hostStyle; hostStyle.size = 12.5; hostStyle.color = pal.onSurfaceVariant;
        hostStyle.align = PANGO_ALIGN_LEFT;
        std::string hostLine = p.server + ":" + p.port;
        PangoLayout* layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, hostLine.c_str(), -1);
        PangoFontDescription* desc = pango_font_description_from_string(Theme::FontFamily);
        pango_font_description_set_size(desc, (int)(hostStyle.size * PANGO_SCALE));
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        pango_layout_set_width(layout, (int)((w - pad * 2) * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);
        pango_layout_set_alignment(layout, hostStyle.align);
        SetColor(cr, hostStyle.color);
        cairo_move_to(cr, leftX, rowY);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        rowY += 26;
    }

    // --- Bottom row: copy/bookmark icons on the left, CONNECT button on the right ---
    double btnW = 118, btnH = 36;
    double btnX = rightX - btnW;
    double btnY = y + cardH - btnH - 14;
    double iconY = btnY + btnH / 2;

    double copyX = leftX;
    cairo_save(cr);
    Icon::SetStroke(cr, pal.onSurfaceVariant, 1.6);
    cairo_rectangle(cr, copyX, iconY - 6, 12, 14);
    cairo_stroke(cr);
    cairo_rectangle(cr, copyX + 4, iconY - 10, 12, 14);
    cairo_stroke(cr);
    cairo_restore(cr);
    AddContentHit(app, copyX - 6, iconY - 16, 28, 32, "copy:" + std::to_string(index));

    double bmX = copyX + 34;
    Icon::Bookmark(cr, bmX, iconY - 9, 18, p.isFavorite ? pal.primary : pal.onSurfaceVariant, p.isFavorite);
    AddContentHit(app, bmX - 6, iconY - 14, 26, 32, "fav:" + std::to_string(index));

    // Connect pill button
    RoundedRect(cr, btnX, btnY, btnW, btnH, Theme::CornerRadiusPill);
    SetColor(cr, pal.primary);
    cairo_fill(cr);
    DrawLabelCentered(cr, L(app, Loc::Key::Connect), btnX, btnY + btnH/2, btnW - 26, 12.5, true, pal.onPrimary);
    Icon::Arrow(cr, btnX + btnW - 16, btnY + btnH / 2, 9, pal.onPrimary, false);
    AddContentHit(app, btnX, btnY, btnW, btnH, "connect:" + std::to_string(index));

    return cardH;
}

// ---------------------------------------------------------------------------
// Screen: Home
// ---------------------------------------------------------------------------
static double DrawHomeScreen(cairo_t* cr, AppState* app, double width, double height) {
    const auto& pal = Pal(app);
    bool rtl = IsRtl(app);

    int aliveCount = 0;
    {
        std::lock_guard<std::mutex> lk(app->proxiesMutex);
        for (auto& p : app->proxies) if (p.IsAlive()) aliveCount++;
    }
    // "CONNECTED PROXIES · N" (uppercase, count last) to match the reference
    // Windows-app layout, instead of "N proxies connected".
    std::string subtitle =
        (app->settings.language == "fa" ? "پروکسی‌های متصل" :
         app->settings.language == "ru" ? "ПРОКСИ ПОДКЛЮЧЕНО" : "CONNECTED PROXIES") +
        std::string(" \xC2\xB7 ") + std::to_string(aliveCount); // \xC2\xB7 = UTF-8 middle dot "·"

    app->currentBarH = Theme::TopBarHeight;
    double barH = DrawTopBar(cr, app, width, L(app, Loc::Key::AppName), false, true, subtitle);
    // Clip all scrollable content to the area below the fixed top bar, so
    // nothing (banner, cards, badges...) can visually bleed up into the top
    // bar once the user has scrolled — the per-card clips further below
    // only ever covered the proxy cards themselves, not the banner/
    // disclaimer/offline-notice blocks above them.
    cairo_save(cr);
    cairo_rectangle(cr, 0, barH, width, height - barH);
    cairo_clip(cr);

    double y = barH + kPad - app->scrollY;

    // Promo banner (contain-fit, aspect-ratio sized — PORT_SPEC §5.1)
    if (app->settings.bannerEnabled && app->banner.loaded && !app->banner.pixbufs.empty()) {
        GdkPixbuf* pix = app->banner.pixbufs[app->banner.current % app->banner.pixbufs.size()];
        if (pix) {
            int iw = gdk_pixbuf_get_width(pix), ih = gdk_pixbuf_get_height(pix);
            double bw = width - kPad * 2;
            double bh = bw * ((double)ih / (double)iw);
            bh = std::min(bh, 160.0);
            if (y + bh > barH && y < height) {
                cairo_save(cr);
                RoundedRect(cr, kPad, y, bw, bh, Theme::CornerRadiusLarge);
                cairo_clip(cr);
                double scale = bw / iw;
                cairo_translate(cr, kPad, y);
                cairo_scale(cr, scale, scale);
                gdk_cairo_set_source_pixbuf(cr, pix, 0, 0);
                cairo_paint(cr);
                cairo_restore(cr);
                AddContentHit(app, kPad, y, bw, bh, "banner:open");
            }
            y += bh + kPad;
        }
    }

    // Disclaimer
    {
        TextStyle ds; ds.size = 11.5; ds.color = pal.onSurfaceVariant;
        ds.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
        double h = TextHeight(cr, L(app, Loc::Key::Disclaimer), width - kPad * 2, ds);
        if (y + h > barH && y < height) DrawText(cr, app, L(app, Loc::Key::Disclaimer), kPad, y, width - kPad * 2, ds);
        y += h + kPad;
    }

    // Offline-cache warning banner
    if (app->isOffline && !app->offlineNoticeDismissed) {
        double bh = 50;
        if (y + bh > barH && y < height) {
            RoundedRect(cr, kPad, y, width - kPad * 2, bh, Theme::CornerRadiusLarge);
            SetColor(cr, pal.errorContainer);
            cairo_fill(cr);
            Icon::WarningTriangle(cr, kPad + 24, y + bh / 2, 18, pal.error);
            TextStyle ws; ws.size = 11; ws.color = pal.onSurface;
            ws.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
            DrawText(cr, app, L(app, Loc::Key::OfflineNotice), kPad + 44, y + 8, width - kPad * 2 - 60, ws);
        }
        y += bh + kPad;
    }

    // Proxy list (dead/unreachable proxies hidden entirely — PORT_SPEC §5.1)
    {
        std::vector<ProxyItem> visible;
        {
            std::lock_guard<std::mutex> lk(app->proxiesMutex);
            for (size_t i = 0; i < app->proxies.size(); ++i) {
                const auto& p = app->proxies[i];
                if (p.isScanned && !p.IsAlive()) continue; // hidden, not badge-marked offline
                visible.push_back(p);
            }
        }
        if (visible.empty() && !app->isScanning) {
            TextStyle es; es.size = 13; es.color = pal.onSurfaceVariant; es.align = PANGO_ALIGN_CENTER;
            DrawText(cr, app, app->loadError ? L(app, Loc::Key::ErrorLoadTitle) : L(app, Loc::Key::NoProxiesFound),
                     kPad, y + 20, width - kPad * 2, es);
            y += 60;
        } else {
            std::lock_guard<std::mutex> lk(app->proxiesMutex);
            for (size_t i = 0; i < app->proxies.size(); ++i) {
                const auto& p = app->proxies[i];
                if (p.isScanned && !p.IsAlive()) continue;
                double estH = 150;
                if (y + estH > barH && y < height) {
                    cairo_save(cr);
                    cairo_rectangle(cr, 0, barH, width, height - barH);
                    cairo_clip(cr);
                    estH = DrawProxyCard(cr, app, kPad, y, width - kPad * 2, p, (int)i);
                    cairo_restore(cr);
                }
                y += estH + Theme::SpacingSmall;
            }
        }
    }

    y += Theme::BottomBarHeight + 24; // bottom padding (§8) + room for scan button bar
    cairo_restore(cr); // matches the clip pushed right after DrawTopBar above
    return y + app->scrollY - barH; // content height below top bar
}

// Bottom "Scan Proxies" bar, drawn last so it stays fixed at the bottom of
// the window (fixed overlay, not part of the scrollable content).
static void DrawHomeBottomBar(cairo_t* cr, AppState* app, double width, double height) {
    const auto& pal = Pal(app);
    double barH = Theme::BottomBarHeight;
    double y = height - barH;
    SetColor(cr, pal.surface);
    cairo_rectangle(cr, 0, y, width, barH);
    cairo_fill(cr);
    SetColor(cr, pal.tertiary, 0.5);
    cairo_rectangle(cr, 0, y, width, 1);
    cairo_fill(cr);

    double btnX = kPad, btnY = y + 14, btnW = width - kPad * 2, btnH = barH - 28;
    RoundedRect(cr, btnX, btnY, btnW, btnH, Theme::CornerRadiusPill);
    SetColor(cr, pal.primary);
    cairo_fill(cr);
    std::string label = app->isScanning ? L(app, Loc::Key::Scanning) : L(app, Loc::Key::ScanProxies);
    // Treat the bolt icon + label as a single centered group (icon
    // immediately left of the text) instead of pinning the icon at a fixed
    // x while separately centering the text across the whole button — that
    // left the icon visibly detached from the (independently centered)
    // word whenever the button was wider than the icon+text pair.
    double iconSize = 20, gap = 10;
    double labelW = MeasureLabelWidth(cr, label, 14, true);
    double groupW = iconSize + gap + labelW;
    double groupX = btnX + (btnW - groupW) / 2;
    Icon::Bolt(cr, groupX + iconSize / 2, btnY + btnH / 2, iconSize, pal.onPrimary, false);
    DrawLabelCentered(cr, label, groupX + iconSize + gap, btnY + btnH / 2, labelW, 14, true, pal.onPrimary);
    AddContentHit(app, btnX, btnY, btnW, btnH, "action:scan");
}

// ---------------------------------------------------------------------------
// Screen: Saved
// ---------------------------------------------------------------------------
static double DrawSavedScreen(cairo_t* cr, AppState* app, double width, double height) {
    const auto& pal = Pal(app);
    app->currentBarH = Theme::TopBarHeight;
    double barH = DrawTopBar(cr, app, width, L(app, Loc::Key::SavedProxies), true, false);
    cairo_save(cr);
    cairo_rectangle(cr, 0, barH, width, height - barH);
    cairo_clip(cr);
    double y = barH + kPad - app->scrollY;

    std::vector<std::pair<size_t, ProxyItem>> favs;
    {
        std::lock_guard<std::mutex> lk(app->proxiesMutex);
        for (size_t i = 0; i < app->proxies.size(); ++i)
            if (app->proxies[i].isFavorite) favs.push_back({i, app->proxies[i]});
    }

    if (favs.empty()) {
        TextStyle es; es.size = 13; es.color = pal.onSurfaceVariant; es.align = PANGO_ALIGN_CENTER;
        DrawText(cr, app, L(app, Loc::Key::NoSavedProxies), kPad, y + 40, width - kPad * 2, es);
        TextStyle hs; hs.size = 11.5; hs.color = pal.onSurfaceVariant; hs.align = PANGO_ALIGN_CENTER;
        DrawText(cr, app, L(app, Loc::Key::TapStarHint), kPad, y + 70, width - kPad * 2, hs);
        y += 140;
    } else {
        for (auto& [idx, p] : favs) {
            double estH = 150;
            if (y + estH > barH && y < height) {
                cairo_save(cr);
                cairo_rectangle(cr, 0, barH, width, height - barH);
                cairo_clip(cr);
                estH = DrawProxyCard(cr, app, kPad, y, width - kPad * 2, p, (int)idx);
                cairo_restore(cr);
            }
            y += estH + Theme::SpacingSmall;
        }
        // Clear favorites button
        double btnH = 44;
        if (y + btnH > barH && y < height) {
            RoundedRect(cr, kPad, y, width - kPad * 2, btnH, Theme::CornerRadiusPill);
            SetColor(cr, pal.error, 0.12);
            cairo_fill(cr);
            DrawLabelCentered(cr, L(app, Loc::Key::ClearFavorites), kPad, y + btnH / 2, width - kPad * 2, 13, true, pal.error);
            AddContentHit(app, kPad, y, width - kPad * 2, btnH, "action:clearfav");
        }
        y += btnH + kPad;
    }
    y += 24;
    cairo_restore(cr);
    return y + app->scrollY - barH;
}

// ---------------------------------------------------------------------------
// Screen: Settings
// ---------------------------------------------------------------------------
static double DrawNavRow(cairo_t* cr, AppState* app, double x, double y, double w,
                          const std::string& text, Theme::Color tint,
                          std::function<void(cairo_t*, double, double)> iconFn,
                          const std::string& action) {
    bool rtl = IsRtl(app);
    double h = 56;
    RoundedRect(cr, x, y, w, h, Theme::CornerRadiusCard);
    SetColor(cr, tint, 0.14);
    cairo_fill(cr);
    Icon::SetStroke(cr, tint, 1.8);
    RoundedRect(cr, x, y, w, h, Theme::CornerRadiusCard);
    SetColor(cr, tint, 0.35);
    cairo_stroke(cr);
    // Icon and label are grouped together and moved to the same side as a
    // pair — the right side in Persian, the left in English — rather than
    // pinned to opposite edges of the row.
    double iconX = rtl ? x + w - 30 : x + 30;
    iconFn(cr, iconX, y + h/2);
    TextStyle ts; ts.size = 13.5; ts.bold = true; ts.color = tint;
    ts.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
    double tx = rtl ? x + 16 : x + 56;
    // Vertically center using the text's *actual* measured height instead
    // of a fixed "assume ~18px" guess — Persian glyphs render with
    // different line metrics than Latin ones at the same point size, so a
    // fixed offset left Persian labels visibly lower than their icon.
    double textH = TextHeight(cr, text, w - 72, ts);
    DrawText(cr, app, text, tx, y + (h - textH) / 2, w - 72, ts);
    AddContentHit(app, x, y, w, h, action);
    return h;
}

static double DrawSettingsScreen(cairo_t* cr, AppState* app, double width, double height) {
    const auto& pal = Pal(app);
    bool rtl = IsRtl(app);
    app->currentBarH = Theme::TopBarHeight;
    app->autoScanSliderW = 0; // reset each frame; only set back below if actually drawn/visible
    double barH = DrawTopBar(cr, app, width, L(app, Loc::Key::Settings), true, false);
    cairo_save(cr);
    cairo_rectangle(cr, 0, barH, width, height - barH);
    cairo_clip(cr);
    double y = barH + kPad - app->scrollY;
    double contentW = width - kPad * 2;

    // Three color-coded nav rows
    y += DrawNavRow(cr, app, kPad, y, contentW, L(app, Loc::Key::SavedProxies), Theme::Color(0x7C93E8),
        [&](cairo_t* c, double cx, double cy){ Icon::Bookmark(c, cx-6, cy-9, 18, Theme::Color(0x7C93E8), true); },
        "nav:saved") + Theme::SpacingSmall;
    y += DrawNavRow(cr, app, kPad, y, contentW, L(app, Loc::Key::ProxySpeedTest), Theme::Color(0x4DB6AC),
        [&](cairo_t* c, double cx, double cy){ Icon::Speedometer(c, cx, cy, 12, Theme::Color(0x4DB6AC)); },
        "nav:speedtest") + Theme::SpacingSmall;
    y += DrawNavRow(cr, app, kPad, y, contentW, L(app, Loc::Key::SupportTitle), Theme::Color(0xE879A6),
        [&](cairo_t* c, double cx, double cy){ Icon::Heart(c, cx, cy, 18, Theme::Color(0xE879A6)); },
        "nav:support") + kPad;

    // Appearance: three equal-width square cards
    {
        TextStyle hs; hs.size = 13; hs.bold = true; hs.color = pal.onSurfaceVariant;
        hs.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
        DrawText(cr, app, L(app, Loc::Key::Theme), kPad, y, contentW, hs);
        y += 26;
        double cardW = (contentW - Theme::SpacingSmall * 2) / 3, cardH = 76;
        struct { const char* mode; Loc::Key label; } modes[3] = {
            {"dark", Loc::Key::ThemeDark}, {"light", Loc::Key::ThemeLight}, {"system", Loc::Key::ThemeSystem}
        };
        for (int i = 0; i < 3; ++i) {
            double cx0 = kPad + i * (cardW + Theme::SpacingSmall);
            bool selected = app->settings.themeMode == modes[i].mode;
            RoundedRect(cr, cx0, y, cardW, cardH, Theme::CornerRadiusCard);
            SetColor(cr, selected ? pal.primary : pal.surfaceVariant, selected ? 0.16 : 1.0);
            cairo_fill(cr);
            if (selected) {
                Icon::SetStroke(cr, pal.primary, 2.0);
                RoundedRect(cr, cx0, y, cardW, cardH, Theme::CornerRadiusCard);
                cairo_stroke(cr);
            }
            double iconCx = cx0 + cardW/2, iconCy = y + 26;
            // Dark/Light cards both use a yellow icon (moon/sun); System
            // uses a half-black/half-yellow split circle, per product
            // direction — previously these all just used the neutral
            // onSurface/onSurfaceVariant text colors.
            Theme::Color themeYellow(0xFFC107);
            if (i == 0) Icon::Moon(cr, iconCx, iconCy, 14, themeYellow);
            else if (i == 1) Icon::Sun(cr, iconCx, iconCy, 14, themeYellow);
            else Icon::HalfDarkLight(cr, iconCx, iconCy, 14, Theme::Color(0x000000), themeYellow);
            TextStyle ls; ls.size = 11; ls.bold = selected; ls.color = pal.onSurface; ls.align = PANGO_ALIGN_CENTER;
            double lh = TextHeight(cr, L(app, modes[i].label), cardW, ls);
            DrawText(cr, app, L(app, modes[i].label), cx0, y + cardH - 14 - lh, cardW, ls);
            AddContentHit(app, cx0, y, cardW, cardH, std::string("action:theme:") + modes[i].mode);
        }
        y += cardH + kPad;
    }

    // Promo-banner toggle
    {
        double h = 50;
        RoundedRect(cr, kPad, y, contentW, h, Theme::CornerRadiusCard);
        SetColor(cr, pal.surfaceVariant);
        cairo_fill(cr);
        TextStyle ts; ts.size = 13; ts.color = pal.onSurface;
        ts.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
        // Proper RTL mirroring: label on the right, toggle on the left, in
        // Persian — the mirror image of the English layout.
        double tx = rtl ? kPad + 70 : kPad + 14;
        double textH = TextHeight(cr, L(app, Loc::Key::BannerSliderToggle), contentW - 90, ts);
        DrawText(cr, app, L(app, Loc::Key::BannerSliderToggle), tx, y + (h - textH) / 2, contentW - 90, ts);
        double swX = rtl ? kPad + 14 : kPad + contentW - 56;
        double swY = y + 13;
        RoundedRect(cr, swX, swY, 42, 24, 12);
        SetColor(cr, app->settings.bannerEnabled ? pal.primary : pal.tertiary);
        cairo_fill(cr);
        cairo_arc(cr, swX + (app->settings.bannerEnabled ? 30 : 12), swY + 12, 9, 0, 2*M_PI);
        SetColor(cr, pal.onPrimary);
        cairo_fill(cr);
        AddContentHit(app, kPad, y, contentW, h, "action:togglebanner");
        y += h + kPad;
    }

    // Language rows (full-width, radio + flag/heart + native name)
    {
        TextStyle hs; hs.size = 13; hs.bold = true; hs.color = pal.onSurfaceVariant;
        hs.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
        DrawText(cr, app, L(app, Loc::Key::Language), kPad, y, contentW, hs);
        y += 26;
        struct { const char* code; const char* native; } langs[3] = {
            {"fa", "فارسی"}, {"en", "English"}, {"ru", "Русский"}
        };
        for (int i = 0; i < 3; ++i) {
            double h = 52;
            bool selected = app->settings.language == langs[i].code;
            RoundedRect(cr, kPad, y, contentW, h, Theme::CornerRadiusCard);
            SetColor(cr, selected ? pal.primary : pal.surfaceVariant, selected ? 0.12 : 1.0);
            cairo_fill(cr);
            // Fixed left-to-right layout for every language row (radio,
            // flag, then label in that order) — Persian used to mirror this
            // whole row to the opposite edge, landing it in a different
            // "column" than the English/Russian rows below/above it.
            double radioX = kPad + 24;
            cairo_save(cr);
            Icon::SetStroke(cr, selected ? pal.primary : pal.onSurfaceVariant, 1.8);
            cairo_arc(cr, radioX, y + h/2, 8, 0, 2*M_PI);
            cairo_stroke(cr);
            if (selected) { SetColor(cr, pal.primary); cairo_arc(cr, radioX, y + h/2, 4, 0, 2*M_PI); cairo_fill(cr); }
            cairo_restore(cr);
            double flagX = kPad + 52;
            if (std::string(langs[i].code) == "fa") {
                Icon::Heart(cr, flagX, y + h/2, 16, Theme::Color(0xE879A6));
            } else if (std::string(langs[i].code) == "en") {
                Icon::FlagUS(cr, flagX - 11, y + h/2 - 8, 22, 16);
            } else {
                Icon::FlagRU(cr, flagX - 11, y + h/2 - 8, 22, 16);
            }
            TextStyle ns; ns.size = 13.5; ns.color = pal.onSurface;
            ns.align = PANGO_ALIGN_LEFT;
            double nsH = TextHeight(cr, langs[i].native, contentW - 100, ns);
            DrawText(cr, app, langs[i].native, kPad + 80, y + (h - nsH) / 2, contentW - 100, ns);
            AddContentHit(app, kPad, y, contentW, h, std::string("action:lang:") + langs[i].code);
            y += h + Theme::SpacingSmall;
        }
        y += kPad - Theme::SpacingSmall;
    }

    // Auto-scan section
    {
        double h = 50;
        RoundedRect(cr, kPad, y, contentW, h, Theme::CornerRadiusCard);
        SetColor(cr, pal.surfaceVariant);
        cairo_fill(cr);
        TextStyle ts; ts.size = 13; ts.color = pal.onSurface;
        ts.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
        // Proper RTL mirroring: label on the right, toggle on the left, in
        // Persian.
        double tx = rtl ? kPad + 70 : kPad + 14;
        double textH0 = TextHeight(cr, L(app, Loc::Key::AutoScanEnableToggle), contentW - 90, ts);
        DrawText(cr, app, L(app, Loc::Key::AutoScanEnableToggle), tx, y + (h - textH0) / 2, contentW - 90, ts);
        double swX = rtl ? kPad + 14 : kPad + contentW - 56;
        double swY = y + 13;
        RoundedRect(cr, swX, swY, 42, 24, 12);
        SetColor(cr, app->settings.autoScanEnabled ? pal.primary : pal.tertiary);
        cairo_fill(cr);
        cairo_arc(cr, swX + (app->settings.autoScanEnabled ? 30 : 12), swY + 12, 9, 0, 2*M_PI);
        SetColor(cr, pal.onPrimary);
        cairo_fill(cr);
        AddContentHit(app, kPad, y, contentW, h, "action:toggleautoscan");
        y += h + Theme::SpacingSmall;

        if (app->settings.autoScanEnabled) {
            double h2 = 66;
            RoundedRect(cr, kPad, y, contentW, h2, Theme::CornerRadiusCard);
            SetColor(cr, pal.surfaceVariant);
            cairo_fill(cr);

            char buf[64];
            std::string tmpl = L(app, Loc::Key::AutoScanIntervalText);
            snprintf(buf, sizeof(buf), tmpl.c_str(), app->settings.autoScanIntervalS);
            TextStyle its; its.size = 12; its.bold = true; its.color = pal.onSurface;
            its.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
            DrawText(cr, app, buf, kPad + 14, y + 10, contentW - 28, its);

            // Draggable slider — 5s to 240s (4 minutes), per the "up to a
            // max of 4 minutes" requirement. Track bounds are recorded into
            // AppState so the drag handlers can map pointer position back
            // to a value. The track itself stays left-to-right (min..max)
            // in both languages — only the label above it mirrors.
            double trackX = kPad + 14, trackW = contentW - 28, trackY = y + 42;
            app->autoScanSliderX = trackX;
            app->autoScanSliderW = trackW;
            app->autoScanSliderY = trackY;
            constexpr double kMinInterval = 5.0, kMaxInterval = 240.0;
            double frac = (app->settings.autoScanIntervalS - kMinInterval) / (kMaxInterval - kMinInterval);
            frac = std::clamp(frac, 0.0, 1.0);
            RoundedRect(cr, trackX, trackY - 3, trackW, 6, 3);
            SetColor(cr, pal.tertiary);
            cairo_fill(cr);
            RoundedRect(cr, trackX, trackY - 3, trackW * frac, 6, 3);
            SetColor(cr, pal.primary);
            cairo_fill(cr);
            SetColor(cr, pal.primary);
            cairo_arc(cr, trackX + trackW * frac, trackY, 9, 0, 2 * M_PI);
            cairo_fill(cr);
            SetColor(cr, pal.onPrimary);
            cairo_arc(cr, trackX + trackW * frac, trackY, 4, 0, 2 * M_PI);
            cairo_fill(cr);
            AddContentHit(app, trackX - 12, trackY - 14, trackW + 24, 28, "slider:autoscan");
            y += h2 + Theme::SpacingSmall;
        }
        y += kPad;
    }

    // Danger-zone Clear favorites button
    {
        double h = 46;
        RoundedRect(cr, kPad, y, contentW, h, Theme::CornerRadiusPill);
        SetColor(cr, pal.error, 0.12);
        cairo_fill(cr);
        DrawLabelCentered(cr, L(app, Loc::Key::ClearFavorites), kPad, y + h / 2, contentW, 13, true, pal.error);
        AddContentHit(app, kPad, y, contentW, h, "action:clearfav");
        y += h + kPad;
    }

    y += 24;
    cairo_restore(cr);
    return y + app->scrollY - barH;
}

// ---------------------------------------------------------------------------
// Screen: Support
// ---------------------------------------------------------------------------
static double DrawSupportScreen(cairo_t* cr, AppState* app, double width, double height) {
    const auto& pal = Pal(app);
    bool rtl = IsRtl(app);
    app->currentBarH = Theme::TopBarHeight;
    double barH = DrawTopBar(cr, app, width, L(app, Loc::Key::SupportTitle), true, false);
    cairo_save(cr);
    cairo_rectangle(cr, 0, barH, width, height - barH);
    cairo_clip(cr);
    double y = barH + kPad - app->scrollY;
    double contentW = width - kPad * 2;
    PangoAlignment al = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;

    {
        TextStyle ts; ts.size = 13; ts.color = pal.onSurfaceVariant; ts.align = al;
        double h = TextHeight(cr, L(app, Loc::Key::SupportBanner), contentW, ts);
        DrawText(cr, app, L(app, Loc::Key::SupportBanner), kPad, y, contentW, ts);
        y += h + kPad;
    }

    {
        TextStyle hs; hs.size = 14; hs.bold = true; hs.color = pal.onSurface; hs.align = al;
        DrawText(cr, app, L(app, Loc::Key::CryptoHeader), kPad, y, contentW, hs);
        y += 28;
    }

    struct { const char* label; const char* addr; } wallets[3] = {
        {"USDT (Polygon)", "0x3f2A9c7E1B4d8F6a0C5e2D9b7A1c4E8f3B6d9A2c"},
        {"BTC / ETH", "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh"},
        {"TRX (TRON)", "TXn9sV2mK8pQ4rL7wY1zC6dB3fH5jG0aE9"},
    };
    for (int i = 0; i < 3; ++i) {
        double h = 64;
        RoundedRect(cr, kPad, y, contentW, h, Theme::CornerRadiusCard);
        SetColor(cr, pal.surfaceVariant);
        cairo_fill(cr);
        TextStyle ls; ls.size = 11.5; ls.bold = true; ls.color = pal.onSurfaceVariant; ls.align = al;
        std::string label = (i == 2) ? L(app, Loc::Key::TrxRecommended) : wallets[i].label;
        DrawText(cr, app, label, kPad + 12, y + 8, contentW - 60, ls);
        TextStyle as; as.size = 10.5; as.mono = true; as.color = pal.onSurface; as.align = al;
        DrawText(cr, app, wallets[i].addr, kPad + 12, y + 28, contentW - 60, as);
        // copy icon
        double cix = rtl ? kPad + 20 : kPad + contentW - 36;
        Icon::SetStroke(cr, pal.onSurfaceVariant, 1.6);
        cairo_rectangle(cr, cix, y + 20, 14, 16);
        cairo_stroke(cr);
        cairo_rectangle(cr, cix + 5, y + 15, 14, 16);
        cairo_stroke(cr);
        AddContentHit(app, kPad, y, contentW, h, "action:copywallet:" + std::to_string(i));
        y += h + Theme::SpacingSmall;
    }
    y += kPad - Theme::SpacingSmall;

    {
        double h = 54;
        RoundedRect(cr, kPad, y, contentW, h, Theme::CornerRadiusLarge);
        SetColor(cr, pal.errorContainer);
        cairo_fill(cr);
        Icon::WarningTriangle(cr, kPad + 24, y + h/2, 16, pal.error);
        TextStyle ws; ws.size = 11; ws.color = pal.onSurface; ws.align = al;
        DrawText(cr, app, L(app, Loc::Key::NetworkWarning), kPad + 42, y + 10, contentW - 56, ws);
        y += h + kPad;
    }

    // GitHub / Telegram link rows — icon and label are grouped together
    // and both moved to the right side in Persian (traditional RTL), not
    // split across opposite edges of the row.
    {
        double h = 54;
        double iconCx = rtl ? kPad + contentW - 28 : kPad + 28;
        double tx = rtl ? kPad : kPad + 52;
        double tw = contentW - 60;

        RoundedRect(cr, kPad, y, contentW, h, Theme::CornerRadiusCard);
        SetColor(cr, pal.surfaceVariant);
        cairo_fill(cr);
        TextStyle ts; ts.size = 13; ts.bold = true; ts.color = pal.onSurface; ts.align = al;
        Icon::GitHubLogo(cr, iconCx, y + h / 2, 24, pal.onSurface);
        double th1 = TextHeight(cr, L(app, Loc::Key::GitHub), tw, ts);
        DrawText(cr, app, L(app, Loc::Key::GitHub), tx, y + (h - th1) / 2, tw, ts);
        AddContentHit(app, kPad, y, contentW, h, "action:opengithub");
        y += h + Theme::SpacingSmall;

        RoundedRect(cr, kPad, y, contentW, h, Theme::CornerRadiusCard);
        SetColor(cr, pal.surfaceVariant);
        cairo_fill(cr);
        Icon::TelegramLogo(cr, iconCx, y + h / 2, 24, Theme::Color(0x29A9EA));
        double th2 = TextHeight(cr, L(app, Loc::Key::TelegramChannel), tw, ts);
        DrawText(cr, app, L(app, Loc::Key::TelegramChannel), tx, y + (h - th2) / 2, tw, ts);
        AddContentHit(app, kPad, y, contentW, h, "action:opentelegram");
        y += h + kPad;
    }

    {
        TextStyle ts; ts.size = 12.5; ts.color = pal.onSurfaceVariant; ts.align = PANGO_ALIGN_CENTER;
        DrawText(cr, app, L(app, Loc::Key::ThankYou), kPad, y, contentW, ts);
        y += 30;
    }

    y += 24;
    cairo_restore(cr);
    return y + app->scrollY - barH;
}

// ---------------------------------------------------------------------------
// Screen: Speed Test
// ---------------------------------------------------------------------------
// Stat cell used in the Speed Test result grid: a small tinted icon badge
// top-leading, a bold value, and a caption label underneath — consistent
// spacing/rhythm with the rest of the app's cards instead of icons crammed
// into the corner.
static double DrawStatCell(cairo_t* cr, AppState* app, double x, double y, double w, double h,
                            const std::string& label, const std::string& value, Theme::Color accent,
                            std::function<void(cairo_t*, double, double, double)> drawIcon) {
    const auto& pal = Pal(app);
    RoundedRect(cr, x, y, w, h, Theme::CornerRadiusCard);
    SetColor(cr, pal.surfaceVariant);
    cairo_fill(cr);

    // Icon badge + label sit side by side on the same row (the label reads
    // as this stat's name, e.g. "Stability"/"پایداری") — previously the
    // label was drawn as a caption underneath the big value instead, which
    // separated it from the icon it's supposed to caption.
    double badgeR = 14;
    double badgeCx = x + 16 + badgeR;
    double badgeCy = y + 16 + badgeR;
    cairo_save(cr);
    SetColor(cr, accent, 0.14);
    cairo_arc(cr, badgeCx, badgeCy, badgeR, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_restore(cr);
    drawIcon(cr, badgeCx, badgeCy, 14);

    double labelX = badgeCx + badgeR + 8;
    double labelW = x + w - 12 - labelX;
    TextStyle ls; ls.size = 10.5; ls.bold = true; ls.color = pal.onSurfaceVariant;
    ls.align = PANGO_ALIGN_LEFT;
    double labelH = 0;
    DrawText(cr, app, label, labelX, badgeCy - 7, labelW, ls, &labelH);

    double textX = x + 16;
    double textW = w - 32;
    TextStyle vs; vs.size = 17; vs.bold = true; vs.color = pal.onSurface;
    vs.align = PANGO_ALIGN_LEFT;
    DrawText(cr, app, value, textX, y + h - 32, textW, vs);
    return h;
}

static double DrawSpeedTestScreen(cairo_t* cr, AppState* app, double width, double height) {
    const auto& pal = Pal(app);
    bool rtl = IsRtl(app);
    app->currentBarH = Theme::TopBarHeight;
    double barH = DrawTopBar(cr, app, width, L(app, Loc::Key::ProxySpeedTest), true, false);
    cairo_save(cr);
    cairo_rectangle(cr, 0, barH, width, height - barH);
    cairo_clip(cr);
    double y = barH + kPad - app->scrollY;
    double contentW = width - kPad * 2;

    {
        TextStyle ds; ds.size = 12; ds.color = pal.onSurfaceVariant;
        ds.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
        double h = TextHeight(cr, L(app, Loc::Key::ProxySpeedTestDesc), contentW, ds);
        DrawText(cr, app, L(app, Loc::Key::ProxySpeedTestDesc), kPad, y, contentW, ds);
        y += h + kPad;
    }

    // Input field with clear button — fully hand-drawn (text + blinking
    // caret), no overlaid GTK widget. An overlaid GtkText previously caused
    // a stray diagonal-line rendering artifact on some systems, and its
    // off-screen hit-testing region may also have been silently swallowing
    // clicks meant for the fixed-position back button on other screens.
    double inputH = 50;
    RoundedRect(cr, kPad, y, contentW, inputH, Theme::CornerRadiusPill);
    SetColor(cr, pal.surfaceVariant);
    cairo_fill(cr);
    if (app->speedInputFocused) {
        Icon::SetStroke(cr, pal.primary, 1.6);
        RoundedRect(cr, kPad, y, contentW, inputH, Theme::CornerRadiusPill);
        cairo_stroke(cr);
    }
    app->speedInputRectX = kPad + 16;
    app->speedInputRectY = y;
    app->speedInputRectW = contentW - 32 - (app->speedTest.inputText.empty() ? 0 : 28);
    app->speedInputRectH = inputH;
    {
        bool hasText = !app->speedTest.inputText.empty();
        std::string shown = hasText ? app->speedTest.inputText : L(app, Loc::Key::ProxyInputHint);
        TextStyle is; is.size = 13; is.mono = hasText;
        is.color = hasText ? pal.onSurface : pal.onSurfaceVariant;
        is.align = PANGO_ALIGN_LEFT;
        // Text is intentionally NOT ellipsized: for a long pasted link we'd
        // rather it just extend past the visible pill edge (still fully
        // present in app->speedTest.inputText, still editable) than hide
        // the part the user most likely wants to check (the secret).
        PangoLayout* layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, shown.c_str(), -1);
        PangoFontDescription* desc = pango_font_description_from_string(is.mono ? Theme::MonoFontFamily : Theme::FontFamily);
        pango_font_description_set_size(desc, (int)(is.size * PANGO_SCALE));
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        pango_layout_set_single_paragraph_mode(layout, TRUE);
        int textW = 0, textH = 0;
        pango_layout_get_pixel_size(layout, &textW, &textH);
        cairo_save(cr);
        RoundedRect(cr, kPad, y, contentW, inputH, Theme::CornerRadiusPill);
        cairo_clip(cr);
        SetColor(cr, is.color);
        cairo_move_to(cr, app->speedInputRectX, y + (inputH - textH) / 2);
        pango_cairo_show_layout(cr, layout);
        if (app->speedInputFocused && hasText && app->caretVisible) {
            double caretX = app->speedInputRectX + textW + 2;
            SetColor(cr, pal.onSurface);
            cairo_rectangle(cr, caretX, y + (inputH - textH) / 2, 1.5, textH);
            cairo_fill(cr);
        } else if (app->speedInputFocused && !hasText && app->caretVisible) {
            SetColor(cr, pal.onSurface);
            cairo_rectangle(cr, app->speedInputRectX, y + (inputH - 16) / 2, 1.5, 16);
            cairo_fill(cr);
        }
        cairo_restore(cr);
        g_object_unref(layout);
    }
    if (!app->speedTest.inputText.empty()) {
        double cx = kPad + contentW - 30;
        Icon::SetStroke(cr, pal.onSurfaceVariant, 1.8);
        cairo_move_to(cr, cx - 6, y + inputH/2 - 6); cairo_line_to(cr, cx + 6, y + inputH/2 + 6);
        cairo_move_to(cr, cx + 6, y + inputH/2 - 6); cairo_line_to(cr, cx - 6, y + inputH/2 + 6);
        cairo_stroke(cr);
        AddContentHit(app, cx - 12, y + inputH/2 - 12, 24, 24, "action:clearinput");
    }
    AddContentHit(app, kPad, y, contentW - 40, inputH, "action:focusinput");
    y += inputH + kPad;

    // Paste + Start buttons side by side (PORT_SPEC §5.3: not stacked)
    {
        double pasteW = 90, gap = 10;
        double startW = contentW - pasteW - gap;
        double btnH = 48;
        double startX = rtl ? kPad + pasteW + gap : kPad;
        double pasteX = rtl ? kPad : kPad + startW + gap;

        RoundedRect(cr, pasteX, y, pasteW, btnH, Theme::CornerRadiusPill);
        SetColor(cr, pal.tertiary);
        cairo_fill(cr);
        DrawLabelCentered(cr, L(app, Loc::Key::Paste), pasteX, y + btnH / 2, pasteW, 12.5, true, pal.onSurface);
        AddContentHit(app, pasteX, y, pasteW, btnH, "action:paste");

        RoundedRect(cr, startX, y, startW, btnH, Theme::CornerRadiusPill);
        SetColor(cr, pal.primary);
        cairo_fill(cr);
        // Label stays "Start Speed Test" always (never relabels — PORT_SPEC §5.3)
        DrawLabelCentered(cr, app->speedTest.running ? L(app, Loc::Key::Testing) : L(app, Loc::Key::StartTest),
                           startX, y + btnH / 2, startW, 13, true, pal.onPrimary);
        AddContentHit(app, startX, y, startW, btnH, "action:starttest");

        y += btnH + kPad;
    }

    if (app->speedTest.hasResult) {
        const auto& r = app->speedTest.result;

        // Circular ping gauge
        {
            double cx = width / 2, cy = y + 70, rad = 60;
            Theme::Color ringColor = QualityColor(r.quality);

            // Background track (full circle).
            cairo_save(cr);
            cairo_new_path(cr);
            SetColor(cr, pal.surfaceVariant);
            cairo_set_line_width(cr, 10);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
            cairo_arc(cr, cx, cy, rad, 0, 2 * M_PI);
            cairo_stroke(cr);
            cairo_new_path(cr);
            cairo_restore(cr);

            // Progress arc, drawn in its own isolated save/restore with an
            // explicit new_path before AND after — defensive against a
            // rendering-backend quirk (seen on some software/GL drivers)
            // where a round-capped partial-arc stroke can leave a stray
            // straight line connecting the arc's start/end points instead
            // of just the curve itself. A plain butt cap on a path that's
            // freshly cleared both before and after sidesteps that class of
            // bug entirely, at the cost of slightly squared-off arc ends
            // instead of rounded ones.
            double frac = r.avgMs < 0 ? 0.0 : std::min(1.0, r.avgMs / 500.0);
            double sweep = 2 * M_PI * (1.0 - frac);
            if (sweep > 0.001) {
                cairo_save(cr);
                cairo_new_path(cr);
                SetColor(cr, ringColor);
                cairo_set_line_width(cr, 10);
                cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
                cairo_arc(cr, cx, cy, rad, -M_PI/2, -M_PI/2 + sweep);
                cairo_stroke(cr);
                cairo_new_path(cr);
                cairo_restore(cr);
            }

            TextStyle big; big.size = 30; big.bold = true; big.color = pal.onSurface; big.align = PANGO_ALIGN_CENTER;
            std::string msTxt = r.avgMs >= 0 ? std::to_string(r.avgMs) : "--";
            double numH = 0;
            // Measure first (without drawing) so the number+unit block can be
            // vertically centered as a whole — a fixed y-offset guess here
            // previously put "ms" close enough to overlap the number for
            // some font metrics.
            numH = TextHeight(cr, msTxt, rad * 2, big);
            double unitH = TextHeight(cr, "ms", rad * 2, TextStyle{});
            double gap = 2;
            double blockH = numH + gap + unitH;
            double blockTop = cy - blockH / 2;
            DrawText(cr, app, msTxt, cx - rad, blockTop, rad * 2, big);
            TextStyle small; small.size = 11; small.color = pal.onSurfaceVariant; small.align = PANGO_ALIGN_CENTER;
            DrawText(cr, app, "ms", cx - rad, blockTop + numH + gap, rad * 2, small);
            y = cy + rad + 16;
        }

        // Quality pill, centered
        {
            std::string qtext;
            QualityKey(r.quality, app, qtext);
            std::string full = L(app, Loc::Key::Quality) + std::string(": ") + qtext;
            double pw = MeasureLabelWidth(cr, full, 12, true) + 34;
            double px = (width - pw) / 2;
            RoundedRect(cr, px, y, pw, 28, 14);
            SetColor(cr, QualityColor(r.quality), 0.18);
            cairo_fill(cr);
            SetColor(cr, QualityColor(r.quality));
            cairo_arc(cr, px + 14, y + 14, 4, 0, 2 * M_PI);
            cairo_fill(cr);
            DrawLabelCentered(cr, full, px + 18, y + 14, pw - 18, 12, true, QualityColor(r.quality));
            y += 28 + kPad;
        }

        // 2x3 stat grid — exact icon set per PORT_SPEC §5.3
        {
            double cellW = (contentW - Theme::SpacingSmall) / 2, cellH = 90;
            char buf[64];
            auto cell = [&](int col, int row, const std::string& label, const std::string& value,
                            Theme::Color accent, auto iconFn) {
                int visualCol = rtl ? 1 - col : col;
                double cx0 = kPad + visualCol * (cellW + Theme::SpacingSmall);
                double cy0 = y + row * (cellH + Theme::SpacingSmall);
                DrawStatCell(cr, app, cx0, cy0, cellW, cellH, label, value, accent, iconFn);
            };
            snprintf(buf, sizeof(buf), "%.0f%%", 100.0 - r.packetLossPct);
            cell(0, 0, L(app, Loc::Key::Stability), buf, pal.success,
                 [&](cairo_t* c, double cx, double cy, double s){ Icon::CheckCircle(c, cx, cy, s * 0.6, pal.success); });
            snprintf(buf, sizeof(buf), "%d ms", r.avgMs);
            cell(1, 0, L(app, Loc::Key::AvgPing), buf, pal.primary,
                 [&](cairo_t* c, double cx, double cy, double s){ Icon::SignalBars(c, cx - s * 0.5, cy - s * 0.4, s * 0.9, pal.primary); });
            snprintf(buf, sizeof(buf), "%.1f Mbps", r.uploadMbps);
            cell(0, 1, L(app, Loc::Key::UploadSpeed), buf, pal.primary,
                 [&](cairo_t* c, double cx, double cy, double s){ Icon::Bolt(c, cx, cy, s * 0.9, pal.primary, false); });
            snprintf(buf, sizeof(buf), "%.1f Mbps", r.downloadMbps);
            cell(1, 1, L(app, Loc::Key::DownloadSpeed), buf, pal.success,
                 [&](cairo_t* c, double cx, double cy, double s){ Icon::Bolt(c, cx, cy, s * 0.9, pal.success, true); });
            snprintf(buf, sizeof(buf), "%.0f%%", r.packetLossPct);
            cell(0, 2, L(app, Loc::Key::PacketLoss), buf, pal.error,
                 [&](cairo_t* c, double cx, double cy, double s){ Icon::RefreshArrow(c, cx, cy, s * 0.5, pal.error); });
            // "0 ms" (value-then-unit, matching every other cell) — an
            // earlier revision accidentally emitted "ms 0" here.
            snprintf(buf, sizeof(buf), "%.0f ms", r.jitterMs);
            cell(1, 2, L(app, Loc::Key::Jitter), buf, pal.onSurfaceVariant,
                 [&](cairo_t* c, double cx, double cy, double s){ Icon::Zigzag(c, cx - s * 0.5, cy - s * 0.4, s * 0.9, pal.onSurfaceVariant); });
            y += 3 * (cellH + Theme::SpacingSmall) + kPad;
        }

        // Real Telegram download speed card — height is computed from the
        // actual (possibly 2-line) title instead of assuming it always fits
        // on one line, which used to make the number/disclaimer collide
        // with a wrapped title.
        {
            double pad2 = 16;
            double iconColW = 44;
            double badgeW0 = MeasureLabelWidth(cr, L(app, Loc::Key::EstimatedBadge), 9.5, true) + 20;
            double titleW = contentW - pad2 * 2 - iconColW - badgeW0 - 8;
            TextStyle ts; ts.size = 12.5; ts.bold = true; ts.color = pal.onSurface;
            ts.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
            double titleH = TextHeight(cr, L(app, Loc::Key::RealTelegramSpeedTitle), titleW, ts);

            double numH = 30, discH = 32;
            double h = pad2 + titleH + 10 + numH + 6 + discH + pad2;

            RoundedRect(cr, kPad, y, contentW, h, Theme::CornerRadiusLarge);
            SetColor(cr, pal.surfaceVariant);
            cairo_fill(cr);

            double iconCx = rtl ? kPad + contentW - pad2 - iconColW / 2 : kPad + pad2 + iconColW / 2;
            Icon::CloudDown(cr, iconCx, y + pad2 + titleH / 2, 26, pal.primary);

            double titleX = rtl ? kPad + contentW - pad2 - iconColW - titleW : kPad + pad2 + iconColW;
            DrawText(cr, app, L(app, Loc::Key::RealTelegramSpeedTitle), titleX, y + pad2, titleW, ts);

            std::string badge = L(app, Loc::Key::EstimatedBadge);
            double badgeX = rtl ? kPad + pad2 : kPad + contentW - pad2 - badgeW0;
            RoundedRect(cr, badgeX, y + pad2, badgeW0, 20, 10);
            SetColor(cr, pal.primary, 0.16);
            cairo_fill(cr);
            DrawLabelCentered(cr, badge, badgeX, y + pad2 + 10, badgeW0, 9.5, true, pal.primary);

            // Number first, unit right after it (in reading order) instead
            // of the unit label preceding the number.
            double mbps = SpeedTester::EstimateTelegramMBps(r);
            char mbBuf[32]; snprintf(mbBuf, sizeof(mbBuf), "%.2f", mbps);
            double numY = y + pad2 + titleH + 10;
            double numX = rtl ? kPad + contentW - pad2 - iconColW : kPad + pad2 + iconColW;
            double numW = MeasureLabelWidth(cr, mbBuf, 22, true);
            TextStyle num; num.size = 22; num.bold = true; num.color = pal.primary;
            num.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
            double numDrawX = rtl ? numX - numW : numX;
            DrawText(cr, app, mbBuf, numDrawX, numY, numW + 4, num);
            TextStyle unit; unit.size = 13; unit.bold = true; unit.color = pal.primary;
            unit.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
            double unitX = rtl ? numDrawX - 46 : numDrawX + numW + 6;
            DrawText(cr, app, "MB/s", unitX, numY + 6, 46, unit);

            TextStyle disc; disc.size = 9.5; disc.color = pal.onSurfaceVariant;
            disc.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
            double discX = rtl ? kPad + contentW - pad2 - iconColW - (contentW - pad2 * 2 - iconColW) : kPad + pad2 + iconColW;
            DrawText(cr, app, L(app, Loc::Key::TelegramSpeedDisclaimer), discX, numY + numH, contentW - pad2 * 2 - iconColW, disc);
            y += h + kPad;
        }

        // Download time estimator
        {
            TextStyle ts; ts.size = 13; ts.bold = true; ts.color = pal.onSurface;
            ts.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
            DrawText(cr, app, L(app, Loc::Key::FileDownloadEstimator), kPad, y, contentW, ts);
            y += 26;
            double chips[5] = {1000, 500, 100, 50, 10};
            double gap = 6;
            double chipW = (contentW - 4 * gap) / 5, chipH = 32;
            for (int i = 0; i < 5; ++i) {
                int visualI = rtl ? 4 - i : i;
                double cx0 = kPad + visualI * (chipW + gap);
                bool active = std::abs(app->speedTest.fileSizeMb - chips[i]) < 0.01;
                RoundedRect(cr, cx0, y, chipW, chipH, 16);
                SetColor(cr, active ? pal.primary : pal.surfaceVariant);
                cairo_fill(cr);
                char cb[16]; snprintf(cb, sizeof(cb), "%d", (int)chips[i]);
                DrawLabelCentered(cr, cb, cx0, y + chipH / 2, chipW, 11, true, active ? pal.onPrimary : pal.onSurfaceVariant);
                AddContentHit(app, cx0, y, chipW, chipH, "action:chip:" + std::to_string((int)chips[i]));
            }
            y += chipH + 12;
            double secs = SpeedTester::EstimateDownloadSeconds(r, app->speedTest.fileSizeMb);
            char resBuf[64];
            if (secs >= 0) snprintf(resBuf, sizeof(resBuf), "%.1f s", secs); else snprintf(resBuf, sizeof(resBuf), "--");
            TextStyle rs; rs.size = 13; rs.color = pal.onSurface;
            rs.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
            DrawText(cr, app, L(app, Loc::Key::EstimatedTimeResult) + std::string(": ") + resBuf, kPad, y, contentW, rs);
            y += 30 + kPad;
        }

        // Connection info block (only once DNS resolved)
        if (r.dnsLookupMs >= 0) {
            double pad2 = 16;
            double h = 96;
            RoundedRect(cr, kPad, y, contentW, h, Theme::CornerRadiusLarge);
            SetColor(cr, pal.surfaceVariant);
            cairo_fill(cr);
            TextStyle ts; ts.size = 12.5; ts.bold = true; ts.color = pal.onSurface;
            ts.align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
            DrawText(cr, app, L(app, Loc::Key::ConnectionInfo), kPad + pad2, y + pad2 - 4, contentW - pad2 * 2, ts);
            TextStyle ms; ms.size = 11.5; ms.mono = true; ms.color = pal.onSurfaceVariant;
            ms.align = PANGO_ALIGN_LEFT; // host/IP/timing values stay LTR even in RTL UI
            std::string line1 = app->speedTest.resolvedHost + ":" + app->speedTest.resolvedPort;
            std::string line2 = "IP: " + (r.resolvedIp.empty() ? std::string("-") : r.resolvedIp);
            char dnsBuf[48]; snprintf(dnsBuf, sizeof(dnsBuf), "%s: %dms", L(app, Loc::Key::DnsLookupLabel).c_str(), r.dnsLookupMs);
            double lineX = kPad + pad2;
            DrawText(cr, app, line1, lineX, y + pad2 + 20, contentW - pad2 * 2, ms);
            DrawText(cr, app, line2, lineX, y + pad2 + 38, contentW - pad2 * 2, ms);
            DrawText(cr, app, dnsBuf, lineX, y + pad2 + 56, contentW - pad2 * 2, ms);
            y += h + kPad;
        }

        // Bottom actions: Connect (full width), then Copy/Save side by side
        {
            double btnH = 46;
            RoundedRect(cr, kPad, y, contentW, btnH, Theme::CornerRadiusPill);
            SetColor(cr, pal.primary);
            cairo_fill(cr);
            DrawLabelCentered(cr, L(app, Loc::Key::Connect), kPad, y + btnH / 2, contentW, 13, true, pal.onPrimary);
            double arrowX2 = rtl ? kPad + 24 : kPad + contentW - 24;
            Icon::Arrow(cr, arrowX2, y + btnH / 2, 10, pal.onPrimary, rtl);
            AddContentHit(app, kPad, y, contentW, btnH, "action:sttest_connect");
            y += btnH + Theme::SpacingSmall;

            double halfW = (contentW - Theme::SpacingSmall) / 2;
            Icon::SetStroke(cr, pal.primary, 1.6);
            RoundedRect(cr, kPad, y, halfW, btnH, Theme::CornerRadiusPill);
            cairo_stroke(cr);
            DrawLabelCentered(cr, L(app, Loc::Key::Copy), kPad, y + btnH / 2, halfW, 12.5, true, pal.primary);
            AddContentHit(app, kPad, y, halfW, btnH, "action:sttest_copy");

            double sx = kPad + halfW + Theme::SpacingSmall;
            RoundedRect(cr, sx, y, halfW, btnH, Theme::CornerRadiusPill);
            cairo_stroke(cr);
            DrawLabelCentered(cr, L(app, Loc::Key::Save), sx, y + btnH / 2, halfW, 12.5, true, pal.primary);
            AddContentHit(app, sx, y, halfW, btnH, "action:sttest_save");
            y += btnH + kPad;
        }
    }

    y += 60; // extra margin so Copy/Save are fully visible at max scroll, not clipped at the window edge
    cairo_restore(cr);
    return y + app->scrollY - barH;
}

// ---------------------------------------------------------------------------
// Top-level Paint()
// ---------------------------------------------------------------------------
static void PaintApp(cairo_t* cr, AppState* app, double width, double height) {
    const auto& pal = Pal(app);
    app->hitRects.clear();

    // Defensive: guarantee there's no leftover path state from whatever the
    // previous frame (or an earlier draw call this same frame) last left on
    // the context — cairo_fill/cairo_stroke clear the path themselves, but
    // this costs nothing and rules out an entire class of "stray line"
    // rendering bug outright.
    cairo_new_path(cr);
    SetColor(cr, pal.background);
    cairo_paint(cr);
    cairo_new_path(cr);

    double contentH = 0;
    switch (app->screen) {
        case Screen::Home:      contentH = DrawHomeScreen(cr, app, width, height); break;
        case Screen::Saved:     contentH = DrawSavedScreen(cr, app, width, height); break;
        case Screen::SpeedTest: contentH = DrawSpeedTestScreen(cr, app, width, height); break;
        case Screen::Settings:  contentH = DrawSettingsScreen(cr, app, width, height); break;
        case Screen::Support:   contentH = DrawSupportScreen(cr, app, width, height); break;
    }
    app->contentHeight = contentH;

    if (app->screen == Screen::Home) {
        DrawHomeBottomBar(cr, app, width, height);
    }
}

static void OnDraw(GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer data) {
    auto* app = static_cast<AppState*>(data);
    app->lastPaintedWidth = width;
    app->lastPaintedHeight = height;
    PaintApp(cr, app, width, height);
}

// ---------------------------------------------------------------------------
// Favorites <-> proxies sync
// ---------------------------------------------------------------------------
static void ApplyFavoritesLocked(AppState* app) {
    auto favKeys = Storage::LoadFavoriteKeys();
    for (auto& p : app->proxies) p.isFavorite = favKeys.count(p.Key()) > 0;
}

static void PersistFavorites(AppState* app) {
    std::unordered_set<std::string> keys;
    {
        std::lock_guard<std::mutex> lk(app->proxiesMutex);
        for (auto& p : app->proxies) if (p.isFavorite) keys.insert(p.Key());
    }
    Storage::SaveFavoriteKeys(keys);
}

// ---------------------------------------------------------------------------
// Background networking (fetch -> parse -> ping), posted back via g_idle_add
// ---------------------------------------------------------------------------
struct FetchDoneMsg { AppState* app; ProxySource::FetchResult fetch; };

static gboolean OnFetchDoneIdle(gpointer data) {
    auto* msg = static_cast<FetchDoneMsg*>(data);
    AppState* app = msg->app;

    if (!msg->fetch.success) {
        app->isScanning = false;
        app->loadError = true;
        gtk_widget_queue_draw(app->drawingArea);
        delete msg;
        return G_SOURCE_REMOVE;
    }

    app->isOffline = (msg->fetch.origin == ProxySource::FetchOrigin::Cache);
    app->loadError = false;

    auto parsed = ProxyParser::Parse(msg->fetch.rawText);
    {
        std::lock_guard<std::mutex> lk(app->proxiesMutex);
        app->proxies = std::move(parsed);
        ApplyFavoritesLocked(app);
    }
    gtk_widget_queue_draw(app->drawingArea);

    // Kick off pinging on another background thread.
    std::thread([app]() {
        std::vector<ProxyItem> copy;
        {
            std::lock_guard<std::mutex> lk(app->proxiesMutex);
            copy = app->proxies;
        }
        PingService::PingAll(copy, [app](size_t) {
            // Throttled redraw could be added; for simplicity redraw on completion only.
        });
        {
            std::lock_guard<std::mutex> lk(app->proxiesMutex);
            // Merge ping results back by key (list identity is stable within a scan).
            for (auto& p : copy) {
                for (auto& orig : app->proxies) {
                    if (orig.Key() == p.Key()) { orig.pingMs = p.pingMs; orig.isScanned = p.isScanned; break; }
                }
            }
        }
        g_idle_add([](gpointer d) -> gboolean {
            auto* a = static_cast<AppState*>(d);
            a->isScanning = false;
            gtk_widget_queue_draw(a->drawingArea);
            return G_SOURCE_REMOVE;
        }, app);
    }).detach();

    delete msg;
    return G_SOURCE_REMOVE;
}

static void StartScan(AppState* app) {
    if (app->isScanning) return;
    app->isScanning = true;
    app->loadError = false;
    gtk_widget_queue_draw(app->drawingArea);
    std::thread([app]() {
        auto* msg = new FetchDoneMsg{app, ProxySource::Fetch()};
        g_idle_add(OnFetchDoneIdle, msg);
    }).detach();
}

static void StartBannerLoad(AppState* app) {
    if (app->banner.loading || app->banner.loaded) return;
    app->banner.loading = true;
    std::thread([app]() {
        auto items = BannerSlideshow::FetchBannerItems();
        std::vector<GdkPixbuf*> pixbufs;
        for (auto& it : items) {
            std::string bytes;
            GdkPixbuf* pix = nullptr;
            if (BannerSlideshow::HttpGetBytes(it.imageUrl, bytes)) {
                pix = BannerSlideshow::DecodeImage(bytes);
            }
            pixbufs.push_back(pix);
        }
        struct Msg { AppState* app; std::vector<BannerSlideshow::BannerItem> items; std::vector<GdkPixbuf*> pixbufs; };
        auto* msg = new Msg{app, std::move(items), std::move(pixbufs)};
        g_idle_add([](gpointer d) -> gboolean {
            auto* m = static_cast<Msg*>(d);
            m->app->banner.items = std::move(m->items);
            m->app->banner.pixbufs = std::move(m->pixbufs);
            m->app->banner.loaded = true;
            m->app->banner.loading = false;
            gtk_widget_queue_draw(m->app->drawingArea);
            delete m;
            return G_SOURCE_REMOVE;
        }, msg);
    }).detach();
}

static gboolean OnBannerTick(gpointer data) {
    auto* app = static_cast<AppState*>(data);
    if (!app->banner.pixbufs.empty()) {
        app->banner.current = (app->banner.current + 1) % (int)app->banner.pixbufs.size();
        gtk_widget_queue_draw(app->drawingArea);
    }
    return G_SOURCE_CONTINUE;
}

static gboolean OnAutoScanTick(gpointer data) {
    auto* app = static_cast<AppState*>(data);
    if (app->settings.autoScanEnabled) StartScan(app);
    return G_SOURCE_CONTINUE;
}

// ---------------------------------------------------------------------------
// Speed test background run
// ---------------------------------------------------------------------------
static void StartSpeedTest(AppState* app) {
    auto parsed = SpeedTester::ParseInput(app->speedTest.inputText);
    if (!parsed.valid) return;
    app->speedTest.running = true;
    app->speedTest.resolvedHost = parsed.server;
    app->speedTest.resolvedPort = parsed.port;
    gtk_widget_queue_draw(app->drawingArea);
    std::thread([app, parsed]() {
        auto result = SpeedTester::Run(parsed);
        struct Msg { AppState* app; SpeedTester::Result result; };
        auto* msg = new Msg{app, result};
        g_idle_add([](gpointer d) -> gboolean {
            auto* m = static_cast<Msg*>(d);
            m->app->speedTest.running = false;
            m->app->speedTest.hasResult = true;
            m->app->speedTest.result = m->result;
            gtk_widget_queue_draw(m->app->drawingArea);
            delete m;
            return G_SOURCE_REMOVE;
        }, msg);
    }).detach();
}

// ---------------------------------------------------------------------------
// Clipboard helper
// ---------------------------------------------------------------------------
static void CopyToClipboard(AppState* app, const std::string& text) {
    GdkClipboard* cb = gtk_widget_get_clipboard(app->window);
    gdk_clipboard_set_text(cb, text.c_str());
}

// ---------------------------------------------------------------------------
// Click handling — hit-tests against the rects recorded during the last
// paint pass, mirroring the Windows build's own "record then hit-test"
// approach for its hand-painted UI.
// ---------------------------------------------------------------------------
static ProxyItem* FindProxyByIndex(AppState* app, int idx) {
    if (idx < 0 || (size_t)idx >= app->proxies.size()) return nullptr;
    return &app->proxies[idx];
}

// ---------------------------------------------------------------------------
// Clear-favorites confirmation (native GtkAlertDialog — GTK 4.10+)
// ---------------------------------------------------------------------------
static void DoClearFavorites(AppState* app) {
    {
        std::lock_guard<std::mutex> lk(app->proxiesMutex);
        for (auto& p : app->proxies) p.isFavorite = false;
    }
    PersistFavorites(app);
    gtk_widget_queue_draw(app->drawingArea);
}

static void OnClearFavConfirmResponse(GObject* src, GAsyncResult* res, gpointer data) {
    auto* app = static_cast<AppState*>(data);
    GError* error = nullptr;
    int idx = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, &error);
    if (error) { g_error_free(error); return; } // dismissed (e.g. Escape) -> treat as cancel
    if (idx == 1) DoClearFavorites(app); // buttons[1] == "Clear"
}

static void ShowClearFavoritesConfirm(AppState* app) {
    GtkAlertDialog* dialog = gtk_alert_dialog_new("%s", L(app, Loc::Key::ClearFavConfirmTitle).c_str());
    gtk_alert_dialog_set_detail(dialog, L(app, Loc::Key::ClearFavConfirmBody).c_str());
    std::string cancelLabel = L(app, Loc::Key::Cancel);
    std::string clearLabel = L(app, Loc::Key::Clear);
    const char* buttons[3] = { cancelLabel.c_str(), clearLabel.c_str(), nullptr };
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    gtk_alert_dialog_choose(dialog, GTK_WINDOW(app->window), nullptr, OnClearFavConfirmResponse, app);
    g_object_unref(dialog);
}

static void HandleAction(AppState* app, const std::string& action) {
    // Clicking anything other than the input field itself (or paste, which
    // implies focusing it) drops focus/caret from the hand-drawn text input.
    if (action != "action:focusinput" && action != "action:paste" && action != "action:clearinput") {
        app->speedInputFocused = false;
    }
    if (action == "nav:back") {
        app->screen = app->settingsReturnTo;
        app->scrollY = 0;
        // If "back" just landed us ON the Settings screen (e.g. we were on
        // Saved/SpeedTest/Support, reached from Settings), reset the return
        // target back to Home. Without this, settingsReturnTo stayed
        // pointing at whichever sub-screen we'd last visited, so a SECOND
        // "back" press from Settings itself silently sent us right back to
        // that sub-screen instead of Home — looking exactly like "back
        // doesn't work" even though the button was registering the click
        // correctly the whole time.
        if (app->screen == Screen::Settings) app->settingsReturnTo = Screen::Home;
    } else if (action == "nav:settings") {
        app->settingsReturnTo = Screen::Home;
        app->screen = Screen::Settings;
        app->scrollY = 0;
    } else if (action == "nav:saved") {
        app->settingsReturnTo = Screen::Settings;
        app->screen = Screen::Saved;
        app->scrollY = 0;
    } else if (action == "nav:speedtest") {
        app->settingsReturnTo = Screen::Settings;
        app->screen = Screen::SpeedTest;
        app->scrollY = 0;
    } else if (action == "nav:support") {
        app->settingsReturnTo = Screen::Settings;
        app->screen = Screen::Support;
        app->scrollY = 0;
    } else if (action == "action:scan") {
        StartScan(app);
    } else if (action.rfind("connect:", 0) == 0) {
        int idx = std::atoi(action.substr(8).c_str());
        std::lock_guard<std::mutex> lk(app->proxiesMutex);
        if (auto* p = FindProxyByIndex(app, idx)) TelegramLauncher::OpenProxyLink(p->link);
    } else if (action.rfind("copy:", 0) == 0) {
        int idx = std::atoi(action.substr(5).c_str());
        std::lock_guard<std::mutex> lk(app->proxiesMutex);
        if (auto* p = FindProxyByIndex(app, idx)) CopyToClipboard(app, p->link);
    } else if (action.rfind("fav:", 0) == 0) {
        int idx = std::atoi(action.substr(4).c_str());
        {
            std::lock_guard<std::mutex> lk(app->proxiesMutex);
            if (auto* p = FindProxyByIndex(app, idx)) p->isFavorite = !p->isFavorite;
        }
        PersistFavorites(app);
    } else if (action == "action:clearfav") {
        ShowClearFavoritesConfirm(app);
    } else if (action == "banner:open") {
        if (!app->banner.items.empty()) {
            auto& item = app->banner.items[app->banner.current % app->banner.items.size()];
            if (!item.targetLink.empty()) {
                // t.me links open inside Telegram itself; anything else
                // (a plain https URL) opens in the browser as before.
                if (item.targetLink.find("t.me/") != std::string::npos || item.targetLink.rfind("tg://", 0) == 0)
                    TelegramLauncher::OpenTelegramLink(item.targetLink);
                else
                    TelegramLauncher::OpenPlainUrl(item.targetLink);
            }
        }
    } else if (action.rfind("action:theme:", 0) == 0) {
        app->settings.themeMode = action.substr(13);
        app->darkMode = app->settings.themeMode != "light";
        Config::Save(app->settings);
    } else if (action.rfind("action:lang:", 0) == 0) {
        app->settings.language = action.substr(12);
        Config::Save(app->settings);
        gtk_widget_set_direction(app->window, IsRtl(app) ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR);
    } else if (action == "action:togglebanner") {
        app->settings.bannerEnabled = !app->settings.bannerEnabled;
        Config::Save(app->settings);
        if (app->settings.bannerEnabled) StartBannerLoad(app);
    } else if (action == "action:toggleautoscan") {
        app->settings.autoScanEnabled = !app->settings.autoScanEnabled;
        Config::Save(app->settings);
    } else if (action == "action:clearinput") {
        app->speedTest.inputText.clear();
    } else if (action == "action:focusinput") {
        app->speedInputFocused = true;
        app->caretVisible = true;
    } else if (action == "action:paste") {
        app->speedInputFocused = true;
        GdkClipboard* cb = gtk_widget_get_clipboard(app->window);
        gdk_clipboard_read_text_async(cb, nullptr, [](GObject* src, GAsyncResult* res, gpointer data) {
            auto* app = static_cast<AppState*>(data);
            GError* err = nullptr;
            char* text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(src), res, &err);
            if (text) {
                app->speedTest.inputText = text;
                g_free(text);
            }
            if (err) g_error_free(err);
            gtk_widget_queue_draw(app->drawingArea);
        }, app);
    } else if (action == "action:starttest") {
        StartSpeedTest(app);
    } else if (action.rfind("action:chip:", 0) == 0) {
        app->speedTest.fileSizeMb = std::atof(action.substr(12).c_str());
    } else if (action == "action:sttest_connect") {
        if (!app->speedTest.inputText.empty()) TelegramLauncher::OpenProxyLink(app->speedTest.inputText);
    } else if (action == "action:sttest_copy") {
        CopyToClipboard(app, app->speedTest.inputText);
    } else if (action == "action:sttest_save") {
        auto parsed = SpeedTester::ParseInput(app->speedTest.inputText);
        if (parsed.valid) {
            std::lock_guard<std::mutex> lk(app->proxiesMutex);
            bool found = false;
            for (auto& p : app->proxies) {
                if (p.server == parsed.server && p.port == parsed.port) { p.isFavorite = true; found = true; break; }
            }
            if (!found) {
                ProxyItem item;
                item.id = (int)app->proxies.size() + 1;
                item.server = parsed.server; item.port = parsed.port; item.secret = parsed.secret;
                item.link = app->speedTest.inputText;
                item.isFavorite = true;
                app->proxies.push_back(item);
            }
            PersistFavorites(app);
        }
    } else if (action.rfind("action:copywallet:", 0) == 0) {
        static const char* addrs[3] = {
            "0x3f2A9c7E1B4d8F6a0C5e2D9b7A1c4E8f3B6d9A2c",
            "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
            "TXn9sV2mK8pQ4rL7wY1zC6dB3fH5jG0aE9",
        };
        int idx = std::atoi(action.substr(19).c_str());
        if (idx >= 0 && idx < 3) CopyToClipboard(app, addrs[idx]);
    } else if (action == "action:opengithub") {
        TelegramLauncher::OpenPlainUrl("https://github.com/Iwanian");
    } else if (action == "action:opentelegram") {
        // Route through tg://resolve so it opens inside the Telegram app
        // itself instead of a browser landing page.
        TelegramLauncher::OpenTelegramLink("https://t.me/Iwanian");
    }

    gtk_widget_queue_draw(app->drawingArea);
}

static void OnClick(GtkGestureClick*, int, double x, double y, gpointer data) {
    auto* app = static_cast<AppState*>(data);
    if (app->draggingAutoScanSlider) return; // the press that started this drag shouldn't also fire a click
    for (auto it = app->hitRects.rbegin(); it != app->hitRects.rend(); ++it) {
        if (x >= it->x && x <= it->x + it->w && y >= it->y && y <= it->y + it->h) {
            HandleAction(app, it->action);
            break;
        }
    }
}

static gboolean OnScroll(GtkEventControllerScroll*, double, double dy, gpointer data) {
    auto* app = static_cast<AppState*>(data);
    double maxScroll = std::max(0.0, app->contentHeight - app->lastPaintedHeight);
    app->scrollY = std::clamp(app->scrollY + dy * 40.0, 0.0, maxScroll);
    gtk_widget_queue_draw(app->drawingArea);
    return TRUE;
}

// ---------------------------------------------------------------------------
// Auto-scan interval slider (draggable, 5s..240s) — GtkGestureDrag rather
// than the simple click gesture, since a slider needs continuous pointer
// tracking rather than a single press/release point.
// ---------------------------------------------------------------------------
constexpr double kAutoScanMinInterval = 5.0, kAutoScanMaxInterval = 240.0;

static void ApplySliderValueFromX(AppState* app, double x) {
    if (app->autoScanSliderW <= 0) return;
    double frac = (x - app->autoScanSliderX) / app->autoScanSliderW;
    frac = std::clamp(frac, 0.0, 1.0);
    int interval = (int)std::round(kAutoScanMinInterval + frac * (kAutoScanMaxInterval - kAutoScanMinInterval));
    if (interval != app->settings.autoScanIntervalS) {
        app->settings.autoScanIntervalS = interval;
        gtk_widget_queue_draw(app->drawingArea);
    }
}

static void OnAutoScanSliderPress(GtkGestureClick*, int, double startX, double startY, gpointer data) {
    auto* app = static_cast<AppState*>(data);
    if (app->screen != Screen::Settings || app->autoScanSliderW <= 0) return;
    bool withinY = startY >= app->autoScanSliderY - 16 && startY <= app->autoScanSliderY + 16;
    bool withinX = startX >= app->autoScanSliderX - 12 && startX <= app->autoScanSliderX + app->autoScanSliderW + 12;
    if (withinY && withinX) {
        app->draggingAutoScanSlider = true;
        ApplySliderValueFromX(app, startX);
    }
}

static void OnAutoScanSliderMotion(GtkEventControllerMotion*, double x, double, gpointer data) {
    auto* app = static_cast<AppState*>(data);
    if (!app->draggingAutoScanSlider) return;
    ApplySliderValueFromX(app, x);
}

static void OnAutoScanSliderRelease(GtkGestureClick*, int, double, double, gpointer data) {
    auto* app = static_cast<AppState*>(data);
    if (!app->draggingAutoScanSlider) return;
    app->draggingAutoScanSlider = false;
    Config::Save(app->settings);
    // Restart the periodic timer at the new interval.
    if (app->autoScanTimerId) g_source_remove(app->autoScanTimerId);
    app->autoScanTimerId = g_timeout_add_seconds(app->settings.autoScanIntervalS, OnAutoScanTick, app);
}

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------
// Hardware keyboard handling for the hand-drawn Speed Test input (see
// DrawSpeedTestScreen). Proxy links/configs are always plain ASCII
// (tg://server:port:secret), so a simple keyval->ASCII mapping is enough —
// no IME/GtkIMContext machinery needed.
static gboolean OnKeyPress(GtkEventControllerKey*, guint keyval, guint /*keycode*/,
                            GdkModifierType state, gpointer data) {
    auto* app = static_cast<AppState*>(data);
    if (!app->speedInputFocused) return FALSE;
    auto& text = app->speedTest.inputText;

    if (keyval == GDK_KEY_BackSpace) {
        if (!text.empty()) {
            // Erase one UTF-8 codepoint, not just one byte (irrelevant for
            // proxy links in practice, but cheap to get right).
            size_t i = text.size() - 1;
            while (i > 0 && (text[i] & 0xC0) == 0x80) --i;
            text.erase(i);
        }
        gtk_widget_queue_draw(app->drawingArea);
        return TRUE;
    }
    if (keyval == GDK_KEY_Escape) {
        app->speedInputFocused = false;
        gtk_widget_queue_draw(app->drawingArea);
        return TRUE;
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        StartSpeedTest(app);
        return TRUE;
    }
    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_v || keyval == GDK_KEY_V)) {
        HandleAction(app, "action:paste");
        return TRUE;
    }
    guint32 uni = gdk_keyval_to_unicode(keyval);
    // Printable-ASCII only — matches what any real proxy link/config
    // actually contains; anything else (arrows, function keys, non-Latin
    // input) is simply ignored rather than inserted.
    if (uni >= 0x20 && uni < 0x7F) {
        text += (char)uni;
        gtk_widget_queue_draw(app->drawingArea);
        return TRUE;
    }
    return FALSE;
}

static gboolean OnCaretBlink(gpointer data) {
    auto* app = static_cast<AppState*>(data);
    if (app->speedInputFocused) {
        app->caretVisible = !app->caretVisible;
        gtk_widget_queue_draw(app->drawingArea);
    } else if (!app->caretVisible) {
        app->caretVisible = true; // leave it "on" while unfocused so it doesn't reappear mid-blink
    }
    return G_SOURCE_CONTINUE;
}

static void Activate(GtkApplication* gtkApp, gpointer userData) {
    auto* app = static_cast<AppState*>(userData);

    app->window = gtk_application_window_new(gtkApp);
    gtk_window_set_title(GTK_WINDOW(app->window), "Iwana Proxy");
    gtk_window_set_default_size(GTK_WINDOW(app->window), (int)kWindowWidth, 800);
    gtk_widget_set_direction(app->window, IsRtl(app) ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR);

    // Disable GTK's implicit widget transitions/animations app-wide. Our UI
    // is entirely hand-painted via Cairo every frame, so there should never
    // be an animated widget transition running at all.
    g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, nullptr);

    app->drawingArea = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(app->drawingArea), (int)kWindowWidth);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(app->drawingArea), 800);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->drawingArea), OnDraw, app, nullptr);
    gtk_widget_set_focusable(app->drawingArea, TRUE);
    gtk_widget_set_can_focus(app->drawingArea, TRUE);

    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(OnAutoScanSliderPress), app);
    g_signal_connect(click, "released", G_CALLBACK(OnClick), app);
    g_signal_connect(click, "released", G_CALLBACK(OnAutoScanSliderRelease), app);
    gtk_widget_add_controller(app->drawingArea, GTK_EVENT_CONTROLLER(click));

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(OnAutoScanSliderMotion), app);
    gtk_widget_add_controller(app->drawingArea, motion);

    GtkEventController* scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(OnScroll), app);
    gtk_widget_add_controller(app->drawingArea, scroll);

    // Hand-drawn Speed Test input's hardware-keyboard handling (see
    // OnKeyPress) — replaces a previously-overlaid real GtkText widget,
    // which could show a stray rendering artifact on some systems and may
    // have been swallowing clicks meant for the back button via its
    // off-screen (but apparently still hit-testable) position.
    GtkEventController* keyCtrl = gtk_event_controller_key_new();
    g_signal_connect(keyCtrl, "key-pressed", G_CALLBACK(OnKeyPress), app);
    gtk_widget_add_controller(app->drawingArea, keyCtrl);
    g_timeout_add(500, OnCaretBlink, app);

    gtk_window_set_child(GTK_WINDOW(app->window), app->drawingArea);
    gtk_window_present(GTK_WINDOW(app->window));
    gtk_widget_grab_focus(app->drawingArea);

    // Kick off initial data load.
    StartScan(app);
    if (app->settings.bannerEnabled) StartBannerLoad(app);
    app->banner.timerId = g_timeout_add_seconds(4, OnBannerTick, app); // 4s auto-advance, PORT_SPEC §5.1
    app->autoScanTimerId = g_timeout_add_seconds(app->settings.autoScanIntervalS, OnAutoScanTick, app);
}


int main(int argc, char** argv) {
    AppState* app = app_new();
    GtkApplication* gtkApp = gtk_application_new("io.iwanian.iwanaproxy", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtkApp, "activate", G_CALLBACK(Activate), app);
    int status = g_application_run(G_APPLICATION(gtkApp), argc, argv);
    g_object_unref(gtkApp);
    delete app;
    return status;
}
