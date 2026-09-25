package ai.northtrail.spa

import ai.northtrail.spa.model.BrokerConfig
import ai.northtrail.spa.model.HeatMode
import ai.northtrail.spa.model.SpaTopics
import ai.northtrail.spa.model.TempRange
import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.StateFlow

class SpaViewModel(application: Application) : AndroidViewModel(application) {
    private val app = application as SpaApplication

    // The topic root is read once; changing it takes effect on the next app start.
    private val controller = SpaController(
        transport = app.repository,
        topics = SpaTopics(app.configStore.load().topicRoot),
        scope = viewModelScope,
        clock = System::currentTimeMillis,
        configured = app.configStore.isConfigured(),
        displayUnit = app.configStore.loadDisplayUnit(),
    ).also { it.start() }

    val ui: StateFlow<UiState> = controller.ui

    val brokerConfig: BrokerConfig get() = app.configStore.load().copy(password = "")

    fun connect() = app.repository.connect()
    fun disconnect() = app.repository.disconnect()

    fun saveBroker(config: BrokerConfig) {
        app.repository.saveAndConnect(config)
        controller.markConfigured()
    }

    fun setDisplayUnit(unit: DisplayUnit) {
        app.configStore.saveDisplayUnit(unit)
        controller.setDisplayUnit(unit)
    }

    fun stepTarget(direction: Int) = controller.stepTarget(direction)
    fun cycleJets() = controller.cycleJets()
    fun toggleLight() = controller.toggleLight()
    fun setHold(on: Boolean) = controller.setHold(on)
    fun setHeatMode(mode: HeatMode) = controller.setHeatMode(mode)
    fun setRange(range: TempRange) = controller.setRange(range)
    fun saveFilter1(startHour: Int, durationMinutes: Int) = controller.saveFilter1(startHour, durationMinutes)
    fun clearNotification() = controller.clearNotification()
    fun consumeMessage() = controller.consumeMessage()
}
