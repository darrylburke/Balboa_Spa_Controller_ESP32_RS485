package ai.northtrail.spa.data

import ai.northtrail.spa.model.LinkState
import ai.northtrail.spa.model.Publish
import com.hivemq.client.mqtt.MqttClientState
import com.hivemq.client.mqtt.MqttGlobalPublishFilter
import com.hivemq.client.mqtt.datatypes.MqttQos
import com.hivemq.client.mqtt.mqtt3.Mqtt3AsyncClient
import com.hivemq.client.mqtt.mqtt3.Mqtt3Client
import com.hivemq.client.mqtt.mqtt3.exceptions.Mqtt3ConnAckException
import com.hivemq.client.mqtt.mqtt3.message.connect.connack.Mqtt3ConnAckReturnCode
import javax.net.ssl.SSLHandshakeException
import java.util.concurrent.TimeUnit
import javax.net.ssl.TrustManagerFactory

/**
 * One MQTT client lifetime. Plain JVM (no Android types) so its reconnect and
 * shutdown behaviour can be tested against a local socket.
 */
class HiveMqSession(
    host: String,
    port: Int,
    private val username: String,
    private val password: ByteArray,
    clientId: String,
    trustManagerFactory: TrustManagerFactory?,
    private val subscription: String,
    private val onLink: (LinkState, String, Long?) -> Unit,
    private val onMessage: (IncomingMessage) -> Unit,
) {
    @Volatile private var stopped = false
    private val client: Mqtt3AsyncClient

    init {
        var builder = Mqtt3Client.builder()
            .identifier(clientId)
            .serverHost(host)
            .serverPort(port)
        if (trustManagerFactory != null) {
            builder = builder.sslConfig().trustManagerFactory(trustManagerFactory).applySslConfig()
        }
        client = builder
            .automaticReconnect()
                .initialDelay(1, TimeUnit.SECONDS)
                .maxDelay(60, TimeUnit.SECONDS)
                .applyAutomaticReconnect()
            .addConnectedListener {
                // A stopped session that still managed to (re)connect must hang up.
                if (stopped) client.disconnect() else subscribe()
            }
            .addDisconnectedListener { context ->
                // disconnect() cannot cancel a scheduled automatic reconnect; only the
                // listener can. Without this a stopped session retries forever.
                if (stopped) {
                    context.reconnector.reconnect(false)
                    return@addDisconnectedListener
                }
                val fatal = fatalReason(context.cause)
                if (fatal != null) {
                    // Wrong password, ACL or certificate: retrying only hammers the broker.
                    stopped = true
                    context.reconnector.reconnect(false)
                    onLink(LinkState.REJECTED, fatal, null)
                } else {
                    onLink(LinkState.DISCONNECTED, "Connection lost | retrying", null)
                }
            }
            .buildAsync()
        client.publishes(MqttGlobalPublishFilter.ALL) { publish ->
            if (stopped) return@publishes
            onMessage(
                IncomingMessage(
                    topic = publish.topic.toString(),
                    payload = publish.payloadAsBytes.toString(Charsets.UTF_8),
                    retained = publish.isRetain,
                ),
            )
        }
    }

    val clientState: MqttClientState get() = client.state

    fun start() {
        onLink(LinkState.CONNECTING, "Connecting with TLS", null)
        client.connectWith()
            .cleanSession(true)
            .keepAlive(30)
            .simpleAuth()
                .username(username)
                .password(password)
                .applySimpleAuth()
            .send()
            .whenComplete { _, error ->
                if (stopped || error == null) return@whenComplete
                onLink(LinkState.DISCONNECTED, "Connect failed: ${safeMessage(error)}", null)
            }
    }

    private fun subscribe() {
        client.subscribeWith()
            .topicFilter(subscription)
            .qos(MqttQos.AT_LEAST_ONCE)
            .send()
            .whenComplete { _, error ->
                if (stopped) return@whenComplete
                if (error == null) {
                    onLink(LinkState.CONNECTED, "Connected | TLS", System.currentTimeMillis())
                } else {
                    onLink(LinkState.DISCONNECTED, "Subscription failed: ${safeMessage(error)}", null)
                }
            }
    }

    fun stop() {
        stopped = true
        client.disconnect()
    }

    fun publish(publish: Publish): Boolean {
        if (stopped || !client.state.isConnected) return false
        return runCatching {
            client.publishWith()
                .topic(publish.topic)
                .qos(MqttQos.AT_LEAST_ONCE)
                .payload(publish.payload.toByteArray())
                .retain(false)
                .send()
            true
        }.getOrDefault(false)
    }
}

private fun fatalReason(cause: Throwable): String? {
    for (e in generateSequence(cause) { it.cause }) {
        if (e is Mqtt3ConnAckException) {
            val code = e.mqttMessage.returnCode
            if (code == Mqtt3ConnAckReturnCode.BAD_USER_NAME_OR_PASSWORD ||
                code == Mqtt3ConnAckReturnCode.NOT_AUTHORIZED
            ) return "Broker rejected credentials or ACL"
        }
        if (e is SSLHandshakeException) return "TLS handshake failed: ${safeMessage(e)}"
    }
    return null
}

internal fun safeMessage(error: Throwable): String =
    error.message?.replace(Regex("(?i)(password|username)=[^, ]+"), "$1=[hidden]")
        ?.take(100) ?: error.javaClass.simpleName
