package ai.northtrail.spa.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BrokerConfigTest {
    @Test
    fun defaultsLeaveBrokerAndAccountBlank() {
        val c = BrokerConfig()
        assertEquals("", c.host)
        assertEquals(8883, c.port)
        assertEquals("", c.username)
        assertEquals("spa", c.topicRoot)
    }

    @Test
    fun usableOnlyWithPasswordAndSanePort() {
        val filled = BrokerConfig(host = "broker.example.com", username = "spa-app", password = "x")
        assertFalse(BrokerConfig().isUsable)
        assertFalse(filled.copy(password = "").isUsable)
        assertTrue(filled.isUsable)
        assertFalse(filled.copy(port = 0).isUsable)
        assertFalse(filled.copy(host = " ").isUsable)
        assertFalse(filled.copy(username = " ").isUsable)
    }
}
