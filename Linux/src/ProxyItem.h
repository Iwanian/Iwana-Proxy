// ProxyItem.h — Data model for a parsed MTProto proxy.
// Direct port of the Windows build's ProxyItem.h; only std::wstring ->
// std::string changed (UTF-8 throughout on Linux).
#pragma once
#include <string>

struct ProxyItem {
    int         id = 0;
    std::string server;
    std::string port;
    std::string secret;
    std::string link;        // original tg://proxy?... link, used to open Telegram
    int         pingMs = -1; // -1 = not yet tested / unreachable
    bool        isScanned = false; // true once a ping attempt has completed (success or fail)
    bool        isFavorite = false;
    bool        isForDownload = false; // "starred" marker found in source list
    bool        isRussian = false;

    bool IsAlive() const { return isScanned && pingMs >= 0; }
    std::string Key() const { return server + ":" + port; }
};
