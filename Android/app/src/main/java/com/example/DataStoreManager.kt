package com.example

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.core.stringSetPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.firstOrNull
import kotlinx.coroutines.flow.map

class DataStoreManager(private val context: Context) {

    companion object {
        private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "iwana_proxy_prefs")
        private val KEY_SELECTED_LANGUAGE = stringPreferencesKey("selected_language")
        private val KEY_FAVORITES = stringSetPreferencesKey("favorites")
        private val KEY_THEME_MODE = stringPreferencesKey("theme_mode")
        private val KEY_AUTO_SCAN_ENABLED = booleanPreferencesKey("auto_scan_enabled")
        private val KEY_AUTO_SCAN_INTERVAL = intPreferencesKey("auto_scan_interval")
        private val KEY_BANNER_SLIDER_ENABLED = booleanPreferencesKey("banner_slider_enabled")
        private val KEY_CACHED_PROXIES = stringPreferencesKey("cached_proxies")
        private val KEY_CACHED_BANNERS = stringPreferencesKey("cached_banners")
    }

    val selectedLanguageFlow: Flow<String?> = context.dataStore.data.map { preferences ->
        preferences[KEY_SELECTED_LANGUAGE]
    }

    val themeModeFlow: Flow<String> = context.dataStore.data.map { preferences ->
        preferences[KEY_THEME_MODE] ?: "system"
    }

    val favoritesFlow: Flow<Set<String>> = context.dataStore.data.map { preferences ->
        preferences[KEY_FAVORITES] ?: emptySet()
    }

    val autoScanEnabledFlow: Flow<Boolean> = context.dataStore.data.map { preferences ->
        preferences[KEY_AUTO_SCAN_ENABLED] ?: false
    }

    val autoScanIntervalFlow: Flow<Int> = context.dataStore.data.map { preferences ->
        preferences[KEY_AUTO_SCAN_INTERVAL] ?: 15
    }

    val bannerSliderEnabledFlow: Flow<Boolean> = context.dataStore.data.map { preferences ->
        preferences[KEY_BANNER_SLIDER_ENABLED] ?: true
    }

    suspend fun saveSelectedLanguage(languageCode: String) {
        context.dataStore.edit { preferences ->
            preferences[KEY_SELECTED_LANGUAGE] = languageCode
        }
    }

    suspend fun saveFavorites(favorites: Set<String>) {
        context.dataStore.edit { preferences ->
            preferences[KEY_FAVORITES] = favorites
        }
    }

    suspend fun addFavorite(proxyLink: String) {
        context.dataStore.edit { preferences ->
            val current = preferences[KEY_FAVORITES] ?: emptySet()
            preferences[KEY_FAVORITES] = current + proxyLink
        }
    }

    suspend fun removeFavorite(proxyLink: String) {
        context.dataStore.edit { preferences ->
            val current = preferences[KEY_FAVORITES] ?: emptySet()
            preferences[KEY_FAVORITES] = current - proxyLink
        }
    }

    suspend fun toggleFavorite(proxyLink: String) {
        context.dataStore.edit { preferences ->
            val current = preferences[KEY_FAVORITES] ?: emptySet()
            if (current.contains(proxyLink)) {
                preferences[KEY_FAVORITES] = current - proxyLink
            } else {
                preferences[KEY_FAVORITES] = current + proxyLink
            }
        }
    }

    suspend fun saveThemeMode(themeMode: String) {
        context.dataStore.edit { preferences ->
            preferences[KEY_THEME_MODE] = themeMode
        }
    }

    suspend fun saveAutoScanEnabled(enabled: Boolean) {
        context.dataStore.edit { preferences ->
            preferences[KEY_AUTO_SCAN_ENABLED] = enabled
        }
    }

    suspend fun saveAutoScanInterval(seconds: Int) {
        context.dataStore.edit { preferences ->
            preferences[KEY_AUTO_SCAN_INTERVAL] = seconds
        }
    }

    suspend fun saveBannerSliderEnabled(enabled: Boolean) {
        context.dataStore.edit { preferences ->
            preferences[KEY_BANNER_SLIDER_ENABLED] = enabled
        }
    }

    suspend fun saveCachedProxies(proxiesRaw: String) {
        context.dataStore.edit { preferences ->
            preferences[KEY_CACHED_PROXIES] = proxiesRaw
        }
    }

    suspend fun getCachedProxies(): String? {
        return context.dataStore.data.firstOrNull()?.get(KEY_CACHED_PROXIES)
    }

    suspend fun saveCachedBanners(bannersJson: String) {
        context.dataStore.edit { preferences ->
            preferences[KEY_CACHED_BANNERS] = bannersJson
        }
    }

    suspend fun getCachedBanners(): String? {
        return context.dataStore.data.firstOrNull()?.get(KEY_CACHED_BANNERS)
    }
}
