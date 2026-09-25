package ai.northtrail.spa.model

/** Every topic the gateway publishes or accepts (verified against live discovery 2026-09-25). */
class SpaTopics(root: String = "spa") {
    private val r = root.trimEnd('/')

    val subscription = "$r/#"

    val status = "$r/status"
    val busConnected = "$r/binary_sensor/spa_bus_connected/state"
    val currentTemp = "$r/sensor/spa_current_temperature/state"
    val targetTemp = "$r/sensor/spa_target_temperature/state"
    val heating = "$r/binary_sensor/spa_heating/state"
    val priming = "$r/binary_sensor/spa_priming/state"
    val filter1Running = "$r/binary_sensor/spa_filter_cycle_1_running/state"
    val jetsState = "$r/fan/spa_jets/state"
    val jetsLevel = "$r/fan/spa_jets/speed_level/state"
    val light = "$r/switch/spa_light/state"
    val hold = "$r/switch/spa_hold/state"
    val heatMode = "$r/select/spa_heating_mode/state"
    val range = "$r/select/spa_temperature_range/state"
    val scale = "$r/select/spa_temperature_scale/state"
    val filter1StartHour = "$r/number/spa_filter_1_start_hour/state"
    val filter1Duration = "$r/number/spa_filter_1_duration/state"
    val model = "$r/sensor/spa_model/state"
    val firmware = "$r/sensor/spa_firmware_version/state"
    val notification = "$r/sensor/spa_notification/state"

    val targetTempCmd = "$r/climate/spa/target_temperature/command"
    val jetsCmd = "$r/fan/spa_jets/command"
    val jetsLevelCmd = "$r/fan/spa_jets/speed_level/command"
    val lightCmd = "$r/switch/spa_light/command"
    val holdCmd = "$r/switch/spa_hold/command"
    val heatModeCmd = "$r/select/spa_heating_mode/command"
    val rangeCmd = "$r/select/spa_temperature_range/command"
    val filter1StartHourCmd = "$r/number/spa_filter_1_start_hour/command"
    val filter1DurationCmd = "$r/number/spa_filter_1_duration/command"
    val clearNotificationCmd = "$r/button/spa_clear_notification/command"

    fun isUnderRoot(topic: String): Boolean = topic.startsWith("$r/")
}
