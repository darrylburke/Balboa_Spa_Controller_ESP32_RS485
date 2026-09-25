package ai.northtrail.spa.data

import ai.northtrail.spa.model.BrokerConfig
import ai.northtrail.spa.model.LinkState
import ai.northtrail.spa.model.LinkStatus
import ai.northtrail.spa.model.Publish
import ai.northtrail.spa.model.SpaTopics
import android.content.Context
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import java.security.KeyStore
import java.security.cert.CertificateFactory
import javax.net.ssl.TrustManagerFactory

class MqttRepository(
    private val context: Context,
    private val configStore: ConfigStore,
) : SpaTransport {
    private val mutableLink = MutableStateFlow(LinkStatus())
    override val link: StateFlow<LinkStatus> = mutableLink.asStateFlow()
    private val mutableMessages = MutableSharedFlow<IncomingMessage>(
        extraBufferCapacity = 256,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    override val messages: SharedFlow<IncomingMessage> = mutableMessages.asSharedFlow()

    @Volatile private var session: HiveMqSession? = null

    @Synchronized
    fun saveAndConnect(config: BrokerConfig) {
        configStore.save(config)
        disconnect()
        connect()
    }

    @Synchronized
    override fun connect() {
        val config = configStore.load()
        if (!config.isUsable) return
        if (session?.clientState?.isConnectedOrReconnect == true) return
        // Never drop a session without stopping it: an abandoned client keeps
        // reconnecting with the same client id and knocks the live one off.
        session?.stop()
        session = null
        val trust = try {
            createTrustManagerFactory()
        } catch (error: Throwable) {
            mutableLink.value = LinkStatus(LinkState.DISCONNECTED, "TLS setup failed: ${safeMessage(error)}")
            return
        }
        lateinit var created: HiveMqSession
        created = HiveMqSession(
            host = config.host,
            port = config.port,
            username = config.username,
            password = config.password.toByteArray(),
            clientId = "spa-${configStore.uiId()}",
            trustManagerFactory = trust,
            subscription = SpaTopics(config.topicRoot).subscription,
            // A superseded session must never overwrite the current link state.
            onLink = { state, detail, at -> setLinkFrom(created, state, detail, at) },
            onMessage = { if (session === created) mutableMessages.tryEmit(it) },
        )
        session = created
        created.start()
    }

    @Synchronized
    override fun disconnect() {
        session?.stop()
        session = null
        mutableLink.value = LinkStatus(LinkState.DISCONNECTED, "Disconnected")
    }

    override fun publish(publish: Publish): Boolean = session?.publish(publish) ?: false

    @Synchronized
    private fun setLinkFrom(source: HiveMqSession, state: LinkState, detail: String, connectedAt: Long?) {
        if (session !== source) return
        mutableLink.value = LinkStatus(state, detail, connectedAt)
    }

    /**
     * Trusts only `assets/broker_ca.pem` when it is bundled (a broker on a private
     * CA), otherwise the system trust store. TLS is used either way.
     */
    private fun createTrustManagerFactory(): TrustManagerFactory {
        val keyStore = if (BROKER_CA_ASSET in context.assets.list("").orEmpty()) {
            val certificate = context.assets.open(BROKER_CA_ASSET).use { stream ->
                CertificateFactory.getInstance("X.509").generateCertificate(stream)
            }
            KeyStore.getInstance(KeyStore.getDefaultType()).apply {
                load(null)
                setCertificateEntry("broker-ca", certificate)
            }
        } else {
            null
        }
        return TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm()).apply {
            init(keyStore)
        }
    }

    private companion object {
        const val BROKER_CA_ASSET = "broker_ca.pem"
    }
}
