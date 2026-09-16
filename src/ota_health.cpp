#include "ota_health.h"
#include "config.h"
#include "worker.h"      // trackLog()
#include <Arduino.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"

// Override the core's weak hook (esp32-hal-misc.c). Returning true tells
// arduino-esp32 NOT to auto-mark a PENDING_VERIFY image valid at boot, leaving
// the confirm/rollback decision to the app.
extern "C" bool verifyRollbackLater() { return true; }

static bool g_pendingVerify = false;

void otaHealthBegin() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  if (running && esp_ota_get_state_partition(running, &st) == ESP_OK &&
      st == ESP_OTA_IMG_PENDING_VERIFY) {
    g_pendingVerify = true;
    DBG_PRINTLN("[OTA] running image PENDING_VERIFY -> awaiting health check");
    trackLog("OTA_PENDING_VERIFY", "New firmware booted, awaiting health confirmation");
  }
}

void otaHealthTick(bool healthy) {
  if (!g_pendingVerify || !healthy) return;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    g_pendingVerify = false;
    DBG_PRINTLN("[OTA] health OK -> image marked valid, rollback cancelled");
    trackLog("OTA_CONFIRMED", "New firmware confirmed healthy (WiFi+MQTT up)");
  }
}

bool otaIsPendingVerify() { return g_pendingVerify; }
