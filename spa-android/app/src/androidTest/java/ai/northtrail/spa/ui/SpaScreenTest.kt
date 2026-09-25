package ai.northtrail.spa.ui

import ai.northtrail.spa.UiState
import ai.northtrail.spa.model.Health
import ai.northtrail.spa.model.Observed
import ai.northtrail.spa.model.Problem
import ai.northtrail.spa.model.SpaState
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import org.junit.Rule
import org.junit.Test

class SpaScreenTest {
    @get:Rule val rule = createComposeRule()

    private val spa = SpaState(
        currentTempC = Observed(30.6, true),
        targetTempC = Observed(31.1, true),
        jetsOn = Observed(false, true),
        light = Observed(false, true),
    )

    private fun show(ui: UiState) = rule.setContent {
        SpaTheme { SpaScreen(ui, onStep = {}, onJets = {}, onLight = {}, onHeatMode = {}, onRange = {}) }
    }

    @Test
    fun unreachableStateDisablesEveryControl() {
        show(UiState(configured = true, spa = spa, health = Health(Problem.GATEWAY_OFFLINE, 840_000L)))
        rule.onNodeWithText("Gateway offline", substring = true).assertExists()
        rule.onNodeWithContentDescription("Raise target").assertIsNotEnabled()
        rule.onNodeWithContentDescription("Lower target").assertIsNotEnabled()
        rule.onNodeWithContentDescription("Jets").assertIsNotEnabled()
        rule.onNodeWithContentDescription("Light").assertIsNotEnabled()
    }

    @Test
    fun healthyStateEnablesControlsAndShowsTemperatures() {
        show(UiState(configured = true, spa = spa, health = Health(null, 2_000L)))
        rule.onNodeWithText("87°F").assertExists()
        rule.onNodeWithText("Set 88°F").assertExists()
        rule.onNodeWithContentDescription("Raise target").assertIsEnabled()
    }
}
