package com.example

sealed interface UiState {
    object Loading : UiState
    data class Success(val proxies: List<ProxyItem>) : UiState
    data class Error(val message: String) : UiState
}
