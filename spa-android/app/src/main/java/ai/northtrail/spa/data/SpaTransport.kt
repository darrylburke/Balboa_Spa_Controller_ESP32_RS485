package ai.northtrail.spa.data

import ai.northtrail.spa.model.LinkStatus
import ai.northtrail.spa.model.Publish
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow

data class IncomingMessage(val topic: String, val payload: String, val retained: Boolean)

interface SpaTransport {
    val link: StateFlow<LinkStatus>
    val messages: SharedFlow<IncomingMessage>
    fun connect()
    fun disconnect()

    /** Returns false if the message could not be handed to a connected client. */
    fun publish(publish: Publish): Boolean
}
