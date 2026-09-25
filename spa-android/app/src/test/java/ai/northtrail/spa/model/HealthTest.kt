package ai.northtrail.spa.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class HealthTest {
    private val connected = LinkStatus(LinkState.CONNECTED, "ok", connectedAtMillis = 0L)
    private val healthySpa = SpaState(
        gatewayOnline = Observed(true, true),
        busConnected = Observed(true, true),
        lastLiveMessageAtMillis = 0L,
    )

    @Test
    fun healthyWhenAllThreeLinksAreGood() {
        val h = Liveness.evaluate(connected, healthySpa, 5_000L)
        assertNull(h.problem)
        assertTrue(h.controlsEnabled)
        assertEquals(5_000L, h.dataAgeMillis)
    }

    @Test
    fun firstFailingLinkIsReported() {
        assertEquals(Problem.CREDENTIALS_REJECTED, Liveness.evaluate(LinkStatus(LinkState.REJECTED), healthySpa, 0).problem)
        assertEquals(Problem.BROKER_UNREACHABLE, Liveness.evaluate(LinkStatus(LinkState.CONNECTING), healthySpa, 0).problem)
        assertEquals(Problem.WAITING_FOR_GATEWAY, Liveness.evaluate(connected, SpaState(), 0).problem)
        assertEquals(Problem.GATEWAY_OFFLINE, Liveness.evaluate(connected, healthySpa.copy(gatewayOnline = Observed(false, true)), 0).problem)
        assertEquals(Problem.BUS_DISCONNECTED, Liveness.evaluate(connected, healthySpa.copy(busConnected = Observed(false, true)), 0).problem)
        assertFalse(Liveness.evaluate(connected, healthySpa.copy(busConnected = null), 0).controlsEnabled)
    }

    @Test
    fun staleOnlyAfterNinetySecondsFromLaterOfLastMessageAndConnect() {
        assertNull(Liveness.evaluate(connected, healthySpa, 90_000L).problem)
        assertEquals(Problem.STALE, Liveness.evaluate(connected, healthySpa, 90_001L).problem)
        val reconnected = connected.copy(connectedAtMillis = 80_000L)
        assertNull(Liveness.evaluate(reconnected, healthySpa, 100_000L).problem)
    }

    @Test
    fun openingTheAppWithOnlyRetainedDataIsNotStale() {
        val retainedOnly = healthySpa.copy(lastLiveMessageAtMillis = null)
        val justConnected = connected.copy(connectedAtMillis = 1_000_000L)
        assertNull(Liveness.evaluate(justConnected, retainedOnly, 1_005_000L).problem)
    }

    @Test
    fun agesFormatCompactly() {
        assertEquals("2s", formatAge(2_400L))
        assertEquals("14m", formatAge(14 * 60_000L + 5_000L))
        assertEquals("3h", formatAge(3 * 3_600_000L + 1L))
    }

    @Test
    fun everyProblemHasAPlainLabel() {
        Problem.values().forEach { assertTrue(it.name, it.label.isNotBlank()) }
    }
}
