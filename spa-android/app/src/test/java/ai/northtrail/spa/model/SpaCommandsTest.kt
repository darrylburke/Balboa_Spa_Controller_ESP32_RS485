package ai.northtrail.spa.model

import org.junit.Assert.assertEquals
import org.junit.Test
import java.util.Locale

class SpaCommandsTest {
    private val c = SpaCommands(SpaTopics())

    @Test
    fun targetIsCelsiusWithOneDecimal() {
        assertEquals(
            listOf(Publish("spa/climate/spa/target_temperature/command", "31.1")),
            c.encode(SpaAction.SetTarget(31.1)),
        )
    }

    @Test
    fun targetUsesDotDecimalInAnyLocale() {
        val saved = Locale.getDefault()
        try {
            Locale.setDefault(Locale.GERMANY)
            assertEquals("32.8", c.encode(SpaAction.SetTarget(32.8)).single().payload)
        } finally {
            Locale.setDefault(saved)
        }
    }

    @Test
    fun jetsOffLowHigh() {
        assertEquals(listOf(Publish("spa/fan/spa_jets/command", "OFF")), c.encode(SpaAction.SetJets(JetsSpeed.OFF)))
        assertEquals(listOf(Publish("spa/fan/spa_jets/speed_level/command", "1")), c.encode(SpaAction.SetJets(JetsSpeed.LOW)))
        assertEquals(listOf(Publish("spa/fan/spa_jets/speed_level/command", "2")), c.encode(SpaAction.SetJets(JetsSpeed.HIGH)))
    }

    @Test
    fun switches() {
        assertEquals(listOf(Publish("spa/switch/spa_light/command", "ON")), c.encode(SpaAction.SetLight(true)))
        assertEquals(listOf(Publish("spa/switch/spa_hold/command", "OFF")), c.encode(SpaAction.SetHold(false)))
    }

    @Test
    fun selects() {
        assertEquals(listOf(Publish("spa/select/spa_heating_mode/command", "rest")), c.encode(SpaAction.SetHeatMode(HeatMode.REST)))
        assertEquals(listOf(Publish("spa/select/spa_temperature_range/command", "low")), c.encode(SpaAction.SetRange(TempRange.LOW)))
    }

    @Test
    fun filterOneSendsStartThenDuration() {
        assertEquals(
            listOf(
                Publish("spa/number/spa_filter_1_start_hour/command", "20"),
                Publish("spa/number/spa_filter_1_duration/command", "120"),
            ),
            c.encode(SpaAction.SetFilter1(20, 120)),
        )
    }

    @Test(expected = IllegalArgumentException::class)
    fun filterStartHourOutOfRangeIsRejected() {
        c.encode(SpaAction.SetFilter1(24, 120))
    }

    @Test(expected = IllegalArgumentException::class)
    fun filterDurationOutOfRangeIsRejected() {
        c.encode(SpaAction.SetFilter1(20, 1440))
    }

    @Test
    fun clearNotificationPressesTheButton() {
        assertEquals(listOf(Publish("spa/button/spa_clear_notification/command", "PRESS")), c.encode(SpaAction.ClearNotification))
    }
}
