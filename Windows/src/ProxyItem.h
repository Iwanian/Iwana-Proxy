// ProxyItem.h — Data model for a parsed MTProto proxy (mirrors Android ProxyItem.kt).
#pragma once
#include <string>

struct ProxyItem {
    int          id = 0;
    std::wstring server;
    std::wstring port;
    std::wstring secret;
    std::wstring link;        // original tg://proxy?... link, used to open Telegram
    int          pingMs = -1; // -1 = not yet tested / unreachable
    bool         isScanned = false; // true once a ping attempt has completed (success or fail)
    bool         isFavorite = false;
    bool         isForDownload = false; // "starred" marker found in source list
    bool         isRussian = false;

    bool IsAlive() const { return isScanned && pingMs >= 0; }
    std::wstring Key() const { return server + L":" + port; }
};
