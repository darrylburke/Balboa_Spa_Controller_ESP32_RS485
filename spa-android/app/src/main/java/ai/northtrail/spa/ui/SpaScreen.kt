package ai.northtrail.spa.ui

import ai.northtrail.spa.UiState
import ai.northtrail.spa.model.Control
import ai.northtrail.spa.model.HeatMode
import ai.northtrail.spa.model.JetsSpeed
import ai.northtrail.spa.model.TempRange
import ai.northtrail.spa.model.Temperature
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun SpaScreen(
    ui: UiState,
    onStep: (Int) -> Unit,
    onJets: () -> Unit,
    onLight: () -> Unit,
    onHeatMode: (HeatMode) -> Unit,
    onRange: (TempRange) -> Unit,
) {
    val enabled = ui.health.controlsEnabled
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        StatusStrip(ui)
        TemperatureDial(ui, Modifier.align(Alignment.CenterHorizontally).padding(top = 8.dp))
        TargetStepper(ui, enabled, onStep, Modifier.align(Alignment.CenterHorizontally))
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            val jets = ui.spa.jets?.value
            Tile(
                title = "Jets",
                value = when (jets) { JetsSpeed.OFF -> "Off"; JetsSpeed.LOW -> "Low"; JetsSpeed.HIGH -> "High"; null -> "—" },
                hint = "tap: Off → Low → High",
                active = jets != null && jets != JetsSpeed.OFF,
                pending = ui.isPending(Control.JETS),
                enabled = enabled && jets != null,
                onClick = onJets,
                modifier = Modifier.weight(1f),
            )
            val light = ui.spa.light?.value
            Tile(
                title = "Light",
                value = when (light) { true -> "On"; false -> "Off"; null -> "—" },
                hint = "tap to toggle",
                active = light == true,
                pending = ui.isPending(Control.LIGHT),
                enabled = enabled && light != null,
                onClick = onLight,
                modifier = Modifier.weight(1f),
            )
        }
        Choice(
            label = "Heat mode",
            options = listOf(HeatMode.READY to "Ready", HeatMode.REST to "Rest"),
            selected = ui.spa.heatMode?.value,
            pending = ui.isPending(Control.HEAT_MODE),
            enabled = enabled,
            onSelect = onHeatMode,
        )
        Choice(
            label = "Temperature range",
            options = listOf(TempRange.HIGH to "High", TempRange.LOW to "Low"),
            selected = ui.spa.range?.value,
            pending = ui.isPending(Control.RANGE),
            enabled = enabled,
            onSelect = onRange,
        )
        Spacer(Modifier.height(16.dp))
    }
}

@Composable
private fun TemperatureDial(ui: UiState, modifier: Modifier = Modifier) {
    val enabled = ui.health.controlsEnabled
    val current = ui.spa.currentTempC?.value
    val heating = ui.spa.heating?.value == true
    val range = ui.spa.range?.value ?: TempRange.HIGH
    val limits = Temperature.limits(range, ui.spaScale)
    val lowC = Temperature.celsiusFromSpaUnits(limits.start, ui.spaScale)
    val highC = Temperature.celsiusFromSpaUnits(limits.endInclusive, ui.spaScale)
    val fraction = current?.let { ((it - lowC) / (highC - lowC)).coerceIn(0.0, 1.0).toFloat() } ?: 0f
    val arc = when {
        !enabled -> SpaColors.Disabled
        heating -> SpaColors.Heat
        else -> SpaColors.Accent
    }
    val target = targetLabel(ui)
    Box(modifier.size(220.dp), contentAlignment = Alignment.Center) {
        Canvas(Modifier.fillMaxSize().padding(10.dp)) {
            val stroke = Stroke(width = 16.dp.toPx(), cap = StrokeCap.Round)
            drawArc(SpaColors.Track, 135f, 270f, false, style = stroke)
            drawArc(arc, 135f, 270f * fraction, false, style = stroke)
        }
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                current?.let { Temperature.format(it, ui.shownUnit) } ?: "—",
                fontSize = 48.sp,
                fontWeight = FontWeight.SemiBold,
            )
            Text(
                when {
                    // Disabled, or still showing a retained value from before we connected.
                    !enabled || ui.spa.currentTempC?.live == false -> "last known"
                    heating && target != null -> "Heating to $target"
                    else -> "Idle"
                },
                color = if (heating && enabled) SpaColors.Heat else MaterialTheme.colorScheme.onSurface,
                style = MaterialTheme.typography.labelLarge,
            )
            Text(if (range == TempRange.HIGH) "High range" else "Low range", style = MaterialTheme.typography.labelSmall)
        }
    }
}

private fun targetLabel(ui: UiState): String? =
    ui.targetSpaUnits?.let { Temperature.format(Temperature.celsiusFromSpaUnits(it, ui.spaScale), ui.shownUnit) }

@Composable
private fun TargetStepper(ui: UiState, enabled: Boolean, onStep: (Int) -> Unit, modifier: Modifier = Modifier) {
    val pending = ui.isPending(Control.TARGET) || ui.targetDraft != null
    Row(modifier.padding(bottom = 12.dp), verticalAlignment = Alignment.CenterVertically) {
        FilledTonalIconButton(
            onClick = { onStep(-1) },
            enabled = enabled,
            modifier = Modifier.semantics { contentDescription = "Lower target" },
        ) { Icon(Icons.Filled.Remove, contentDescription = null) }
        Text(
            "Set ${targetLabel(ui) ?: "—"}" + if (pending) " · sending" else "",
            modifier = Modifier.padding(horizontal = 24.dp),
            style = MaterialTheme.typography.titleMedium,
        )
        FilledTonalIconButton(
            onClick = { onStep(+1) },
            enabled = enabled,
            modifier = Modifier.semantics { contentDescription = "Raise target" },
        ) { Icon(Icons.Filled.Add, contentDescription = null) }
    }
}

@Composable
private fun Tile(
    title: String,
    value: String,
    hint: String,
    active: Boolean,
    pending: Boolean,
    enabled: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Card(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier.semantics { contentDescription = title },
        colors = CardDefaults.cardColors(
            containerColor = if (active) SpaColors.SurfaceActive else SpaColors.Surface,
            disabledContainerColor = SpaColors.Surface.copy(alpha = 0.5f),
        ),
    ) {
        Column(Modifier.padding(14.dp)) {
            Text(title, style = MaterialTheme.typography.labelMedium)
            Text(value + if (pending) " · sending" else "", style = MaterialTheme.typography.titleLarge)
            Text(hint, style = MaterialTheme.typography.labelSmall)
        }
    }
}

@Composable
private fun <T> Choice(
    label: String,
    options: List<Pair<T, String>>,
    selected: T?,
    pending: Boolean,
    enabled: Boolean,
    onSelect: (T) -> Unit,
) {
    Text(
        label.uppercase() + if (pending) " · sending" else "",
        modifier = Modifier.padding(start = 16.dp, top = 16.dp, bottom = 6.dp),
        style = MaterialTheme.typography.labelSmall,
    )
    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
        options.forEachIndexed { index, (value, text) ->
            SegmentedButton(
                selected = value == selected,
                onClick = { if (value != selected) onSelect(value) },
                enabled = enabled,
                shape = SegmentedButtonDefaults.itemShape(index, options.size),
            ) { Text(text) }
        }
    }
}
