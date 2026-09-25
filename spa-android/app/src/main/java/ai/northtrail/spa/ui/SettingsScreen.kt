package ai.northtrail.spa.ui

import ai.northtrail.spa.DisplayUnit
import ai.northtrail.spa.UiState
import ai.northtrail.spa.model.LinkState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

@Composable
fun SettingsScreen(
    ui: UiState,
    brokerHost: String,
    onEditBroker: () -> Unit,
    onDisplayUnit: (DisplayUnit) -> Unit,
    onClearNotification: () -> Unit,
) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        SectionCard {
            Text("Connection", style = MaterialTheme.typography.titleMedium)
            val brokerOk = ui.link.state == LinkState.CONNECTED
            KeyValue("Broker", if (brokerOk) "● connected" else "● ${ui.link.detail.ifBlank { "disconnected" }}", brokerOk)
            val gw = ui.spa.gatewayOnline?.value
            KeyValue("Gateway", when (gw) { true -> "● online"; false -> "● offline"; null -> "● unknown" }, gw == true)
            val bus = ui.spa.busConnected?.value
            KeyValue("Spa bus", when (bus) { true -> "● connected"; false -> "● disconnected"; null -> "● unknown" }, bus == true)
            KeyValue("Host", brokerHost, null)
            OutlinedButton(onClick = onEditBroker, modifier = Modifier.fillMaxWidth().padding(top = 8.dp)) {
                Text("Edit broker / password")
            }
        }
        SectionCard {
            Text("Spa", style = MaterialTheme.typography.titleMedium)
            KeyValue("Model", ui.spa.model?.value ?: "—", null)
            KeyValue("Firmware", ui.spa.firmware?.value ?: "—", null)
            Text("Display units", modifier = Modifier.padding(top = 8.dp, bottom = 4.dp))
            val options = listOf(DisplayUnit.FOLLOW_SPA to "Follow spa", DisplayUnit.FAHRENHEIT to "°F", DisplayUnit.CELSIUS to "°C")
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                options.forEachIndexed { i, (unit, label) ->
                    SegmentedButton(
                        selected = ui.displayUnit == unit,
                        onClick = { onDisplayUnit(unit) },
                        shape = SegmentedButtonDefaults.itemShape(i, options.size),
                    ) { Text(label) }
                }
            }
        }
        SectionCard {
            Text("Notification", style = MaterialTheme.typography.titleMedium)
            KeyValue("Spa message", ui.spa.notification?.value?.ifBlank { null } ?: "None", null)
            OutlinedButton(
                onClick = onClearNotification,
                enabled = ui.health.controlsEnabled,
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
            ) { Text("Clear notification") }
        }
    }
}

@Composable
private fun KeyValue(key: String, value: String, good: Boolean?) {
    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(key, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f))
        Text(
            value,
            color = when (good) { true -> SpaColors.Ok; false -> SpaColors.Bad; null -> Color.Unspecified },
        )
    }
}
