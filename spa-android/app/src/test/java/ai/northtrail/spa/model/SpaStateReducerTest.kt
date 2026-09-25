package ai.northtrail.spa.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SpaStateReducerTest {
    private val topics = SpaTopics()
    private val reducer = SpaStateReducer(topics)

    /** Captured from the live broker on 2026-09-25. */
    private val fixture = listOf(
        "spa/status" to "online",
        "spa/binary_sensor/spa_bus_connected/state" to "ON",
        "spa/sensor/spa_current_temperature/state" to "30.6",
        "spa/sensor/spa_target_temperature/state" to "31.1",
        "spa/binary_sensor/spa_heating/state" to "ON",
        "spa/fan/spa_jets/state" to "ON",
        "spa/fan/spa_jets/speed_level/state" to "1",
        "spa/switch/spa_light/state" to "OFF",
        "spa/switch/spa_hold/state" to "OFF",
        "spa/select/spa_heating_mode/state" to "ready",
        "spa/select/spa_temperature_range/state" to "high",
        "spa/select/spa_temperature_scale/state" to "fahrenheit",
        "spa/number/spa_filter_1_start_hour/state" to "20",
        "spa/number/spa_filter_1_duration/state" to "120",
        "spa/binary_sensor/spa_filter_cycle_1_running/state" to "OFF",
        "spa/binary_sensor/spa_priming/state" to "OFF",
        "spa/sensor/spa_model/state" to "CNBP501X",
        "spa/sensor/spa_firmware_version/state" to "V36.0",
        "spa/sensor/spa_notification/state" to "none",
    )

    private fun apply(messages: List<Pair<String, String>>, retained: Boolean = false, now: Long = 1_000L) =
        messages.fold(SpaState()) { s, (t, p) -> reducer.reduce(s, t, p, retained, now) }

    @Test
    fun liveFixturePopulatesEveryField() {
        val s = apply(fixture)
        assertEquals(true, s.gatewayOnline?.value)
        assertEquals(true, s.busConnected?.value)
        assertEquals(30.6, s.currentTempC!!.value, 0.0)
        assertEquals(31.1, s.targetTempC!!.value, 0.0)
        assertEquals(true, s.heating?.value)
        assertEquals(JetsSpeed.LOW, s.jets?.value)
        assertEquals(false, s.light?.value)
        assertEquals(false, s.hold?.value)
        assertEquals(HeatMode.READY, s.heatMode?.value)
        assertEquals(TempRange.HIGH, s.range?.value)
        assertEquals(TempScale.FAHRENHEIT, s.scale?.value)
        assertEquals(20, s.filter1StartHour?.value)
        assertEquals(120, s.filter1DurationMin?.value)
        assertEquals(false, s.filter1Running?.value)
        assertEquals(false, s.priming?.value)
        assertEquals("CNBP501X", s.model?.value)
        assertEquals("V36.0", s.firmware?.value)
        assertEquals("none", s.notification?.value)
        assertTrue(s.currentTempC!!.live)
        assertEquals(1_000L, s.lastLiveMessageAtMillis)
    }

    @Test
    fun retainedValuesAreMarkedNotLiveAndDoNotStampTime() {
        val s = apply(fixture, retained = true)
        assertFalse(s.currentTempC!!.live)
        assertNull(s.lastLiveMessageAtMillis)
    }

    @Test
    fun jetsOffIgnoresTheSpeedLevelOfOne() {
        val s = apply(listOf("spa/fan/spa_jets/state" to "OFF", "spa/fan/spa_jets/speed_level/state" to "1"))
        assertEquals(JetsSpeed.OFF, s.jets?.value)
    }

    @Test
    fun jetsHighFromSpeedLevelTwo() {
        val s = apply(listOf("spa/fan/spa_jets/state" to "ON", "spa/fan/spa_jets/speed_level/state" to "2"))
        assertEquals(JetsSpeed.HIGH, s.jets?.value)
    }

    @Test
    fun nanAndGarbageAreIgnored() {
        val before = apply(fixture)
        val afterNan = reducer.reduce(before, topics.currentTemp, "nan", false, 2_000L)
        assertEquals(30.6, afterNan.currentTempC!!.value, 0.0)
        val afterGarbage = reducer.reduce(afterNan, topics.heatMode, "banana", false, 3_000L)
        assertEquals(HeatMode.READY, afterGarbage.heatMode?.value)
        val afterBadBool = reducer.reduce(afterGarbage, topics.light, "maybe", false, 4_000L)
        assertEquals(false, afterBadBool.light?.value)
    }

    @Test
    fun anyLiveMessageUnderRootStampsTime() {
        val s = reducer.reduce(SpaState(), "spa/debug", "[I] diag", false, 5_000L)
        assertEquals(5_000L, s.lastLiveMessageAtMillis)
    }

    @Test
    fun gatewayOfflineFromLastWill() {
        val s = reducer.reduce(apply(fixture), topics.status, "offline", false, 9_000L)
        assertEquals(false, s.gatewayOnline?.value)
    }

    @Test
    fun topicsHonourACustomRoot() {
        val custom = SpaTopics("hottub/")
        assertEquals("hottub/status", custom.status)
        assertEquals("hottub/#", custom.subscription)
    }

    @Test
    fun outOfRangeIntegersAreIgnored() {
        val before = apply(fixture)
        var s = reducer.reduce(before, topics.filter1StartHour, "24", false, 2_000L)
        s = reducer.reduce(s, topics.filter1Duration, "1440", false, 2_000L)
        s = reducer.reduce(s, topics.jetsLevel, "3", false, 2_000L)
        assertEquals(20, s.filter1StartHour?.value)
        assertEquals(120, s.filter1DurationMin?.value)
        assertEquals(1, s.jetsLevel?.value)
    }
}
