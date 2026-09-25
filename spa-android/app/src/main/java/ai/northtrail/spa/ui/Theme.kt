package ai.northtrail.spa.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

object SpaColors {
    val Background = Color(0xFF0F1720)
    val Surface = Color(0xFF1A2532)
    val SurfaceActive = Color(0xFF1E3A52)
    val Track = Color(0xFF26323F)
    val Accent = Color(0xFF4FC3F7)
    val Heat = Color(0xFFFF8A3D)
    val Ok = Color(0xFF7EE787)
    val OkBackground = Color(0xFF12301F)
    val Bad = Color(0xFFFF8A80)
    val BadBackground = Color(0xFF3A1616)
    val Disabled = Color(0xFF555555)
}

@Composable
fun SpaTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = SpaColors.Accent,
            onPrimary = SpaColors.Background,
            background = SpaColors.Background,
            surface = SpaColors.Background,
            surfaceVariant = SpaColors.Surface,
            onBackground = Color(0xFFE6EDF3),
            onSurface = Color(0xFFE6EDF3),
        ),
        content = content,
    )
}
