package com.example

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun LanguageFlag(
    optionCode: String,
    emoji: String,
    size: Dp = 24.dp,
    modifier: Modifier = Modifier
) {
    if (optionCode == "fa") {
        // Draw the simplified design: 3 equal bands (Green, White, Red) and a golden circle in the center.
        Card(
            modifier = modifier
                .width(size * 1.5f)
                .height(size),
            shape = RoundedCornerShape(3.dp),
            colors = CardDefaults.cardColors(containerColor = Color.White),
            elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
        ) {
            Canvas(modifier = Modifier.fillMaxSize()) {
                val w = this.size.width
                val h = this.size.height

                // Green band (نوار سبز) - rgb(0, 153, 0)
                drawRect(
                    color = Color(0, 153, 0),
                    topLeft = Offset(0f, 0f),
                    size = androidx.compose.ui.geometry.Size(w, h / 3f)
                )

                // White band (نوار سفید) - WHITE
                drawRect(
                    color = Color.White,
                    topLeft = Offset(0f, h / 3f),
                    size = androidx.compose.ui.geometry.Size(w, h / 3f)
                )

                // Red band (نوار قرمز) - rgb(204, 0, 0)
                drawRect(
                    color = Color(204, 0, 0),
                    topLeft = Offset(0f, 2f * h / 3f),
                    size = androidx.compose.ui.geometry.Size(w, h / 3f)
                )

                // Central golden circle (نماد مرکزی ساده - دایره طلایی) - rgb(255, 215, 0) of radius h / 10
                drawCircle(
                    color = Color(255, 215, 0),
                    radius = h / 10f,
                    center = Offset(w / 2f, h / 2f)
                )
            }
        }
    } else {
        // Fallback for standard ISO emoji flags
        Text(
            text = emoji,
            fontSize = (size.value * 1.1f).sp,
            modifier = modifier
        )
    }
}
