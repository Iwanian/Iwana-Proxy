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
        LangOption("en", "English", "🇺🇸"),
        LangOption("fa", "فارسی", "🇮🇷"),
        LangOption("ru", "Русский", "🇷🇺"),
        LangOption("zh", "中文", "🇨🇳"),
        LangOption("hi", "हिन्दी", "🇮🇳")
    )

    fun wrapContext(context: Context, languageCode: String?): Context {
        if (languageCode.isNullOrEmpty()) return context
        
        val locale = when (languageCode) {
            "fa" -> Locale("fa")
            "ru" -> Locale("ru")
            "zh" -> Locale("zh")
            "hi" -> Locale("hi")
            else -> Locale("en")
        }
        
        Locale.setDefault(locale)
        val config = Configuration(context.resources.configuration)
        config.setLocale(locale)
        config.setLayoutDirection(locale)
        
        return context.createConfigurationContext(config)
    }
}
