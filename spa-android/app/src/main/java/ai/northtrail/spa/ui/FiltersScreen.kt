package ai.northtrail.spa.ui

import ai.northtrail.spa.UiState
import ai.northtrail.spa.model.Control
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import java.util.Locale

@Composable
fun FiltersScreen(ui: UiState, onSaveFilter1: (Int, Int) -> Unit, onHold: (Boolean) -> Unit) {
    val enabled = ui.health.controlsEnabled
    val spaStart = ui.spa.filter1StartHour?.value
    val spaDuration = ui.spa.filter1DurationMin?.value
    var start by remember(spaStart) { mutableIntStateOf(spaStart ?: 20) }
    var duration by remember(spaDuration) { mutableIntStateOf(spaDuration ?: 120) }
    var confirmFilter by remember { mutableStateOf(false) }
    var confirmHold by remember { mutableStateOf<Boolean?>(null) }

    Column(Modifier.fillMaxSize()) {
        StatusStrip(ui)
        SectionCard {
            val running = ui.spa.filter1Running?.value == true
            Text("Filter cycle 1" + if (running) "  ● running" else "", style = MaterialTheme.typography.titleMedium)
            Stepper("Starts", String.format(Locale.US, "%02d:00", start), enabled,
                onMinus = { start = (start + 23) % 24 }, onPlus = { start = (start + 1) % 24 })
            Stepper("Duration", durationLabel(duration), enabled,
                onMinus = { duration = (duration - 15).coerceAtLeast(0) },
                onPlus = { duration = (duration + 15).coerceAtMost(1425) })
            Button(
                onClick = { confirmFilter = true },
                enabled = enabled && spaStart != null && spaDuration != null && (start != spaStart || duration != spaDuration),
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
            ) { Text(if (ui.isPending(Control.FILTER1)) "Saving…" else "Save to spa") }
            Text("Start is whole hours only (what the gateway exposes today).", style = MaterialTheme.typography.labelSmall)
        }
        SectionCard {
            val hold = ui.spa.hold?.value == true
            Text("Hold", style = MaterialTheme.typography.titleMedium)
            Text(if (hold) "Pumps and heater paused" else "Off", style = MaterialTheme.typography.bodyMedium)
            OutlinedButton(
                onClick = { confirmHold = !hold },
                enabled = enabled && ui.spa.hold != null,
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
            ) { Text(if (ui.isPending(Control.HOLD)) "Sending…" else if (hold) "End hold" else "Start hold (service)") }
        }
    }

    if (confirmFilter) {
        ConfirmDialog(
            text = "Set filter cycle 1 to start at ${String.format(Locale.US, "%02d:00", start)} for ${durationLabel(duration)}?",
            onConfirm = { confirmFilter = false; onSaveFilter1(start, duration) },
            onDismiss = { confirmFilter = false },
        )
    }
    confirmHold?.let { on ->
        ConfirmDialog(
            text = if (on) "Pause pumps and heater for service?" else "End hold and resume normal operation?",
            onConfirm = { confirmHold = null; onHold(on) },
            onDismiss = { confirmHold = null },
        )
    }
}

private fun durationLabel(minutes: Int): String =
    if (minutes % 60 == 0) "${minutes / 60} h" else "${minutes / 60} h ${minutes % 60} min"

@Composable
internal fun SectionCard(content: @Composable () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp),
        colors = CardDefaults.cardColors(containerColor = SpaColors.Surface),
    ) { Column(Modifier.padding(14.dp)) { content() } }
}

@Composable
private fun Stepper(label: String, value: String, enabled: Boolean, onMinus: () -> Unit, onPlus: () -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label)
        Row(verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onMinus, enabled = enabled) { Text("−") }
            Text(value)
            TextButton(onClick = onPlus, enabled = enabled) { Text("+") }
        }
    }
}

@Composable
internal fun ConfirmDialog(text: String, onConfirm: () -> Unit, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        text = { Text(text) },
        confirmButton = { TextButton(onClick = onConfirm) { Text("Confirm") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
