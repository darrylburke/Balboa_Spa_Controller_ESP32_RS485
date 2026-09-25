package ai.northtrail.spa.data

import ai.northtrail.spa.model.LinkState
import com.hivemq.client.mqtt.MqttClientState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.net.ServerSocket
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread

class HiveMqSessionTest {
    private fun session(port: Int, links: MutableList<LinkState>) = HiveMqSession(
        host = "127.0.0.1",
        port = port,
        username = "spa-app",
        password = "x".toByteArray(),
        clientId = "test-${System.nanoTime()}",
        trustManagerFactory = null,
        subscription = "spa/#",
        onLink = { state, _, _ -> synchronized(links) { links += state } },
        onMessage = {},
    )

    /** A broker that answers every CONNECT with CONNACK return code 5 (not authorized). */
    private fun rejectingBroker(accepted: AtomicInteger): ServerSocket {
        val server = ServerSocket(0)
        thread(isDaemon = true) {
            while (!server.isClosed) {
                val socket = runCatching { server.accept() }.getOrNull() ?: break
                accepted.incrementAndGet()
                thread(isDaemon = true) {
                    socket.use {
                        it.getInputStream().read(ByteArray(512))
                        it.getOutputStream().write(byteArrayOf(0x20, 0x02, 0x00, 0x05))
                        it.getOutputStream().flush()
                        Thread.sleep(300)
                    }
                }
            }
        }
        return server
    }

    @Test
    fun stoppedSessionStopsReconnectingWhileTheBrokerIsUnreachable() {
        val closedPort = ServerSocket(0).use { it.localPort }
        val s = session(closedPort, mutableListOf())
        s.start()
        Thread.sleep(1_500) // first attempt fails; automatic reconnect is now scheduled
        s.stop()
        Thread.sleep(5_000) // long enough for the next scheduled attempt to run and fail
        assertEquals(MqttClientState.DISCONNECTED, s.clientState)
    }

    @Test
    fun rejectedCredentialsReportRejectedAndDoNotRetry() {
        val accepted = AtomicInteger()
        val broker = rejectingBroker(accepted)
        val links = mutableListOf<LinkState>()
        val s = session(broker.localPort, links)
        s.start()
        Thread.sleep(4_000)
        broker.close()
        assertTrue("links seen: $links", synchronized(links) { links.toList() }.contains(LinkState.REJECTED))
        assertEquals("connection attempts", 1, accepted.get())
        assertEquals(MqttClientState.DISCONNECTED, s.clientState)
    }
}
