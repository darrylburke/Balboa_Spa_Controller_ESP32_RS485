package ai.northtrail.spa.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PendingCommandsTest {
    private val fahrenheitSpa = SpaState(scale = Observed(TempScale.FAHRENHEIT, true))

    @Test
    fun controlForEachAction() {
        assertEquals(Control.TARGET, PendingCommands.controlOf(SpaAction.SetTarget(31.1)))
        assertEquals(Control.JETS, PendingCommands.controlOf(SpaAction.SetJets(JetsSpeed.LOW)))
        assertEquals(Control.FILTER1, PendingCommands.controlOf(SpaAction.SetFilter1(20, 120)))
        assertNull(PendingCommands.controlOf(SpaAction.ClearNotification))
    }

    @Test
    fun targetConfirmedByLiveEchoAtSpaResolution() {
        val spa = fahrenheitSpa.copy(targetTempC = Observed(31.1, true))
        assertTrue(PendingCommands.isConfirmed(SpaAction.SetTarget(31.1), spa))
        assertFalse(PendingCommands.isConfirmed(SpaAction.SetTarget(31.7), spa))
    }

    @Test
    fun retainedEchoDoesNotConfirm() {
        val spa = fahrenheitSpa.copy(light = Observed(true, false), targetTempC = Observed(31.1, false))
        assertFalse(PendingCommands.isConfirmed(SpaAction.SetLight(true), spa))
        assertFalse(PendingCommands.isConfirmed(SpaAction.SetTarget(31.1), spa))
    }

    @Test
    fun jetsConfirmedOnlyWhenBothStateAndLevelMatch() {
        val low = SpaState(jetsOn = Observed(true, true), jetsLevel = Observed(1, true))
        assertTrue(PendingCommands.isConfirmed(SpaAction.SetJets(JetsSpeed.LOW), low))
        assertFalse(PendingCommands.isConfirmed(SpaAction.SetJets(JetsSpeed.HIGH), low))
        val off = SpaState(jetsOn = Observed(false, true), jetsLevel = Observed(1, true))
        assertTrue(PendingCommands.isConfirmed(SpaAction.SetJets(JetsSpeed.OFF), off))
    }

    @Test
    fun filterConfirmedWhenBothValuesEchoed() {
        val spa = SpaState(filter1StartHour = Observed(21, true), filter1DurationMin = Observed(90, true))
        assertTrue(PendingCommands.isConfirmed(SpaAction.SetFilter1(21, 90), spa))
        assertFalse(PendingCommands.isConfirmed(SpaAction.SetFilter1(21, 120), spa))
    }

    @Test
    fun resolveSplitsConfirmedTimedOutAndStillPending() {
        val spa = SpaState(light = Observed(true, true))
        val pending = mapOf(
            Control.LIGHT to Pending(SpaAction.SetLight(true), sentAtMillis = 0L),
            Control.HOLD to Pending(SpaAction.SetHold(true), sentAtMillis = 0L),
            Control.RANGE to Pending(SpaAction.SetRange(TempRange.LOW), sentAtMillis = 5_000L),
        )
        val r = PendingCommands.resolve(pending, spa, nowMillis = 10_000L)
        assertEquals(setOf(Control.LIGHT), r.confirmed)
        assertEquals(setOf(Control.HOLD), r.timedOut)
        assertEquals(setOf(Control.RANGE), r.pending.keys)
    }
}
