package com.example

import android.content.Context
import android.content.res.Configuration
import java.util.Locale

object LanguageManager {
    
    data class LangOption(
        val code: String,
        val displayName: String,
        val emoji: String
    )

    val supportedLanguages = listOf(
        LangOption("fa", "فارسی", "❤️"),
        LangOption("en", "English", "🇺🇸"),
        LangOption("ru", "Русский", "🇷🇺")
    )

    fun wrapContext(context: Context, languageCode: String?): Context {
        if (languageCode.isNullOrEmpty()) return context
        
        val locale = when (languageCode) {
            "fa" -> Locale("fa")
            "ru" -> Locale("ru")
            else -> Locale("en")
        }
        
        Locale.setDefault(locale)
        val config = Configuration(context.resources.configuration)
        config.setLocale(locale)
        config.setLayoutDirection(locale)
        
        return context.createConfigurationContext(config)
    }
}
