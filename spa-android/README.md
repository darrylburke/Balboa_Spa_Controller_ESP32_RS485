# Hot Tub — Android

Native Android app for a Balboa spa, talking to the ESP32 gateway
(`../balboa-esp32-mqtt`) over MQTT/TLS.

## Build

    ./gradlew testDebugUnitTest assembleDebug

APK: `app/build/outputs/apk/debug/app-debug.apk`

    ~/Android/Sdk/platform-tools/adb install -r app/build/outputs/apk/debug/app-debug.apk

## Broker certificate

TLS is always on; plain MQTT is not supported.

- Broker with a publicly trusted certificate (e.g. Let's Encrypt): nothing to do,
  the system trust store is used.
- Broker on a private CA: copy the CA certificate (PEM) to
  `app/src/main/assets/broker_ca.pem` before building. When that file is present
  it is the **only** CA trusted. It is git-ignored so it never gets committed.

## First run

Enter your broker host, port (default 8883), username and password, then tap
**Save and connect**. The topic root defaults to `spa`, matching the gateway.
The password is encrypted with an Android Keystore key and never shown again.

Give the app its own broker account, limited to the spa's topics (e.g. an ACL of
`topic readwrite spa/#`), rather than reusing the gateway's account.

## Design

`docs/superpowers/specs/2026-09-25-spa-android-app-design.md`
