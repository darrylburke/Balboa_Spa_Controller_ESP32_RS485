package ai.northtrail.spa.ui

import ai.northtrail.spa.UiState
import ai.northtrail.spa.model.formatAge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun StatusStrip(ui: UiState, modifier: Modifier = Modifier) {
    val problem = ui.health.problem
    val age = ui.health.dataAgeMillis?.let { formatAge(it) }
    val (label, fg, bg) = if (problem == null) {
        Triple("● Spa connected", SpaColors.Ok, SpaColors.OkBackground)
    } else {
        Triple("● ${problem.label}", SpaColors.Bad, SpaColors.BadBackground)
    }
    Row(
        modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp)
            .background(bg, RoundedCornerShape(12.dp))
            .padding(horizontal = 12.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(label, color = fg, style = MaterialTheme.typography.labelLarge)
        if (age != null) Text(if (problem == null) "$age ago" else age, color = fg, style = MaterialTheme.typography.labelLarge)
    }
}
