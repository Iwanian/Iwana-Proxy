package com.example

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.widget.Toast

object TelegramLauncher {

    /**
     * Converts a t.me/telegram.me link to a tg:// link and opens Telegram directly via Intent.
     */
    fun launchProxy(context: Context, link: String) {
        val tgLink = when {
            link.startsWith("https://t.me/proxy") -> {
                link.replace("https://t.me/proxy", "tg://proxy")
            }
            link.startsWith("https://telegram.me/proxy") -> {
                link.replace("https://telegram.me/proxy", "tg://proxy")
            }
            else -> link
        }
        
        launchTelegramUri(context, tgLink)
    }

    /**
     * Opens the @I_w_a_n_a channel using tg://resolve?domain=I_w_a_n_a.
     */
    fun launchChannel(context: Context) {
        launchTelegramUri(context, "tg://resolve?domain=I_w_a_n_a")
    }

    private fun launchTelegramUri(context: Context, uriString: String) {
        val intent = Intent(Intent.ACTION_VIEW, Uri.parse(uriString))
        
        // Attempt to direct launch via Telegram packages to bypass browsers entirely
        val telegramPackages = listOf(
            "org.telegram.messenger",
            "org.thunderdog.challegram",  // Telegram X
            "org.telegram.messenger.web"
        )
        
        var launched = false
        for (pkg in telegramPackages) {
            try {
                val targetedIntent = Intent(Intent.ACTION_VIEW, Uri.parse(uriString)).apply {
                    setPackage(pkg)
                    flags = Intent.FLAG_ACTIVITY_NEW_TASK
                }
                context.startActivity(targetedIntent)
                launched = true
                break
            } catch (e: Exception) {
                // Try next candidate
            }
        }
        
        if (!launched) {
            // General implicit launch
            try {
                val generalIntent = Intent(Intent.ACTION_VIEW, Uri.parse(uriString)).apply {
                    flags = Intent.FLAG_ACTIVITY_NEW_TASK
                }
                context.startActivity(generalIntent)
            } catch (e: Exception) {
                Toast.makeText(
                    context, 
                    context.getString(R.string.telegram_not_installed), 
                    Toast.LENGTH_SHORT
                ).show()
            }
        }
    }
}
