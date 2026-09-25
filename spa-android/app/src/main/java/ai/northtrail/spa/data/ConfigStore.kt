package ai.northtrail.spa.data

import ai.northtrail.spa.DisplayUnit
import ai.northtrail.spa.model.BrokerConfig
import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import java.util.UUID
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

class ConfigStore(context: Context) {
    private val preferences = context.getSharedPreferences("spa_config", Context.MODE_PRIVATE)

    fun load(): BrokerConfig {
        val defaults = BrokerConfig()
        return BrokerConfig(
            host = preferences.getString("host", defaults.host).orEmpty(),
            port = preferences.getInt("port", defaults.port),
            username = preferences.getString("username", defaults.username).orEmpty(),
            password = decryptPassword(),
            topicRoot = preferences.getString("topic_root", defaults.topicRoot).orEmpty(),
        )
    }

    fun isConfigured(): Boolean = preferences.getBoolean("configured", false) && load().isUsable

    fun save(config: BrokerConfig) {
        require(config.isUsable) { "Broker configuration is incomplete" }
        val (ciphertext, iv) = encrypt(config.password)
        preferences.edit()
            .putString("host", config.host.trim())
            .putInt("port", config.port)
            .putString("username", config.username.trim())
            .putString("topic_root", config.topicRoot.trim().trimEnd('/'))
            .putString("password_ciphertext", ciphertext)
            .putString("password_iv", iv)
            .putBoolean("configured", true)
            .apply()
    }

    fun uiId(): String {
        val existing = preferences.getString("ui_id", null)
        if (!existing.isNullOrBlank()) return existing
        val created = "android-${UUID.randomUUID().toString().replace("-", "").take(12)}"
        preferences.edit().putString("ui_id", created).apply()
        return created
    }

    fun loadDisplayUnit(): DisplayUnit =
        runCatching { DisplayUnit.valueOf(preferences.getString("display_unit", null).orEmpty()) }
            .getOrDefault(DisplayUnit.FOLLOW_SPA)

    fun saveDisplayUnit(unit: DisplayUnit) {
        preferences.edit().putString("display_unit", unit.name).apply()
    }

    private fun encrypt(cleartext: String): Pair<String, String> {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, secretKey())
        return Base64.encodeToString(cipher.doFinal(cleartext.toByteArray()), Base64.NO_WRAP) to
            Base64.encodeToString(cipher.iv, Base64.NO_WRAP)
    }

    private fun decryptPassword(): String {
        val ciphertext = preferences.getString("password_ciphertext", null) ?: return ""
        val iv = preferences.getString("password_iv", null) ?: return ""
        return runCatching {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, secretKey(), GCMParameterSpec(128, Base64.decode(iv, Base64.NO_WRAP)))
            String(cipher.doFinal(Base64.decode(ciphertext, Base64.NO_WRAP)))
        }.getOrDefault("")
    }

    private fun secretKey(): SecretKey {
        val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (keyStore.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        generator.init(
            KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build(),
        )
        return generator.generateKey()
    }

    private companion object {
        const val KEY_ALIAS = "spa_mqtt_password"
        const val TRANSFORMATION = "AES/GCM/NoPadding"
    }
}
