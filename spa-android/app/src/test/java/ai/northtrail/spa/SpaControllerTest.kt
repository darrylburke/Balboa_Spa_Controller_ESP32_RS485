package ai.northtrail.spa

import ai.northtrail.spa.data.IncomingMessage
import ai.northtrail.spa.data.SpaTransport
import ai.northtrail.spa.model.Control
import ai.northtrail.spa.model.LinkState
import ai.northtrail.spa.model.LinkStatus
import ai.northtrail.spa.model.Problem
import ai.northtrail.spa.model.Publish
import ai.northtrail.spa.model.SpaTopics
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

private class FakeTransport : SpaTransport {
    override val link = MutableStateFlow(LinkStatus())
    override val messages = MutableSharedFlow<IncomingMessage>(extraBufferCapacity = 64)
    val published = mutableListOf<Publish>()
    var publishSucceeds = true
    override fun connect() {}
    override fun disconnect() {}
    override fun publish(publish: Publish): Boolean {
        if (publishSucceeds) published += publish
        return publishSucceeds
    }
}

@OptIn(ExperimentalCoroutinesApi::class)
class SpaControllerTest {
    private val transport = FakeTransport()

    private fun TestScope.controller(): SpaController =
        SpaController(
            transport = transport,
            topics = SpaTopics(),
            scope = backgroundScope,
            clock = { testScheduler.currentTime },
            configured = true,
            displayUnit = DisplayUnit.FOLLOW_SPA,
        ).also { it.start(); runCurrent() }

    private fun TestScope.emit(topic: String, payload: String, retained: Boolean = false) {
        transport.messages.tryEmit(IncomingMessage(topic, payload, retained))
        runCurrent()
    }

    /** Connected broker plus a live snapshot: 87°F water, target 88°F, High range, °F. */
    private fun TestScope.healthy(range: String = "high", targetC: String = "31.1") {
        transport.link.value = LinkStatus(LinkState.CONNECTED, "ok", connectedAtMillis = testScheduler.currentTime)
        runCurrent()
        emit("spa/status", "online")
        emit("spa/binary_sensor/spa_bus_connected/state", "ON")
        emit("spa/select/spa_temperature_scale/state", "fahrenheit")
        emit("spa/select/spa_temperature_range/state", range)
        emit("spa/sensor/spa_current_temperature/state", "30.6")
        emit("spa/sensor/spa_target_temperature/state", targetC)
        emit("spa/fan/spa_jets/state", "OFF")
        emit("spa/fan/spa_jets/speed_level/state", "1")
        emit("spa/switch/spa_light/state", "OFF")
    }

    @Test
    fun controlsDisabledUntilGatewayAndBusAreLive() = runTest {
        val c = controller()
        assertFalse(c.ui.value.health.controlsEnabled)
        healthy()
        assertTrue(c.ui.value.health.controlsEnabled)
        assertEquals(88.0, c.ui.value.targetSpaUnits!!, 0.0)
    }

    @Test
    fun rapidTapsSendOneCommandAfterTheDebounce() = runTest {
        val c = controller(); healthy()
        repeat(3) { c.stepTarget(+1); advanceTimeBy(300) }
        assertEquals(91.0, c.ui.value.targetSpaUnits!!, 0.0)
        assertTrue(transport.published.isEmpty())
        advanceTimeBy(1_001); runCurrent()
        assertEquals(listOf(Publish("spa/climate/spa/target_temperature/command", "32.8")), transport.published)
        assertTrue(c.ui.value.isPending(Control.TARGET))
    }

    @Test
    fun liveEchoConfirmsAndClearsTheDraft() = runTest {
        val c = controller(); healthy()
        c.stepTarget(+1); advanceTimeBy(1_001); runCurrent()
        emit("spa/sensor/spa_target_temperature/state", "31.7")
        assertFalse(c.ui.value.isPending(Control.TARGET))
        assertNull(c.ui.value.targetDraft)
        assertEquals(89.0, c.ui.value.targetSpaUnits!!, 0.0)
    }

