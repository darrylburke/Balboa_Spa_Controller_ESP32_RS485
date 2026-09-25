package ai.northtrail.spa.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TemperatureTest {
    @Test
    fun everyWholeFahrenheitRoundTripsThroughTheFirmware() {
        for (f in 50..104) {
            val c = Temperature.commandCelsius(f.toDouble(), TempScale.FAHRENHEIT)
            assertEquals("°F $f via $c", f, Temperature.celsiusToSpaRaw(c, TempScale.FAHRENHEIT))
        }
    }

    @Test
    fun everyHalfCelsiusRoundTripsThroughTheFirmware() {
        var c = 10.0
        while (c <= 40.0) {
            val sent = Temperature.commandCelsius(c, TempScale.CELSIUS)
            assertEquals("°C $c", (c * 2).toInt(), Temperature.celsiusToSpaRaw(sent, TempScale.CELSIUS))
            c += 0.5
        }
    }

    @Test
    fun eightyEightFahrenheitIsTheValueProvenOnTheSpa() {
        assertEquals(31.1, Temperature.commandCelsius(88.0, TempScale.FAHRENHEIT), 0.0)
    }

    @Test
    fun publishedCelsiusReadsBackAsWholeFahrenheit() {
        assertEquals(87.0, Temperature.spaUnitsFromCelsius(30.6, TempScale.FAHRENHEIT), 0.0)
        assertEquals(88.0, Temperature.spaUnitsFromCelsius(31.1, TempScale.FAHRENHEIT), 0.0)
        assertEquals(30.5, Temperature.spaUnitsFromCelsius(30.5, TempScale.CELSIUS), 0.0)
    }

    @Test
    fun limitsPerRangeAndScale() {
        assertEquals(80.0..104.0, Temperature.limits(TempRange.HIGH, TempScale.FAHRENHEIT))
        assertEquals(50.0..99.0, Temperature.limits(TempRange.LOW, TempScale.FAHRENHEIT))
        assertEquals(26.5..40.0, Temperature.limits(TempRange.HIGH, TempScale.CELSIUS))
        assertEquals(10.0..37.0, Temperature.limits(TempRange.LOW, TempScale.CELSIUS))
    }

    @Test
    fun sameSpaSettingComparesAtSpaResolution() {
        assertTrue(Temperature.sameSpaSetting(31.1, 31.11, TempScale.FAHRENHEIT))
        assertFalse(Temperature.sameSpaSetting(31.1, 31.7, TempScale.FAHRENHEIT))
    }

    @Test
    fun formatsInTheRequestedUnit() {
        assertEquals("87°F", Temperature.format(30.6, TempScale.FAHRENHEIT))
        assertEquals("30.6°C", Temperature.format(30.6, TempScale.CELSIUS))
    }

    @Test
    fun stepIsOneFahrenheitOrHalfCelsius() {
        assertEquals(1.0, Temperature.step(TempScale.FAHRENHEIT), 0.0)
        assertEquals(0.5, Temperature.step(TempScale.CELSIUS), 0.0)
    }
}
