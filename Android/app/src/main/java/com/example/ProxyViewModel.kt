package com.example

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject
import java.io.IOException

class ProxyViewModel(
    private val repository: ProxyRepository,
    val dataStoreManager: DataStoreManager,
    private val bannerRepository: BannerRepository = BannerRepository()
) : ViewModel() {

    private val _bannerItems = MutableStateFlow<List<BannerItem>>(emptyList())
    val bannerItems: StateFlow<List<BannerItem>> = _bannerItems.asStateFlow()

    private val _isBannerDismissed = MutableStateFlow(false)
    val isBannerDismissed: StateFlow<Boolean> = _isBannerDismissed.asStateFlow()

    fun dismissBanner() {
        _isBannerDismissed.value = true
    }

    private val _uiState = MutableStateFlow<UiState>(UiState.Loading)
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    private val _isRefreshing = MutableStateFlow(false)
    val isRefreshing: StateFlow<Boolean> = _isRefreshing.asStateFlow()

    private val _isOfflineNoticeVisible = MutableStateFlow(false)
    val isOfflineNoticeVisible: StateFlow<Boolean> = _isOfflineNoticeVisible.asStateFlow()

    private val _searchQuery = MutableStateFlow("")
    val searchQuery: StateFlow<String> = _searchQuery.asStateFlow()

    val selectedLanguage: StateFlow<String?> = dataStoreManager.selectedLanguageFlow
        .stateIn(viewModelScope, SharingStarted.Eagerly, null)

    val themeMode: StateFlow<String> = dataStoreManager.themeModeFlow
        .stateIn(viewModelScope, SharingStarted.Eagerly, "system")

    val autoScanEnabled: StateFlow<Boolean> = dataStoreManager.autoScanEnabledFlow
        .stateIn(viewModelScope, SharingStarted.Eagerly, false)

    val autoScanInterval: StateFlow<Int> = dataStoreManager.autoScanIntervalFlow
        .stateIn(viewModelScope, SharingStarted.Eagerly, 15)

    val bannerSliderEnabled: StateFlow<Boolean> = dataStoreManager.bannerSliderEnabledFlow
        .stateIn(viewModelScope, SharingStarted.Eagerly, true)

    val savedLinks: StateFlow<Set<String>> = dataStoreManager.favoritesFlow
        .stateIn(viewModelScope, SharingStarted.Eagerly, emptySet())

    // Complete list of currently parsed proxies (might have some dead ones or unscanned ones initially)
    private val _allProxiesList = MutableStateFlow<List<ProxyItem>>(emptyList())

    val savedProxies: StateFlow<List<ProxyItem>> = combine(
        _allProxiesList,
        savedLinks
    ) { allList, favSet ->
        val existingSaved = allList.filter { favSet.contains(it.link) }
        val existingLinks = existingSaved.map { it.link }.toSet()
        val missingLinks = favSet - existingLinks
        if (missingLinks.isEmpty()) {
            existingSaved.map { it.copy(isFavorite = true) }
        } else {
            val parsedMissing = ProxyParser.parse(missingLinks.joinToString("\n"))
            (existingSaved + parsedMissing).map { it.copy(isFavorite = true) }
        }
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())

    fun toggleSave(link: String) {
        viewModelScope.launch {
            dataStoreManager.toggleFavorite(link)
        }
    }

    // Reactive list of proxies that are filtered of dead entries and very high ping entries, matched with favorites, and searched
    val displayProxies: StateFlow<List<ProxyItem>> = combine(
        _allProxiesList,
        _searchQuery,
        _isOfflineNoticeVisible
    ) { proxies, query, isOfflineNotice ->
        val hasAnyAlive = proxies.any { it.isAlive }
        val allScanned = proxies.isNotEmpty() && proxies.all { it.isScanned }

        val filtered = when {
            isOfflineNotice -> proxies // Always show all cached proxies in offline fallback mode
            allScanned && !hasAnyAlive -> proxies // If ping scan fails on all (e.g. offline device), preserve all proxies
            else -> {
                val aliveOrUnscanned = proxies.filter { !it.isScanned || (it.isAlive && it.ping in 1..1200L) }
                if (aliveOrUnscanned.isEmpty() && proxies.isNotEmpty()) {
                    proxies
                } else {
                    aliveOrUnscanned
                }
            }
        }

        val searched = if (query.isBlank()) {
            filtered
        } else {
            filtered.filter { proxy ->
                proxy.server.contains(query, ignoreCase = true) ||
                proxy.port.toString().contains(query)
            }
        }

        // Sort: verified alive proxies first (sorted by ping), then others (sorted by stable original ID)
        searched.sortedWith(compareBy<ProxyItem> { !it.isAlive }.thenBy { if (it.isAlive) it.ping else it.id.toLong() })
    }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())

    private var scanningJob: Job? = null
    private var autoScanJob: Job? = null

    private fun startLiveScan(proxies: List<ProxyItem>) {
        scanningJob?.cancel()
        val scope = this // Keep reference to our ViewModel / scope-capable object
        scanningJob = viewModelScope.launch(Dispatchers.Default) {
            val unscannedList = proxies.map { it.copy(isScanned = false, ping = -1, isAlive = false) }
            _allProxiesList.value = unscannedList
            _uiState.value = UiState.Success(unscannedList)
            
            val latestList = unscannedList.toMutableList()
            val semaphore = kotlinx.coroutines.sync.Semaphore(24)
            unscannedList.mapIndexed { index, proxy ->
                launch {
                    semaphore.acquire()
                    try {
                        val latency = PingService.ping(proxy.server, proxy.port)
                        val updated = if (latency > 0) {
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
        // Fetch banner images
        fetchBannerImages()

        // Preload cached proxies immediately so UI has data on instant launch
        viewModelScope.launch {
            val cachedRaw = dataStoreManager.getCachedProxies()
            if (!cachedRaw.isNullOrBlank()) {
                val cachedProxies = ProxyParser.parse(cachedRaw)
                if (cachedProxies.isNotEmpty() && _allProxiesList.value.isEmpty()) {
                    _allProxiesList.value = cachedProxies
                    _uiState.value = UiState.Success(cachedProxies)
                }
            }
        }

        // Fetch and scan on initiation
        fetchAndScanProxies()

        // Reactive auto-scan monitoring
        viewModelScope.launch {
            combine(autoScanEnabled, autoScanInterval) { enabled, interval ->
                Pair(enabled, interval)
            }.collectLatest { (enabled, interval) ->
                if (enabled) {
                    startAutoScanProgress(interval)
                } else {
                    stopAutoScanProgress()
                }
            }
        }
    }

    fun fetchBannerImages() {
        viewModelScope.launch {
            // Immediately load cached banners if available
            val cachedJson = dataStoreManager.getCachedBanners()
            if (!cachedJson.isNullOrBlank() && _bannerItems.value.isEmpty()) {
                val parsedCached = parseBannersJson(cachedJson)
                if (parsedCached.isNotEmpty()) {
                    _bannerItems.value = parsedCached
                }
            }

            try {
                val items = bannerRepository.fetchBannerItems()
                if (items.isNotEmpty()) {
                    _bannerItems.value = items
                    dataStoreManager.saveCachedBanners(serializeBannersJson(items))
                }
            } catch (e: Exception) {
                e.printStackTrace()
                // If network failed and we haven't loaded cached ones yet, try loading cached
                if (_bannerItems.value.isEmpty()) {
                    val cached = dataStoreManager.getCachedBanners()
                    if (!cached.isNullOrBlank()) {
                        _bannerItems.value = parseBannersJson(cached)
                    }
                }
            }
        }
    }

    private fun serializeBannersJson(items: List<BannerItem>): String {
        val array = JSONArray()
        for (item in items) {
            val obj = JSONObject()
            obj.put("imageUrl", item.imageUrl)
            obj.put("targetLink", item.targetLink ?: "")
            array.put(obj)
        }
        return array.toString()
    }

    private fun parseBannersJson(json: String): List<BannerItem> {
        return try {
            val list = mutableListOf<BannerItem>()
            val array = JSONArray(json)
            for (i in 0 until array.length()) {
                val obj = array.getJSONObject(i)
                val imageUrl = obj.getString("imageUrl")
                val targetLink = obj.optString("targetLink").takeIf { it.isNotBlank() }
                list.add(BannerItem(imageUrl = imageUrl, targetLink = targetLink))
            }
            list
        } catch (e: Exception) {
            emptyList()
        }
    }

    /**
     * Downloads proxies from the primary URL or fallback URL and pings them simultaneously.
     * If all online sources fail, falls back to cached proxies and presents them with an offline banner.
     */
    fun fetchAndScanProxies() {
        viewModelScope.launch {
            if (_uiState.value is UiState.Loading && _allProxiesList.value.isNotEmpty()) {
                // Keep displaying current proxies while loading in background
            } else if (_allProxiesList.value.isEmpty()) {
                _uiState.value = UiState.Loading
            }
            
            try {
                val rawString = repository.fetchProxiesRaw()
                val downloaded = ProxyParser.parse(rawString)
                if (downloaded.isNotEmpty()) {
                    _isOfflineNoticeVisible.value = false
                    dataStoreManager.saveCachedProxies(rawString)
                    startLiveScan(downloaded)
                } else {
                    handleFetchFailure(Exception("Received empty proxy list from server"))
                }
            } catch (e: Exception) {
                handleFetchFailure(e)
            }
        }
    }

    private suspend fun handleFetchFailure(e: Exception) {
        val cachedRaw = dataStoreManager.getCachedProxies()
        if (!cachedRaw.isNullOrBlank()) {
            val cachedProxies = ProxyParser.parse(cachedRaw)
            if (cachedProxies.isNotEmpty()) {
                _isOfflineNoticeVisible.value = true
                startLiveScan(cachedProxies)
                return
            }
        }

        if (_allProxiesList.value.isNotEmpty()) {
            _isOfflineNoticeVisible.value = true
            _uiState.value = UiState.Success(_allProxiesList.value)
        } else {
            _uiState.value = UiState.Error(e.localizedMessage ?: "Failed to load proxies")
        }
    }

    /**
     * Pull-to-refresh execution
     */
    fun refresh() {
        viewModelScope.launch {
            _isRefreshing.value = true
            try {
                val rawString = repository.fetchProxiesRaw()
                val downloaded = ProxyParser.parse(rawString)
                if (downloaded.isNotEmpty()) {
                    _isOfflineNoticeVisible.value = false
                    dataStoreManager.saveCachedProxies(rawString)
                    startLiveScan(downloaded)
                } else {
                    handleFetchFailure(Exception("Received empty proxy list from server"))
                }
            } catch (e: Exception) {
                handleFetchFailure(e)
            } finally {
                _isRefreshing.value = false
            }
        }
    }

    fun onSearchQueryChanged(query: String) {
        _searchQuery.value = query
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

    fun setBannerSliderEnabled(enabled: Boolean) {
        viewModelScope.launch {
            dataStoreManager.saveBannerSliderEnabled(enabled)
        }
    }

    fun setAutoScanEnabled(enabled: Boolean) {
        viewModelScope.launch {
            dataStoreManager.saveAutoScanEnabled(enabled)
        }
    }

    fun setAutoScanInterval(seconds: Int) {
        viewModelScope.launch {
            dataStoreManager.saveAutoScanInterval(seconds)
        }
    }

    private fun startAutoScanProgress(intervalSeconds: Int) {
        autoScanJob?.cancel()
        autoScanJob = viewModelScope.launch {
            while (true) {
                delay(intervalSeconds * 1000L)
                try {
                    val rawString = repository.fetchProxiesRaw()
                    val downloaded = ProxyParser.parse(rawString)
                    if (downloaded.isNotEmpty()) {
                        _isOfflineNoticeVisible.value = false
                        dataStoreManager.saveCachedProxies(rawString)
                        startLiveScan(downloaded)
                    }
                } catch (ignored: Exception) {
                    val cachedRaw = dataStoreManager.getCachedProxies()
                    if (!cachedRaw.isNullOrBlank()) {
                        val cachedProxies = ProxyParser.parse(cachedRaw)
                        if (cachedProxies.isNotEmpty()) {
                            _isOfflineNoticeVisible.value = true
                            startLiveScan(cachedProxies)
                        }
                    }
                }
            }
        }
    }

    private fun stopAutoScanProgress() {
        autoScanJob?.cancel()
        autoScanJob = null
    }

    override fun onCleared() {
        super.onCleared()
        stopAutoScanProgress()
        scanningJob?.cancel()
        scanningJob = null
    }
}
