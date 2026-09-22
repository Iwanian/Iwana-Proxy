// Config.h — Lightweight settings persistence and the canonical ordered
// proxy-source list used by the fetch pipeline.
//
// Ported from the Windows build's Config.h. Same semantics, but:
//   - Paths follow the XDG Base Directory spec instead of %APPDATA%, exactly
//     as suggested in PORT_SPEC.md §10:
//       settings.ini        -> ~/.config/iwana-proxy/settings.ini
//       proxies_cache.txt   -> ~/.cache/iwana-proxy/proxies_cache.txt
//       favorites.txt       -> ~/.local/share/iwana-proxy/favorites.txt (see Storage.h)
//   - No WinAPI GetPrivateProfileString*: a tiny hand-rolled flat "key=value"
//     INI reader/writer replaces it (no external deps needed for something
//     this small).
//
// IMPORTANT (per product requirement, unchanged from Windows):
//   Source fetch order is strict and sequential:
//     1) Primary GitHub raw URL
//     2) Fallback GitHub-adjacent URL
//     3) If both fail -> use last cached raw text on disk (offline mode)
//   This file only declares the source list; ProxySource.h implements the
//   actual sequential-fallback fetch logic.
#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <cstdlib>

namespace Config {

    // Mirrors the Windows build's Config::ProxySourceUrls exactly.
    inline const std::vector<std::string> ProxySourceUrls = {
        "https://raw.githubusercontent.com/Iwanian/Sub/main/Proxy-Channel-%2540I_w_a_n_a.txt",
        "https://c-mamad.ir/proxies/proxy.txt"
    };

    struct AppSettings {
        std::string language   = "en";     // "fa" | "en" | "ru"
        std::string themeMode  = "system"; // "light" | "dark" | "system"
        bool autoScanEnabled   = false;
        int  autoScanIntervalS = 15;
        bool bannerEnabled     = true;
    };

    inline std::string HomeDir() {
        const char* home = std::getenv("HOME");
        return home ? std::string(home) : std::string(".");
    }

    inline void EnsureDir(const std::string& dir) {
        // Real recursive mkdir -p. The single mkdir() this replaced silently
        // failed (ENOENT, unchecked) whenever an intermediate component
        // didn't already exist — e.g. ~/.local/share/iwana-proxy on any
        // system/container where ~/.local itself has never been created by
        // another app yet. That failure was invisible: settings/favorites
        // just never persisted, with no error anywhere.
        if (dir.empty() || dir == "/") return;
        std::string path;
        size_t pos = 0;
        if (dir[0] == '/') { path = "/"; pos = 1; }
        while (pos <= dir.size()) {
            size_t slash = dir.find('/', pos);
            std::string component = dir.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
            if (!component.empty()) {
                path += component;
                mkdir(path.c_str(), 0755); // ignore EEXIST and other errors; verified by caller's fopen success
                path += "/";
            }
            if (slash == std::string::npos) break;
            pos = slash + 1;
        }
    }

    inline std::string XdgDir(const char* envVar, const std::string& fallbackRelToHome) {
        const char* v = std::getenv(envVar);
        std::string base = (v && *v) ? std::string(v) : (HomeDir() + fallbackRelToHome);
        EnsureDir(base);
        std::string dir = base + "/iwana-proxy";
        EnsureDir(dir);
        return dir;
    }

    inline std::string ConfigDir() { return XdgDir("XDG_CONFIG_HOME", "/.config"); }
    inline std::string CacheDir()  { return XdgDir("XDG_CACHE_HOME", "/.cache"); }
    inline std::string DataDir()   { return XdgDir("XDG_DATA_HOME", "/.local/share"); }

    inline std::string SettingsFilePath() { return ConfigDir() + "/settings.ini"; }
    inline std::string CacheFilePath()    { return CacheDir() + "/proxies_cache.txt"; }

    // Minimal flat "key=value" INI reader (one [App] section, ini-ish but we
    // don't bother parsing section headers since there's only ever one).
    inline AppSettings Load() {
        AppSettings s;
        std::ifstream f(SettingsFilePath());
        if (!f.is_open()) return s;
        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if (key == "Language") s.language = val;
            else if (key == "Theme") s.themeMode = val;
            else if (key == "AutoScanEnabled") s.autoScanEnabled = (val == "1");
            else if (key == "AutoScanInterval") s.autoScanIntervalS = std::atoi(val.c_str());
            else if (key == "BannerEnabled") s.bannerEnabled = (val == "1");
        }
        return s;
    }

    inline void Save(const AppSettings& s) {
        std::ofstream f(SettingsFilePath(), std::ios::trunc);
        if (!f.is_open()) return;
        f << "Language=" << s.language << "\n";
        f << "Theme=" << s.themeMode << "\n";
        f << "AutoScanEnabled=" << (s.autoScanEnabled ? "1" : "0") << "\n";
        f << "AutoScanInterval=" << s.autoScanIntervalS << "\n";
        f << "BannerEnabled=" << (s.bannerEnabled ? "1" : "0") << "\n";
    }

}
