package com.example.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable

private val DarkColorScheme = darkColorScheme(
    primary = DarkPrimary,
    secondary = DarkSecondary,
    tertiary = DarkTertiary,
    background = DarkBackground,
    surface = DarkSurface,
    onBackground = DarkOnSurface,
    onSurface = DarkOnSurface,
    primaryContainer = DarkTertiary,
    onPrimaryContainer = DarkOnSurface,
    surfaceVariant = DarkSurface.copy(alpha = 0.8f),
    onSurfaceVariant = DarkOnSurface.copy(alpha = 0.9f)
)

private val LightColorScheme = lightColorScheme(
    primary = LightPrimary,
    secondary = LightSecondary,
    tertiary = LightTertiary,
    background = LightBackground,
    surface = LightSurface,
    onBackground = LightOnSurface,
    onSurface = LightOnSurface,
    primaryContainer = LightTertiary.copy(alpha = 0.25f),
    onPrimaryContainer = LightOnSurface,
    surfaceVariant = LightSurface,
    onSurfaceVariant = LightOnSurface
)

@Composable
fun MyApplicationTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colorScheme = if (darkTheme) DarkColorScheme else LightColorScheme

    MaterialTheme(
        colorScheme = colorScheme,
        typography = Typography,
        content = content
    )
}

@Composable
fun isAppDarkTheme(): Boolean {
    return MaterialTheme.colorScheme.background == DarkBackground
}
