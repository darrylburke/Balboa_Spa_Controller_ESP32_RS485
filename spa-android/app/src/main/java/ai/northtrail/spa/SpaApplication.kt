package ai.northtrail.spa

import ai.northtrail.spa.data.ConfigStore
import ai.northtrail.spa.data.MqttRepository
import android.app.Application

class SpaApplication : Application() {
    lateinit var configStore: ConfigStore
        private set
    lateinit var repository: MqttRepository
        private set

    override fun onCreate() {
        super.onCreate()
        configStore = ConfigStore(this)
        repository = MqttRepository(this, configStore)
    }
}
