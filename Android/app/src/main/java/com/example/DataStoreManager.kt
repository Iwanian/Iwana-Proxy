package com.example

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.core.stringSetPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

class DataStoreManager(private val context: Context) {

    companion object {
        private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "iwana_proxy_prefs")
        private val KEY_SELECTED_LANGUAGE = stringPreferencesKey("selected_language")
        private val KEY_FAVORITES = stringSetPreferencesKey("favorites")
        private val KEY_AUTO_SCAN = booleanPreferencesKey("auto_scan")
        private val KEY_THEME_MODE = stringPreferencesKey("theme_mode")
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

    val autoScanFlow: Flow<Boolean> = context.dataStore.data.map { preferences ->
        preferences[KEY_AUTO_SCAN] ?: false
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

    suspend fun setAutoScan(enabled: Boolean) {
        context.dataStore.edit { preferences ->
            preferences[KEY_AUTO_SCAN] = enabled
        }
    }

    suspend fun saveThemeMode(themeMode: String) {
        context.dataStore.edit { preferences ->
            preferences[KEY_THEME_MODE] = themeMode
        }
    }
}
