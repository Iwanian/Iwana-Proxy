// Storage.h — Persistent favorites storage (equivalent of Android DataStoreManager.kt
// for the favorites feature). Plain newline-delimited "server:port" list on disk.
#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include "Config.h"

namespace Storage {

    inline std::wstring FavoritesFilePath() {
        return Config::GetAppDataDir() + L"\\favorites.txt";
    }

    inline std::unordered_set<std::wstring> LoadFavoriteKeys() {
        std::unordered_set<std::wstring> keys;
        std::wifstream f(FavoritesFilePath().c_str());
        if (!f.is_open()) return keys;
        std::wstring line;
        while (std::getline(f, line)) {
            if (!line.empty()) keys.insert(line);
        }
        return keys;
    }

    inline void SaveFavoriteKeys(const std::unordered_set<std::wstring>& keys) {
        std::wofstream f(FavoritesFilePath().c_str(), std::ios::trunc);
        if (!f.is_open()) return;
        for (const auto& k : keys) f << k << L"\n";
    }

}
