package ai.northtrail.spa.model

enum class Control { TARGET, JETS, LIGHT, HOLD, HEAT_MODE, RANGE, FILTER1 }

data class Pending(val action: SpaAction, val sentAtMillis: Long)

data class Resolution(
    val pending: Map<Control, Pending>,
    val confirmed: Set<Control>,
    val timedOut: Set<Control>,
)

object PendingCommands {
    const val TIMEOUT_MS = 10_000L

    fun controlOf(action: SpaAction): Control? = when (action) {
        is SpaAction.SetTarget -> Control.TARGET
        is SpaAction.SetJets -> Control.JETS
        is SpaAction.SetLight -> Control.LIGHT
        is SpaAction.SetHold -> Control.HOLD
        is SpaAction.SetHeatMode -> Control.HEAT_MODE
        is SpaAction.SetRange -> Control.RANGE
        is SpaAction.SetFilter1 -> Control.FILTER1
        SpaAction.ClearNotification -> null
    }

    /** Only a LIVE echo counts: a retained value may predate the command. */
    fun isConfirmed(action: SpaAction, spa: SpaState): Boolean = when (action) {
        is SpaAction.SetTarget -> {
            val t = spa.targetTempC
            val scale = spa.scale?.value ?: TempScale.FAHRENHEIT
            t != null && t.live && Temperature.sameSpaSetting(t.value, action.celsius, scale)
        }
        is SpaAction.SetJets -> spa.jets.matches(action.speed)
        is SpaAction.SetLight -> spa.light.matches(action.on)
        is SpaAction.SetHold -> spa.hold.matches(action.on)
        is SpaAction.SetHeatMode -> spa.heatMode.matches(action.mode)
        is SpaAction.SetRange -> spa.range.matches(action.range)
        is SpaAction.SetFilter1 ->
            spa.filter1StartHour.matches(action.startHour) &&
                spa.filter1DurationMin.matches(action.durationMinutes)
        SpaAction.ClearNotification -> true
    }

    fun resolve(pending: Map<Control, Pending>, spa: SpaState, nowMillis: Long): Resolution {
        val confirmed = mutableSetOf<Control>()
        val timedOut = mutableSetOf<Control>()
        val remaining = mutableMapOf<Control, Pending>()
        for ((control, p) in pending) {
            when {
                isConfirmed(p.action, spa) -> confirmed += control
                nowMillis - p.sentAtMillis >= TIMEOUT_MS -> timedOut += control
                else -> remaining[control] = p
            }
        }
        return Resolution(remaining, confirmed, timedOut)
    }

    private fun <T> Observed<T>?.matches(expected: T): Boolean =
        this != null && live && value == expected
}
