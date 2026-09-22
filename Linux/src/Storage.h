// Storage.h — Persistent favorites storage. Plain newline-delimited
// "server:port" list on disk, same format as the Windows build, just moved
// to the XDG data dir (PORT_SPEC.md §10: ~/.local/share/iwana-proxy/).
#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include "Config.h"

namespace Storage {

    inline std::string FavoritesFilePath() {
        return Config::DataDir() + "/favorites.txt";
    }

    inline std::unordered_set<std::string> LoadFavoriteKeys() {
        std::unordered_set<std::string> keys;
        std::ifstream f(FavoritesFilePath());
        if (!f.is_open()) return keys;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty()) keys.insert(line);
        }
        return keys;
    }

    inline void SaveFavoriteKeys(const std::unordered_set<std::string>& keys) {
        std::ofstream f(FavoritesFilePath(), std::ios::trunc);
        if (!f.is_open()) return;
        for (const auto& k : keys) f << k << "\n";
    }

}
