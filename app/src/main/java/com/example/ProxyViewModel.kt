package com.example

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch
import java.io.IOException

class ProxyViewModel(
    private val repository: ProxyRepository,
    val dataStoreManager: DataStoreManager
) : ViewModel() {

    private val _uiState = MutableStateFlow<UiState>(UiState.Loading)
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    private val _isRefreshing = MutableStateFlow(false)
    val isRefreshing: StateFlow<Boolean> = _isRefreshing.asStateFlow()

    private val _searchQuery = MutableStateFlow("")
    val searchQuery: StateFlow<String> = _searchQuery.asStateFlow()

    private val _autoScan = MutableStateFlow(false)
    val autoScan: StateFlow<Boolean> = _autoScan.asStateFlow()

    val selectedLanguage: StateFlow<String?> = dataStoreManager.selectedLanguageFlow
        .stateIn(viewModelScope, SharingStarted.Eagerly, null)

    val themeMode: StateFlow<String> = dataStoreManager.themeModeFlow
        .stateIn(viewModelScope, SharingStarted.Eagerly, "system")

    val favorites: StateFlow<Set<String>> = dataStoreManager.favoritesFlow
        .stateIn(viewModelScope, SharingStarted.Lazily, emptySet())

    // Complete list of currently parsed proxies (might have some dead ones or unscanned ones initially)
    private val _allProxiesList = MutableStateFlow<List<ProxyItem>>(emptyList())

    // Reactive list of proxies that are filtered of dead entries, matched with favorites, and searched
    val displayProxies: StateFlow<List<ProxyItem>> = combine(
        _allProxiesList,
        _searchQuery,
        favorites
    ) { proxies, query, favSet ->
        val mappedAndFiltered = proxies.map { proxy ->
            proxy.copy(isFavorite = favSet.contains(proxy.link))
        }.filter { proxy ->
            !proxy.isScanned || proxy.isAlive
        }

        val searched = if (query.isBlank()) {
            mappedAndFiltered
        } else {
            mappedAndFiltered.filter { proxy ->
                proxy.server.contains(query, ignoreCase = true) ||
                proxy.port.toString().contains(query)
            }
        }

        // Sort: verified alive proxies first (sorted by ping), then unscanned proxies (sorted by stable original ID)
        searched.sortedWith(compareBy<ProxyItem> { !it.isAlive }.thenBy { if (it.isAlive) it.ping else it.id.toLong() })
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())

    private var autoScanJob: Job? = null
    private var scanningJob: Job? = null

    private fun startLiveScan(proxies: List<ProxyItem>) {
        scanningJob?.cancel()
        val scope = this // Keep reference to our ViewModel / scope-capable object
        scanningJob = viewModelScope.launch(Dispatchers.Default) {
            val unscannedList = proxies.map { it.copy(isScanned = false, ping = -1, isAlive = false) }
            _allProxiesList.value = unscannedList
            _uiState.value = UiState.Success(unscannedList)
            
            val latestList = unscannedList.toMutableList()
            val semaphore = kotlinx.coroutines.sync.Semaphore(8)
            unscannedList.mapIndexed { index, proxy ->
                launch {
                    semaphore.acquire()
                    try {
                        val latency = PingService.ping(proxy.server, proxy.port)
                        val updated = if (latency >= 0) {
                            proxy.copy(ping = latency, isAlive = true, isScanned = true)
                        } else {
                            proxy.copy(ping = -1, isAlive = false, isScanned = true)
                        }
                        synchronized(latestList) {
                            latestList[index] = updated
                            _allProxiesList.value = latestList.toList()
                        }
                    } finally {
                        semaphore.release()
                    }
                }
            }
        }
    }

    init {
        // Fetch and scan on initiation
        fetchAndScanProxies()

        // Observe Auto Scan setting to start/stop the 60s timer loop
        viewModelScope.launch {
            dataStoreManager.autoScanFlow.collect { enabled ->
                _autoScan.value = enabled
                if (enabled) {
                    startAutoScanProgress()
                } else {
                    stopAutoScanProgress()
                }
            }
        }
    }

    /**
     * Downloads proxies from the GitHub raw endpoint and pings them simultaneously.
     */
    fun fetchAndScanProxies() {
        viewModelScope.launch {
            if (_uiState.value is UiState.Loading && _allProxiesList.value.isNotEmpty()) {
                // Keep displaying what we have if already loaded
            } else {
                _uiState.value = UiState.Loading
            }
            
            try {
                val downloaded = repository.fetchProxies()
                if (downloaded.isEmpty()) {
                    _allProxiesList.value = emptyList()
                    _uiState.value = UiState.Success(emptyList())
                    return@launch
                }
                
                // Instantly display and start live background scan
                startLiveScan(downloaded)
            } catch (e: Exception) {
                _uiState.value = UiState.Error(e.localizedMessage ?: "Failed to load proxies")
            }
        }
    }

    /**
     * Pull-to-refresh execution
     */
    fun refresh() {
        viewModelScope.launch {
            _isRefreshing.value = true
            try {
                val downloaded = repository.fetchProxies()
                if (downloaded.isNotEmpty()) {
                    startLiveScan(downloaded)
                }
            } catch (e: Exception) {
                // If refresh fails, keep current state
            } finally {
                _isRefreshing.value = false
            }
        }
    }

    fun onSearchQueryChanged(query: String) {
        _searchQuery.value = query
    }

    fun toggleFavorite(proxy: ProxyItem) {
        viewModelScope.launch(Dispatchers.IO) {
            val currentFavs = favorites.value
            if (currentFavs.contains(proxy.link)) {
                dataStoreManager.removeFavorite(proxy.link)
            } else {
                dataStoreManager.addFavorite(proxy.link)
            }
        }
    }

    fun toggleAutoScan(enabled: Boolean) {
        viewModelScope.launch {
            dataStoreManager.setAutoScan(enabled)
        }
    }

    fun setLanguage(languageCode: String) {
        viewModelScope.launch {
            dataStoreManager.saveSelectedLanguage(languageCode)
        }
    }

    fun setThemeMode(themeMode: String) {
        viewModelScope.launch {
            dataStoreManager.saveThemeMode(themeMode)
        }
    }

    private fun startAutoScanProgress() {
        autoScanJob?.cancel()
        autoScanJob = viewModelScope.launch {
            while (true) {
                delay(60000L) // 60 seconds interval
                try {
                    val downloaded = repository.fetchProxies()
                    if (downloaded.isNotEmpty()) {
                        startLiveScan(downloaded)
                    }
                } catch (ignored: Exception) {}
            }
        }
    }

    private fun stopAutoScanProgress() {
        autoScanJob?.cancel()
        autoScanJob = null
        scanningJob?.cancel()
        scanningJob = null
    }

    override fun onCleared() {
        super.onCleared()
        stopAutoScanProgress()
    }
}
