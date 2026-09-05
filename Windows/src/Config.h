// Config.h — Lightweight settings persistence (INI file in %APPDATA%\IwanaProxy)
// and the canonical ordered proxy-source list used by the fetch pipeline.
//
// IMPORTANT (per product requirement):
//   Source fetch order is strict and sequential:
//     1) Primary GitHub raw URL
//     2) Fallback GitHub-adjacent URL
//     3) If both fail -> use last cached raw text on disk (offline mode)
//   This file only declares the source list; ProxySource.h implements the
//   actual sequential-fallback fetch logic in Stage 5 (business logic).
#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include <shlobj.h>

namespace Config {

    // Mirrors Android ProxyRepository.kt source order exactly.
    inline const std::vector<std::wstring> ProxySourceUrls = {
        L"https://raw.githubusercontent.com/Iwanian/Sub/main/Proxy-Channel-%2540I_w_a_n_a.txt",
        L"https://c-mamad.ir/proxies/proxy.txt"
    };

    struct AppSettings {
        std::wstring language   = L"en";   // "fa" | "en" | "ru"
        std::wstring themeMode  = L"system"; // "light" | "dark" | "system"
        bool autoScanEnabled    = false;
        int  autoScanIntervalS  = 15;
        bool bannerEnabled      = true;
    };

    // Returns e.g. C:\Users\<user>\AppData\Roaming\IwanaProxy
    inline std::wstring GetAppDataDir() {
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
            std::wstring dir = std::wstring(path) + L"\\IwanaProxy";
            CreateDirectoryW(dir.c_str(), nullptr);
            return dir;
        }
        return L".";
    }

    inline std::wstring SettingsFilePath() {
        return GetAppDataDir() + L"\\settings.ini";
    }

    inline std::wstring CacheFilePath() {
        return GetAppDataDir() + L"\\proxies_cache.txt";
    }

    // Minimal INI read/write (no external deps, uses WinAPI profile functions).
    inline AppSettings Load() {
        AppSettings s;
        std::wstring path = SettingsFilePath();
        wchar_t buf[64];

        GetPrivateProfileStringW(L"App", L"Language", L"en", buf, 64, path.c_str());
        s.language = buf;

        GetPrivateProfileStringW(L"App", L"Theme", L"system", buf, 64, path.c_str());
        s.themeMode = buf;

        s.autoScanEnabled  = GetPrivateProfileIntW(L"App", L"AutoScanEnabled", 0, path.c_str()) != 0;
        s.autoScanIntervalS = GetPrivateProfileIntW(L"App", L"AutoScanInterval", 15, path.c_str());
        s.bannerEnabled    = GetPrivateProfileIntW(L"App", L"BannerEnabled", 1, path.c_str()) != 0;
        return s;
    }

    inline void Save(const AppSettings& s) {
        std::wstring path = SettingsFilePath();
        WritePrivateProfileStringW(L"App", L"Language", s.language.c_str(), path.c_str());
        WritePrivateProfileStringW(L"App", L"Theme", s.themeMode.c_str(), path.c_str());
        WritePrivateProfileStringW(L"App", L"AutoScanEnabled", s.autoScanEnabled ? L"1" : L"0", path.c_str());
        WritePrivateProfileStringW(L"App", L"AutoScanInterval", std::to_wstring(s.autoScanIntervalS).c_str(), path.c_str());
        WritePrivateProfileStringW(L"App", L"BannerEnabled", s.bannerEnabled ? L"1" : L"0", path.c_str());
    }

}
