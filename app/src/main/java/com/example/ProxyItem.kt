package com.example

data class ProxyItem(
    val id: Int,
    val server: String,
    val port: Int,
    val secret: String,
    val link: String,
    val ping: Long = -1,
    val isAlive: Boolean = false,
    val isFavorite: Boolean = false,
    val isScanned: Boolean = false
)