    @Test
    fun noEchoTimesOutRevertsAndTellsTheUser() = runTest {
        val c = controller(); healthy()
        c.stepTarget(+1); advanceTimeBy(1_001); runCurrent()
        advanceTimeBy(11_000); runCurrent()
        assertFalse(c.ui.value.isPending(Control.TARGET))
        assertNull(c.ui.value.targetDraft)
        assertEquals(88.0, c.ui.value.targetSpaUnits!!, 0.0)
        assertEquals("Spa didn't confirm", c.ui.value.message)
        c.consumeMessage()
        assertNull(c.ui.value.message)
    }

    @Test
    fun plusAtUpperLimitStaysAtLimit() = runTest {
        val c = controller(); healthy(targetC = "40.0")
        c.stepTarget(+1)
        assertEquals(104.0, c.ui.value.targetSpaUnits!!, 0.0)
        advanceTimeBy(1_001); runCurrent()
        assertEquals("40.0", transport.published.single().payload)
    }

    @Test
    fun lowRangeClampsHighTargetDown() = runTest {
        val c = controller(); healthy(range = "low", targetC = "40.0")
        c.stepTarget(-1)
        assertEquals(99.0, c.ui.value.targetSpaUnits!!, 0.0)
    }

    @Test
    fun jetsCycleOffToLow() = runTest {
        val c = controller(); healthy()
        c.cycleJets()
        assertEquals(listOf(Publish("spa/fan/spa_jets/speed_level/command", "1")), transport.published)
        assertTrue(c.ui.value.isPending(Control.JETS))
    }

    @Test
    fun nothingIsSentWhileTheGatewayIsOffline() = runTest {
        val c = controller(); healthy()
        emit("spa/status", "offline")
        assertEquals(Problem.GATEWAY_OFFLINE, c.ui.value.health.problem)
        c.toggleLight()
        c.stepTarget(+1); advanceTimeBy(1_001); runCurrent()
        assertTrue(transport.published.isEmpty())
    }

    @Test
    fun failedPublishReportsAndLeavesNothingPending() = runTest {
        val c = controller(); healthy()
        transport.publishSucceeds = false
        c.toggleLight()
        assertFalse(c.ui.value.isPending(Control.LIGHT))
        assertEquals("Couldn't send: broker not connected", c.ui.value.message)
    }

    @Test
    fun goesStaleAfterNinetySecondsOfSilence() = runTest {
        val c = controller(); healthy()
        advanceTimeBy(91_000); runCurrent()
        assertEquals(Problem.STALE, c.ui.value.health.problem)
        emit("spa/sensor/spa_current_temperature/state", "30.6")
        assertNull(c.ui.value.health.problem)
    }

    @Test
    fun newEditSurvivesThePreviousTargetsEcho() = runTest {
        val c = controller(); healthy()
        c.stepTarget(+1); advanceTimeBy(1_001); runCurrent()   // 89 sent
        c.stepTarget(+1); c.stepTarget(+1)                      // draft 91, still debouncing
        emit("spa/sensor/spa_target_temperature/state", "31.7") // echo of 89 arrives
        assertEquals(91.0, c.ui.value.targetSpaUnits!!, 0.0)
        advanceTimeBy(1_001); runCurrent()
        assertEquals("32.8", transport.published.last().payload)
    }

    @Test
    fun invalidFilterValuesAreReportedNotCrashed() = runTest {
        val c = controller(); healthy()
        emit("spa/number/spa_filter_1_start_hour/state", "20")
        emit("spa/number/spa_filter_1_duration/state", "120")
        c.saveFilter1(24, 60)
        assertTrue(transport.published.isEmpty())
        assertEquals("Invalid filter settings", c.ui.value.message)
    }

    @Test
    fun filterSaveRefusedUntilTheSpasOwnValuesAreKnown() = runTest {
        val c = controller(); healthy()
        c.saveFilter1(20, 120)
        assertTrue(transport.published.isEmpty())
        assertEquals("Filter settings not known yet", c.ui.value.message)
    }
}
