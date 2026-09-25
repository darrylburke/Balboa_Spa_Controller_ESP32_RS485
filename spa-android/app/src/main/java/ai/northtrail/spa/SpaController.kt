package ai.northtrail.spa

import ai.northtrail.spa.data.SpaTransport
import ai.northtrail.spa.model.Control
import ai.northtrail.spa.model.Health
import ai.northtrail.spa.model.HeatMode
import ai.northtrail.spa.model.JetsSpeed
import ai.northtrail.spa.model.LinkStatus
import ai.northtrail.spa.model.Liveness
import ai.northtrail.spa.model.Pending
import ai.northtrail.spa.model.PendingCommands
import ai.northtrail.spa.model.Problem
import ai.northtrail.spa.model.SpaAction
import ai.northtrail.spa.model.SpaCommands
import ai.northtrail.spa.model.SpaState
import ai.northtrail.spa.model.SpaStateReducer
import ai.northtrail.spa.model.SpaTopics
import ai.northtrail.spa.model.TempRange
import ai.northtrail.spa.model.TempScale
import ai.northtrail.spa.model.Temperature
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

enum class DisplayUnit { FOLLOW_SPA, FAHRENHEIT, CELSIUS }

data class UiState(
    val configured: Boolean,
    val link: LinkStatus = LinkStatus(),
    val spa: SpaState = SpaState(),
    val health: Health = Health(Problem.BROKER_UNREACHABLE, null),
    val pending: Map<Control, Pending> = emptyMap(),
    /** Target being edited, in the spa's own units (whole °F or 0.5 °C). */
    val targetDraft: Double? = null,
    val displayUnit: DisplayUnit = DisplayUnit.FOLLOW_SPA,
    val message: String? = null,
) {
    val spaScale: TempScale get() = spa.scale?.value ?: TempScale.FAHRENHEIT

    val shownUnit: TempScale
        get() = when (displayUnit) {
            DisplayUnit.FOLLOW_SPA -> spaScale
            DisplayUnit.FAHRENHEIT -> TempScale.FAHRENHEIT
            DisplayUnit.CELSIUS -> TempScale.CELSIUS
        }

    /** The draft while editing, otherwise the spa's own reported target, in spa units. */
    val targetSpaUnits: Double?
        get() = targetDraft ?: spa.targetTempC?.let { Temperature.spaUnitsFromCelsius(it.value, spaScale) }

    fun isPending(control: Control): Boolean = control in pending
}

class SpaController(
    private val transport: SpaTransport,
    topics: SpaTopics,
    private val scope: CoroutineScope,
    private val clock: () -> Long,
    configured: Boolean,
    displayUnit: DisplayUnit,
) {
    private val reducer = SpaStateReducer(topics)
    private val commands = SpaCommands(topics)
    private val state = MutableStateFlow(UiState(configured = configured, displayUnit = displayUnit))
    val ui: StateFlow<UiState> = state.asStateFlow()
    private var targetJob: Job? = null

    fun start() {
        scope.launch {
            transport.messages.collect { m ->
                update { it.copy(spa = reducer.reduce(it.spa, m.topic, m.payload, m.retained, clock())) }
            }
        }
        scope.launch { transport.link.collect { l -> update { it.copy(link = l) } } }
        // Re-evaluate staleness and command timeouts even when nothing arrives.
        scope.launch {
            while (isActive) {
                delay(1_000)
                update { it }
            }
        }
    }

    fun stepTarget(direction: Int) {
        val s = state.value
        if (!s.health.controlsEnabled) return
        val scale = s.spaScale
        val current = s.targetSpaUnits ?: return
        val limits = Temperature.limits(s.spa.range?.value ?: TempRange.HIGH, scale)
        val next = (current + direction * Temperature.step(scale))
            .coerceIn(limits.start, limits.endInclusive)
        update { it.copy(targetDraft = next) }
        targetJob?.cancel()
        targetJob = scope.launch {
            delay(TARGET_DEBOUNCE_MS)
            send(SpaAction.SetTarget(Temperature.commandCelsius(next, scale)))
            // Already equal to the spa's target: confirmed on the spot, so drop the draft now.
            update { if (Control.TARGET in it.pending) it else it.copy(targetDraft = null) }
        }
    }

    fun cycleJets() {
        val jets = state.value.spa.jets?.value ?: return
        val next = when (jets) {
            JetsSpeed.OFF -> JetsSpeed.LOW
            JetsSpeed.LOW -> JetsSpeed.HIGH
            JetsSpeed.HIGH -> JetsSpeed.OFF
        }
        send(SpaAction.SetJets(next))
    }

    fun toggleLight() {
        val on = state.value.spa.light?.value ?: return
        send(SpaAction.SetLight(!on))
    }

    fun setHold(on: Boolean) = send(SpaAction.SetHold(on))
    fun setHeatMode(mode: HeatMode) = send(SpaAction.SetHeatMode(mode))
    fun setRange(range: TempRange) = send(SpaAction.SetRange(range))
    fun saveFilter1(startHour: Int, durationMinutes: Int) {
        val spa = state.value.spa
        // Never push defaults over a schedule the user has not seen.
        if (spa.filter1StartHour == null || spa.filter1DurationMin == null) {
            update { it.copy(message = "Filter settings not known yet") }
            return
        }
        send(SpaAction.SetFilter1(startHour, durationMinutes))
    }
    fun clearNotification() = send(SpaAction.ClearNotification)

    fun consumeMessage() = state.update { it.copy(message = null) }
    fun setDisplayUnit(unit: DisplayUnit) = update { it.copy(displayUnit = unit) }
    fun markConfigured() = update { it.copy(configured = true) }

    private fun send(action: SpaAction) {
        if (!state.value.health.controlsEnabled) {
            if (action is SpaAction.SetTarget) update { it.copy(targetDraft = null) }
            return
        }
        val publishes = try {
            commands.encode(action)
        } catch (e: IllegalArgumentException) {
            update { it.copy(message = "Invalid filter settings") }
            return
        }
        val sent = publishes.all { transport.publish(it) }
        if (!sent) {
            update {
                it.copy(
                    message = "Couldn't send: broker not connected",
                    targetDraft = if (action is SpaAction.SetTarget) null else it.targetDraft,
                )
            }
            return
        }
        val control = PendingCommands.controlOf(action) ?: return
        update { it.copy(pending = it.pending + (control to Pending(action, clock()))) }
    }

    /** Applies [change], then recomputes health and resolves pending commands. */
    private fun update(change: (UiState) -> UiState) {
        state.update { current ->
            val next = change(current)
            val now = clock()
            val resolution = PendingCommands.resolve(next.pending, next.spa, now)
            // A draft still debouncing belongs to a newer edit than the command that resolved.
            val targetDone = (Control.TARGET in resolution.confirmed || Control.TARGET in resolution.timedOut) &&
                targetJob?.isActive != true
            next.copy(
                health = Liveness.evaluate(next.link, next.spa, now),
                pending = resolution.pending,
                targetDraft = if (targetDone) null else next.targetDraft,
                message = if (resolution.timedOut.isNotEmpty()) "Spa didn't confirm" else next.message,
            )
        }
    }

    companion object {
        const val TARGET_DEBOUNCE_MS = 1_000L
    }
}
