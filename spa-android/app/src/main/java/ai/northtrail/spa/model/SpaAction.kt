package ai.northtrail.spa.model

sealed interface SpaAction {
    data class SetTarget(val celsius: Double) : SpaAction
    data class SetJets(val speed: JetsSpeed) : SpaAction
    data class SetLight(val on: Boolean) : SpaAction
    data class SetHold(val on: Boolean) : SpaAction
    data class SetHeatMode(val mode: HeatMode) : SpaAction
    data class SetRange(val range: TempRange) : SpaAction
    data class SetFilter1(val startHour: Int, val durationMinutes: Int) : SpaAction
    data object ClearNotification : SpaAction
}
