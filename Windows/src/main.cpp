// main.cpp — Iwana Proxy (Windows)

#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <functional>
#include <thread>
#include <mutex>
#include <cwctype>
#include "Theme.h"
#include "Config.h"
#include "ProxySource.h"
#include "ProxyItem.h"
#include "ProxyParser.h"
#include "PingService.h"
#include "Storage.h"
#include "TelegramLauncher.h"
#include "Loc.h"
#include "SpeedTester.h"
#include "BannerSlideshow.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

using namespace Gdiplus;

// Wide-string build stamp (compile date/time), shown small on the Support
// screen so we can confirm which build is actually running when debugging.
#define WIDE2(x) L##x
#define WIDE1(x) WIDE2(x)
#define kBuildStamp WIDE1(__DATE__) L" " WIDE1(__TIME__)

namespace {
    HINSTANCE       g_hInst = nullptr;
    HWND            g_hWnd = nullptr;
    ULONG_PTR       g_gdiplusToken = 0;
    const Theme::Palette* g_palette = &Theme::Dark;
    Config::AppSettings   g_settings;

    std::wstring TT(Loc::Key k) { return Loc::T(g_settings.language, k); }
    bool IsRTL() { return g_settings.language == L"fa"; }

    enum class Screen { Home, Settings, Saved, SpeedTest, Support };
    Screen g_currentScreen = Screen::Home;
    // Real navigation history (stack), so "Back" always unwinds one screen at
    // a time no matter how deep the user drilled in (Home -> Settings ->
    // Saved -> ...), instead of a single stale "backTarget" that could get
    // stuck pointing at the screen the user is already on.
    std::vector<Screen> g_navStack;
    void NavigateTo(Screen s, Screen /*backTo — unused, kept for call-site compatibility*/ = Screen::Home) {
        g_navStack.push_back(g_currentScreen);
        g_currentScreen = s;
    }
    void NavigateBack() {
        if (!g_navStack.empty()) { g_currentScreen = g_navStack.back(); g_navStack.pop_back(); }
        else g_currentScreen = Screen::Home;
    }

    // ---- Real proxy list state ----
    std::mutex               g_proxiesMutex;
    std::vector<ProxyItem>   g_proxies;
    std::unordered_set<std::wstring> g_favoriteKeys;

    enum class ProxyListState { Loaded, Loading, Empty, Error };
    ProxyListState g_proxyListState = ProxyListState::Loading;
    float g_spinnerAngle = 0.0f;
    constexpr UINT_PTR kSpinnerTimerId  = 1;
    constexpr UINT_PTR kPulseTimerId    = 2;
    constexpr UINT_PTR kBannerTimerId   = 3;
    constexpr UINT_PTR kAutoScanTimerId = 4;
    constexpr UINT kSpinnerIntervalMs = 40;
    constexpr UINT WM_APP_FETCH_DONE    = WM_APP + 1;
    constexpr UINT WM_APP_PING_PROGRESS = WM_APP + 2;
    constexpr UINT WM_APP_SPEEDTEST_DONE = WM_APP + 3;
    constexpr UINT WM_APP_BANNERS_READY  = WM_APP + 4;

    std::mutex        g_fetchMutex;
    ProxySource::FetchResult g_lastFetch;
    bool              g_pingInProgress = false;

    float g_pulseAlpha = 0.4f;
    bool  g_pulseRising = true;

    // ---- Scrolling ----
    int g_homeScrollY = 0;
    int g_savedScrollY = 0;
    int g_settingsScrollY = 0;
    int g_settingsContentHeight = 0;
    int g_speedScrollY = 0;
    int g_speedContentHeight = 0;
    int g_homeContentHeight = 0;
    int g_savedContentHeight = 0;

    // ---- Banner slideshow ----
    std::mutex g_bannerMutex;
    std::vector<Image*> g_bannerImages;
    std::vector<std::wstring> g_bannerLinks; // parallel to g_bannerImages
    int g_bannerIndex = 0;
    bool g_bannerFetchStarted = false;

    // ---- Hit-test rects ----
    Rect g_settingsBtnRect, g_backBtnRect, g_scanBtnRect;
    Rect g_bannerRect; bool g_bannerRectValid = false;
    struct CardHit { std::wstring key, link; Rect cardRect, copyRect, bookmarkRect, connectRect; };
    std::vector<CardHit> g_visibleCards;
    Rect g_navSavedRect, g_navSpeedRect, g_navSupportRect;
    Rect g_langPillRects[3], g_themePillRects[3];
    Rect g_bannerSliderToggleRect;
    Rect g_clearFavBtnRect;

    // Speed test screen state
    std::wstring g_speedInput;
    bool g_speedInputFocused = false;
    bool g_speedTesting = false;
    bool g_speedHasResult = false;
    SpeedTester::Result g_speedResult;
    std::wstring g_speedErrorMsg;
    Rect g_speedInputRect, g_speedStartBtnRect, g_speedPasteBtnRect, g_speedClearBtnRect;
    size_t g_speedInputCursor = 0;
    SpeedTester::ParsedProxy g_lastSpeedProxy; // remembers what was tested, for Connect/Copy/Save
    Rect g_speedConnectBtnRect, g_speedCopyBtnRect, g_speedSaveBtnRect;
    Rect g_fileSizeChipRects[5];
    const double kFileSizeChipValues[5] = { 1000.0, 500.0, 100.0, 50.0, 10.0 };

    // Download Time Estimator (SpeedTest results panel)
    std::wstring g_fileSizeInput;
    bool g_fileSizeInputFocused = false;
    Rect g_fileSizeInputRect;

    // Auto Scan settings (Settings screen)
    Rect g_autoScanToggleRect, g_autoScanMinusRect, g_autoScanPlusRect;
    void ConfigureAutoScanTimer(HWND hWnd) {
        KillTimer(hWnd, kAutoScanTimerId);
        if (g_settings.autoScanEnabled) {
            int intervalMs = (std::max)(5, g_settings.autoScanIntervalS) * 1000;
            SetTimer(hWnd, kAutoScanTimerId, (UINT)intervalMs, nullptr);
        }
    }

    // Support screen state
    struct CryptoRow { std::wstring label, address; Rect rect; };
    std::vector<CryptoRow> g_cryptoRows;
    Rect g_supportGithubRect, g_supportTelegramRect;

    const wchar_t* kLangCodes[]  = { L"fa", L"en", L"ru" };
    const wchar_t* kLangNative[] = { L"فارسی", L"English", L"Русский" };
    const wchar_t* kThemeCodes[] = { L"dark", L"light", L"system" };

    bool g_confirmDialogOpen = false;
    bool g_confirmDialogResult = false;
    Rect g_dlgYesRect, g_dlgNoRect;

    int SelectedLangIndex() { for (int i=0;i<3;++i) if (g_settings.language==kLangCodes[i]) return i; return 1; }
    int SelectedThemeIndex() { for (int i=0;i<3;++i) if (g_settings.themeMode==kThemeCodes[i]) return i; return 0; }
    void ApplyThemeFromSettings() { g_palette = (g_settings.themeMode == L"light") ? &Theme::Light : &Theme::Dark; }

    bool ShowConfirmDialog(HWND parent);
    void StartFetch(HWND hWnd);
    void StartPing(HWND hWnd);
    void StartBannerFetch(HWND hWnd);

    Bitmap* g_backBuffer = nullptr;
    int     g_bbWidth = 0, g_bbHeight = 0;
    void EnsureBackBuffer(int w, int h) {
        if (g_backBuffer && g_bbWidth == w && g_bbHeight == h) return;
        delete g_backBuffer;
        g_backBuffer = new Bitmap(w, h, PixelFormat32bppARGB);
        g_bbWidth = w; g_bbHeight = h;
    }

    GraphicsPath* RoundedRectPath(const Rect& r, int radius) {
        GraphicsPath* path = new GraphicsPath();
        int d = radius * 2;
        path->AddArc(r.X, r.Y, d, d, 180, 90);
        path->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
        path->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
        path->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
        path->CloseFigure();
        return path;
    }

    Color FromColorref(COLORREF c) { return Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }
    Color FromColorrefAlpha(COLORREF c, BYTE a) { return Color(a, GetRValue(c), GetGValue(c), GetBValue(c)); }
    bool IsDarkTheme() { return g_palette == &Theme::Dark; }

    Color LatencyColor(int pingMs) {
        if (pingMs >= 0 && pingMs <= 150) return Color(255, 0x00, 0xE6, 0x76);
        if (pingMs >= 151 && pingMs <= 300) return Color(255, 0xFF, 0xD6, 0x00);
        return Color(255, 0xFF, 0x17, 0x44);
    }

    bool PtInRect(const Rect& r, int x, int y) {
        return x >= r.X && x <= r.X + r.Width && y >= r.Y && y <= r.Y + r.Height;
    }

