#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Blocking HTTP / OTA. EVERY function here runs on the Core 0 worker task only
// (they share a single HTTPClient instance). Never call these from loop()/Core 1
// — enqueue a Job via worker.h instead.
// ---------------------------------------------------------------------------

// Configure the shared HTTPClient (call once from setup, before the task starts).
void netHttpInit();

// Error reporting (Core 0). trackLogError posts immediately; trackLogRL is the
// rate-limited variant used internally and by the JOB_LOG handler.
bool trackLogError(const String& errorCode, const String& errorMessage);
bool trackLogRL(const String& code, const String& message, unsigned long cooldownMs = 60000);

// Backend fetches. Update shared state (lastSetupValue / schedules[]) under stateMutex.
String getDeviceSettings(const String& deviceUid, const String& deviceSSID);
String getScheduleSettings(const String& deviceUid, const String& deviceSSID);
bool   registerDevice(const String& deviceId, const String& deviceName, const String& userId);
bool   updateFirmwareVersion(const String& firmwareVersion);

// OTA.
String getFirmwareS3URL(const String& version = "latest");
bool   performFOTAUpdate(const String& firmwareURL);
void   handleFirmwareUpdate();

// Enqueue an OTA/status JSON line for Core 1 to publish over MQTT. Safe from any
// core (does not touch mqttClient directly).
void   publishOTAStatus(const String& status, const String& message = "", int progress = -1);
