package ai.northtrail.spa.model

class SpaStateReducer(private val topics: SpaTopics) {

    /** Unknown topics and unparseable payloads leave the field unchanged; nothing throws. */
    fun reduce(state: SpaState, topic: String, payload: String, retained: Boolean, nowMillis: Long): SpaState {
        val live = !retained
        val p = payload.trim()
        fun <T> obs(v: T) = Observed(v, live)

        val next: SpaState = when (topic) {
            topics.status -> onlineFlag(p)?.let { state.copy(gatewayOnline = obs(it)) }
            topics.busConnected -> onOff(p)?.let { state.copy(busConnected = obs(it)) }
            topics.currentTemp -> number(p)?.let { state.copy(currentTempC = obs(it)) }
            topics.targetTemp -> number(p)?.let { state.copy(targetTempC = obs(it)) }
            topics.heating -> onOff(p)?.let { state.copy(heating = obs(it)) }
            topics.priming -> onOff(p)?.let { state.copy(priming = obs(it)) }
            topics.filter1Running -> onOff(p)?.let { state.copy(filter1Running = obs(it)) }
            topics.jetsState -> onOff(p)?.let { state.copy(jetsOn = obs(it)) }
            topics.jetsLevel -> int(p, 1..2)?.let { state.copy(jetsLevel = obs(it)) }
            topics.light -> onOff(p)?.let { state.copy(light = obs(it)) }
            topics.hold -> onOff(p)?.let { state.copy(hold = obs(it)) }
            topics.heatMode -> heatMode(p)?.let { state.copy(heatMode = obs(it)) }
            topics.range -> range(p)?.let { state.copy(range = obs(it)) }
            topics.scale -> scale(p)?.let { state.copy(scale = obs(it)) }
            topics.filter1StartHour -> int(p, 0..23)?.let { state.copy(filter1StartHour = obs(it)) }
            topics.filter1Duration -> int(p, 0..1439)?.let { state.copy(filter1DurationMin = obs(it)) }
            topics.model -> state.copy(model = obs(p))
            topics.firmware -> state.copy(firmware = obs(p))
            topics.notification -> state.copy(notification = obs(p))
            else -> null
        } ?: state

        return if (live && topics.isUnderRoot(topic)) next.copy(lastLiveMessageAtMillis = nowMillis) else next
    }

    private fun onOff(p: String): Boolean? = when (p) {
        "ON" -> true
        "OFF" -> false
        else -> null
    }

    private fun onlineFlag(p: String): Boolean? = when (p) {
        "online" -> true
        "offline" -> false
        else -> null
    }

    private fun number(p: String): Double? = p.toDoubleOrNull()?.takeIf { it.isFinite() }

    /** Out-of-range values are dropped: they would later fail command encoding. */
    private fun int(p: String, valid: IntRange): Int? = number(p)?.toInt()?.takeIf { it in valid }

    private fun heatMode(p: String): HeatMode? = when (p) {
        "ready" -> HeatMode.READY
        "rest" -> HeatMode.REST
        else -> null
    }

    private fun range(p: String): TempRange? = when (p) {
        "high" -> TempRange.HIGH
        "low" -> TempRange.LOW
        else -> null
    }

    private fun scale(p: String): TempScale? = when (p) {
        "fahrenheit" -> TempScale.FAHRENHEIT
        "celsius" -> TempScale.CELSIUS
        else -> null
    }
}
