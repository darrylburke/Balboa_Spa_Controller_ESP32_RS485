package ai.northtrail.spa.model

import java.util.Locale

enum class TempScale { FAHRENHEIT, CELSIUS }
enum class TempRange { HIGH, LOW }

object Temperature {
    fun cToF(c: Double): Double = c * 9.0 / 5.0 + 32.0
    fun fToC(f: Double): Double = (f - 32.0) * 5.0 / 9.0

    /** Mirrors the firmware's celsius_to_spa_raw() (std::lround: half away from zero). */
    fun celsiusToSpaRaw(celsius: Double, scale: TempScale): Int = when (scale) {
        TempScale.CELSIUS -> lround(celsius * 2.0)
        TempScale.FAHRENHEIT -> lround(cToF(celsius))
    }

    /** A published °C value expressed in the spa's own units: whole °F, or 0.5 °C steps. */
    fun spaUnitsFromCelsius(celsius: Double, scale: TempScale): Double = when (scale) {
        TempScale.CELSIUS -> celsiusToSpaRaw(celsius, scale) / 2.0
        TempScale.FAHRENHEIT -> celsiusToSpaRaw(celsius, scale).toDouble()
    }

    fun celsiusFromSpaUnits(units: Double, scale: TempScale): Double = when (scale) {
        TempScale.CELSIUS -> units
        TempScale.FAHRENHEIT -> fToC(units)
    }

    /** The °C payload value that the firmware maps back to exactly [spaUnits]. */
    fun commandCelsius(spaUnits: Double, scale: TempScale): Double = when (scale) {
        TempScale.CELSIUS -> spaUnits
        TempScale.FAHRENHEIT -> lround(fToC(spaUnits) * 10.0) / 10.0
    }

    fun step(scale: TempScale): Double = if (scale == TempScale.CELSIUS) 0.5 else 1.0

    /** Balboa standard set-point limits. UNVERIFIED on this spa (spec §7.3). */
    fun limits(range: TempRange, scale: TempScale): ClosedFloatingPointRange<Double> = when (scale) {
        TempScale.FAHRENHEIT -> if (range == TempRange.HIGH) 80.0..104.0 else 50.0..99.0
        TempScale.CELSIUS -> if (range == TempRange.HIGH) 26.5..40.0 else 10.0..37.0
    }

    fun sameSpaSetting(aCelsius: Double, bCelsius: Double, scale: TempScale): Boolean =
        celsiusToSpaRaw(aCelsius, scale) == celsiusToSpaRaw(bCelsius, scale)

    fun format(celsius: Double, unit: TempScale): String = when (unit) {
        TempScale.FAHRENHEIT -> "${lround(cToF(celsius))}°F"
        TempScale.CELSIUS -> String.format(Locale.US, "%.1f°C", celsius)
    }

    private fun lround(x: Double): Int =
        (if (x >= 0) Math.floor(x + 0.5) else Math.ceil(x - 0.5)).toInt()
}
