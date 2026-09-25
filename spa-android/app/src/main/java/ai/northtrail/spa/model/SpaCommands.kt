package ai.northtrail.spa.model

import java.util.Locale

data class Publish(val topic: String, val payload: String)

class SpaCommands(private val t: SpaTopics) {
    fun encode(action: SpaAction): List<Publish> = when (action) {
        is SpaAction.SetTarget ->
            listOf(Publish(t.targetTempCmd, String.format(Locale.US, "%.1f", action.celsius)))
        is SpaAction.SetJets -> when (action.speed) {
            // The firmware turns the pump on at the requested speed; no separate ON needed.
            JetsSpeed.OFF -> listOf(Publish(t.jetsCmd, "OFF"))
            JetsSpeed.LOW -> listOf(Publish(t.jetsLevelCmd, "1"))
            JetsSpeed.HIGH -> listOf(Publish(t.jetsLevelCmd, "2"))
        }
        is SpaAction.SetLight -> listOf(Publish(t.lightCmd, onOff(action.on)))
        is SpaAction.SetHold -> listOf(Publish(t.holdCmd, onOff(action.on)))
        is SpaAction.SetHeatMode ->
            listOf(Publish(t.heatModeCmd, if (action.mode == HeatMode.REST) "rest" else "ready"))
        is SpaAction.SetRange ->
            listOf(Publish(t.rangeCmd, if (action.range == TempRange.LOW) "low" else "high"))
        is SpaAction.SetFilter1 -> {
            require(action.startHour in 0..23) { "start hour ${action.startHour} not in 0..23" }
            require(action.durationMinutes in 0..1439) { "duration ${action.durationMinutes} not in 0..1439" }
            listOf(
                Publish(t.filter1StartHourCmd, action.startHour.toString()),
                Publish(t.filter1DurationCmd, action.durationMinutes.toString()),
            )
        }
        SpaAction.ClearNotification -> listOf(Publish(t.clearNotificationCmd, "PRESS"))
    }

    private fun onOff(on: Boolean) = if (on) "ON" else "OFF"
}
