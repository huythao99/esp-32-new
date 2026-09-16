#pragma once

#include <Arduino.h>

// MQTT + WiFi management. All of this runs on Core 1 (mqttClient is only ever
// touched here and in the loop) — never from the Core 0 worker.

// Connect + (re)subscribe. Builds the MQTT_TOPIC_* strings on success.
bool connectToMqtt();

// Install the MQTT message callback (call once from setup).
void setupMqttCallback();

// Publish any OTA status lines the worker queued (call every loop iteration).
void drainOtaStatus();

// Non-blocking WiFi reset (clears DNS cache after repeated MQTT failures).
// Kicks off disconnect+reconnect; completion is handled in loop().
void startWiFiReset();