    std::vector<ProxyItem> SnapshotProxies() {
        std::lock_guard<std::mutex> lock(g_proxiesMutex);
        return g_proxies;
    }
    // Dead proxies (ping finished, no response) are dropped entirely instead
    // of shown with an "Offline" badge — a proxy the user can't connect to
    // isn't useful in the list. Still-in-progress items (!isScanned) are kept
    // so the "TESTING" state remains visible while a scan is running.
    std::vector<ProxyItem> GetHomeProxies() {
        std::vector<ProxyItem> all = SnapshotProxies();
        all.erase(std::remove_if(all.begin(), all.end(), [](const ProxyItem& p) {
            return p.isScanned && !p.IsAlive();
        }), all.end());
        std::stable_sort(all.begin(), all.end(), [](const ProxyItem& a, const ProxyItem& b) {
            if (a.IsAlive() != b.IsAlive()) return a.IsAlive() && !b.IsAlive();
            if (a.IsAlive() && b.IsAlive()) return a.pingMs < b.pingMs;
            return false;
        });
        return all;
    }
    std::vector<ProxyItem> GetFavoriteProxies() {
        std::vector<ProxyItem> all = SnapshotProxies();
        std::vector<ProxyItem> out;
        for (auto& item : all) if (item.isFavorite && !(item.isScanned && !item.IsAlive())) out.push_back(item);
        return out;
    }
    void ToggleFavorite(const std::wstring& key) {
        { std::lock_guard<std::mutex> lock(g_proxiesMutex);
          for (auto& item : g_proxies) if (item.Key() == key) { item.isFavorite = !item.isFavorite; break; } }
        if (g_favoriteKeys.count(key)) g_favoriteKeys.erase(key); else g_favoriteKeys.insert(key);
        Storage::SaveFavoriteKeys(g_favoriteKeys);
    }
    // Saves a proxy tested from the Speed Test screen's free-text field (which
    // may not be part of the main scanned list) into Favorites — inserting it
    // into g_proxies if it isn't already there, so it actually shows up on
    // the Saved Proxies screen, not just in the on-disk key list.
    void SaveManualProxy(const SpeedTester::ParsedProxy& p, const std::wstring& rawLink, int pingMs) {
        ProxyItem item;
        item.server = p.server; item.port = p.port; item.secret = p.secret;
        item.link = rawLink;
        item.pingMs = pingMs; item.isScanned = (pingMs >= 0); item.isFavorite = true;
        std::wstring key = item.Key();
        { std::lock_guard<std::mutex> lock(g_proxiesMutex);
          bool found = false;
          for (auto& existing : g_proxies) {
              if (existing.Key() == key) {
                  existing.isFavorite = true;
                  if (!existing.isScanned) { existing.pingMs = pingMs; existing.isScanned = (pingMs >= 0); }
                  if (existing.link.empty()) existing.link = rawLink;
                  found = true;
                  break;
              }
          }
          if (!found) g_proxies.push_back(item);
        }
        g_favoriteKeys.insert(key);
        Storage::SaveFavoriteKeys(g_favoriteKeys);
    }
    void CopyToClipboard(HWND hWnd, const std::wstring& text) {
        if (!OpenClipboard(hWnd)) return;
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (hMem) {
            void* p = GlobalLock(hMem);
            memcpy(p, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
    void OpenUrl(HWND owner, const std::wstring& url) { TelegramLauncher::OpenPlainUrl(owner, url); }
    void OpenProxy(HWND owner, const std::wstring& link) { TelegramLauncher::OpenProxyLink(owner, link); }

    // Resolves a font family name with a fallback chain, so text never
    // silently fails to render if the preferred font isn't available on this
    // system (observed under Wine/non-standard installs — GDI+ can construct
    // a FontFamily for a missing name yet have every subsequent DrawString
    // using it silently draw nothing). Returns a name guaranteed to resolve,
    // ending in GDI+'s built-in generic sans-serif if nothing else matches.
    inline std::wstring ResolveFontFamilyName(const wchar_t* preferred) {
        FontFamily f1(preferred);
        if (f1.GetLastStatus() == Ok && f1.IsAvailable()) return preferred;
        const wchar_t* fallbacks[] = { L"Tahoma", L"Arial", L"Verdana", L"Consolas" };
        for (auto name : fallbacks) {
            FontFamily fb(name);
            if (fb.GetLastStatus() == Ok && fb.IsAvailable()) return name;
        }
        return L"";
    }
    inline FontFamily ResolveFontFamily(const wchar_t* preferred) {
        std::wstring name = ResolveFontFamilyName(preferred);
        if (!name.empty()) return FontFamily(name.c_str());
        return FontFamily(); // GDI+'s built-in generic sans-serif — always valid
    }

    // ---- Icon glyphs rendered from Windows' built-in "Segoe Fluent Icons" /
    // "Segoe MDL2 Assets" symbol font (ships with Windows 10/11). This gives
    // crisp, professional, pixel-hinted icons identical to native Windows
    // apps, instead of hand-drawn shapes — with zero extra binary size since
    // the font is already on the system. ----
    inline const FontFamily& IconFontFamily() {
        static FontFamily fluent(L"Segoe Fluent Icons");
        static FontFamily mdl2(L"Segoe MDL2 Assets");
        static bool haveFluent = (fluent.GetLastStatus() == Ok);
        return haveFluent ? fluent : mdl2;
    }

    void DrawIconGlyph(Graphics& g, const Rect& r, const wchar_t* glyph, Color color, float scale = 0.52f) {
        float size = (std::max)(8.0f, (std::min)(r.Width, r.Height) * scale);
        Font f(&IconFontFamily(), size, FontStyleRegular, UnitPixel);
        SolidBrush b(color);
        StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(glyph, -1, &f, RectF((REAL)r.X, (REAL)r.Y, (REAL)r.Width, (REAL)r.Height), &fmt, &b);
    }

    void DrawGearIcon(Graphics& g, const Rect& r, Color color, Color /*holeColor*/) {
        // Proper filled cog (trapezoidal teeth + true center hole cut via
        // even-odd fill), matching a standard modern settings icon — not a
        // ring-with-ticks "sun" shape.
        int cx = r.X + r.Width/2, cy = r.Y + r.Height/2;
        float m = (float)(std::min)(r.Width, r.Height);
        const int teeth = 6;
        double toothHalf = (2*3.14159265/teeth) * 0.26;
        double rootHalf  = (2*3.14159265/teeth) * 0.40;
        float outerR = m * 0.46f, rootR = m * 0.33f, holeR = m * 0.19f;
        std::vector<PointF> pts;
        for (int i = 0; i < teeth; ++i) {
            double c = i * (2*3.14159265/teeth);
            double aR1=c-rootHalf, aT1=c-toothHalf, aT2=c+toothHalf, aR2=c+rootHalf;
            pts.push_back(PointF((REAL)(cx+cos(aR1)*rootR),  (REAL)(cy+sin(aR1)*rootR)));
            pts.push_back(PointF((REAL)(cx+cos(aT1)*outerR), (REAL)(cy+sin(aT1)*outerR)));
            pts.push_back(PointF((REAL)(cx+cos(aT2)*outerR), (REAL)(cy+sin(aT2)*outerR)));
            pts.push_back(PointF((REAL)(cx+cos(aR2)*rootR),  (REAL)(cy+sin(aR2)*rootR)));
        }
        GraphicsPath path(FillModeAlternate);
        path.AddPolygon(pts.data(), (int)pts.size());
        path.StartFigure();
        path.AddEllipse((REAL)(cx-holeR), (REAL)(cy-holeR), (REAL)(holeR*2), (REAL)(holeR*2));
        SolidBrush b(color);
        g.FillPath(&b, &path);
    }

    // Clean hand-drawn line arrow (shaft + chevron head) — used for the
    // header back button. Drawn by hand instead of a font glyph so the
    // proportions and stroke weight are fully controlled and stay crisp.
    void DrawLineArrow(Graphics& g, const Rect& r, Color color, bool pointRight) {
        float m = (float)(std::min)(r.Width, r.Height);
        REAL cy = r.Y + r.Height/2.0f;
        REAL shaftL = r.X + m*0.20f, shaftR = r.X + m*0.80f;
        REAL lw = (std::max)(1.6f, m*0.11f);
        Pen pen(color, lw); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
        g.DrawLine(&pen, shaftL, cy, shaftR, cy);
        REAL headX = pointRight ? shaftR : shaftL;
        REAL tailX = pointRight ? shaftR - m*0.28f : shaftL + m*0.28f;
        REAL headH = m*0.26f;
        PointF head[3] = { PointF(tailX, cy-headH), PointF(headX, cy), PointF(tailX, cy+headH) };
        g.DrawLines(&pen, head, 3);
    }

    void DrawBackArrow(Graphics& g, const Rect& r, Color color) { DrawLineArrow(g, r, color, false); }
    void DrawForwardArrow(Graphics& g, const Rect& r, Color color) { DrawLineArrow(g, r, color, true); }

    // Filled "send/play" style arrow — matches the Android app's Connect
    // button glyph (a solid arrowhead with a slight concave back edge, not a
    // thin stroked chevron).
    void DrawChevron(Graphics& g, const Rect& r, Color color) {
        int w = r.Width, h = r.Height;
        auto P = [&](double ux, double uy) { return PointF((REAL)(r.X + ux*w), (REAL)(r.Y + uy*h)); };
        PointF pts[4] = { P(0.12,0.12), P(0.95,0.5), P(0.12,0.88), P(0.34,0.5) };
        SolidBrush b(color);
        g.FillPolygon(&b, pts, 4);
    }

    // Modern "Bookmark" ribbon icon (not a star) — filled when saved. Kept
    // slim (thin stroke / modest fill) so it reads at the same visual weight
    // as the Copy icon next to it, instead of looking bulkier.
    void DrawBookmarkIcon(Graphics& g, const Rect& r, Color color, bool filled) {
        int w = r.Width, h = r.Height;
        int bw = (int)(w * 0.44), bh = (int)(h * 0.58);
        int bx = r.X + (w - bw)/2, by = r.Y + (h - bh)/2 - 1;
        GraphicsPath path;
        PointF pts[5] = {
            PointF((REAL)bx, (REAL)by),
            PointF((REAL)(bx+bw), (REAL)by),
            PointF((REAL)(bx+bw), (REAL)(by+bh)),
            PointF((REAL)(bx+bw/2), (REAL)(by+bh - bw*0.46)),
            PointF((REAL)bx, (REAL)(by+bh)),
        };
        path.AddPolygon(pts, 5);
        path.CloseFigure();
        if (filled) {
            SolidBrush b(color);
            g.FillPath(&b, &path);
        } else {
            Pen pen(color, 1.4f);
            pen.SetLineJoin(LineJoinRound);
            g.DrawPath(&pen, &path);
        }
    }

    void DrawCopyIcon(Graphics& g, const Rect& r, Color color) {
        DrawIconGlyph(g, r, L"\uE8C8", color, 0.44f); // Copy — smaller scale to match the slim bookmark icon
    }

    // Theme-picker icons: 0 = dark (crescent moon), 1 = light (sun), 2 = system (half/half).
    void DrawThemeIcon(Graphics& g, const Rect& r, int kind, Color color, Color lightHalfColor) {
        int cx = r.X + r.Width/2, cy = r.Y + r.Height/2;
        float radius = (std::min)(r.Width, r.Height) * 0.42f;
        if (kind == 0) {
            GraphicsPath path(FillModeAlternate);
            path.AddEllipse((REAL)(cx-radius), (REAL)(cy-radius), (REAL)(radius*2), (REAL)(radius*2));
            path.StartFigure();
            float r2 = radius * 0.82f;
            float offx = radius * 0.42f, offy = -radius * 0.15f;
            path.AddEllipse((REAL)(cx-r2+offx), (REAL)(cy-r2+offy), (REAL)(r2*2), (REAL)(r2*2));
            SolidBrush b(color);
            g.FillPath(&b, &path);
        } else if (kind == 1) {
            SolidBrush b(color);
            float ir = radius * 0.55f;
            g.FillEllipse(&b, cx-ir, cy-ir, ir*2, ir*2);
            Pen pen(color, 1.7f); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
            for (int i = 0; i < 8; ++i) {
                double a = i * (2*3.14159265/8);
                float x1 = cx+(float)cos(a)*radius*0.78f, y1 = cy+(float)sin(a)*radius*0.78f;
                float x2 = cx+(float)cos(a)*radius*1.08f, y2 = cy+(float)sin(a)*radius*1.08f;
                g.DrawLine(&pen, x1, y1, x2, y2);
            }
        } else {
            SolidBrush dark(color), light(lightHalfColor);
            g.FillPie(&dark,  (REAL)(cx-radius), (REAL)(cy-radius), (REAL)(radius*2), (REAL)(radius*2), 90, 180);
            g.FillPie(&light, (REAL)(cx-radius), (REAL)(cy-radius), (REAL)(radius*2), (REAL)(radius*2), -90, 180);
            Pen outline(color, 1.3f);
            g.DrawEllipse(&outline, (REAL)(cx-radius), (REAL)(cy-radius), (REAL)(radius*2), (REAL)(radius*2));
        }
    }

    // (Persian language row uses DrawHeartIcon directly, defined further below.)

    // Filled circle with a white checkmark — "Stability" stat icon.
    void DrawStabilityIcon(Graphics& g, const Rect& r, Color color) {
        int cx = r.X + r.Width/2, cy = r.Y + r.Height/2;
        float radius = (std::min)(r.Width, r.Height) * 0.46f;
        SolidBrush circleBrush(color);
        g.FillEllipse(&circleBrush, (REAL)(cx-radius), (REAL)(cy-radius), (REAL)(radius*2), (REAL)(radius*2));
        Pen pen(Color(255,255,255,255), (std::max)(1.3f, r.Width*0.11f)); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
        auto P = [&](double ux, double uy) { return PointF((REAL)(r.X + ux*r.Width), (REAL)(r.Y + uy*r.Height)); };
        PointF chk[3] = { P(0.28,0.52), P(0.44,0.68), P(0.75,0.33) };
        g.DrawLines(&pen, chk, 3);
    }

    // Wifi/signal icon — "Avg ping" stat icon.
    void DrawWifiIcon(Graphics& g, const Rect& r, Color color) {
        int cx = r.X + r.Width/2, by = r.Y + (int)(r.Height*0.82f);
        float lw = (std::max)(1.3f, r.Width*0.11f);
        Pen pen(color, lw); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
        float m = (float)(std::min)(r.Width, r.Height);
        for (int i = 0; i < 3; ++i) {
            float radius = m * (0.18f + i*0.16f);
            RectF box((REAL)(cx-radius), (REAL)(by-radius), (REAL)(radius*2), (REAL)(radius*2));
            g.DrawArc(&pen, box, 225.0f, 90.0f);
        }
        SolidBrush dot(color);
        float dotR = m*0.07f;
        g.FillEllipse(&dot, cx-dotR, by-dotR, dotR*2, dotR*2);
    }

    // Simple up/down arrow — "Upload"/"Download" stat icons.
    void DrawArrowIcon(Graphics& g, const Rect& r, Color color, bool up) {
        int cx = r.X + r.Width/2;
        int topY = r.Y + (int)(r.Height*0.12f), botY = r.Y + (int)(r.Height*0.88f);
        float lw = (std::max)(1.3f, r.Width*0.13f);
        Pen pen(color, lw); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
        int a = up ? botY : topY, b = up ? topY : botY;
        g.DrawLine(&pen, (REAL)cx, (REAL)a, (REAL)cx, (REAL)b);
        int headY = b, dir = up ? 1 : -1;
        float headW = r.Width*0.26f;
        PointF pts[3] = { PointF((REAL)cx,(REAL)headY), PointF((REAL)(cx-headW),(REAL)(headY+dir*headW)), PointF((REAL)(cx+headW),(REAL)(headY+dir*headW)) };
        g.DrawLines(&pen, pts, 3);
    }

    // Circular arrow (ring + solid arrowhead) — "Packet loss" stat icon.
    void DrawRefreshIcon(Graphics& g, const Rect& r, Color color) {
        int cx = r.X + r.Width/2, cy = r.Y + r.Height/2;
        float radius = (std::min)(r.Width, r.Height) * 0.36f;
        float lw = (std::max)(1.5f, r.Width*0.12f);
        Pen pen(color, lw); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
        float startDeg = -50.0f, sweepDeg = 260.0f;
        g.DrawArc(&pen, (REAL)(cx-radius), (REAL)(cy-radius), (REAL)(radius*2), (REAL)(radius*2), startDeg, sweepDeg);
        // A clear solid triangular arrowhead at the open end of the arc —
        // a thin stroked chevron was too small to read at icon size and
        // just looked like a broken ring.
        double endA = (startDeg) * 3.14159265/180.0;
        REAL ax = cx + (REAL)(cos(endA))*radius, ay = cy + (REAL)(sin(endA))*radius;
        double tangent = endA - 3.14159265/2.0; // direction of travel along the arc at this point
        REAL dirX = (REAL)cos(tangent), dirY = (REAL)sin(tangent);
        REAL perpX = -dirY, perpY = dirX;
        REAL headLen = radius*0.75f, headW = radius*0.62f;
        PointF tip(ax + dirX*headLen*0.5f, ay + dirY*headLen*0.5f);
        PointF back1(ax - dirX*headLen*0.5f + perpX*headW*0.5f, ay - dirY*headLen*0.5f + perpY*headW*0.5f);
        PointF back2(ax - dirX*headLen*0.5f - perpX*headW*0.5f, ay - dirY*headLen*0.5f - perpY*headW*0.5f);
        SolidBrush headBrush(color);
        PointF tri[3] = { tip, back1, back2 };
        g.FillPolygon(&headBrush, tri, 3);
    }

    // Small zigzag wave — "Jitter" stat icon.
    void DrawWaveIcon(Graphics& g, const Rect& r, Color color) {
        float lw = (std::max)(1.3f, r.Width*0.11f);
        Pen pen(color, lw); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
        auto P = [&](double ux, double uy) { return PointF((REAL)(r.X + ux*r.Width), (REAL)(r.Y + uy*r.Height)); };
        PointF pts[5] = { P(0.05,0.65), P(0.30,0.30), P(0.50,0.70), P(0.72,0.28), P(0.95,0.55) };
        g.DrawLines(&pen, pts, 5);
    }

    // Cloud with a download arrow — "Real Telegram speed" card icon.
    void DrawCloudDownloadIcon(Graphics& g, const Rect& r, Color color) {
        float m = (float)(std::min)(r.Width, r.Height);
        int cx = r.X + r.Width/2, cy = r.Y + (int)(r.Height*0.42f);
        Pen pen(color, (std::max)(1.3f, m*0.10f)); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
        float r1 = m*0.20f, r2 = m*0.26f, r3 = m*0.20f;
        g.DrawArc(&pen, (REAL)(cx-m*0.36f-r1), (REAL)(cy-r1), (REAL)(r1*2), (REAL)(r1*2), 90.0f, 180.0f);
        g.DrawArc(&pen, (REAL)(cx-r2), (REAL)(cy-r2-m*0.06f), (REAL)(r2*2), (REAL)(r2*2), 180.0f, 180.0f);
        g.DrawArc(&pen, (REAL)(cx+m*0.14f), (REAL)(cy-r3), (REAL)(r3*2), (REAL)(r3*2), 270.0f, 180.0f);
        g.DrawLine(&pen, (REAL)(cx-m*0.36f), (REAL)cy, (REAL)(cx+m*0.34f), (REAL)cy);
        REAL ax = (REAL)cx, ay0 = (REAL)(cy+m*0.06f), ay1 = (REAL)(cy+m*0.42f);
        g.DrawLine(&pen, ax, ay0, ax, ay1);
        PointF head[3] = { PointF(ax-m*0.14f, ay1-m*0.14f), PointF(ax, ay1), PointF(ax+m*0.14f, ay1-m*0.14f) };
        g.DrawLines(&pen, head, 3);
    }

    // Simple, small flag swatches (not pixel-accurate flags, but readable at
    // small icon sizes — plain rectangles, no clip region, thick bands).
    void DrawFlagUS(Graphics& g, const Rect& r) {
        SolidBrush white(Color(255,255,255,255));
        g.FillRectangle(&white, r.X, r.Y, r.Width, r.Height);
        SolidBrush red(Color(255,0xB2,0x22,0x34));
        const int bands = 5; // 3 red + 2 white gaps, thick enough to read at small sizes
        int bandH = (std::max)(2, r.Height / bands);
        for (int i = 0; i < bands; i += 2) {
            int y = r.Y + i*bandH;
            int h = (i == bands-1) ? (r.Y + r.Height - y) : bandH;
            g.FillRectangle(&red, r.X, y, r.Width, h);
        }
        SolidBrush navy(Color(255,0x3C,0x3B,0x6E));
        g.FillRectangle(&navy, r.X, r.Y, (int)(r.Width*0.42f), bandH*2);
        Pen border(Color(60,0,0,0), 1.0f);
        g.DrawRectangle(&border, r.X, r.Y, r.Width-1, r.Height-1);
    }
    void DrawFlagRU(Graphics& g, const Rect& r) {
        int bandH = r.Height/3;
        SolidBrush white(Color(255,255,255,255)); g.FillRectangle(&white, r.X, r.Y, r.Width, bandH);
        SolidBrush blue(Color(255,0,57,166));     g.FillRectangle(&blue,  r.X, r.Y+bandH, r.Width, bandH);
        SolidBrush red(Color(255,213,43,30));     g.FillRectangle(&red,   r.X, r.Y+bandH*2, r.Width, r.Height-bandH*2);
        Pen border(Color(60,0,0,0), 1.0f);
        g.DrawRectangle(&border, r.X, r.Y, r.Width-1, r.Height-1);
    }
    void DrawClockIcon(Graphics& g, const Rect& r, Color color) {
        int cx = r.X + r.Width/2, cy = r.Y + r.Height/2;
        int radius = (std::min)(r.Width, r.Height)/2 - 3;
        Pen pen(color, 1.8f); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
        g.DrawEllipse(&pen, cx-radius, cy-radius, radius*2, radius*2);
        g.DrawLine(&pen, cx, cy, cx, cy - (int)(radius*0.6));
        g.DrawLine(&pen, cx, cy, cx + (int)(radius*0.4), cy);
    }

    // Simple filled lightning-bolt glyph, for the "Scan" action.
    void DrawFlashIcon(Graphics& g, const Rect& r, Color color, bool pointDown = false) {
        int w = r.Width, h = r.Height;
        auto P = [&](double ux, double uy) { return PointF((REAL)(r.X + ux*w), (REAL)(r.Y + (pointDown ? 1.0-uy : uy)*h)); };
        PointF pts[6] = { P(0.58,0.02), P(0.10,0.58), P(0.44,0.58), P(0.40,0.98), P(0.92,0.40), P(0.56,0.40) };
        SolidBrush b(color);
        g.FillPolygon(&b, pts, 6, FillModeWinding);
    }

    // Simple filled warning triangle with an exclamation mark.
    void DrawWarningIcon(Graphics& g, const Rect& r, Color color) {
        int w = r.Width, h = r.Height;
        auto P = [&](double ux, double uy) { return PointF((REAL)(r.X + ux*w), (REAL)(r.Y + uy*h)); };
        GraphicsPath tri;
        PointF t[3] = { P(0.5,0.06), P(0.06,0.92), P(0.94,0.92) };
        tri.AddPolygon(t, 3);
        Pen pen(color, 1.8f); pen.SetLineJoin(LineJoinRound); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
        g.DrawPath(&pen, &tri);
        g.DrawLine(&pen, P(0.5,0.40).X, P(0.5,0.40).Y, P(0.5,0.68).X, P(0.5,0.68).Y);
        SolidBrush dot(color);
        g.FillEllipse(&dot, P(0.5,0.79).X-1.6f, P(0.5,0.79).Y-1.6f, 3.2f, 3.2f);
    }

    // Crescent moon — "Dark mode" card icon. Built with a true region
    // subtraction (not even-odd fill, which left both leftover slivers
    // visible and looked like two crescents stuck together).
    void DrawMoonIcon(Graphics& g, const Rect& r, Color color) {
        int cx = r.X + r.Width/2, cy = r.Y + r.Height/2;
        float radius = (std::min)(r.Width, r.Height) * 0.34f;
        float offset = radius * 0.85f;
        GraphicsPath outer;
        outer.AddEllipse((REAL)(cx-radius), (REAL)(cy-radius), (REAL)(radius*2), (REAL)(radius*2));
        GraphicsPath cutter;
        float ccx = cx + offset, ccy = cy - offset*0.25f;
        cutter.AddEllipse((REAL)(ccx-radius), (REAL)(ccy-radius), (REAL)(radius*2), (REAL)(radius*2));
        Region reg(&outer);
        reg.Exclude(&cutter);
        SolidBrush b(color);
        g.FillRegion(&b, &reg);
    }

    // Sun with rays — "Light mode" card icon.
    void DrawSunIcon(Graphics& g, const Rect& r, Color color) {
        int cx = r.X + r.Width/2, cy = r.Y + r.Height/2;
        float radius = (std::min)(r.Width, r.Height) * 0.22f;
        float rayInner = radius * 1.35f, rayOuter = radius * 2.05f;
        Pen pen(color, (std::max)(1.5f, r.Width*0.07f)); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
        for (int i = 0; i < 8; ++i) {
            double a = i * (3.14159265/4);
            g.DrawLine(&pen, (REAL)(cx+cos(a)*rayInner), (REAL)(cy+sin(a)*rayInner), (REAL)(cx+cos(a)*rayOuter), (REAL)(cy+sin(a)*rayOuter));
        }
        SolidBrush b(color);
        g.FillEllipse(&b, cx-radius, cy-radius, radius*2, radius*2);
    }

    // Half-dark/half-light circle — "System default" card icon.
    void DrawSystemThemeIcon(Graphics& g, const Rect& r, Color darkColor, Color lightColor) {
        int cx = r.X + r.Width/2, cy = r.Y + r.Height/2;
        float radius = (std::min)(r.Width, r.Height) * 0.36f;
        SolidBrush lb(lightColor);
        g.FillEllipse(&lb, cx-radius, cy-radius, radius*2, radius*2);
        GraphicsPath half;
        half.AddPie((REAL)(cx-radius), (REAL)(cy-radius), (REAL)(radius*2), (REAL)(radius*2), 90.0f, 180.0f);
        SolidBrush db(darkColor);
        g.FillPath(&db, &half);
    }

    // Speedometer/gauge with a needle — "Speed Test" nav icon (matches the
    // Android app; it's a gauge, not a checkmark).
    void DrawCheckCircleIcon(Graphics& g, const Rect& r, Color color) {
        float size = (REAL)(std::min)(r.Width, r.Height);
        REAL cx = r.X + size*0.5f, cy = r.Y + size*0.56f;
        REAL radius = size*0.32f;
        REAL lw = (std::max)(1.6f, size*0.09f);
        Pen pen(color, lw); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
        g.DrawArc(&pen, cx-radius, cy-radius, radius*2, radius*2, 160.0f, 220.0f); // open at the top
        double needleA = -35.0 * 3.14159265/180.0;
        REAL nx = cx + (REAL)(cos(needleA))*radius*0.75f, ny = cy + (REAL)(sin(needleA))*radius*0.75f;
        g.DrawLine(&pen, cx, cy, nx, ny);
        REAL dotR = lw*0.9f;
        SolidBrush dot(color);
        g.FillEllipse(&dot, cx-dotR, cy-dotR, dotR*2, dotR*2);
    }


    void DrawHeartIcon(Graphics& g, const Rect& r, Color color) {
        int w = r.Width, h = r.Height;
        int s = (std::min)(w, h);
        int x0 = r.X + (w - s)/2, y0 = r.Y + (h - s)/2;
        auto P = [&](double ux, double uy) { return PointF((REAL)(x0 + ux*s), (REAL)(y0 + uy*s)); };
        GraphicsPath path;
        PointF bottom      = P(0.50, 0.92);
        PointF leftOuter   = P(0.03, 0.36);
        PointF notch       = P(0.50, 0.20);
        PointF rightOuter  = P(0.97, 0.36);
        path.StartFigure();
        path.AddBezier(bottom, P(0.18, 0.72), P(-0.05, 0.55), leftOuter);
        path.AddBezier(leftOuter, P(0.06, 0.08), P(0.35, 0.02), notch);
        path.AddBezier(notch, P(0.65, 0.02), P(0.94, 0.08), rightOuter);
        path.AddBezier(rightOuter, P(1.05, 0.55), P(0.82, 0.72), bottom);
        path.CloseFigure();
        SolidBrush b(color);
        g.FillPath(&b, &path);
    }

    // ---- Brand logos (official GitHub / Telegram marks), embedded as PNG
    // resources so the Support screen shows the real logos instead of a
    // generic star/paper-dart placeholder. ----
    Image* LoadPngResource(int resId) {
        HRSRC hRes = FindResourceW(g_hInst, MAKEINTRESOURCEW(resId), RT_RCDATA);
        if (!hRes) return nullptr;
        HGLOBAL hData = LoadResource(g_hInst, hRes);
        if (!hData) return nullptr;
        DWORD size = SizeofResource(g_hInst, hRes);
        void* src = LockResource(hData);
        if (!src || size == 0) return nullptr;
        HGLOBAL hBuf = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!hBuf) return nullptr;
        void* dst = GlobalLock(hBuf);
        memcpy(dst, src, size);
        GlobalUnlock(hBuf);
        IStream* stream = nullptr;
        if (CreateStreamOnHGlobal(hBuf, TRUE, &stream) != S_OK) { GlobalFree(hBuf); return nullptr; }
        Image* img = new Image(stream);
        stream->Release();
        return img;
    }

    Image* g_githubLogoDark = nullptr, *g_githubLogoLight = nullptr, *g_telegramLogo = nullptr;
    Image* g_flagUsImg = nullptr, *g_flagRuImg = nullptr;
    void EnsureBrandLogosLoaded() {
        if (!g_githubLogoDark)  g_githubLogoDark  = LoadPngResource(201);
        if (!g_githubLogoLight) g_githubLogoLight = LoadPngResource(202);
        if (!g_telegramLogo)    g_telegramLogo    = LoadPngResource(203);
        if (!g_flagUsImg)       g_flagUsImg       = LoadPngResource(204);
        if (!g_flagRuImg)       g_flagRuImg       = LoadPngResource(205);
    }
    void DrawBrandLogo(Graphics& g, Image* img, const Rect& r) {
        if (!img) return;
        g.DrawImage(img, r.X, r.Y, r.Width, r.Height);
    }


    int DrawBadge(Graphics& g, const FontFamily& ff, const std::wstring& text, int x, int y, Color bg, Color fg) {
        Font f(&ff, 7.5f, FontStyleBold, UnitPoint);
        RectF bounds; g.MeasureString(text.c_str(), -1, &f, PointF(0, 0), &bounds);
        int w = (int)bounds.Width + 14, h = 18;
        Rect r(x, y, w, h);
        SolidBrush bgBrush(bg);
        GraphicsPath* p = RoundedRectPath(r, 6); g.FillPath(&bgBrush, p); delete p;
        SolidBrush fgBrush(fg);
        StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(text.c_str(), -1, &f, RectF((REAL)r.X,(REAL)r.Y,(REAL)r.Width,(REAL)r.Height), &fmt, &fgBrush);
        return x + w + 6;
    }

    void DrawProxyCard(Graphics& g, const FontFamily& ff, const FontFamily& mono,
                        const ProxyItem& item, const Rect& cardRect, std::vector<CardHit>& hits) {
        bool dark = IsDarkTheme();
        SolidBrush cardBg(FromColorref(g_palette->surfaceVariant));
        GraphicsPath* cp = RoundedRectPath(cardRect, Theme::CornerRadiusCard);
        g.FillPath(&cardBg, cp);
        Pen border(dark ? Color(30,255,255,255) : FromColorref(g_palette->tertiary), 1.0f);
        g.DrawPath(&border, cp); delete cp;

        int pad = 14, cx = cardRect.X + pad, cy = cardRect.Y + pad;
        Font titleFont(&ff, 12, FontStyleBold, UnitPoint);
        SolidBrush titleBrush(FromColorref(g_palette->onSurface));
        std::wstring title = L"Proxy " + std::to_wstring(item.id);
        g.DrawString(title.c_str(), -1, &titleFont, PointF((REAL)cx, (REAL)cy), &titleBrush);
        RectF titleBounds; g.MeasureString(title.c_str(), -1, &titleFont, PointF(0,0), &titleBounds);
        int bx = cx + (int)titleBounds.Width + 10;

        if (!item.isScanned) bx = DrawBadge(g, ff, L"TESTING", bx, cy+1, FromColorrefAlpha(g_palette->secondary,140), FromColorref(g_palette->primary));
        else if (item.IsAlive()) bx = DrawBadge(g, ff, TT(Loc::Key::Online), bx, cy+1, Color(255,0xE2,0xF5,0xEA), Color(255,0x1B,0x5E,0x20));
        else bx = DrawBadge(g, ff, TT(Loc::Key::Offline), bx, cy+1, FromColorrefAlpha(g_palette->errorContainer,180), FromColorref(g_palette->error));
        if (item.isForDownload) bx = DrawBadge(g, ff, TT(Loc::Key::ForDownloadBadge), bx, cy+1, Color(255,0xE8,0xF0,0xFE), Color(255,0x19,0x67,0xD2));
        if (item.isRussian) bx = DrawBadge(g, ff, TT(Loc::Key::RussianBadge), bx, cy+1, Color(255,0xFF,0xF3,0xE0), Color(255,0xE6,0x51,0x00));

        Font monoFont(&mono, 9.5f, FontStyleRegular, UnitPoint);
        SolidBrush monoBrush(FromColorrefAlpha(g_palette->onSurfaceVariant, 200));
        std::wstring serverLine = item.server + L":" + item.port;
        g.DrawString(serverLine.c_str(), -1, &monoFont, PointF((REAL)cx, (REAL)(cy+22)), &monoBrush);

        int rightEdge = cardRect.X + cardRect.Width - pad;
        Font bigFont(&ff, 16, FontStyleBold, UnitPoint);
        Font smallFont(&ff, 7.5f, FontStyleBold, UnitPoint);
        StringFormat rightFmt; rightFmt.SetAlignment(StringAlignmentFar);
        if (!item.isScanned) {
            SolidBrush dim(FromColorrefAlpha(g_palette->onSurfaceVariant,110));
            g.DrawString(L"...", -1, &bigFont, RectF((REAL)(rightEdge-90),(REAL)cy,90,22), &rightFmt, &dim);
            g.DrawString(L"TESTING", -1, &smallFont, RectF((REAL)(rightEdge-90),(REAL)(cy+20),90,14), &rightFmt, &dim);
        } else if (item.IsAlive()) {
            SolidBrush latBrush(LatencyColor(item.pingMs));
            std::wstring msText = std::to_wstring(item.pingMs) + L" ms";
            g.DrawString(msText.c_str(), -1, &bigFont, RectF((REAL)(rightEdge-90),(REAL)cy,90,22), &rightFmt, &latBrush);
            SolidBrush dim(FromColorrefAlpha(g_palette->onSurfaceVariant,150));
            g.DrawString(L"LATENCY", -1, &smallFont, RectF((REAL)(rightEdge-90),(REAL)(cy+20),90,14), &rightFmt, &dim);
        } else {
            SolidBrush dim(FromColorrefAlpha(g_palette->onSurfaceVariant,110));
            g.DrawString(L"--", -1, &bigFont, RectF((REAL)(rightEdge-90),(REAL)cy,90,22), &rightFmt, &dim);
            g.DrawString(L"TIMEOUT", -1, &smallFont, RectF((REAL)(rightEdge-90),(REAL)(cy+20),90,14), &rightFmt, &dim);
        }

        int dividerY = cy + 44;
        Pen dividerPen(FromColorrefAlpha(g_palette->tertiary, dark?40:200), 1.0f);
        g.DrawLine(&dividerPen, cardRect.X+pad, dividerY, cardRect.X+cardRect.Width-pad, dividerY);

        int actionY = dividerY + 10;
        Rect copyRect(cx, actionY, 22, 22);
        Rect bookmarkRect(cx+26, actionY, 22, 22);
        DrawCopyIcon(g, copyRect, FromColorrefAlpha(g_palette->onSurfaceVariant, 200));
        DrawBookmarkIcon(g, bookmarkRect, item.isFavorite ? FromColorref(g_palette->primary) : FromColorrefAlpha(g_palette->onSurfaceVariant, 200), item.isFavorite);

        int connectW = 104;
        Rect connectRect(cardRect.X + cardRect.Width - pad - connectW, actionY - 2, connectW, 30);
        SolidBrush connectBg(FromColorref(g_palette->primary));
        GraphicsPath* conPath = RoundedRectPath(connectRect, 10); g.FillPath(&connectBg, conPath); delete conPath;
        Font connectFont(&ff, 9.5f, FontStyleBold, UnitPoint);
        SolidBrush connectText(Color(255,255,255,255));
        std::wstring connectLabel = TT(Loc::Key::Connect);
        RectF connLabelSize; g.MeasureString(connectLabel.c_str(), -1, &connectFont, PointF(0,0), &connLabelSize);
        float chevSize = 13, chevGap = 5;
        float connGroupW = chevSize + chevGap + connLabelSize.Width;
        float connStartX = connectRect.X + (connectRect.Width - connGroupW) / 2.0f;
        float connMidY = connectRect.Y + connectRect.Height / 2.0f;
        bool cardRtl = IsRTL();
        Rect chevRect(cardRtl ? (int)connStartX : (int)(connStartX + connLabelSize.Width + chevGap),
                      (int)(connMidY - chevSize/2), (int)chevSize, (int)chevSize);
        DrawChevron(g, chevRect, Color(255,255,255,255));
        StringFormat connLf; connLf.SetLineAlignment(StringAlignmentCenter);
        float connTextX = cardRtl ? (connStartX + chevSize + chevGap) : connStartX;
        g.DrawString(connectLabel.c_str(), -1, &connectFont, RectF(connTextX, (REAL)connectRect.Y, connLabelSize.Width+4, (REAL)connectRect.Height), &connLf, &connectText);

        hits.push_back({ item.Key(), item.link, cardRect, copyRect, bookmarkRect, connectRect });
    }

    void StartPing(HWND hWnd) {
        g_pingInProgress = true;
        std::thread([hWnd]() {
            std::vector<ProxyItem>* itemsPtr;
            { std::lock_guard<std::mutex> lock(g_proxiesMutex); itemsPtr = &g_proxies; }
            PingService::PingAll(*itemsPtr, [hWnd](size_t) { PostMessageW(hWnd, WM_APP_PING_PROGRESS, 0, 0); });
            PostMessageW(hWnd, WM_APP_PING_PROGRESS, 1, 0);
        }).detach();
    }

    void StartFetch(HWND hWnd) {
        g_proxyListState = ProxyListState::Loading;
        g_spinnerAngle = 0.0f;
        g_homeScrollY = 0;
        SetTimer(hWnd, kSpinnerTimerId, kSpinnerIntervalMs, nullptr);
        std::thread([hWnd]() {
            ProxySource::FetchResult result = ProxySource::Fetch();
            if (result.success) {
                std::vector<ProxyItem> parsed = ProxyParser::Parse(result.rawText);
                for (auto& item : parsed) item.isFavorite = g_favoriteKeys.count(item.Key()) > 0;
                std::lock_guard<std::mutex> lock(g_proxiesMutex);
                g_proxies = std::move(parsed);
            }
            { std::lock_guard<std::mutex> lock(g_fetchMutex); g_lastFetch = result; }
            PostMessageW(hWnd, WM_APP_FETCH_DONE, 0, 0);
        }).detach();
    }

    void StartBannerFetch(HWND hWnd) {
        if (g_bannerFetchStarted) return;
        g_bannerFetchStarted = true;
        std::thread([hWnd]() {
            auto items = BannerSlideshow::FetchBannerItems();
            std::vector<Image*> decoded;
            std::vector<std::wstring> links;
            for (size_t i = 0; i < items.size() && decoded.size() < 5; ++i) {
                std::string bytes;
                if (BannerSlideshow::HttpGetBytes(items[i].imageUrl, bytes)) {
                    Image* img = BannerSlideshow::DecodeImage(bytes);
                    if (img) { decoded.push_back(img); links.push_back(items[i].targetLink); }
                }
            }
            if (!decoded.empty()) {
                std::lock_guard<std::mutex> lock(g_bannerMutex);
                g_bannerImages = std::move(decoded);
                g_bannerLinks = std::move(links);
            }
            PostMessageW(hWnd, WM_APP_BANNERS_READY, 0, 0);
        }).detach();
    }

    void RunSpeedTestAsync(HWND hWnd, SpeedTester::ParsedProxy proxy) {
        g_speedTesting = true;
        g_speedHasResult = false;
        std::thread([hWnd, proxy]() {
            SpeedTester::Result r = SpeedTester::Run(proxy, 8);
            { std::lock_guard<std::mutex> lock(g_fetchMutex); g_speedResult = r; }
            PostMessageW(hWnd, WM_APP_SPEEDTEST_DONE, 0, 0);
        }).detach();
    }

    // ---------------------------------------------------------------------
    void Paint(HDC hdc, RECT client) {
        int w = client.right - client.left;
        int h = client.bottom - client.top;
        if (w <= 0 || h <= 0) return;
        EnsureBackBuffer(w, h);

        Graphics gBuf(g_backBuffer);
        gBuf.SetSmoothingMode(SmoothingModeAntiAlias);
        gBuf.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        gBuf.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        gBuf.SetPixelOffsetMode(PixelOffsetModeHighQuality);

        SolidBrush bgBrush(FromColorref(g_palette->background));
        gBuf.FillRectangle(&bgBrush, 0, 0, w, h);

        FontFamily fontFamily = ResolveFontFamily(Theme::FontFamily);
        FontFamily monoFamily = ResolveFontFamily(Theme::MonoFontFamily);

        // ============================================================= TOP BAR
        SolidBrush topBg(FromColorref(g_palette->background));
        gBuf.FillRectangle(&topBg, 0, 0, w, Theme::TopBarHeight);

        if (g_currentScreen == Screen::Home) {
            bool rtl = IsRTL();
            Font titleFont(&fontFamily, 17, FontStyleBold, UnitPoint);
            SolidBrush titleBrush(FromColorref(g_palette->primary));
            Font statusFont(&fontFamily, 8, FontStyleBold, UnitPoint);
            SolidBrush statusBrush(FromColorrefAlpha(g_palette->onSurfaceVariant, 160));
            SolidBrush dotBrush(FromColorrefAlpha(RGB(0x4C,0xAF,0x50), (BYTE)(g_pulseAlpha*255)));
            int aliveCount = 0;
            { std::lock_guard<std::mutex> lock(g_proxiesMutex); for (auto& it : g_proxies) if (it.IsAlive()) aliveCount++; }
            std::wstring statusText = TT(Loc::Key::SystemReady) + L" \u00B7 " + std::to_wstring(aliveCount);

            if (!rtl) {
                gBuf.DrawString(TT(Loc::Key::AppName).c_str(), -1, &titleFont, PointF(20, 10), &titleBrush);
                gBuf.FillEllipse(&dotBrush, 20, 52, 8, 8);
                gBuf.DrawString(statusText.c_str(), -1, &statusFont, PointF(34, 51), &statusBrush);

                g_settingsBtnRect = Rect(w-20-40, 19, 40, 40);
            } else {
                StringFormat rf; rf.SetAlignment(StringAlignmentFar);
                gBuf.DrawString(TT(Loc::Key::AppName).c_str(), -1, &titleFont, RectF(0, 10, (REAL)(w-20), 28), &rf, &titleBrush);
                gBuf.FillEllipse(&dotBrush, w-28, 52, 8, 8);
                StringFormat rfs; rfs.SetAlignment(StringAlignmentFar);
                gBuf.DrawString(statusText.c_str(), -1, &statusFont, RectF(0, 51, (REAL)(w-40), 16), &rfs, &statusBrush);

                g_settingsBtnRect = Rect(20, 19, 40, 40);
            }
            SolidBrush settingsBg(IsDarkTheme() ? FromColorref(g_palette->surfaceVariant) : Color(255,0xF3,0xF4,0xF9));
            GraphicsPath* sp = RoundedRectPath(g_settingsBtnRect, 20); gBuf.FillPath(&settingsBg, sp); delete sp;
            DrawGearIcon(gBuf, g_settingsBtnRect, FromColorref(g_palette->primary),
                         IsDarkTheme() ? FromColorref(g_palette->surfaceVariant) : Color(255,0xF3,0xF4,0xF9));
        } else {
            bool rtl = IsRTL();
            std::wstring screenTitle =
                g_currentScreen == Screen::Settings  ? TT(Loc::Key::Settings) :
                g_currentScreen == Screen::Saved     ? TT(Loc::Key::SavedProxies) :
                g_currentScreen == Screen::SpeedTest ? TT(Loc::Key::ProxySpeedTest) : TT(Loc::Key::SupportTitle);
            Font titleFont(&fontFamily, 15, FontStyleBold, UnitPoint);
            SolidBrush titleBrush(FromColorref(g_palette->onSurface));

            if (!rtl) {
                g_backBtnRect = Rect(16, 12, 40, 40);
                SolidBrush backBg(IsDarkTheme() ? FromColorref(g_palette->surfaceVariant) : Color(255,0xF3,0xF4,0xF9));
                GraphicsPath* bbp = RoundedRectPath(g_backBtnRect, 20); gBuf.FillPath(&backBg, bbp); delete bbp;
                DrawBackArrow(gBuf, g_backBtnRect, FromColorref(g_palette->primary));
                gBuf.DrawString(screenTitle.c_str(), -1, &titleFont, PointF(68, 22), &titleBrush);
            } else {
                g_backBtnRect = Rect(w-20-40, 12, 40, 40);
                SolidBrush backBg(IsDarkTheme() ? FromColorref(g_palette->surfaceVariant) : Color(255,0xF3,0xF4,0xF9));
                GraphicsPath* bbp = RoundedRectPath(g_backBtnRect, 20); gBuf.FillPath(&backBg, bbp); delete bbp;
                DrawForwardArrow(gBuf, g_backBtnRect, FromColorref(g_palette->primary));
                StringFormat rf; rf.SetAlignment(StringAlignmentFar); rf.SetLineAlignment(StringAlignmentCenter);
                // (No forced RTL direction flag — GDI+ already runs the
                // Unicode Bidi algorithm on this logically-ordered string,
                // so forcing direction here double-reverses embedded
                // Latin/number runs like "GitHub" or "85.0 Mbps".)
                gBuf.DrawString(screenTitle.c_str(), -1, &titleFont, RectF(20, 10, (REAL)(w-20-40-16-20), 44), &rf, &titleBrush);
            }
        }
        Pen topBorder(FromColorrefAlpha(g_palette->tertiary, 60), 1.0f);
        gBuf.DrawLine(&topBorder, 0, Theme::TopBarHeight, w, Theme::TopBarHeight);

        // ============================================================= BOTTOM BAR (Home only)
        int contentBottom = h;
        if (g_currentScreen == Screen::Home) {
            contentBottom = h - Theme::BottomBarHeight;
            SolidBrush bottomBg(FromColorref(g_palette->surface));
            GraphicsPath* bp = RoundedRectPath(Rect(0, contentBottom, w, Theme::BottomBarHeight+40), 24);
            gBuf.FillPath(&bottomBg, bp); delete bp;
            gBuf.FillRectangle(&bottomBg, 0, contentBottom, w, 24);

            g_scanBtnRect = Rect(20, contentBottom+16, w-40, 46);
            bool loading = (g_proxyListState == ProxyListState::Loading) || g_pingInProgress;
            SolidBrush scanBg(FromColorref(g_palette->primary));
            GraphicsPath* scp = RoundedRectPath(g_scanBtnRect, 20); gBuf.FillPath(&scanBg, scp); delete scp;
            Font scanFont(&fontFamily, 11, FontStyleBold, UnitPoint);
            SolidBrush scanText(Color(255,255,255,255));
            std::wstring scanLabel = loading ? TT(Loc::Key::Scanning) : TT(Loc::Key::ScanProxies);
            RectF textSize; gBuf.MeasureString(scanLabel.c_str(), -1, &scanFont, PointF(0,0), &textSize);
            float iconSize = 17, gap = 8;
            float groupW = iconSize + gap + textSize.Width;
            float startX = g_scanBtnRect.X + (g_scanBtnRect.Width - groupW) / 2.0f;
            float midY = g_scanBtnRect.Y + g_scanBtnRect.Height / 2.0f;
            bool rtl = IsRTL();
            Rect iconRect(rtl ? (int)(startX + textSize.Width + gap) : (int)startX,
                          (int)(midY - iconSize/2), (int)iconSize, (int)iconSize);
            DrawFlashIcon(gBuf, iconRect, Color(255,255,255,255));
            StringFormat lf; lf.SetLineAlignment(StringAlignmentCenter);
            float textX = rtl ? startX : (startX + iconSize + gap);
            gBuf.DrawString(scanLabel.c_str(), -1, &scanFont, RectF(textX, (REAL)g_scanBtnRect.Y, textSize.Width+4, (REAL)g_scanBtnRect.Height), &lf, &scanText);
        }

        Rect content(0, Theme::TopBarHeight, w, contentBottom - Theme::TopBarHeight);

        // ============================================================= HOME
        if (g_currentScreen == Screen::Home) {
            // Clip to content area so scrolled content never overlaps top/bottom bars.
            Region oldClip; gBuf.GetClip(&oldClip);
            gBuf.SetClip(RectF((REAL)content.X, (REAL)content.Y, (REAL)content.Width, (REAL)content.Height));

            int cy = content.Y - g_homeScrollY + 12;
            int startCy = cy;

            // Banner slideshow (if any images were fetched).
            g_bannerRectValid = false;
            {
                std::lock_guard<std::mutex> lock(g_bannerMutex);
                if (g_settings.bannerEnabled && !g_bannerImages.empty()) {
                    // True responsive sizing: the banner's height is derived
                    // from *this image's own aspect ratio* at the current
                    // window width (contain-fit, not cover-fit) — so the
                    // whole banner is always shown intact, never cropped,
                    // and scales smoothly as the window is resized.
                    Image* img = g_bannerImages[g_bannerIndex % g_bannerImages.size()];
                    int bannerW = w - 32;
                    double imgAr = (img->GetHeight() > 0) ? ((double)img->GetWidth() / img->GetHeight()) : 2.2;
                    int bannerH = (int)(bannerW / imgAr);
                    bannerH = (std::max)(70, (std::min)(bannerH, 220));
                    Rect bannerRect(16, cy, bannerW, bannerH);
                    g_bannerRect = bannerRect;
                    g_bannerRectValid = true;
                    GraphicsPath* clipPath = RoundedRectPath(bannerRect, Theme::CornerRadiusCard);
                    Region bannerClip(clipPath);
                    Region prevClip; gBuf.GetClip(&prevClip);
                    gBuf.SetClip(&bannerClip, CombineModeIntersect);
                    // Contain-fit within the (now aspect-matched) box — in the
                    // rare case bannerH got clamped, this still avoids any
                    // stretching by centering with small letterbox bars.
                    double boxAr = (double)bannerRect.Width / bannerRect.Height;
                    int dw, dh, dx, dy;
                    if (imgAr > boxAr) { dw = bannerRect.Width; dh = (int)(dw / imgAr); dx = bannerRect.X; dy = bannerRect.Y + (bannerRect.Height - dh)/2; }
                    else { dh = bannerRect.Height; dw = (int)(dh * imgAr); dx = bannerRect.X + (bannerRect.Width - dw)/2; dy = bannerRect.Y; }
                    SolidBrush letterbox(FromColorref(g_palette->surfaceVariant));
                    gBuf.FillRectangle(&letterbox, bannerRect);
                    gBuf.DrawImage(img, dx, dy, dw, dh);
                    gBuf.SetClip(&prevClip);
                    delete clipPath;
                    // Dot indicators
                    int dotsW = (int)g_bannerImages.size() * 12;
                    int dotX = bannerRect.X + bannerRect.Width/2 - dotsW/2;
                    for (size_t i = 0; i < g_bannerImages.size(); ++i) {
                        SolidBrush dot(i == (size_t)(g_bannerIndex % g_bannerImages.size()) ? Color(255,255,255,255) : Color(120,255,255,255));
                        gBuf.FillEllipse(&dot, dotX + (int)i*12, bannerRect.Y + bannerRect.Height - 14, 6, 6);
                    }
                    cy += bannerRect.Height + 10;
                }
            }

            // Disclaimer card (exact string from strings.xml)
            Rect discRect(16, cy, w - 32, 0);
            {
                Font discFont(&fontFamily, 9, FontStyleRegular, UnitPoint);
                std::wstring discText = TT(Loc::Key::Disclaimer);
                RectF measured;
                gBuf.MeasureString(discText.c_str(), -1, &discFont, RectF(0,0,(REAL)(discRect.Width-28),999), &measured);
                discRect.Height = (int)measured.Height + 28;
                SolidBrush discBg(FromColorrefAlpha(g_palette->tertiary, IsDarkTheme()?50:210));
                GraphicsPath* dp = RoundedRectPath(discRect, Theme::CornerRadiusLarge);
                gBuf.FillPath(&discBg, dp); delete dp;
                SolidBrush discTextBrush(FromColorrefAlpha(g_palette->onSurfaceVariant, 220));
                StringFormat discFmt; if (IsRTL()) { discFmt.SetAlignment(StringAlignmentFar); }
                gBuf.DrawString(discText.c_str(), -1, &discFont, RectF((REAL)(discRect.X+14),(REAL)(discRect.Y+12),(REAL)(discRect.Width-28),(REAL)(discRect.Height-20)), &discFmt, &discTextBrush);
                cy += discRect.Height + 10;
            }

            // Offline/cache notice — shown when the current list came from the
            // local disk cache because both live sources failed (mirrors the
            // Android app's offline_notice string).
            {
                ProxySource::FetchOrigin origin;
                { std::lock_guard<std::mutex> lock(g_fetchMutex); origin = g_lastFetch.origin; }
                if (origin == ProxySource::FetchOrigin::Cache && g_proxyListState == ProxyListState::Loaded) {
                    Rect noticeRect(16, cy, w - 32, 0);
                    Font noticeFont(&fontFamily, 9, FontStyleBold, UnitPoint);
                    std::wstring noticeText = TT(Loc::Key::OfflineNotice);
                    int iconGutter = 30; // reserved for the warning glyph
                    RectF measured;
                    gBuf.MeasureString(noticeText.c_str(), -1, &noticeFont, RectF(0,0,(REAL)(noticeRect.Width-28-iconGutter),999), &measured);
                    noticeRect.Height = (int)measured.Height + 24;
                    SolidBrush noticeBg(FromColorrefAlpha(g_palette->errorContainer, IsDarkTheme()?70:210));
                    GraphicsPath* np = RoundedRectPath(noticeRect, Theme::CornerRadiusLarge);
                    gBuf.FillPath(&noticeBg, np); delete np;
                    bool rtl = IsRTL();
                    Rect warnIconRect(rtl ? (noticeRect.X + noticeRect.Width - 14 - 18) : (noticeRect.X + 14), noticeRect.Y + 12, 18, 18);
                    DrawWarningIcon(gBuf, warnIconRect, FromColorref(g_palette->error));
                    SolidBrush noticeTextBrush(FromColorref(g_palette->error));
                    StringFormat noticeFmt; if (rtl) { noticeFmt.SetAlignment(StringAlignmentFar); }
                    REAL textX = rtl ? (REAL)(noticeRect.X + 14) : (REAL)(noticeRect.X + 14 + iconGutter);
                    gBuf.DrawString(noticeText.c_str(), -1, &noticeFont, RectF(textX,(REAL)(noticeRect.Y+10),(REAL)(noticeRect.Width-28-iconGutter),(REAL)(noticeRect.Height-16)), &noticeFmt, &noticeTextBrush);
                    cy += noticeRect.Height + 10;
                }
            }

            g_visibleCards.clear();
            if (g_proxyListState == ProxyListState::Loading) {
                int cxs = w/2, cys = cy + 60, r = 18;
                Pen spinnerPen(FromColorref(g_palette->primary), 3.0f);
                spinnerPen.SetStartCap(LineCapRound); spinnerPen.SetEndCap(LineCapRound);
                gBuf.DrawArc(&spinnerPen, cxs-r, cys-r, r*2, r*2, g_spinnerAngle, 100);
                cy += 130;
            } else if (g_proxyListState == ProxyListState::Error) {
                Font tFont(&fontFamily, 12, FontStyleBold, UnitPoint);
                SolidBrush tBrush(FromColorref(g_palette->error));
                StringFormat cf; cf.SetAlignment(StringAlignmentCenter);
                gBuf.DrawString(TT(Loc::Key::ErrorLoadTitle).c_str(), -1, &tFont, RectF(0,(REAL)cy,(REAL)w,24), &cf, &tBrush);
                cy += 60;
            } else if (g_proxyListState == ProxyListState::Empty) {
                Font tFont(&fontFamily, 11, FontStyleBold, UnitPoint);
                SolidBrush tBrush(FromColorrefAlpha(g_palette->onSurfaceVariant,200));
                StringFormat cf; cf.SetAlignment(StringAlignmentCenter);
                gBuf.DrawString(TT(Loc::Key::NoProxiesFound).c_str(), -1, &tFont, RectF(0,(REAL)cy,(REAL)w,24), &cf, &tBrush);
                cy += 60;
            } else {
                std::vector<ProxyItem> list = GetHomeProxies();
                if (list.empty() && !g_pingInProgress) {
                    // Every fetched proxy turned out dead once pinged, and
                    // dead entries are hidden — tell the user instead of
                    // leaving a blank screen.
                    Font tFont(&fontFamily, 11, FontStyleBold, UnitPoint);
                    SolidBrush tBrush(FromColorrefAlpha(g_palette->onSurfaceVariant,200));
                    StringFormat cf; cf.SetAlignment(StringAlignmentCenter);
                    gBuf.DrawString(TT(Loc::Key::NoProxiesFound).c_str(), -1, &tFont, RectF(0,(REAL)cy,(REAL)w,24), &cf, &tBrush);
                    cy += 60;
                } else {
                    for (const auto& item : list) {
                        int cardH = 116;
                        Rect cardRect(16, cy, w-32, cardH);
                        DrawProxyCard(gBuf, fontFamily, monoFamily, item, cardRect, g_visibleCards);
                        cy += cardH + 10;
                    }
                }
            }
            cy += 24; // breathing room so the last card/button isn't flush against the bottom edge
            g_homeContentHeight = (cy - startCy) + 24; // + bottom padding so content never ends flush at the last card
            gBuf.SetClip(&oldClip);

            // Clamp scroll now that we know content height.
            int maxScroll = (std::max)(0, g_homeContentHeight - content.Height);
            if (g_homeScrollY > maxScroll) g_homeScrollY = maxScroll;
            if (g_homeScrollY < 0) g_homeScrollY = 0;
        }
        // ============================================================= SAVED
        else if (g_currentScreen == Screen::Saved) {
            Region oldClip; gBuf.GetClip(&oldClip);
            gBuf.SetClip(RectF((REAL)content.X,(REAL)content.Y,(REAL)content.Width,(REAL)content.Height));
            std::vector<ProxyItem> favorites = GetFavoriteProxies();
            g_visibleCards.clear();
            int cy = content.Y - g_savedScrollY + 12;
            int startCy = cy;
            if (favorites.empty()) {
                Font tFont(&fontFamily, 11, FontStyleBold, UnitPoint);
                Font mFont(&fontFamily, 9.5f, FontStyleRegular, UnitPoint);
                SolidBrush tBrush(FromColorref(g_palette->onSurface));
                SolidBrush mBrush(FromColorrefAlpha(g_palette->onSurfaceVariant,200));
                StringFormat cf; cf.SetAlignment(StringAlignmentCenter);
                gBuf.DrawString(TT(Loc::Key::NoSavedProxies).c_str(), -1, &tFont, RectF(0,(REAL)(content.Y+content.Height/3),(REAL)w,24), &cf, &tBrush);
                gBuf.DrawString(TT(Loc::Key::TapStarHint).c_str(), -1, &mFont, RectF(20,(REAL)(content.Y+content.Height/3+26),(REAL)(w-40),24), &cf, &mBrush);
            } else {
                for (const auto& item : favorites) {
                    int cardH = 116;
                    Rect cardRect(16, cy, w-32, cardH);
                    DrawProxyCard(gBuf, fontFamily, monoFamily, item, cardRect, g_visibleCards);
                    cy += cardH + 10;
                }
            }
            cy += 24; // breathing room so the last card/button isn't flush against the bottom edge
            g_savedContentHeight = (cy - startCy) + 24; // + bottom padding
            gBuf.SetClip(&oldClip);
            int maxScroll = (std::max)(0, g_savedContentHeight - content.Height);
            if (g_savedScrollY > maxScroll) g_savedScrollY = maxScroll;
            if (g_savedScrollY < 0) g_savedScrollY = 0;
        }
        // ============================================================= SPEED TEST
        else if (g_currentScreen == Screen::SpeedTest) {
            Region speedOldClip; gBuf.GetClip(&speedOldClip);
            gBuf.SetClip(RectF((REAL)content.X, (REAL)content.Y, (REAL)content.Width, (REAL)content.Height));
            int cy = content.Y - g_speedScrollY + 16;
            int speedStartCy = cy;

            bool rtl = IsRTL();

            // Input field (with a small clear "X" button and a real editable
            // text cursor — click to position the caret, type/backspace/
            // delete/arrow-keys all work at the cursor, not just at the end).
            int clearBtnSz = 26;
            g_speedInputRect = Rect(20, cy, w-40, 42);
            SolidBrush inputBg(FromColorref(g_palette->surfaceVariant));
            GraphicsPath* ip = RoundedRectPath(g_speedInputRect, 12); gBuf.FillPath(&inputBg, ip); delete ip;
            if (g_speedInputFocused) {
                Pen focusPen(FromColorref(g_palette->primary), 1.5f);
                GraphicsPath* fp = RoundedRectPath(g_speedInputRect, 12); gBuf.DrawPath(&focusPen, fp); delete fp;
            }
            Font inputFont(&fontFamily, 9.5f, FontStyleRegular, UnitPoint);
            // Clear button sits on the side opposite where the text starts.
            g_speedClearBtnRect = Rect(rtl ? g_speedInputRect.X+6 : g_speedInputRect.X+g_speedInputRect.Width-6-clearBtnSz,
                                        g_speedInputRect.Y + (g_speedInputRect.Height-clearBtnSz)/2, clearBtnSz, clearBtnSz);
            if (g_speedInput.empty()) {
                SolidBrush ph(FromColorrefAlpha(g_palette->onSurfaceVariant, 150));
                StringFormat hf; if (rtl) hf.SetAlignment(StringAlignmentFar);
                int hintX = rtl ? g_speedInputRect.X : g_speedInputRect.X+12;
                int hintW = g_speedInputRect.Width - 16;
                gBuf.DrawString(TT(Loc::Key::ProxyInputHint).c_str(), -1, &inputFont, RectF((REAL)hintX,(REAL)(g_speedInputRect.Y+14),(REAL)hintW,18), &hf, &ph);
            } else {
                Region prevClip; gBuf.GetClip(&prevClip);
                int textAreaX = g_speedInputRect.X + (rtl ? clearBtnSz+8 : 12);
                int textAreaW = g_speedInputRect.Width - clearBtnSz - 20;
                gBuf.SetClip(RectF((REAL)textAreaX, (REAL)(g_speedInputRect.Y+2), (REAL)textAreaW, (REAL)(g_speedInputRect.Height-4)), CombineModeIntersect);
                SolidBrush tx(FromColorref(g_palette->onSurface));
                size_t cursor = (std::min)(g_speedInputCursor, g_speedInput.size());
                std::wstring before = g_speedInput.substr(0, cursor);
                std::wstring after  = g_speedInput.substr(cursor);
                RectF beforeM; gBuf.MeasureString(before.c_str(), -1, &inputFont, PointF(0,0), &beforeM);
                // Horizontal scroll offset so the caret always stays visible.
                static float scrollPx = 0;
                if (!g_speedInputFocused) scrollPx = 0;
                float caretX = beforeM.Width;
                if (caretX - scrollPx > textAreaW - 8) scrollPx = caretX - textAreaW + 8;
                if (caretX - scrollPx < 0) scrollPx = caretX;
                float drawX = textAreaX - scrollPx;
                gBuf.DrawString(g_speedInput.c_str(), -1, &inputFont, PointF(drawX, (REAL)(g_speedInputRect.Y+14)), &tx);
                if (g_speedInputFocused) {
                    Pen caretPen(FromColorref(g_palette->primary), 1.4f);
                    REAL cxp = drawX + caretX;
                    gBuf.DrawLine(&caretPen, cxp, (REAL)(g_speedInputRect.Y+9), cxp, (REAL)(g_speedInputRect.Y+33));
                }
                gBuf.SetClip(&prevClip);
            }
            // Clear (X) button — only shown once there's something to clear.
            if (!g_speedInput.empty()) {
                Color xColor = FromColorrefAlpha(g_palette->onSurfaceVariant, 200);
                int cx0 = g_speedClearBtnRect.X + g_speedClearBtnRect.Width/2, cy0 = g_speedClearBtnRect.Y + g_speedClearBtnRect.Height/2;
                Pen xPen(xColor, 1.6f); xPen.SetStartCap(LineCapRound); xPen.SetEndCap(LineCapRound);
                int s = 5;
                gBuf.DrawLine(&xPen, cx0-s, cy0-s, cx0+s, cy0+s);
                gBuf.DrawLine(&xPen, cx0-s, cy0+s, cx0+s, cy0-s);
            }
            cy += 42 + 12;

            if (!g_speedErrorMsg.empty()) {
                Font eFont(&fontFamily, 9, FontStyleRegular, UnitPoint);
                SolidBrush eBrush(FromColorref(g_palette->error));
                StringFormat ef; if (rtl) ef.SetAlignment(StringAlignmentFar);
                gBuf.DrawString(g_speedErrorMsg.c_str(), -1, &eFont, RectF(20,(REAL)cy,(REAL)(w-40),20), &ef, &eBrush);
                cy += 24;
            }

            // Paste and Start-Test buttons, side by side.
            {
                int rowH = 44, gap = 10;
                int pasteW = 110;
                int startW = w - 40 - pasteW - gap;
                Rect pasteRect(rtl ? 20+startW+gap : 20, cy, pasteW, rowH);
                Rect startRect(rtl ? 20 : 20+pasteW+gap, cy, startW, rowH);
                g_speedPasteBtnRect = pasteRect;
                g_speedStartBtnRect = startRect;

                SolidBrush pasteBg(FromColorref(g_palette->surfaceVariant));
                GraphicsPath* pp = RoundedRectPath(pasteRect, 14); gBuf.FillPath(&pasteBg, pp); delete pp;
                Font pasteFont(&fontFamily, 9.5f, FontStyleBold, UnitPoint);
                SolidBrush pasteText(FromColorref(g_palette->onSurface));
                StringFormat pcf; pcf.SetAlignment(StringAlignmentCenter); pcf.SetLineAlignment(StringAlignmentCenter);
                gBuf.DrawString(TT(Loc::Key::Paste).c_str(), -1, &pasteFont, RectF((REAL)pasteRect.X,(REAL)pasteRect.Y,(REAL)pasteRect.Width,(REAL)pasteRect.Height), &pcf, &pasteText);

                SolidBrush runBg(FromColorref(g_palette->primary));
                GraphicsPath* rp = RoundedRectPath(startRect, 14); gBuf.FillPath(&runBg, rp); delete rp;
                Font runFont(&fontFamily, 9.5f, FontStyleBold, UnitPoint);
                SolidBrush runText(Color(255,255,255,255));
                StringFormat cf; cf.SetAlignment(StringAlignmentCenter); cf.SetLineAlignment(StringAlignmentCenter);
                // Always "Start Test" — never relabeled to "Test Again".
                std::wstring btnLabel = g_speedTesting ? TT(Loc::Key::Testing) : TT(Loc::Key::StartTest);
                gBuf.DrawString(btnLabel.c_str(), -1, &runFont, RectF((REAL)startRect.X,(REAL)startRect.Y,(REAL)startRect.Width,(REAL)startRect.Height), &cf, &runText);
                cy += rowH + 20;
            }

            if (g_speedHasResult && !g_speedTesting) {
                SpeedTester::Result r;
                { std::lock_guard<std::mutex> lock(g_fetchMutex); r = g_speedResult; }

                if (r.avgMs < 0) {
                    Rect offCard(20, cy, w-40, 60);
                    SolidBrush cardBg(FromColorref(g_palette->surfaceVariant));
                    GraphicsPath* ocp = RoundedRectPath(offCard, Theme::CornerRadiusCard); gBuf.FillPath(&cardBg, ocp); delete ocp;
                    Font offFont(&fontFamily, 13, FontStyleBold, UnitPoint);
                    SolidBrush offBrush(FromColorref(g_palette->error));
                    StringFormat offFmt; offFmt.SetAlignment(StringAlignmentCenter); offFmt.SetLineAlignment(StringAlignmentCenter);
                    gBuf.DrawString(TT(Loc::Key::QualityOffline).c_str(), -1, &offFont, RectF((REAL)offCard.X,(REAL)offCard.Y,(REAL)offCard.Width,(REAL)offCard.Height), &offFmt, &offBrush);
                    cy += 60 + 20;
                } else {
                    Color qColor = r.quality == SpeedTester::Quality::Excellent ? Color(255,0x1B,0xC4,0x7D)
                                 : r.quality == SpeedTester::Quality::Good ? Color(255,0xE0,0xB4,0x00)
                                 : r.quality == SpeedTester::Quality::Fair ? Color(255,0xF5,0x9E,0x0B)
                                 : FromColorref(g_palette->error);
                    std::wstring qualityText =
                        r.quality == SpeedTester::Quality::Excellent ? TT(Loc::Key::QualityExcellent) :
                        r.quality == SpeedTester::Quality::Good ? TT(Loc::Key::QualityGood) :
                        r.quality == SpeedTester::Quality::Fair ? TT(Loc::Key::QualityFair) :
                        r.quality == SpeedTester::Quality::Poor ? TT(Loc::Key::QualityPoor) : TT(Loc::Key::QualityOffline);

                    // ---- Circular ping gauge ----
                    int gaugeD = 150;
                    int gcx = w/2, gcy = cy + gaugeD/2;
                    Pen ringBg(FromColorrefAlpha(g_palette->onSurfaceVariant, 40), 6.0f);
                    gBuf.DrawEllipse(&ringBg, gcx-gaugeD/2, gcy-gaugeD/2, gaugeD, gaugeD);
                    Pen ringFg(qColor, 6.0f); ringFg.SetStartCap(LineCapRound); ringFg.SetEndCap(LineCapRound);
                    gBuf.DrawEllipse(&ringFg, gcx-gaugeD/2, gcy-gaugeD/2, gaugeD, gaugeD);
                    Font bigNumFont(&fontFamily, 28, FontStyleBold, UnitPoint);
                    SolidBrush bigNumBrush(qColor);
                    StringFormat centerFmt; centerFmt.SetAlignment(StringAlignmentCenter); centerFmt.SetLineAlignment(StringAlignmentCenter);
                    gBuf.DrawString(std::to_wstring(r.avgMs).c_str(), -1, &bigNumFont, RectF((REAL)(gcx-gaugeD/2),(REAL)(gcy-24),(REAL)gaugeD,34), &centerFmt, &bigNumBrush);
                    Font msFont(&fontFamily, 9, FontStyleRegular, UnitPoint);
                    SolidBrush msBrush(FromColorrefAlpha(g_palette->onSurfaceVariant, 200));
                    gBuf.DrawString(L"ms", -1, &msFont, RectF((REAL)(gcx-gaugeD/2),(REAL)(gcy+10),(REAL)gaugeD,18), &centerFmt, &msBrush);
                    cy += gaugeD + 14;

                    // Quality pill (dot + text) centered under the gauge.
                    {
                        Font qFont(&fontFamily, 9.5f, FontStyleBold, UnitPoint);
                        std::wstring qLabel = TT(Loc::Key::Quality) + L": " + qualityText;
                        RectF measured; gBuf.MeasureString(qLabel.c_str(), -1, &qFont, PointF(0,0), &measured);
                        int pillW = (int)measured.Width + 56, pillH = 28;
                        Rect pillRect(w/2 - pillW/2, cy, pillW, pillH);
                        SolidBrush pillBg(Color(35, qColor.GetR(), qColor.GetG(), qColor.GetB()));
                        GraphicsPath* pp = RoundedRectPath(pillRect, pillH/2); gBuf.FillPath(&pillBg, pp); delete pp;
                        SolidBrush dotBrush(qColor);
                        int dotX = rtl ? (pillRect.X + pillRect.Width - 20) : (pillRect.X + 12);
                        gBuf.FillEllipse(&dotBrush, dotX, pillRect.Y+pillH/2-4, 8, 8);
                        SolidBrush qTextBrush(qColor);
                        StringFormat qfmt; qfmt.SetAlignment(StringAlignmentCenter); qfmt.SetLineAlignment(StringAlignmentCenter);
                        gBuf.DrawString(qLabel.c_str(), -1, &qFont, RectF((REAL)(pillRect.X+16),(REAL)pillRect.Y,(REAL)(pillW-22),(REAL)pillH), &qfmt, &qTextBrush);
                        cy += pillH + 20;
                    }

                    // ---- Stat grid: 3 rows x 2 cols, with icons ----
                    {
                        Rect gridCard(20, cy, w-40, 3*74);
                        SolidBrush cardBg(FromColorref(g_palette->surfaceVariant));
                        GraphicsPath* gcp = RoundedRectPath(gridCard, Theme::CornerRadiusCard); gBuf.FillPath(&cardBg, gcp); delete gcp;

                        wchar_t stabBuf[16]; swprintf(stabBuf, 16, L"%.0f%%", (std::max)(0.0, 100.0 - r.packetLossPct));
                        wchar_t pingBuf[24]; swprintf(pingBuf, 24, L"ms %d", r.avgMs);
                        wchar_t upBuf[32]; swprintf(upBuf, 32, L"Mbps %.1f", r.uploadMbps);
                        wchar_t dlBuf[32]; swprintf(dlBuf, 32, L"Mbps %.1f", r.downloadMbps);
                        wchar_t lossBuf[16]; swprintf(lossBuf, 16, L"%.0f%%", r.packetLossPct);
                        wchar_t jitBuf[24]; swprintf(jitBuf, 24, L"ms %.0f", r.jitterMs);

                        Font gLabelFont(&fontFamily, 8.5f, FontStyleRegular, UnitPoint);
                        Font gValueFont(&fontFamily, 12.5f, FontStyleBold, UnitPoint);
                        SolidBrush gLabelBrush(FromColorrefAlpha(g_palette->onSurfaceVariant, 190));
                        SolidBrush gValueBrush(FromColorref(g_palette->onSurface));
                        Color accent = FromColorref(g_palette->primary);

                        // Fixed physical layout matching the Android screenshot: right
                        // column = Stability/Download/PacketLoss, left column =
                        // AvgPing/Upload/Jitter (columns are X-position based, not
                        // mirrored per-language).
                        struct Cell { std::wstring label; std::wstring value; int iconKind; };
                        Cell rightCol[3] = {
                            { TT(Loc::Key::Stability), stabBuf, 0 },
                            { TT(Loc::Key::DownloadSpeed), dlBuf, 3 },
                            { TT(Loc::Key::PacketLoss), lossBuf, 4 },
                        };
                        Cell leftCol[3] = {
                            { TT(Loc::Key::AvgPing), pingBuf, 1 },
                            { TT(Loc::Key::UploadSpeed), upBuf, 2 },
                            { TT(Loc::Key::Jitter), jitBuf, 5 },
                        };
                        int colW = (gridCard.Width) / 2;
                        auto drawIcon = [&](int kind, const Rect& ir) {
                            switch (kind) {
                                case 0: DrawStabilityIcon(gBuf, ir, accent); break;
                                case 1: DrawWifiIcon(gBuf, ir, accent); break;
                                case 2: DrawFlashIcon(gBuf, ir, accent, false); break; // Upload — flash pointing up
                                case 3: DrawFlashIcon(gBuf, ir, accent, true); break;  // Download — flash pointing down
                                case 4: DrawRefreshIcon(gBuf, ir, accent); break;
                                default: DrawWaveIcon(gBuf, ir, accent); break;
                            }
                        };
                        auto drawCell = [&](const Cell& c, int sx, int cellW, int sy) {
                            int iconSz = 18, pad = 14;
                            int iconX = rtl ? (sx + cellW - iconSz - pad) : (sx + pad);
                            Rect ir(iconX, sy, iconSz, iconSz);
                            drawIcon(c.iconKind, ir);
                            int textX = rtl ? sx : (sx + iconSz + pad + 8);
                            int textW = cellW - iconSz - pad - 16;
                            StringFormat lf; if (rtl) lf.SetAlignment(StringAlignmentFar);
                            gBuf.DrawString(c.label.c_str(), -1, &gLabelFont, RectF((REAL)textX,(REAL)(sy+1),(REAL)textW,16), &lf, &gLabelBrush);
                            gBuf.DrawString(c.value.c_str(), -1, &gValueFont, RectF((REAL)textX,(REAL)(sy+20),(REAL)textW,20), &lf, &gValueBrush);
                        };
                        for (int row = 0; row < 3; ++row) {
                            int sy = gridCard.Y + 14 + row*74;
                            drawCell(leftCol[row],  gridCard.X + 6,        colW - 6, sy);
                            drawCell(rightCol[row], gridCard.X + colW, colW - 6, sy);
                        }
                        cy += gridCard.Height + 18;
                    }

                    // ---- Real Telegram download speed card ----
                    {
                        double mbps = SpeedTester::EstimateTelegramMBps(r);
                        Rect tgCard(20, cy, w-40, 168);
                        SolidBrush tgBg(FromColorrefAlpha(g_palette->primary, IsDarkTheme()?28:18));
                        GraphicsPath* tp = RoundedRectPath(tgCard, Theme::CornerRadiusCard); gBuf.FillPath(&tgBg, tp);
                        Pen tgBorder(FromColorrefAlpha(g_palette->primary, 90), 1.2f); gBuf.DrawPath(&tgBorder, tp); delete tp;

                        int iconSz = 22;
                        Rect cloudR(rtl ? (tgCard.X+tgCard.Width-18-iconSz) : (tgCard.X+18), tgCard.Y+14, iconSz, iconSz);
                        DrawCloudDownloadIcon(gBuf, cloudR, FromColorref(g_palette->primary));

                        // "Estimated" pill — opposite top corner from the cloud icon.
                        Font estFont(&fontFamily, 7.5f, FontStyleBold, UnitPoint);
                        std::wstring estLabel = TT(Loc::Key::EstimatedBadge);
                        RectF estMeasured; gBuf.MeasureString(estLabel.c_str(), -1, &estFont, PointF(0,0), &estMeasured);
                        int estPillW = (int)estMeasured.Width + 18;
                        Rect estPill(rtl ? tgCard.X+16 : tgCard.X+tgCard.Width-16-estPillW, tgCard.Y+14, estPillW, 20);
                        SolidBrush estBg(FromColorrefAlpha(g_palette->primary, 45));
                        GraphicsPath* ep = RoundedRectPath(estPill, 10); gBuf.FillPath(&estBg, ep); delete ep;
                        SolidBrush estText(FromColorref(g_palette->primary));
                        StringFormat estFmt; estFmt.SetAlignment(StringAlignmentCenter); estFmt.SetLineAlignment(StringAlignmentCenter);
                        gBuf.DrawString(estLabel.c_str(), -1, &estFont, RectF((REAL)estPill.X,(REAL)estPill.Y,(REAL)estPill.Width,(REAL)estPill.Height), &estFmt, &estText);

                        // Title, its own row below the icon/pill row.
                        Font titleFont(&fontFamily, 9.5f, FontStyleBold, UnitPoint);
                        SolidBrush titleBrush(FromColorref(g_palette->onSurface));
                        StringFormat titleFmt; titleFmt.SetAlignment(StringAlignmentCenter);
                        gBuf.DrawString(TT(Loc::Key::RealTelegramSpeedTitle).c_str(), -1, &titleFont, RectF((REAL)(tgCard.X+14),(REAL)(tgCard.Y+42),(REAL)(tgCard.Width-28),20), &titleFmt, &titleBrush);

                        // Big "MB/s X.XX" number.
                        Font unitFont(&fontFamily, 11, FontStyleBold, UnitPoint);
                        Font numFont(&fontFamily, 20, FontStyleBold, UnitPoint);
                        SolidBrush numBrush(FromColorref(g_palette->primary));
                        wchar_t mbpsBuf[32]; swprintf(mbpsBuf, 32, L"%.2f", mbps);
                        RectF unitM; gBuf.MeasureString(L"MB/s", -1, &unitFont, PointF(0,0), &unitM);
                        RectF numM; gBuf.MeasureString(mbpsBuf, -1, &numFont, PointF(0,0), &numM);
                        int totalW = (int)(unitM.Width + numM.Width) + 10;
                        int startX = tgCard.X + tgCard.Width/2 - totalW/2;
                        int rowY = tgCard.Y + 68;
                        if (rtl) {
                            gBuf.DrawString(L"MB/s", -1, &unitFont, PointF((REAL)(startX+numM.Width+10),(REAL)(rowY+8)), &numBrush);
                            gBuf.DrawString(mbpsBuf, -1, &numFont, PointF((REAL)startX,(REAL)rowY), &numBrush);
                        } else {
                            gBuf.DrawString(L"MB/s", -1, &unitFont, PointF((REAL)startX,(REAL)(rowY+8)), &numBrush);
                            gBuf.DrawString(mbpsBuf, -1, &numFont, PointF((REAL)(startX+unitM.Width+10),(REAL)rowY), &numBrush);
                        }

                        Font discFont(&fontFamily, 7.5f, FontStyleRegular, UnitPoint);
                        SolidBrush discBrush(FromColorrefAlpha(g_palette->onSurfaceVariant, 180));
                        StringFormat discFmt; discFmt.SetAlignment(StringAlignmentCenter);
                        gBuf.DrawString(TT(Loc::Key::TelegramSpeedDisclaimer).c_str(), -1, &discFont, RectF((REAL)(tgCard.X+14),(REAL)(tgCard.Y+112),(REAL)(tgCard.Width-28),44), &discFmt, &discBrush);
                        cy += tgCard.Height + 18;
                    }

                    // ---- Download Time Estimator (with quick-size chips) ----
                    {
                        Font estTitleFont(&fontFamily, 10.5f, FontStyleBold, UnitPoint);
                        SolidBrush estTitleBrush(FromColorref(g_palette->onSurface));
                        StringFormat etf; if (rtl) etf.SetAlignment(StringAlignmentFar);
                        gBuf.DrawString(TT(Loc::Key::FileDownloadEstimator).c_str(), -1, &estTitleFont, RectF(20,(REAL)cy,(REAL)(w-40),20), &etf, &estTitleBrush);
                        cy += 28;

                        g_fileSizeInputRect = Rect(20, cy, w-40, 40);
                        SolidBrush fsBg(FromColorref(g_palette->surfaceVariant));
                        GraphicsPath* fsp = RoundedRectPath(g_fileSizeInputRect, 12); gBuf.FillPath(&fsBg, fsp); delete fsp;
                        if (g_fileSizeInputFocused) {
                            Pen focusPen(FromColorref(g_palette->primary), 1.5f);
                            GraphicsPath* ffp = RoundedRectPath(g_fileSizeInputRect, 12); gBuf.DrawPath(&focusPen, ffp); delete ffp;
                        }
                        Font fsFont(&fontFamily, 9.5f, FontStyleRegular, UnitPoint);
                        StringFormat fsFmt; if (rtl) fsFmt.SetAlignment(StringAlignmentFar); fsFmt.SetLineAlignment(StringAlignmentCenter);
                        if (g_fileSizeInput.empty()) {
                            SolidBrush ph(FromColorrefAlpha(g_palette->onSurfaceVariant, 150));
                            gBuf.DrawString(TT(Loc::Key::FileSizeMbHint).c_str(), -1, &fsFont, RectF((REAL)(g_fileSizeInputRect.X+12),(REAL)g_fileSizeInputRect.Y,(REAL)(g_fileSizeInputRect.Width-24),(REAL)g_fileSizeInputRect.Height), &fsFmt, &ph);
                        } else {
                            SolidBrush tx(FromColorref(g_palette->onSurface));
                            std::wstring shown = g_fileSizeInput + (g_fileSizeInputFocused ? L"\u2502" : L"");
                            gBuf.DrawString(shown.c_str(), -1, &fsFont, RectF((REAL)(g_fileSizeInputRect.X+12),(REAL)g_fileSizeInputRect.Y,(REAL)(g_fileSizeInputRect.Width-24),(REAL)g_fileSizeInputRect.Height), &fsFmt, &tx);
                        }
                        cy += 40 + 10;

                        // Quick-select size chips.
                        {
                            int chipGap = 8;
                            int chipW = (w-40 - chipGap*4) / 5;
                            int cx0 = 20;
                            Font chipFont(&fontFamily, 8.5f, FontStyleBold, UnitPoint);
                            for (int i = 0; i < 5; ++i) {
                                int slot = rtl ? (4 - i) : i;
                                Rect chip(cx0 + slot*(chipW+chipGap), cy, chipW, 30);
                                g_fileSizeChipRects[i] = chip;
                                SolidBrush chipBg(FromColorref(g_palette->surfaceVariant));
                                GraphicsPath* cp = RoundedRectPath(chip, 10); gBuf.FillPath(&chipBg, cp); delete cp;
                                Pen chipBorder(FromColorrefAlpha(g_palette->onSurfaceVariant, 70), 1.0f);
                                gBuf.DrawPath(&chipBorder, cp);
                                wchar_t chipLbl[16]; swprintf(chipLbl, 16, L"M %d", (int)kFileSizeChipValues[i]);
                                SolidBrush chipText(FromColorref(g_palette->onSurface));
                                StringFormat cf; cf.SetAlignment(StringAlignmentCenter); cf.SetLineAlignment(StringAlignmentCenter);
                                gBuf.DrawString(chipLbl, -1, &chipFont, RectF((REAL)chip.X,(REAL)chip.Y,(REAL)chip.Width,(REAL)chip.Height), &cf, &chipText);
                            }
                            cy += 30 + 14;
                        }

                        double fileSizeMB = 0.0;
                        try { fileSizeMB = g_fileSizeInput.empty() ? 0.0 : std::stod(g_fileSizeInput); } catch (...) { fileSizeMB = 0.0; }
                        double seconds = SpeedTester::EstimateDownloadSeconds(r, fileSizeMB);
                        if (seconds > 0) {
                            wchar_t timeBuf[64];
                            if (seconds < 60) swprintf(timeBuf, 64, L"%.1f s", seconds);
                            else swprintf(timeBuf, 64, L"%.1f min", seconds / 60.0);
                            Font resFont(&fontFamily, 9, FontStyleRegular, UnitPoint);
                            Font resValFont(&fontFamily, 13, FontStyleBold, UnitPoint);
                            SolidBrush resLabelBrush(FromColorrefAlpha(g_palette->onSurfaceVariant, 190));
                            SolidBrush resValBrush(FromColorref(g_palette->primary));
                            StringFormat rf1; if (rtl) rf1.SetAlignment(StringAlignmentFar);
                            gBuf.DrawString(TT(Loc::Key::EstimatedTimeResult).c_str(), -1, &resFont, RectF(20,(REAL)cy,(REAL)(w-40),18), &rf1, &resLabelBrush);
                            gBuf.DrawString(timeBuf, -1, &resValFont, RectF(20,(REAL)(cy+16),(REAL)(w-40),22), &rf1, &resValBrush);
                            cy += 44;
                        }
                    }

                    // ---- Connection info ----
                    if (!r.resolvedIp.empty()) {
                        Font ciTitleFont(&fontFamily, 9.5f, FontStyleBold, UnitPoint);
                        SolidBrush ciTitleBrush(FromColorref(g_palette->onSurface));
                        StringFormat cif; if (rtl) cif.SetAlignment(StringAlignmentFar);
                        gBuf.DrawString(TT(Loc::Key::ConnectionInfo).c_str(), -1, &ciTitleFont, RectF(20,(REAL)cy,(REAL)(w-40),20), &cif, &ciTitleBrush);
                        cy += 24;
                        Font ciFont(&fontFamily, 9, FontStyleRegular, UnitPoint);
                        SolidBrush ciBrush(FromColorrefAlpha(g_palette->onSurfaceVariant, 200));
                        std::wstring hostLine = g_lastSpeedProxy.server + L":" + g_lastSpeedProxy.port;
                        gBuf.DrawString(hostLine.c_str(), -1, &ciFont, RectF(20,(REAL)cy,(REAL)(w-40),18), &cif, &ciBrush);
                        cy += 20;
                        std::wstring ipLine = L"IP: " + r.resolvedIp;
                        gBuf.DrawString(ipLine.c_str(), -1, &ciFont, RectF(20,(REAL)cy,(REAL)(w-40),18), &cif, &ciBrush);
                        cy += 20;
                        if (r.dnsLookupMs >= 0) {
                            std::wstring dnsLine = TT(Loc::Key::DnsLookupLabel) + L": " + std::to_wstring(r.dnsLookupMs) + L" ms";
                            gBuf.DrawString(dnsLine.c_str(), -1, &ciFont, RectF(20,(REAL)cy,(REAL)(w-40),18), &cif, &ciBrush);
                            cy += 20;
                        }
                        cy += 8;
                    }

                    // ---- Bottom action buttons: Connect, then Copy/Save ----
                    {
                        g_speedConnectBtnRect = Rect(20, cy, w-40, 46);
                        SolidBrush connBg(FromColorref(g_palette->primary));
                        GraphicsPath* cbp = RoundedRectPath(g_speedConnectBtnRect, 16); gBuf.FillPath(&connBg, cbp); delete cbp;
                        Font connFont(&fontFamily, 10.5f, FontStyleBold, UnitPoint);
                        SolidBrush connText(Color(255,255,255,255));
                        int chevW = 20;
                        Rect chevRect(rtl ? g_speedConnectBtnRect.X+16 : g_speedConnectBtnRect.X+g_speedConnectBtnRect.Width-16-chevW, g_speedConnectBtnRect.Y+13, chevW, 20);
                        DrawChevron(gBuf, chevRect, Color(255,255,255,255));
                        StringFormat connFmt; connFmt.SetAlignment(StringAlignmentCenter); connFmt.SetLineAlignment(StringAlignmentCenter);
                        gBuf.DrawString(TT(Loc::Key::Connect).c_str(), -1, &connFont, RectF((REAL)g_speedConnectBtnRect.X,(REAL)g_speedConnectBtnRect.Y,(REAL)g_speedConnectBtnRect.Width,(REAL)g_speedConnectBtnRect.Height), &connFmt, &connText);
                        cy += 46 + 10;

                        int gap = 10;
                        int halfW = (w-40-gap)/2;
                        g_speedCopyBtnRect = Rect(20, cy, halfW, 42);
                        g_speedSaveBtnRect = Rect(20+halfW+gap, cy, halfW, 42);
                        auto drawOutlineBtn = [&](const Rect& br, const std::wstring& label, bool star) {
                            SolidBrush bg(FromColorref(g_palette->surface));
                            GraphicsPath* bp = RoundedRectPath(br, 14); gBuf.FillPath(&bg, bp);
                            Pen border(FromColorrefAlpha(g_palette->onSurfaceVariant, 90), 1.2f); gBuf.DrawPath(&border, bp); delete bp;
                            Font f(&fontFamily, 9.5f, FontStyleBold, UnitPoint);
                            SolidBrush textBrush(FromColorref(g_palette->onSurface));
                            RectF measured; gBuf.MeasureString(label.c_str(), -1, &f, PointF(0,0), &measured);
                            int iconSz = 16, gapIcon = 6;
                            int groupW = iconSz + gapIcon + (int)measured.Width;
                            int sx = br.X + (br.Width - groupW)/2;
                            Rect iconR(sx, br.Y + (br.Height-iconSz)/2, iconSz, iconSz);
                            if (star) DrawBookmarkIcon(gBuf, iconR, FromColorref(g_palette->onSurface), false);
                            else DrawCopyIcon(gBuf, iconR, FromColorref(g_palette->onSurface));
                            StringFormat lf; lf.SetLineAlignment(StringAlignmentCenter);
                            gBuf.DrawString(label.c_str(), -1, &f, RectF((REAL)(sx+iconSz+gapIcon),(REAL)br.Y,(REAL)(br.Width),(REAL)br.Height), &lf, &textBrush);
                        };
                        drawOutlineBtn(g_speedCopyBtnRect, TT(Loc::Key::Copy), false);
                        drawOutlineBtn(g_speedSaveBtnRect, TT(Loc::Key::Save), true);
                        cy += 42 + 10;
                    }
                }
            } else if (g_speedTesting) {
                int cxs = w/2, cys = cy + 30, r = 16;
                Pen spinnerPen(FromColorref(g_palette->primary), 3.0f);
                spinnerPen.SetStartCap(LineCapRound); spinnerPen.SetEndCap(LineCapRound);
                gBuf.DrawArc(&spinnerPen, cxs-r, cys-r, r*2, r*2, g_spinnerAngle, 100);
                cy += 80;
            }
            cy += 24; // breathing room so the last card/button isn't flush against the bottom edge
            g_speedContentHeight = (cy - speedStartCy) + 24; // + bottom padding
            {
                int maxScroll = (std::max)(0, g_speedContentHeight - content.Height);
                if (g_speedScrollY > maxScroll) g_speedScrollY = maxScroll;
                if (g_speedScrollY < 0) g_speedScrollY = 0;
            }
            gBuf.SetClip(&speedOldClip);
        }
        // ============================================================= SUPPORT
        else if (g_currentScreen == Screen::Support) {
            int cy = content.Y + 16;
            Rect bannerRect(20, cy, w-40, 0);
            Font bannerFont(&fontFamily, 10, FontStyleBold, UnitPoint);
            std::wstring bannerText = TT(Loc::Key::SupportBanner);
            RectF bmeasured;
            gBuf.MeasureString(bannerText.c_str(), -1, &bannerFont, RectF(0,0,(REAL)(bannerRect.Width-28),999), &bmeasured);
            bannerRect.Height = (int)bmeasured.Height + 28;
            SolidBrush bannerBg(FromColorrefAlpha(g_palette->primary, 45));
            GraphicsPath* bp2 = RoundedRectPath(bannerRect, 16); gBuf.FillPath(&bannerBg, bp2); delete bp2;
            SolidBrush bannerTextBrush(FromColorref(g_palette->primary));
            StringFormat cf; cf.SetAlignment(StringAlignmentCenter);
            gBuf.DrawString(bannerText.c_str(), -1, &bannerFont, RectF((REAL)(bannerRect.X+14),(REAL)(bannerRect.Y+14),(REAL)(bannerRect.Width-28),(REAL)(bannerRect.Height-20)), &cf, &bannerTextBrush);
            cy += bannerRect.Height + 20;

            Font sectionFont(&fontFamily, 11, FontStyleBold, UnitPoint);
            SolidBrush sectionBrush(FromColorref(g_palette->onSurface));
            if (!IsRTL()) gBuf.DrawString(TT(Loc::Key::CryptoHeader).c_str(), -1, &sectionFont, PointF(20,(REAL)cy), &sectionBrush);
            else { StringFormat lf; lf.SetAlignment(StringAlignmentFar); gBuf.DrawString(TT(Loc::Key::CryptoHeader).c_str(), -1, &sectionFont, RectF(0,(REAL)cy,(REAL)(w-20),20), &lf, &sectionBrush); }
            cy += 30;

            g_cryptoRows.clear();
            struct Wallet { std::wstring label, address; };
            std::vector<Wallet> wallets = {
                { L"USDT (Polygon)", L"0x3d76c651ee3f76ac468e2769c9d9fbfcaa545088" },
                { L"BTC (Ethereum)", L"0x3d76c651ee3f76ac468e2769c9d9fbfcaa545088" },
                { TT(Loc::Key::TrxRecommended), L"TFaCWNT4N9wHJ2e1Z9MSuz1waUoMseRGqx" },
            };
            Font walletLabelFont(&fontFamily, 9.5f, FontStyleBold, UnitPoint);
            Font walletAddrFont(&monoFamily, 8, FontStyleRegular, UnitPoint);
            for (auto& wallet : wallets) {
                Rect row(20, cy, w-40, 54);
                SolidBrush rowBg(FromColorref(g_palette->surfaceVariant));
                GraphicsPath* wp = RoundedRectPath(row, 12); gBuf.FillPath(&rowBg, wp); delete wp;
                SolidBrush labelBrush(FromColorref(g_palette->onSurface));
                gBuf.DrawString(wallet.label.c_str(), -1, &walletLabelFont, PointF((REAL)(row.X+14),(REAL)(row.Y+8)), &labelBrush);
                SolidBrush addrBrush(FromColorrefAlpha(g_palette->onSurfaceVariant, 200));
                std::wstring shortAddr = wallet.address.size() > 22 ? (wallet.address.substr(0,10) + L"..." + wallet.address.substr(wallet.address.size()-8)) : wallet.address;
                gBuf.DrawString(shortAddr.c_str(), -1, &walletAddrFont, PointF((REAL)(row.X+14),(REAL)(row.Y+28)), &addrBrush);
                Rect copyBtn(row.X + row.Width - 60, row.Y + 12, 46, 30);
                SolidBrush copyBg(FromColorref(g_palette->primary));
                GraphicsPath* cbp = RoundedRectPath(copyBtn, 8); gBuf.FillPath(&copyBg, cbp); delete cbp;
                Font copyFont(&fontFamily, 8.5f, FontStyleBold, UnitPoint);
                SolidBrush copyText(Color(255,255,255,255));
                StringFormat ccf; ccf.SetAlignment(StringAlignmentCenter); ccf.SetLineAlignment(StringAlignmentCenter);
                gBuf.DrawString(TT(Loc::Key::Copy).c_str(), -1, &copyFont, RectF((REAL)copyBtn.X,(REAL)copyBtn.Y,(REAL)copyBtn.Width,(REAL)copyBtn.Height), &ccf, &copyText);
                g_cryptoRows.push_back({ wallet.label, wallet.address, copyBtn });
                cy += 54 + 8;
            }
            cy += 12;

            Font warnFont(&fontFamily, 8.5f, FontStyleRegular, UnitPoint);
            std::wstring warnText = TT(Loc::Key::NetworkWarning);
            RectF wmeasured;
            gBuf.MeasureString(warnText.c_str(), -1, &warnFont, RectF(0,0,(REAL)(w-68),999), &wmeasured);
            Rect warnRect(20, cy, w-40, (int)wmeasured.Height + 24);
            SolidBrush warnBg(FromColorrefAlpha(g_palette->errorContainer, 130));
            GraphicsPath* wrp = RoundedRectPath(warnRect, 10); gBuf.FillPath(&warnBg, wrp); delete wrp;
            SolidBrush warnTextBrush(FromColorref(g_palette->error));
            StringFormat warnFmt; if (IsRTL()) { warnFmt.SetAlignment(StringAlignmentFar); }
            gBuf.DrawString(warnText.c_str(), -1, &warnFont, RectF((REAL)(warnRect.X+12),(REAL)(warnRect.Y+10),(REAL)(warnRect.Width-24),(REAL)(warnRect.Height-16)), &warnFmt, &warnTextBrush);
            cy += warnRect.Height + 24;

            if (!IsRTL()) gBuf.DrawString(TT(Loc::Key::StarHeader).c_str(), -1, &sectionFont, PointF(20,(REAL)cy), &sectionBrush);
            else { StringFormat lf; lf.SetAlignment(StringAlignmentFar); gBuf.DrawString(TT(Loc::Key::StarHeader).c_str(), -1, &sectionFont, RectF(0,(REAL)cy,(REAL)(w-20),20), &lf, &sectionBrush); }
            cy += 30;
            g_supportGithubRect = Rect(20, cy, w-40, 54);
            SolidBrush ghBg(FromColorref(g_palette->surfaceVariant));
            GraphicsPath* gp2 = RoundedRectPath(g_supportGithubRect, 14); gBuf.FillPath(&ghBg, gp2); delete gp2;
            Font ghFont(&fontFamily, 10, FontStyleBold, UnitPoint);
            SolidBrush ghBrush(FromColorref(g_palette->onSurface));
            DrawBrandLogo(gBuf, IsDarkTheme() ? g_githubLogoDark : g_githubLogoLight, Rect(g_supportGithubRect.X+12, g_supportGithubRect.Y+11, 32, 32));
            gBuf.DrawString(TT(Loc::Key::GitHub).c_str(), -1, &ghFont, PointF((REAL)(g_supportGithubRect.X+48),(REAL)(g_supportGithubRect.Y+17)), &ghBrush);
            cy += 54 + 14;

            g_supportTelegramRect = Rect(20, cy, w-40, 54);
            SolidBrush tgBg(FromColorref(g_palette->surfaceVariant));
            GraphicsPath* tgp = RoundedRectPath(g_supportTelegramRect, 14); gBuf.FillPath(&tgBg, tgp); delete tgp;
            DrawBrandLogo(gBuf, g_telegramLogo, Rect(g_supportTelegramRect.X+12, g_supportTelegramRect.Y+11, 32, 32));
            gBuf.DrawString(TT(Loc::Key::TelegramChannel).c_str(), -1, &ghFont, PointF((REAL)(g_supportTelegramRect.X+48),(REAL)(g_supportTelegramRect.Y+17)), &ghBrush);
            cy += 54 + 20;

            Font thankFont(&fontFamily, 10, FontStyleBold, UnitPoint);
            SolidBrush thankBrush(FromColorrefAlpha(g_palette->onSurfaceVariant, 210));
            StringFormat tcf; tcf.SetAlignment(StringAlignmentCenter);
            gBuf.DrawString(TT(Loc::Key::ThankYou).c_str(), -1, &thankFont, RectF(0,(REAL)cy,(REAL)w,24), &tcf, &thankBrush);
        }
        // ============================================================= SETTINGS
        else if (g_currentScreen == Screen::Settings) {
            Region settingsOldClip; gBuf.GetClip(&settingsOldClip);
            gBuf.SetClip(RectF((REAL)content.X, (REAL)content.Y, (REAL)content.Width, (REAL)content.Height));
            int cy = content.Y - g_settingsScrollY + 12;
            int settingsStartCy = cy;
            bool rtl = IsRTL();

            // ---- Colored nav rows (Saved=blue, SpeedTest=teal, Support=pink),
            // matching the Android app's tinted-card style exactly. ----
            struct NavStyle { Color bg, border, accent; };
            auto mk = [](int r,int g,int b){ return Color(255,(BYTE)r,(BYTE)g,(BYTE)b); };
            NavStyle navStyles[3] = {
                { mk(0xF2,0xF1,0xF8), mk(0xE1,0xDE,0xEC), mk(0x14,0x63,0xB0) }, // Saved — lavender / blue
                { mk(0xDE,0xF5,0xF3), mk(0xBF,0xE6,0xE1), mk(0x16,0x8F,0x82) }, // SpeedTest — mint / teal
                { mk(0xFB,0xDF,0xEC), mk(0xF3,0xC6,0xDE), mk(0xE5,0x3E,0x7E) }, // Support — pink / magenta
            };
            if (IsDarkTheme()) {
                // Deeper, desaturated tints so the cards still read on a dark background.
                navStyles[0] = { mk(0x22,0x28,0x38), mk(0x2E,0x3A,0x50), mk(0x6F,0xAE,0xF0) };
                navStyles[1] = { mk(0x18,0x2E,0x2C), mk(0x22,0x40,0x3C), mk(0x5B,0xCB,0xBC) };
                navStyles[2] = { mk(0x33,0x1F,0x2B), mk(0x46,0x2A,0x3A), mk(0xF0,0x7F,0xAE) };
            }
            auto drawNavRow = [&](const std::wstring& label, int iconId, const NavStyle& style, Rect& outRect) {
                outRect = Rect(16, cy, w-32, 54);
                SolidBrush rowBg(style.bg);
                GraphicsPath* rp = RoundedRectPath(outRect, 14); gBuf.FillPath(&rowBg, rp);
                Pen borderPen(style.border, 1.3f);
                gBuf.DrawPath(&borderPen, rp);
                delete rp;
                Color iconColor = style.accent;
                Font labelFont(&fontFamily, 10.5f, FontStyleBold, UnitPoint);
                SolidBrush labelBrush(style.accent);
                Font chevFont(&fontFamily, 12, FontStyleBold, UnitPoint);
                SolidBrush chevBrush(style.accent);

                if (!rtl) {
                    Rect iconRect(outRect.X+14, outRect.Y+15, 24, 24);
                    if (iconId == 0) DrawBookmarkIcon(gBuf, iconRect, iconColor, true);
                    else if (iconId == 1) DrawCheckCircleIcon(gBuf, iconRect, iconColor);
                    else DrawHeartIcon(gBuf, iconRect, iconColor);
                    gBuf.DrawString(label.c_str(), -1, &labelFont, PointF((REAL)(outRect.X+46),(REAL)(outRect.Y+17)), &labelBrush);
                    StringFormat rf; rf.SetAlignment(StringAlignmentFar); rf.SetLineAlignment(StringAlignmentCenter);
                    gBuf.DrawString(L"\u203A", -1, &chevFont, RectF((REAL)outRect.X,(REAL)outRect.Y,(REAL)(outRect.Width-14),(REAL)outRect.Height), &rf, &chevBrush);
                } else {
                    Rect iconRect(outRect.X+outRect.Width-14-24, outRect.Y+15, 24, 24);
                    if (iconId == 0) DrawBookmarkIcon(gBuf, iconRect, iconColor, true);
                    else if (iconId == 1) DrawCheckCircleIcon(gBuf, iconRect, iconColor);
                    else DrawHeartIcon(gBuf, iconRect, iconColor);
                    StringFormat lf; lf.SetAlignment(StringAlignmentFar); lf.SetLineAlignment(StringAlignmentCenter);
                    gBuf.DrawString(label.c_str(), -1, &labelFont, RectF((REAL)outRect.X, (REAL)(outRect.Y), (REAL)(outRect.Width-46), (REAL)outRect.Height), &lf, &labelBrush);
                    StringFormat rf; rf.SetAlignment(StringAlignmentNear); rf.SetLineAlignment(StringAlignmentCenter);
                    gBuf.DrawString(L"\u2039", -1, &chevFont, RectF((REAL)(outRect.X+14),(REAL)outRect.Y,(REAL)(outRect.Width-14),(REAL)outRect.Height), &rf, &chevBrush);
                }
                cy += 54 + 10;
            };
            drawNavRow(TT(Loc::Key::SavedProxies), 0, navStyles[0], g_navSavedRect);
            drawNavRow(TT(Loc::Key::ProxySpeedTest), 1, navStyles[1], g_navSpeedRect);
            drawNavRow(TT(Loc::Key::SupportTitle), 2, navStyles[2], g_navSupportRect);

            cy += 10;
            Font sectionFont(&fontFamily, 11, FontStyleBold, UnitPoint);
            SolidBrush sectionBrush(FromColorref(g_palette->onSurface));

            // ---- Appearance — three square cards (icon on top, label below),
            // selected card gets a colored border + tinted fill. ----
            if (!rtl) gBuf.DrawString(TT(Loc::Key::Theme).c_str(), -1, &sectionFont, PointF(20,(REAL)cy), &sectionBrush);
            else { StringFormat lf; lf.SetAlignment(StringAlignmentFar); gBuf.DrawString(TT(Loc::Key::Theme).c_str(), -1, &sectionFont, RectF(0,(REAL)cy,(REAL)(w-20),20), &lf, &sectionBrush); }
            cy += 30;
            {
                int selTheme = SelectedThemeIndex();
                const std::wstring themeLabels[3] = { TT(Loc::Key::ThemeDark), TT(Loc::Key::ThemeLight), TT(Loc::Key::ThemeSystem) };
                int gap = 10, cardW = (w - 32 - gap*2) / 3, cardH = 78;
                int px = 16;
                Font cardFont(&fontFamily, 9, FontStyleRegular, UnitPoint);
                for (int i = 0; i < 3; ++i) {
                    Rect r(px, cy, cardW, cardH);
                    g_themePillRects[i] = r;
                    bool sel = (i == selTheme);
                    SolidBrush cardBg(sel ? FromColorrefAlpha(g_palette->primary, 30) : FromColorref(g_palette->surface));
                    GraphicsPath* cp = RoundedRectPath(r, 14); gBuf.FillPath(&cardBg, cp);
                    Pen cardBorder(sel ? FromColorref(g_palette->primary) : FromColorrefAlpha(g_palette->onSurfaceVariant, 90), sel ? 2.0f : 1.0f);
                    gBuf.DrawPath(&cardBorder, cp); delete cp;
                    Rect iconR(r.X + r.Width/2 - 13, r.Y + 12, 26, 26);
                    if (i == 0) DrawMoonIcon(gBuf, iconR, mk(0xF2,0xB2,0x33));
                    else if (i == 1) DrawSunIcon(gBuf, iconR, mk(0xF5,0xA6,0x23));
                    else DrawSystemThemeIcon(gBuf, iconR, mk(0x22,0x2B,0x3A), mk(0xF2,0xC9,0x6B));
                    StringFormat cf; cf.SetAlignment(StringAlignmentCenter);
                    SolidBrush lbl(sel ? FromColorref(g_palette->primary) : FromColorref(g_palette->onSurface));
                    gBuf.DrawString(themeLabels[i].c_str(), -1, &cardFont, RectF((REAL)r.X,(REAL)(r.Y+44),(REAL)r.Width,20), &cf, &lbl);
                    px += cardW + gap;
                }
                cy += cardH + 18;
            }

            // ---- Promo banner slider toggle ----
            {
                Rect toggleRow(16, cy, w-32, 48);
                SolidBrush rowBg(FromColorref(g_palette->surface));
                GraphicsPath* rp = RoundedRectPath(toggleRow, 14); gBuf.FillPath(&rowBg, rp);
                Pen rowBorder(FromColorrefAlpha(g_palette->onSurfaceVariant, 60), 1.0f);
                gBuf.DrawPath(&rowBorder, rp); delete rp;
                g_bannerSliderToggleRect = toggleRow;
                bool on = g_settings.bannerEnabled;
                int swW = 44, swH = 24;
                int pad = 14;
                Rect swRect(rtl ? toggleRow.X+pad : toggleRow.X + toggleRow.Width - swW - pad, toggleRow.Y + (toggleRow.Height - swH)/2, swW, swH);
                SolidBrush swBg(on ? FromColorref(g_palette->primary) : FromColorrefAlpha(g_palette->onSurfaceVariant, 90));
                GraphicsPath* swp = RoundedRectPath(swRect, swH/2); gBuf.FillPath(&swBg, swp); delete swp;
                int knobX = on ? (swRect.X + swW - swH + 2) : (swRect.X + 2);
                SolidBrush knobBrush(Color(255,255,255,255));
                gBuf.FillEllipse(&knobBrush, knobX, swRect.Y+2, swH-4, swH-4);
                Font toggleLabelFont(&fontFamily, 9.5f, FontStyleRegular, UnitPoint);
                SolidBrush toggleLabelBrush(FromColorref(g_palette->onSurface));
                // Label box sits strictly on the opposite side of the switch,
                // with a wide safety margin and no-wrap so it can never be
                // laid out on top of (or wrapped under) the switch.
                int labelX = rtl ? toggleRow.X + pad : toggleRow.X + swW + pad*2;
                int labelW = toggleRow.Width - swW - pad*3;
                StringFormat lf; lf.SetLineAlignment(StringAlignmentCenter);
                lf.SetFormatFlags(StringFormatFlagsNoWrap);
                lf.SetTrimming(StringTrimmingEllipsisCharacter);
                lf.SetAlignment(rtl ? StringAlignmentFar : StringAlignmentNear);
                int labelBoxX = rtl ? (swRect.X + swW + pad) : labelX;
                gBuf.DrawString(TT(Loc::Key::BannerSliderToggle).c_str(), -1, &toggleLabelFont,
                                 RectF((REAL)labelBoxX, (REAL)toggleRow.Y, (REAL)labelW, (REAL)toggleRow.Height), &lf, &toggleLabelBrush);
                cy += 48 + 18;
            }

            // ---- Language — full-width rows with a radio circle + flag,
            // selected row gets the same blue tint/border as the "Saved" nav row. ----
            if (!rtl) gBuf.DrawString(TT(Loc::Key::Language).c_str(), -1, &sectionFont, PointF(20,(REAL)cy), &sectionBrush);
            else { StringFormat lf; lf.SetAlignment(StringAlignmentFar); gBuf.DrawString(TT(Loc::Key::Language).c_str(), -1, &sectionFont, RectF(0,(REAL)cy,(REAL)(w-20),20), &lf, &sectionBrush); }
            cy += 30;
            {
                int selLang = SelectedLangIndex();
                Font langFont(&fontFamily, 10, FontStyleBold, UnitPoint);
                for (int i = 0; i < 3; ++i) {
                    Rect r(16, cy, w-32, 46);
                    g_langPillRects[i] = r;
                    bool sel = (i == selLang);
                    SolidBrush rowBg(sel ? navStyles[0].bg : FromColorref(g_palette->surface));
                    GraphicsPath* rp = RoundedRectPath(r, 12); gBuf.FillPath(&rowBg, rp);
                    Pen rowBorder(sel ? navStyles[0].accent : FromColorrefAlpha(g_palette->onSurfaceVariant, 60), sel ? 1.6f : 1.0f);
                    gBuf.DrawPath(&rowBorder, rp); delete rp;

                    int radioX = rtl ? r.X + r.Width - 14 - 18 : r.X + 14;
                    Rect radioR(radioX, r.Y + r.Height/2 - 9, 18, 18);
                    Pen radioPen(sel ? navStyles[0].accent : FromColorrefAlpha(g_palette->onSurfaceVariant, 160), 1.6f);
                    gBuf.DrawEllipse(&radioPen, radioR.X, radioR.Y, radioR.Width, radioR.Height);
                    if (sel) { SolidBrush dot(navStyles[0].accent); gBuf.FillEllipse(&dot, radioR.X+4, radioR.Y+4, 10, 10); }

                    int flagX = rtl ? r.X + 14 : r.X + r.Width - 14 - 26;
                    Rect flagR(flagX, r.Y + r.Height/2 - 9, 26, 18);
                    if (i == 0) DrawHeartIcon(gBuf, Rect(flagR.X+4, flagR.Y-2, 20, 20), mk(0xE5,0x3E,0x5A));
                    else if (i == 1) DrawBrandLogo(gBuf, g_flagUsImg, flagR);
                    else DrawBrandLogo(gBuf, g_flagRuImg, flagR);

                    SolidBrush labelBrush(sel ? navStyles[0].accent : FromColorref(g_palette->onSurface));
                    StringFormat lf; lf.SetLineAlignment(StringAlignmentCenter);
                    if (!rtl) { lf.SetAlignment(StringAlignmentNear); gBuf.DrawString(kLangNative[i], -1, &langFont, RectF((REAL)(r.X+40),(REAL)r.Y,(REAL)(r.Width-90),(REAL)r.Height), &lf, &labelBrush); }
                    else { lf.SetAlignment(StringAlignmentFar); gBuf.DrawString(kLangNative[i], -1, &langFont, RectF((REAL)(r.X+50),(REAL)r.Y,(REAL)(r.Width-90),(REAL)r.Height), &lf, &labelBrush); }
                    cy += 46 + 8;
                }
                cy += 10;
            }
            cy += 32 + 24;

            // ---- Auto Scan ----
            if (!rtl) gBuf.DrawString(TT(Loc::Key::AutoScanSettingsTitle).c_str(), -1, &sectionFont, PointF(20,(REAL)cy), &sectionBrush);
            else { StringFormat lf; lf.SetAlignment(StringAlignmentFar); gBuf.DrawString(TT(Loc::Key::AutoScanSettingsTitle).c_str(), -1, &sectionFont, RectF(0,(REAL)cy,(REAL)(w-20),20), &lf, &sectionBrush); }
            cy += 30;

            {
                Rect toggleRow(20, cy, w-40, 40);
                g_autoScanToggleRect = toggleRow;
                Font toggleLabelFont(&fontFamily, 9.5f, FontStyleRegular, UnitPoint);
                SolidBrush toggleLabelBrush(FromColorref(g_palette->onSurface));
                bool on = g_settings.autoScanEnabled;
                int swW = 44, swH = 24;
                Rect swRect(rtl ? toggleRow.X : toggleRow.X + toggleRow.Width - swW, toggleRow.Y + (toggleRow.Height - swH)/2, swW, swH);
                g_autoScanToggleRect = Rect(toggleRow.X, toggleRow.Y, toggleRow.Width, toggleRow.Height); // whole row is clickable
                SolidBrush swBg(on ? FromColorref(g_palette->primary) : FromColorrefAlpha(g_palette->onSurfaceVariant, 90));
                GraphicsPath* swp = RoundedRectPath(swRect, swH/2); gBuf.FillPath(&swBg, swp); delete swp;
                int knobX = on ? (swRect.X + swW - swH + 2) : (swRect.X + 2);
                SolidBrush knobBrush(Color(255,255,255,255));
                gBuf.FillEllipse(&knobBrush, knobX, swRect.Y+2, swH-4, swH-4);

                if (!rtl) gBuf.DrawString(TT(Loc::Key::AutoScanEnableToggle).c_str(), -1, &toggleLabelFont, PointF((REAL)toggleRow.X,(REAL)(toggleRow.Y+12)), &toggleLabelBrush);
                else { StringFormat lf; lf.SetAlignment(StringAlignmentFar); gBuf.DrawString(TT(Loc::Key::AutoScanEnableToggle).c_str(), -1, &toggleLabelFont, RectF((REAL)(toggleRow.X+swW+10),(REAL)(toggleRow.Y),(REAL)(toggleRow.Width-swW-10),(REAL)toggleRow.Height), &lf, &toggleLabelBrush); }
                cy += 40 + 8;
            }

            if (g_settings.autoScanEnabled) {
                wchar_t intervalBuf[64];
                swprintf(intervalBuf, 64, TT(Loc::Key::AutoScanIntervalText).c_str(), g_settings.autoScanIntervalS);
                Font ivFont(&fontFamily, 9.5f, FontStyleRegular, UnitPoint);
                SolidBrush ivBrush(FromColorrefAlpha(g_palette->onSurfaceVariant, 220));

                int stepBtnSize = 32;
                Rect minusRect(20, cy, stepBtnSize, stepBtnSize);
                Rect plusRect(20 + stepBtnSize + 100, cy, stepBtnSize, stepBtnSize);
                if (rtl) { minusRect.X = w - 40 - stepBtnSize; plusRect.X = w - 40 - stepBtnSize - 100 - stepBtnSize; }
                g_autoScanMinusRect = minusRect;
                g_autoScanPlusRect = plusRect;

                auto drawStepBtn = [&](Rect r, const wchar_t* sym) {
                    SolidBrush bg(FromColorref(g_palette->surfaceVariant));
                    GraphicsPath* p = RoundedRectPath(r, 8); gBuf.FillPath(&bg, p); delete p;
                    Font symFont(&fontFamily, 12, FontStyleBold, UnitPoint);
                    SolidBrush symBrush(FromColorref(g_palette->onSurface));
                    StringFormat cf; cf.SetAlignment(StringAlignmentCenter); cf.SetLineAlignment(StringAlignmentCenter);
                    gBuf.DrawString(sym, -1, &symFont, RectF((REAL)r.X,(REAL)r.Y,(REAL)r.Width,(REAL)r.Height), &cf, &symBrush);
                };
                drawStepBtn(minusRect, L"\u2212");
                drawStepBtn(plusRect, L"+");

                Rect labelRect(minusRect.X + stepBtnSize + 8, cy, 92, stepBtnSize);
                if (rtl) labelRect.X = plusRect.X + stepBtnSize + 8;
                StringFormat lcf; lcf.SetAlignment(StringAlignmentCenter); lcf.SetLineAlignment(StringAlignmentCenter);
                gBuf.DrawString(intervalBuf, -1, &ivFont, RectF((REAL)labelRect.X,(REAL)labelRect.Y,(REAL)labelRect.Width,(REAL)labelRect.Height), &lcf, &ivBrush);
                cy += stepBtnSize + 20;
            } else {
                g_autoScanMinusRect = Rect(0,0,0,0);
                g_autoScanPlusRect = Rect(0,0,0,0);
                cy += 8;
            }

            g_clearFavBtnRect = Rect(20, cy, 190, 34);
            SolidBrush dangerBg(FromColorref(g_palette->error));
            GraphicsPath* dp = RoundedRectPath(g_clearFavBtnRect, 17); gBuf.FillPath(&dangerBg, dp); delete dp;
            StringFormat dfmt; dfmt.SetAlignment(StringAlignmentCenter); dfmt.SetLineAlignment(StringAlignmentCenter);
            Font clearFavFont(&fontFamily, 9.5f, FontStyleRegular, UnitPoint);
            SolidBrush clearFavTextBrush(Color(255,255,255,255));
            gBuf.DrawString(TT(Loc::Key::ClearFavorites).c_str(), -1, &clearFavFont, RectF((REAL)g_clearFavBtnRect.X,(REAL)g_clearFavBtnRect.Y,(REAL)g_clearFavBtnRect.Width,(REAL)g_clearFavBtnRect.Height), &dfmt, &clearFavTextBrush);
            cy += 34 + 20;

            cy += 24; // breathing room so the last card/button isn't flush against the bottom edge
            g_settingsContentHeight = (cy - settingsStartCy) + 24; // + bottom padding
            {
                int maxScroll = (std::max)(0, g_settingsContentHeight - content.Height);
                if (g_settingsScrollY > maxScroll) g_settingsScrollY = maxScroll;
                if (g_settingsScrollY < 0) g_settingsScrollY = 0;
            }
            gBuf.SetClip(&settingsOldClip);
        }

        Graphics gScreen(hdc);
        gScreen.DrawImage(g_backBuffer, 0, 0, w, h);
    }

    void HandleWheel(int delta, int contentHeightAvail) {
        (void)contentHeightAvail;
        int step = -(delta / WHEEL_DELTA) * 60;
        RECT client; GetClientRect(g_hWnd, &client);
        int viewH = (client.bottom - client.top) - Theme::TopBarHeight
                    - (g_currentScreen == Screen::Home ? Theme::BottomBarHeight : 0);
        if (g_currentScreen == Screen::Home) {
            g_homeScrollY += step;
            int maxScroll = (std::max)(0, g_homeContentHeight - viewH);
            g_homeScrollY = (std::max)(0, (std::min)(g_homeScrollY, maxScroll));
        } else if (g_currentScreen == Screen::Saved) {
            g_savedScrollY += step;
            int maxScroll = (std::max)(0, g_savedContentHeight - viewH);
            g_savedScrollY = (std::max)(0, (std::min)(g_savedScrollY, maxScroll));
        } else if (g_currentScreen == Screen::Settings) {
            g_settingsScrollY += step;
            int maxScroll = (std::max)(0, g_settingsContentHeight - viewH);
            g_settingsScrollY = (std::max)(0, (std::min)(g_settingsScrollY, maxScroll));
        } else if (g_currentScreen == Screen::SpeedTest) {
            g_speedScrollY += step;
            int maxScroll = (std::max)(0, g_speedContentHeight - viewH);
            g_speedScrollY = (std::max)(0, (std::min)(g_speedScrollY, maxScroll));
        }
    }

    LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_ERASEBKGND: return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
                RECT client; GetClientRect(hWnd, &client);
                Paint(hdc, client);
                EndPaint(hWnd, &ps);
                return 0;
            }
            case WM_SIZE: InvalidateRect(hWnd, nullptr, FALSE); return 0;
            case WM_MOUSEWHEEL: {
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                HandleWheel(delta, 0);
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            case WM_LBUTTONDOWN: {
                int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);

                if (g_currentScreen != Screen::Home && PtInRect(g_backBtnRect, x, y)) {
                    NavigateBack();
                    if (g_currentScreen != Screen::SpeedTest) g_speedInputFocused = false;
                    InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }

                if (g_currentScreen == Screen::Home) {
                    if (PtInRect(g_settingsBtnRect, x, y)) { NavigateTo(Screen::Settings, Screen::Home); InvalidateRect(hWnd, nullptr, FALSE); return 0; }
                    if (g_bannerRectValid && PtInRect(g_bannerRect, x, y)) {
                        std::wstring link;
                        { std::lock_guard<std::mutex> lock(g_bannerMutex);
                          if (!g_bannerLinks.empty()) link = g_bannerLinks[g_bannerIndex % g_bannerLinks.size()]; }
                        if (!link.empty()) {
                            if (link[0] == L'@') link = L"tg://resolve?domain=" + link.substr(1);
                            else if (link.rfind(L"t.me/", 0) == 0) link = L"https://" + link;
                            OpenUrl(hWnd, link);
                        }
                        return 0;
                    }
                    if (PtInRect(g_scanBtnRect, x, y) && g_proxyListState != ProxyListState::Loading) { StartFetch(hWnd); InvalidateRect(hWnd, nullptr, FALSE); return 0; }
                    for (const auto& hit : g_visibleCards) {
                        if (PtInRect(hit.copyRect, x, y)) { CopyToClipboard(hWnd, hit.link); return 0; }
                        if (PtInRect(hit.bookmarkRect, x, y)) { ToggleFavorite(hit.key); InvalidateRect(hWnd, nullptr, FALSE); return 0; }
                        if (PtInRect(hit.connectRect, x, y)) { OpenProxy(hWnd, hit.link); return 0; }
                    }
                    return 0;
                }
                if (g_currentScreen == Screen::Saved) {
                    for (const auto& hit : g_visibleCards) {
                        if (PtInRect(hit.copyRect, x, y)) { CopyToClipboard(hWnd, hit.link); return 0; }
                        if (PtInRect(hit.bookmarkRect, x, y)) { ToggleFavorite(hit.key); InvalidateRect(hWnd, nullptr, FALSE); return 0; }
                        if (PtInRect(hit.connectRect, x, y)) { OpenProxy(hWnd, hit.link); return 0; }
                    }
                    return 0;
                }
                if (g_currentScreen == Screen::SpeedTest) {
                    bool clickedInput = PtInRect(g_speedInputRect, x, y);
                    bool clickedFileSize = PtInRect(g_fileSizeInputRect, x, y);
                    if (g_speedInputFocused != clickedInput) { g_speedInputFocused = clickedInput; InvalidateRect(hWnd, nullptr, FALSE); }
                    if (g_fileSizeInputFocused != clickedFileSize) { g_fileSizeInputFocused = clickedFileSize; InvalidateRect(hWnd, nullptr, FALSE); }
                    if (clickedInput) {
                        bool rtlNow = IsRTL();
                        const int clearBtnSz = 26;
                        if (!g_speedInput.empty() && PtInRect(g_speedClearBtnRect, x, y)) {
                            g_speedInput.clear();
                            g_speedInputCursor = 0;
                        } else {
                            // Approximate click-to-cursor: measure substrings
                            // and pick the boundary closest to the click x.
                            HDC hdc = GetDC(hWnd);
                            Graphics mg(hdc);
                            FontFamily ff(Theme::FontFamily);
                            Font f(&ff, 9.5f, FontStyleRegular, UnitPoint);
                            int textStartX = g_speedInputRect.X + (rtlNow ? clearBtnSz+8 : 12);
                            double relX = x - textStartX;
                            size_t best = g_speedInput.size();
                            double bestDiff = 1e18;
                            for (size_t i = 0; i <= g_speedInput.size(); ++i) {
                                RectF m; mg.MeasureString(g_speedInput.substr(0, i).c_str(), -1, &f, PointF(0,0), &m);
                                double diff = fabs((double)m.Width - relX);
                                if (diff < bestDiff) { bestDiff = diff; best = i; }
                            }
                            g_speedInputCursor = best;
                            ReleaseDC(hWnd, hdc);
                        }
                        InvalidateRect(hWnd, nullptr, FALSE);
                        return 0;
                    }
                    if (clickedFileSize) return 0;
                    if (PtInRect(g_speedPasteBtnRect, x, y)) {
                        if (OpenClipboard(hWnd)) {
                            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                            if (hData) {
                                wchar_t* text = (wchar_t*)GlobalLock(hData);
                                if (text) {
                                    size_t cur = (std::min)(g_speedInputCursor, g_speedInput.size());
                                    g_speedInput.insert(cur, text);
                                    g_speedInputCursor = cur + wcslen(text);
                                    GlobalUnlock(hData);
                                }
                            }
                            CloseClipboard();
                            InvalidateRect(hWnd, nullptr, FALSE);
                        }
                        return 0;
                    }
                    if (PtInRect(g_speedStartBtnRect, x, y) && !g_speedTesting) {
                        auto parsed = SpeedTester::ParseInput(g_speedInput);
                        if (!parsed.valid) {
                            g_speedErrorMsg = TT(Loc::Key::InvalidProxyFormat);
                        } else {
                            g_speedErrorMsg.clear();
                            g_lastSpeedProxy = parsed;
                            SetTimer(hWnd, kSpinnerTimerId, kSpinnerIntervalMs, nullptr);
                            RunSpeedTestAsync(hWnd, parsed);
                        }
                        InvalidateRect(hWnd, nullptr, FALSE);
                    }
                    if (g_speedHasResult && !g_speedTesting) {
                        for (int i = 0; i < 5; ++i) {
                            if (PtInRect(g_fileSizeChipRects[i], x, y)) {
                                wchar_t buf[16]; swprintf(buf, 16, L"%d", (int)kFileSizeChipValues[i]);
                                g_fileSizeInput = buf;
                                InvalidateRect(hWnd, nullptr, FALSE);
                                return 0;
                            }
                        }
                        if (PtInRect(g_speedConnectBtnRect, x, y)) { OpenProxy(hWnd, g_speedInput); return 0; }
                        if (PtInRect(g_speedCopyBtnRect, x, y)) { CopyToClipboard(hWnd, g_speedInput); return 0; }
                        if (PtInRect(g_speedSaveBtnRect, x, y)) {
                            SpeedTester::Result r;
                            { std::lock_guard<std::mutex> lock(g_fetchMutex); r = g_speedResult; }
                            SaveManualProxy(g_lastSpeedProxy, g_speedInput, r.avgMs);
                            InvalidateRect(hWnd, nullptr, FALSE);
                            return 0;
                        }
                    }
                    return 0;
                }
                if (g_currentScreen == Screen::Support) {
                    for (auto& row : g_cryptoRows) if (PtInRect(row.rect, x, y)) { CopyToClipboard(hWnd, row.address); return 0; }
                    if (PtInRect(g_supportGithubRect, x, y)) { OpenUrl(hWnd, L"https://github.com/Iwanian/Iwana-Proxy"); return 0; }
                    if (PtInRect(g_supportTelegramRect, x, y)) { OpenUrl(hWnd, L"tg://resolve?domain=I_w_a_n_a"); return 0; }
                    return 0;
                }
                if (g_currentScreen == Screen::Settings) {
                    if (PtInRect(g_navSavedRect, x, y))  { NavigateTo(Screen::Saved, Screen::Settings); InvalidateRect(hWnd, nullptr, FALSE); return 0; }
                    if (PtInRect(g_navSpeedRect, x, y))  { NavigateTo(Screen::SpeedTest, Screen::Settings); InvalidateRect(hWnd, nullptr, FALSE); return 0; }
                    if (PtInRect(g_navSupportRect, x, y)) { NavigateTo(Screen::Support, Screen::Settings); InvalidateRect(hWnd, nullptr, FALSE); return 0; }
                    for (int i = 0; i < 3; ++i) {
                        if (PtInRect(g_langPillRects[i], x, y)) { g_settings.language = kLangCodes[i]; Config::Save(g_settings); InvalidateRect(hWnd, nullptr, FALSE); return 0; }
                        if (PtInRect(g_themePillRects[i], x, y)) { g_settings.themeMode = kThemeCodes[i]; Config::Save(g_settings); ApplyThemeFromSettings(); InvalidateRect(hWnd, nullptr, FALSE); return 0; }
                    }
                    if (PtInRect(g_bannerSliderToggleRect, x, y)) {
                        g_settings.bannerEnabled = !g_settings.bannerEnabled;
                        Config::Save(g_settings);
                        InvalidateRect(hWnd, nullptr, FALSE);
                        return 0;
                    }
                    if (PtInRect(g_autoScanToggleRect, x, y)) {
                        g_settings.autoScanEnabled = !g_settings.autoScanEnabled;
                        Config::Save(g_settings);
                        ConfigureAutoScanTimer(hWnd);
                        InvalidateRect(hWnd, nullptr, FALSE);
                        return 0;
                    }
                    if (g_settings.autoScanEnabled && PtInRect(g_autoScanMinusRect, x, y)) {
                        g_settings.autoScanIntervalS = (std::max)(5, g_settings.autoScanIntervalS - 5);
                        Config::Save(g_settings);
                        ConfigureAutoScanTimer(hWnd);
                        InvalidateRect(hWnd, nullptr, FALSE);
                        return 0;
                    }
                    if (g_settings.autoScanEnabled && PtInRect(g_autoScanPlusRect, x, y)) {
                        g_settings.autoScanIntervalS = (std::min)(300, g_settings.autoScanIntervalS + 5);
                        Config::Save(g_settings);
                        ConfigureAutoScanTimer(hWnd);
                        InvalidateRect(hWnd, nullptr, FALSE);
                        return 0;
                    }
                    if (PtInRect(g_clearFavBtnRect, x, y)) {
                        if (ShowConfirmDialog(hWnd)) {
                            g_favoriteKeys.clear();
                            Storage::SaveFavoriteKeys(g_favoriteKeys);
                            std::lock_guard<std::mutex> lock(g_proxiesMutex);
                            for (auto& item : g_proxies) item.isFavorite = false;
                        }
                        InvalidateRect(hWnd, nullptr, FALSE);
                        return 0;
                    }
                    return 0;
                }
                return 0;
            }
            case WM_CHAR: {
                if (g_currentScreen == Screen::SpeedTest) {
                    wchar_t ch = (wchar_t)wParam;
                    if (g_speedInputFocused) {
                        size_t cur = (std::min)(g_speedInputCursor, g_speedInput.size());
                        if (ch == L'\b') {
                            if (cur > 0) { g_speedInput.erase(cur-1, 1); g_speedInputCursor = cur-1; }
                        } else if (ch >= 0x20 && g_speedInput.size() < 300) {
                            g_speedInput.insert(cur, 1, ch);
                            g_speedInputCursor = cur + 1;
                        }
                        InvalidateRect(hWnd, nullptr, FALSE);
                    } else if (g_fileSizeInputFocused) {
                        // Numeric-only field (digits + a single decimal point).
                        if (ch == L'\b') { if (!g_fileSizeInput.empty()) g_fileSizeInput.pop_back(); }
                        else if ((iswdigit(ch) || (ch == L'.' && g_fileSizeInput.find(L'.') == std::wstring::npos))
                                 && g_fileSizeInput.size() < 12) {
                            g_fileSizeInput += ch;
                        }
                        InvalidateRect(hWnd, nullptr, FALSE);
                    }
                }
                return 0;
            }
            case WM_KEYDOWN: {
                if (g_currentScreen == Screen::SpeedTest && g_speedInputFocused) {
                    size_t cur = (std::min)(g_speedInputCursor, g_speedInput.size());
                    if (wParam == VK_LEFT)  { if (cur > 0) g_speedInputCursor = cur - 1; InvalidateRect(hWnd, nullptr, FALSE); }
                    else if (wParam == VK_RIGHT) { if (cur < g_speedInput.size()) g_speedInputCursor = cur + 1; InvalidateRect(hWnd, nullptr, FALSE); }
                    else if (wParam == VK_HOME)  { g_speedInputCursor = 0; InvalidateRect(hWnd, nullptr, FALSE); }
                    else if (wParam == VK_END)   { g_speedInputCursor = g_speedInput.size(); InvalidateRect(hWnd, nullptr, FALSE); }
                    else if (wParam == VK_DELETE) { if (cur < g_speedInput.size()) { g_speedInput.erase(cur, 1); InvalidateRect(hWnd, nullptr, FALSE); } }
                }
                if (g_currentScreen == Screen::SpeedTest && (g_speedInputFocused || g_fileSizeInputFocused)
                    && wParam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    if (OpenClipboard(hWnd)) {
                        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                        if (hData) {
                            wchar_t* text = (wchar_t*)GlobalLock(hData);
                            if (text) {
                                if (g_speedInputFocused) {
                                    size_t cur = (std::min)(g_speedInputCursor, g_speedInput.size());
                                    g_speedInput.insert(cur, text);
                                    g_speedInputCursor = cur + wcslen(text);
                                } else {
                                    // Filter pasted text down to digits/decimal for the file-size field.
                                    for (wchar_t* p = text; *p; ++p) {
                                        if (iswdigit(*p) || (*p == L'.' && g_fileSizeInput.find(L'.') == std::wstring::npos)) {
                                            if (g_fileSizeInput.size() < 12) g_fileSizeInput += *p;
                                        }
                                    }
                                }
                                GlobalUnlock(hData);
                            }
                        }
                        CloseClipboard();
                        InvalidateRect(hWnd, nullptr, FALSE);
                    }
                }
                return 0;
            }
            case WM_TIMER: {
                if (wParam == kSpinnerTimerId) {
                    g_spinnerAngle += 24.0f; if (g_spinnerAngle >= 360.0f) g_spinnerAngle -= 360.0f;
                    if (g_proxyListState != ProxyListState::Loading && !g_speedTesting) KillTimer(hWnd, kSpinnerTimerId);
                    InvalidateRect(hWnd, nullptr, FALSE);
                } else if (wParam == kPulseTimerId) {
                    if (g_pulseRising) { g_pulseAlpha += 0.03f; if (g_pulseAlpha >= 1.0f) { g_pulseAlpha = 1.0f; g_pulseRising = false; } }
                    else { g_pulseAlpha -= 0.03f; if (g_pulseAlpha <= 0.4f) { g_pulseAlpha = 0.4f; g_pulseRising = true; } }
                    if (g_currentScreen == Screen::Home) InvalidateRect(hWnd, nullptr, FALSE);
                } else if (wParam == kBannerTimerId) {
                    std::lock_guard<std::mutex> lock(g_bannerMutex);
                    if (!g_bannerImages.empty()) { g_bannerIndex = (g_bannerIndex + 1) % (int)g_bannerImages.size(); if (g_currentScreen == Screen::Home) InvalidateRect(hWnd, nullptr, FALSE); }
                } else if (wParam == kAutoScanTimerId) {
                    if (g_settings.autoScanEnabled && g_proxyListState != ProxyListState::Loading) {
                        StartFetch(hWnd);
                    }
                }
                return 0;
            }
            case WM_APP_FETCH_DONE: {
                ProxySource::FetchResult result;
                { std::lock_guard<std::mutex> lock(g_fetchMutex); result = g_lastFetch; }
                size_t count = SnapshotProxies().size();
                if (!result.success) g_proxyListState = ProxyListState::Error;
                else if (count == 0) g_proxyListState = ProxyListState::Empty;
                else { g_proxyListState = ProxyListState::Loaded; StartPing(hWnd); }
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            case WM_APP_PING_PROGRESS: {
                if (wParam == 1) g_pingInProgress = false;
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            case WM_APP_SPEEDTEST_DONE: {
                g_speedTesting = false;
                g_speedHasResult = true;
                KillTimer(hWnd, kSpinnerTimerId);
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            case WM_APP_BANNERS_READY: {
                std::lock_guard<std::mutex> lock(g_bannerMutex);
                if (!g_bannerImages.empty()) SetTimer(hWnd, kBannerTimerId, 4000, nullptr);
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    LRESULT CALLBACK DialogWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_ERASEBKGND: return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
                RECT client; GetClientRect(hWnd, &client);
                int w = client.right, h = client.bottom;
                Bitmap bmp(w, h, PixelFormat32bppARGB);
                Graphics g(&bmp);
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                SolidBrush bg(FromColorref(g_palette->surface));
                GraphicsPath* panel = RoundedRectPath(Rect(0,0,w,h), Theme::CornerRadiusCard);
                g.FillPath(&bg, panel); delete panel;
                Pen border(FromColorrefAlpha(g_palette->tertiary, 120), 1);
                GraphicsPath* bp = RoundedRectPath(Rect(0,0,w-1,h-1), Theme::CornerRadiusCard);
                g.DrawPath(&border, bp); delete bp;

                FontFamily ff(Theme::FontFamily);
                Font titleFont(&ff, 12, FontStyleBold, UnitPoint);
                Font bodyFont(&ff, 9.5f, FontStyleRegular, UnitPoint);
                SolidBrush titleBrush(FromColorref(g_palette->onSurface));
                SolidBrush bodyBrush(FromColorrefAlpha(g_palette->onSurfaceVariant, 210));
                g.DrawString(TT(Loc::Key::ClearFavConfirmTitle).c_str(), -1, &titleFont, PointF(20,18), &titleBrush);
                g.DrawString(TT(Loc::Key::ClearFavConfirmBody).c_str(), -1, &bodyFont, RectF(20,48,(REAL)w-40,40), nullptr, &bodyBrush);

                Font btnFont(&ff, 10, FontStyleBold, UnitPoint);
                StringFormat cf; cf.SetAlignment(StringAlignmentCenter); cf.SetLineAlignment(StringAlignmentCenter);
                g_dlgNoRect = Rect(w-220, h-56, 90, 34);
                SolidBrush noBg(FromColorref(g_palette->surfaceVariant));
                GraphicsPath* np = RoundedRectPath(g_dlgNoRect, 8); g.FillPath(&noBg, np); delete np;
                g.DrawString(TT(Loc::Key::Cancel).c_str(), -1, &btnFont, RectF((REAL)g_dlgNoRect.X,(REAL)g_dlgNoRect.Y,(REAL)g_dlgNoRect.Width,(REAL)g_dlgNoRect.Height), &cf, &titleBrush);

                g_dlgYesRect = Rect(w-120, h-56, 100, 34);
                SolidBrush yesBg(FromColorref(g_palette->error));
                GraphicsPath* yp = RoundedRectPath(g_dlgYesRect, 8); g.FillPath(&yesBg, yp); delete yp;
                SolidBrush onDanger(Color(255,255,255,255));
                g.DrawString(TT(Loc::Key::Clear).c_str(), -1, &btnFont, RectF((REAL)g_dlgYesRect.X,(REAL)g_dlgYesRect.Y,(REAL)g_dlgYesRect.Width,(REAL)g_dlgYesRect.Height), &cf, &onDanger);

                Graphics screen(hdc);
                screen.DrawImage(&bmp, 0, 0);
                EndPaint(hWnd, &ps);
                return 0;
            }
            case WM_LBUTTONDOWN: {
                int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
                if (PtInRect(g_dlgYesRect, x, y)) { g_confirmDialogResult = true; DestroyWindow(hWnd); }
                else if (PtInRect(g_dlgNoRect, x, y)) { g_confirmDialogResult = false; DestroyWindow(hWnd); }
                return 0;
            }
            case WM_DESTROY: g_confirmDialogOpen = false; return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    bool ShowConfirmDialog(HWND parent) {
        static bool classRegistered = false;
        if (!classRegistered) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.lpfnWndProc = DialogWndProc;
            wc.hInstance = g_hInst;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = L"IwanaProxyConfirmDialog";
            RegisterClassExW(&wc);
            classRegistered = true;
        }
        RECT pr; GetWindowRect(parent, &pr);
        int dw = 360, dh = 150;
        int dx = pr.left + ((pr.right-pr.left)-dw)/2;
        int dy = pr.top + ((pr.bottom-pr.top)-dh)/2;
        g_confirmDialogOpen = true; g_confirmDialogResult = false;
        EnableWindow(parent, FALSE);
        HWND hDlg = CreateWindowExW(WS_EX_TOPMOST, L"IwanaProxyConfirmDialog", L"Confirm",
            WS_POPUP | WS_VISIBLE, dx, dy, dw, dh, parent, nullptr, g_hInst, nullptr);
        (void)hDlg;
        MSG msg;
        while (g_confirmDialogOpen && GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
        return g_confirmDialogResult;
    }
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInst = hInstance;
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    g_settings = Config::Load();
    ApplyThemeFromSettings();
    g_favoriteKeys = Storage::LoadFavoriteKeys();
    EnsureBrandLogosLoaded();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"IwanaProxyMainWindow";
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(0, L"IwanaProxyMainWindow", L"Iwana Proxy", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 760, nullptr, nullptr, hInstance, nullptr);
    if (!g_hWnd) return -1;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    SetTimer(g_hWnd, kPulseTimerId, 45, nullptr);
    StartFetch(g_hWnd);
    StartBannerFetch(g_hWnd);
    ConfigureAutoScanTimer(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    { std::lock_guard<std::mutex> lock(g_bannerMutex); for (auto* img : g_bannerImages) delete img; }
    delete g_backBuffer;
    GdiplusShutdown(g_gdiplusToken);
    return (int)msg.wParam;
}
