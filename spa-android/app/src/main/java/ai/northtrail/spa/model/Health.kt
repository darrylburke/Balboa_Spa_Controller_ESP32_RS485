package ai.northtrail.spa.model

enum class LinkState { DISCONNECTED, CONNECTING, CONNECTED, REJECTED }

data class LinkStatus(
    val state: LinkState = LinkState.DISCONNECTED,
    val detail: String = "",
    val connectedAtMillis: Long? = null,
)

enum class Problem(val label: String) {
    CREDENTIALS_REJECTED("Broker rejected the app's login"),
    BROKER_UNREACHABLE("Can't reach broker"),
    WAITING_FOR_GATEWAY("Waiting for gateway"),
    GATEWAY_OFFLINE("Gateway offline"),
    BUS_DISCONNECTED("Gateway can't hear the spa"),
    STALE("No updates from the spa"),
}

data class Health(val problem: Problem?, val dataAgeMillis: Long?) {
    val controlsEnabled: Boolean get() = problem == null
}

object Liveness {
    /** The gateway publishes on change plus a 60 s refresh, so silence is normal below this. */
    const val STALE_AFTER_MS = 90_000L

    fun evaluate(link: LinkStatus, spa: SpaState, nowMillis: Long): Health {
        val age = spa.lastLiveMessageAtMillis?.let { nowMillis - it }
        val gateway = spa.gatewayOnline
        val problem = when {
            link.state == LinkState.REJECTED -> Problem.CREDENTIALS_REJECTED
            link.state != LinkState.CONNECTED -> Problem.BROKER_UNREACHABLE
            gateway == null -> Problem.WAITING_FOR_GATEWAY
            !gateway.value -> Problem.GATEWAY_OFFLINE
            spa.busConnected?.value != true -> Problem.BUS_DISCONNECTED
            else -> {
                // Measure from the later of the last live message and connecting, so that
                // opening the app with only retained data is not immediately "stale".
                val reference = maxOf(spa.lastLiveMessageAtMillis ?: 0L, link.connectedAtMillis ?: 0L)
                if (nowMillis - reference > STALE_AFTER_MS) Problem.STALE else null
            }
        }
        return Health(problem, age)
    }
}

fun formatAge(millis: Long): String {
    val s = millis / 1_000
    return when {
        s < 60 -> "${s}s"
        s < 3_600 -> "${s / 60}m"
        else -> "${s / 3_600}h"
    }
}
