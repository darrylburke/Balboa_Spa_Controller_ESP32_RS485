package ai.northtrail.spa.model

data class BrokerConfig(
    val host: String = "",
    val port: Int = 8883,
    val username: String = "",
    val password: String = "",
    val topicRoot: String = "spa",
) {
    val isUsable: Boolean
        get() = host.isNotBlank() && port in 1..65535 && username.isNotBlank() &&
            password.isNotEmpty() && topicRoot.isNotBlank()
}
