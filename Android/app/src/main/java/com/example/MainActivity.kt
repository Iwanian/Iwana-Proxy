package com.example

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.example.ui.theme.MyApplicationTheme
import kotlinx.coroutines.flow.first
import okhttp3.Cache
import okhttp3.OkHttpClient
import java.io.File
import java.util.concurrent.TimeUnit

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Initialize Core Dependencies with HTTP Disk Cache
        val dataStoreManager = DataStoreManager(applicationContext)
        val okHttpClient = OkHttpClient.Builder()
            .cache(Cache(File(applicationContext.cacheDir, "http_cache"), 30L * 1024 * 1024))
            .connectTimeout(8, TimeUnit.SECONDS)
            .readTimeout(8, TimeUnit.SECONDS)
            .writeTimeout(8, TimeUnit.SECONDS)
            .build()

        val repository = ProxyRepository(client = okHttpClient)
        val bannerRepository = BannerRepository(client = okHttpClient)
        
        // Instantiate our unique shared ProxyViewModel with safe manual constructor factory injection
        val viewModel = ViewModelProvider(this, object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T {
                return ProxyViewModel(repository, dataStoreManager, bannerRepository) as T
            }
        })[ProxyViewModel::class.java]

        enableEdgeToEdge()
        
        setContent {
            val selectedLanguage by viewModel.selectedLanguage.collectAsStateWithLifecycle()
            val themeMode by viewModel.themeMode.collectAsStateWithLifecycle()
            val context = LocalContext.current
            
            // Build dynamically wrapped localized configuration context
            val localizedContext = remember(selectedLanguage) {
                LanguageManager.wrapContext(context, selectedLanguage)
            }
            
            val darkTheme = when (themeMode) {
                "light" -> false
                "dark" -> true
                else -> androidx.compose.foundation.isSystemInDarkTheme()
            }
            
            val layoutDirection = remember(selectedLanguage) {
                if (selectedLanguage == "fa") LayoutDirection.Rtl else LayoutDirection.Ltr
            }

            // Clamp extreme system font scaling to maintain safe layout bounds
            val currentDensity = LocalDensity.current
            val clampedDensity = remember(currentDensity) {
                Density(
                    density = currentDensity.density,
                    fontScale = currentDensity.fontScale.coerceIn(0.85f, 1.15f)
                )
            }

            CompositionLocalProvider(
                LocalContext provides localizedContext,
                LocalLayoutDirection provides layoutDirection,
                LocalDensity provides clampedDensity
            ) {
                MyApplicationTheme(darkTheme = darkTheme) {
                    val navController = rememberNavController()
                    
                    Box(modifier = Modifier.fillMaxSize()) {
                        NavHost(
                            navController = navController,
                            startDestination = "splash",
                            modifier = Modifier.fillMaxSize()
                        ) {
                            // Asynchronous Splash/Onboard redirection checkpoint
                            composable("splash") {
                                Box(
                                    modifier = Modifier.fillMaxSize(),
                                    contentAlignment = Alignment.Center
                                ) {
                                    Column(
                                        horizontalAlignment = Alignment.CenterHorizontally,
                                        verticalArrangement = Arrangement.Center
                                    ) {
                                        Image(
                                            painter = painterResource(id = R.drawable.img_app_icon_cosmic_1782147636663),
                                            contentDescription = "App Icon Splash",
                                            modifier = Modifier
                                                .size(150.dp)
                                                .clip(RoundedCornerShape(32.dp))
                                        )
                                        Spacer(modifier = Modifier.height(20.dp))
                                        Text(
                                            text = "Iwana Proxy",
                                            fontSize = 26.sp,
                                            fontWeight = FontWeight.Black,
                                            color = MaterialTheme.colorScheme.primary
                                        )
                                        Spacer(modifier = Modifier.height(32.dp))
                                        CircularProgressIndicator(
                                            modifier = Modifier.size(24.dp),
                                            strokeWidth = 2.5.dp,
                                            color = MaterialTheme.colorScheme.primary
                                        )
                                    }
                                }
                                
                                LaunchedEffect(Unit) {
                                    // Fetch the exact selected language from storage directly with a timeout to avoid hanging
                                    val currentLang = kotlinx.coroutines.withTimeoutOrNull(2000L) {
                                        viewModel.dataStoreManager.selectedLanguageFlow.first()
                                    }
                                    
                                    // A small visual delay to let users admire the custom artwork splash
                                    kotlinx.coroutines.delay(1000)
                                    
                                    if (currentLang.isNullOrBlank()) {
                                        navController.navigate("language_selection") {
                                            popUpTo("splash") { inclusive = true }
                                        }
                                    } else {
                                        navController.navigate("home") {
                                            popUpTo("splash") { inclusive = true }
                                        }
                                    }
                                }
                            }
                            
                            // Onboarding Language Choice Screen
                            composable("language_selection") {
                                LanguageSelectionScreen(
                                    viewModel = viewModel,
                                    onLanguageSelected = {
                                        navController.navigate("home") {
                                            popUpTo("language_selection") { inclusive = true }
                                        }
                                    }
                                )
                            }
                            
                            // Main working MTProto list screen
                            composable("home") {
                                ProxyScreen(
                                    viewModel = viewModel,
                                    onNavigateToSettings = {
                                        navController.navigate("settings")
                                    }
                                )
                            }
                            
                            // Preferences Configuration (Switch and Locale change) screen
                            composable("settings") {
                                SettingsScreen(
                                    viewModel = viewModel,
                                    onBackClick = {
                                        navController.popBackStack()
                                    },
                                    onNavigateToSavedProxies = {
                                        navController.navigate("saved_proxies")
                                    },
                                    onNavigateToSpeedTest = {
                                        navController.navigate("proxy_speed_test")
                                    },
                                    onNavigateToSupport = {
                                        navController.navigate("support")
                                    }
                                )
                            }

                            // Saved Proxies screen
                            composable("saved_proxies") {
                                SavedProxiesScreen(
                                    viewModel = viewModel,
                                    onBackClick = {
                                        navController.popBackStack()
                                    }
                                )
                            }

                            // Proxy Speed Test screen
                            composable("proxy_speed_test") {
                                ProxySpeedTestScreen(
                                    viewModel = viewModel,
                                    onBackClick = {
                                        navController.popBackStack()
                                    }
                                )
                            }

                            // Support Us Screen
                            composable("support") {
                                SupportScreen(
                                    viewModel = viewModel,
                                    onBackClick = {
                                        navController.popBackStack()
                                    }
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
