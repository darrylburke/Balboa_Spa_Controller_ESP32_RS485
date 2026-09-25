package ai.northtrail.spa.ui

import ai.northtrail.spa.SpaViewModel
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.FilterAlt
import androidx.compose.material.icons.filled.HotTub
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle

private data class Tab(val label: String, val icon: androidx.compose.ui.graphics.vector.ImageVector)
private val tabs = listOf(Tab("Spa", Icons.Filled.HotTub), Tab("Filters", Icons.Filled.FilterAlt), Tab("Settings", Icons.Filled.Settings))

@Composable
fun SpaApp(viewModel: SpaViewModel) {
    val ui by viewModel.ui.collectAsStateWithLifecycle()
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var editingBroker by remember { mutableStateOf(false) }
    val snackbar = remember { SnackbarHostState() }

    LaunchedEffect(ui.message) {
        ui.message?.let {
            snackbar.showSnackbar(it)
            viewModel.consumeMessage()
        }
    }

    if (!ui.configured || editingBroker) {
        SetupScreen(
            initial = viewModel.brokerConfig,
            onSave = { viewModel.saveBroker(it); editingBroker = false },
            onCancel = if (ui.configured) ({ editingBroker = false }) else null,
        )
        return
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbar) },
        bottomBar = {
            NavigationBar {
                tabs.forEachIndexed { i, t ->
                    NavigationBarItem(
                        selected = tab == i,
                        onClick = { tab = i },
                        icon = { Icon(t.icon, contentDescription = null) },
                        label = { Text(t.label) },
                    )
                }
            }
        },
    ) { padding ->
        Box(Modifier.padding(padding)) {
            when (tab) {
                0 -> SpaScreen(ui, viewModel::stepTarget, viewModel::cycleJets, viewModel::toggleLight,
                    viewModel::setHeatMode, viewModel::setRange)
                1 -> FiltersScreen(ui, viewModel::saveFilter1, viewModel::setHold)
                else -> SettingsScreen(ui, viewModel.brokerConfig.host, { editingBroker = true },
                    viewModel::setDisplayUnit, viewModel::clearNotification)
            }
        }
    }
}
