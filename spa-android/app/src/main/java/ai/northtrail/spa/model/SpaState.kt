package ai.northtrail.spa.model

enum class HeatMode { READY, REST }
enum class JetsSpeed { OFF, LOW, HIGH }

/** A value plus whether it arrived live (true) or as a retained, possibly old, message (false). */
data class Observed<T>(val value: T, val live: Boolean)

data class SpaState(
    val gatewayOnline: Observed<Boolean>? = null,
    val busConnected: Observed<Boolean>? = null,
    val currentTempC: Observed<Double>? = null,
    val targetTempC: Observed<Double>? = null,
    val heating: Observed<Boolean>? = null,
    val priming: Observed<Boolean>? = null,
    val filter1Running: Observed<Boolean>? = null,
    val jetsOn: Observed<Boolean>? = null,
    val jetsLevel: Observed<Int>? = null,
    val light: Observed<Boolean>? = null,
    val hold: Observed<Boolean>? = null,
    val heatMode: Observed<HeatMode>? = null,
    val range: Observed<TempRange>? = null,
    val scale: Observed<TempScale>? = null,
    val filter1StartHour: Observed<Int>? = null,
    val filter1DurationMin: Observed<Int>? = null,
    val model: Observed<String>? = null,
    val firmware: Observed<String>? = null,
    val notification: Observed<String>? = null,
    val lastLiveMessageAtMillis: Long? = null,
) {
    /** speed_level reads 1 while the pump is off, so the on/off state decides first. */
    val jets: Observed<JetsSpeed>?
        get() {
            val on = jetsOn ?: return null
            if (!on.value) return Observed(JetsSpeed.OFF, on.live)
            val level = jetsLevel ?: return Observed(JetsSpeed.LOW, on.live)
            val speed = if (level.value >= 2) JetsSpeed.HIGH else JetsSpeed.LOW
            return Observed(speed, on.live && level.live)
        }
}
