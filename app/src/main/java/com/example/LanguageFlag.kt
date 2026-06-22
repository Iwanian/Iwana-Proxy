package com.example

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
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
        // Redraw the historic Lion & Sun Flag perfectly scaled to match standard emoji size.
        // It features a 3-band layout (Green, White, Red) with the Lion and Sun emblems inside.
        Card(
            modifier = modifier
                .width(size * 1.5f)
                .height(size),
            shape = RoundedCornerShape(3.dp),
            colors = CardDefaults.cardColors(containerColor = Color.White),
            elevation = CardDefaults.cardElevation(defaultElevation = 1.dp)
        ) {
            Column(modifier = Modifier.fillMaxSize()) {
                // Vibrant green band (top)
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f)
                        .background(Color(0xFF009A49))
                )
                // Middle white band with the Lion and Sun emblem
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1.1f)
                        .background(Color.White),
                    contentAlignment = Alignment.Center
                ) {
                    Box(
                        contentAlignment = Alignment.Center,
                        modifier = Modifier.size((size.value * 0.55f).dp)
                    ) {
                        // Sun peaking behind
                        Text(
                            text = "☀️",
                            fontSize = (size.value * 0.38f).sp,
                            modifier = Modifier.offset(x = (size.value * 0.08f).dp, y = -(size.value * 0.08f).dp)
                        )
                        // Lion standing in front
                        Text(
                            text = "🦁",
                            fontSize = (size.value * 0.35f).sp,
                            modifier = Modifier.offset(x = -(size.value * 0.06f).dp, y = (size.value * 0.04f).dp)
                        )
                    }
                }
                // Vibrant red band (bottom)
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f)
                        .background(Color(0xFFDA291C))
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
