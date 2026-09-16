#pragma once

// ---------------------------------------------------------------------------
// App-level OTA rollback (Phương án 1: confirm-on-healthy, no forced rollback).
//
// arduino-esp32 normally marks a freshly-OTA'd image valid before setup() runs,
// which defeats rollback. We override verifyRollbackLater() (in ota_health.cpp)
// so the new image stays ESP_OTA_IMG_PENDING_VERIFY, then confirm it ONLY once it
// proves healthy (WiFi + MQTT up). If it crashes or hangs before that, the Task
// Watchdog reboots it and the bootloader rolls back to the previous app.
//
// A device that simply cannot reach the network is NOT rolled back here — it just
// stays unconfirmed until it eventually connects (avoids rollback on a flaky AP).
// ---------------------------------------------------------------------------

// setup(): detect if the running image is awaiting confirmation. Call after the
// worker/queues exist (it may trackLog).
void otaHealthBegin();

// loop(): pass the current health state; marks the image valid on first healthy.
void otaHealthTick(bool healthy);

// True while a freshly-OTA'd image has not yet been confirmed.
bool otaIsPendingVerify();
