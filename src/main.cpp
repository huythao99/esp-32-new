#include <WiFi.h>
#include "ESPAsyncWebServer.h"
#include <TimeLib.h>
#include "time.h"
#include <string>
#include "EEPROM.h"
#include "SoftwareSerial.h"
#include <PubSubClient.h>
#include "mbedtls/md.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <HTTPUpdate.h>
#include "wifi_page.h"

// Debug logging. Set DEBUG to 0 for production to compile out all USB-serial
// debug output (removes ~90 blocking Serial.print calls from the hot paths).
// Note: this only affects the USB Serial; the STM32 link (testSerial) is untouched.
#define DEBUG 1
#if DEBUG
  #define DBG_PRINT(...)   Serial.print(__VA_ARGS__)
  #define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define DBG_PRINT(...)   do {} while (0)
  #define DBG_PRINTLN(...) do {} while (0)
#endif

#define WIFI_BROADCAST_SSID "GTIControl1171"

#define KEY_SPLIT "&&&&"
#define KEY_SPLIT_DATA "#"

#define RX 13 // 13
#define TX 12 // 12

#define STM_READY 14
#define STM_START 2

// MQTT Configuration
#define MQTT_SERVER "giabao-inverter.com"
#define MQTT_PORT 1883
#define MQTT_USERNAME "giabao"
#define MQTT_PASSWORD "0918273645"

// SHA256 Authentication Key
#define SHA_SECRET_KEY "K8mN2pQ7vX4bE9fH3gJ6kL1mP5sT8wZ2"

EspSoftwareSerial::UART testSerial;

// Preferences instance
Preferences preferences;

// MQTT client
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);

// MQTT Topics
String MQTT_TOPIC_DATA;
String MQTT_TOPIC_SETUP;
String MQTT_TOPIC_SCHEDULE;
String MQTT_TOPIC_STATUS;
String MQTT_TOPIC_FIRMWARE;
String MQTT_TOPIC_OTA_STATUS;
String MQTT_TOPIC_CMD_SETTINGS;   // server tells us to re-fetch the setting
String MQTT_TOPIC_CMD_SCHEDULE;   // server tells us to re-fetch the schedule

// Command sync state (debounced): the MQTT callback only sets these flags; the
// actual blocking HTTP fetch runs from loop() so the MQTT callback stays fast.
volatile bool cmdSettingsPending = false;
volatile bool cmdSchedulePending = false;
unsigned long cmdSettingsAt = 0;
unsigned long cmdScheduleAt = 0;
const long cmdDebounce = 500;     // coalesce bursts arriving within 500ms


String DEVICES_PATH = "/devices/inverter/";
String DATA_PATH = "/data/inverter/battery/";
String ERRO_PATH = "/errors/";
AsyncWebServer server(80);

// const String dataExample = "215.37#50.93#1242.45#24.87#472.55#37.00#21.00#60.00#20.00#35.00";

const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "password";
const char* PARAM_INPUT_3 = "uid";

const char* ntpServer = "time.google.com";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 0;

unsigned long previousMillis = 0;  
unsigned long previousMillisWifi = 0;
unsigned long previousMillisMqtt = 0;
unsigned long previousMillisDigital = 0;
unsigned long previousMillisLCD = 0;
unsigned long previousMillisStorage = 0;
unsigned long previousMillisSetting = 0;
unsigned long previousMillisSchedule = 0;
unsigned long previousMillisMqttReconnect = 0;


long double totalA = 0;
long double totalA2 = 0;


int countLCD = 0;

String uid;
String wifiBroadcastSSID; // Variable to store SSID from Preferences
String currentFirmwareVersion = "1.0.7"; // Variable to store current firmware version

int connectMqtt = -1; // -1: pending; 0: failed; 1: success
bool isMqttConnected = false;
unsigned long lastMqttReconnectAttempt = 0;
int mqttFailCount = 0;  // Count consecutive MQTT connection failures
const int MQTT_MAX_FAIL_BEFORE_RESET = 6;  // Reset WiFi after 6 failures (~60 seconds)

bool isStartRegisterDevice = false;

bool isStartChangeModeWifi = false;

bool isUpdateVersion = false;

bool dataChanged = false;
String param_ssid;
String param_password;

bool isStartConnect = true;
bool isStartMqtt = false;

const long interval = 3000; 
const long intervalDigital = 1000;
const long intervalWifi = 60000;
const long intervalMqtt = 1000;
const long intervalMqttReconnect = 10000;
const long invertalSetting = 60000;   // slow backstop poll; real-time via cmd/settings MQTT
const long intervalSchedule = 60000;  // slow backstop poll; real-time via cmd/schedule MQTT
const long intervalScan = 5000;

// WiFi scan state for the setup page. The scan runs from loop() (not from the
// web handler) so repeated /scan polls can't restart it before it finishes.
String scannedNetworksJson = "[]";  // latest scan result as JSON
unsigned long previousMillisScan = 0;
unsigned long lastScanRequest = 0;  // millis() of the last /scan request
bool scanRequested = false;         // set by /scan?refresh=1, cleared after scan

// Setup-page connect attempt result: -1 = in progress/idle, 0 = failed, 1 = success.
int wifiConnectResult = -1;
unsigned long connectAttemptStart = 0;   // 0 = no active attempt
const long connectTimeout = 15000;       // give up after 15s and report failure

String valueSetup = "";

bool isLow = false;

HTTPClient http;


// Structure to hold schedule data
struct ScheduleItem {
    String startTime;
    String endTime;
    String value;
    String outValue;
};

ScheduleItem schedules[10]; // Array to hold up to 10 schedule items
int scheduleCount = 0;

String readStringFromEEPROM(int addr) {
  char data[150]; // Increased size for UTF-8 Vietnamese strings
  int len = 0;
  unsigned char k;
  k = EEPROM.read(addr);
  // Remove ASCII filter to allow UTF-8 multibyte characters (Vietnamese)
  while (k != '\0' && len < sizeof(data) - 1) {
    data[len] = k;
    len++;
    k = EEPROM.read(addr + len);
  }
  data[len] = '\0';
  return String(data);
}

String getUid() {
  if (uid.isEmpty() == false) {
    uid.trim();  // normalise stray whitespace (used in MQTT topics + URLs)
    return uid;
  }
  String strText = readStringFromEEPROM(0);

  if (strText.length() == 0 || strText.isEmpty()) {
    return "";
  }

  String key_split = KEY_SPLIT;
  int index_split = strText.indexOf(key_split);
  param_ssid = strText.substring(0, index_split);
  strText.replace(param_ssid + key_split, "");
  index_split = strText.indexOf(key_split);
  param_password = strText.substring(0, index_split);
  strText.replace(param_password + key_split, "");
  uid = strText;
  uid.trim();
  return uid;
}


String convertSetupValue(const String& input) {
    if (input.length() != 8) {
        return "";
    }
    
    // Extract values from input (e.g., "99001620")
    int pset = input.substring(0, 4).toInt();  // First 4 digits as Pset
    int vset = input.substring(4, 8).toInt();  // Last 4 digits as Vset
    
    // Create formatted string
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "*%d@%d#", pset, vset);
    
    return String(buffer);
}

String lastSetupValue = ""; // Global variable to store the last setup value

// Save setting value to Preferences (NVS - better wear-leveling than EEPROM)
void saveSettingToStorage(const String& value) {
  if (value.isEmpty()) return;
  preferences.begin("device_setting", false);
  preferences.putString("setup_value", value);
  preferences.end();
}

// Load setting value from Preferences
String loadSettingFromStorage() {
  preferences.begin("device_setting", true);
  String value = preferences.getString("setup_value", "");
  preferences.end();
  return value;
}


String createSignedMessage(const String& payload) {
    // unsigned long timestamp = millis();
    // String message = payload + "|" + String(timestamp);
    
    // String signedMessage = "{\"payload\":\"" + payload + "\",\"timestamp\":" + String(timestamp) + ",\"signature\":\"" + signature + "\"}";
    return payload;
}

// Escape characters that would break the JSON string payload.
String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (unsigned int i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += c;
    }
  }
  return out;
}

// Report an error to the backend. No authentication required; the hardware
// POSTs directly. userId = device UID, deviceId = broadcast SSID.
// Silently no-ops when WiFi is down so callers can log freely.
bool trackLogError(const String& errorCode, const String& errorMessage) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String currentUid = getUid();
  String url = "https://giabao-inverter.com/api/track-log-error";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String jsonPayload = "{";
  jsonPayload += "\"userId\":\"" + jsonEscape(currentUid) + "\",";
  jsonPayload += "\"deviceId\":\"" + jsonEscape(wifiBroadcastSSID) + "\",";
  jsonPayload += "\"errorCode\":\"" + jsonEscape(errorCode) + "\",";
  jsonPayload += "\"errorMessage\":\"" + jsonEscape(errorMessage) + "\"";
  jsonPayload += "}";

  int httpResponseCode = http.POST(jsonPayload);
  bool success = (httpResponseCode == 200 || httpResponseCode == 201);

  DBG_PRINT("Track error log [");
  DBG_PRINT(errorCode);
  DBG_PRINT("] -> HTTP ");
  DBG_PRINTLN(httpResponseCode);

  http.end();
  return success;
}

bool isTimeInRange(const String& currentTime, const String& startTime, const String& endTime) {
    // Convert time strings to minutes since midnight for easier comparison
    int currentMinutes = (currentTime.substring(0, 2).toInt() * 60) + currentTime.substring(3, 5).toInt();
    int startMinutes = (startTime.substring(0, 2).toInt() * 60) + startTime.substring(3, 5).toInt();
    
    // Handle end time that goes past midnight (e.g., 27:00 = 03:00 next day)
    int endMinutes;
    if (endTime.substring(0, 2).toInt() >= 24) {
        // Convert 27:00 to 03:00 (27 - 24 = 3)
        int adjustedHour = endTime.substring(0, 2).toInt() - 24;
        endMinutes = (adjustedHour * 60) + endTime.substring(3, 5).toInt();
    } else {
        endMinutes = (endTime.substring(0, 2).toInt() * 60) + endTime.substring(3, 5).toInt();
    }
    
    // Handle overnight schedule
    if (endMinutes < startMinutes) {
        // Schedule spans midnight (e.g., 18:00 to 03:00)
        return currentMinutes >= startMinutes || currentMinutes <= endMinutes;
    }
    
    // Normal schedule within same day
    return currentMinutes >= startMinutes && currentMinutes <= endMinutes;
}

void parseScheduleData(const String& scheduleData) {
    // Get current time
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        // NTP time not available - fallback to stored device setting
        if (!lastSetupValue.isEmpty()) {
            testSerial.write(lastSetupValue.c_str());
        }
        return;
    }

    char timeStr[6];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    String currentTime = String(timeStr);

    // Clear old schedule data to avoid stale entries
    for(int i = 0; i < 10; i++) {
        schedules[i].startTime = "";
        schedules[i].endTime = "";
        schedules[i].value = "";
        schedules[i].outValue = "";
    }
    scheduleCount = 0;
    int startIndex = 0;
    int endIndex = 0;
    bool foundMatchingSchedule = false;
    
    // Loop through the string to find each schedule
    while (startIndex < scheduleData.length() && scheduleCount < 10 && !foundMatchingSchedule) {
        // Find the next schedule using # as separator
        endIndex = scheduleData.indexOf('#', startIndex);
        if (endIndex == -1) endIndex = scheduleData.length();
        
        String schedule = scheduleData.substring(startIndex, endIndex);
        
        // Parse start time
        int startPos = schedule.indexOf("start=");
        int endPos = schedule.indexOf("&", startPos);
        if (startPos != -1) {
            schedules[scheduleCount].startTime = schedule.substring(startPos + 6, endPos);
            schedules[scheduleCount].startTime.trim();
        }

        // Parse end time
        startPos = schedule.indexOf("end=");
        endPos = schedule.indexOf("&", startPos);
        if (startPos != -1) {
            schedules[scheduleCount].endTime = schedule.substring(startPos + 4, endPos);
            schedules[scheduleCount].endTime.trim();
        }

        // Parse value
        startPos = schedule.indexOf("value=");
        if (startPos != -1) {
            schedules[scheduleCount].value = schedule.substring(startPos + 6);
            schedules[scheduleCount].value.trim();
        }
        
        
        // Check if current time is within this schedule
        if(isTimeInRange(currentTime, schedules[scheduleCount].startTime, schedules[scheduleCount].endTime)) {
            String valueSetup = convertSetupValue(schedules[scheduleCount].value);
            testSerial.write(valueSetup.c_str());
            foundMatchingSchedule = true;
            break;
        }
        
        scheduleCount++;
        startIndex = endIndex + 1;
    }
    
    // If no matching schedule found, use the stored setup value
    if(!foundMatchingSchedule && !lastSetupValue.isEmpty()) {
        testSerial.write(lastSetupValue.c_str());
    }
}

void readWifi() {
  String strText = readStringFromEEPROM(0);

  if (strText.length() == 0 || strText.isEmpty()) {
    return;
  }

  String key_split = KEY_SPLIT;
  int index_split = strText.indexOf(key_split);
  param_ssid = strText.substring(0, index_split);
  strText.replace(param_ssid + key_split, "");
  index_split = strText.indexOf(key_split);
  param_password = strText.substring(0, index_split);
  strText.replace(param_password + key_split, "");
  uid = strText;
  uid.trim();
  isStartConnect = true;
}

// Reset WiFi to clear DNS cache and stale sockets
void resetWiFiConnection() {
    DBG_PRINTLN("=== Resetting WiFi to clear DNS cache ===");
    mqttClient.disconnect();
    WiFi.disconnect(true);  // true = erase credentials from memory
    delay(1000);
    WiFi.begin(param_ssid.c_str(), param_password.c_str());
    WiFi.mode(WIFI_AP_STA);

    // Wait for WiFi to reconnect (max 10 seconds)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        DBG_PRINT(".");
        attempts++;
    }
    DBG_PRINTLN();

    if (WiFi.status() == WL_CONNECTED) {
        DBG_PRINTLN("WiFi reconnected after reset");
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    } else {
        DBG_PRINTLN("WiFi reconnect failed, will retry later");
    }
}

// MQTT connection function
bool connectToMqtt() {
    lastMqttReconnectAttempt = millis();

    String clientId = "esp32-" + WiFi.macAddress();

    if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
        isMqttConnected = true;
        connectMqtt = 1;
        mqttFailCount = 0;  // Reset fail counter on success

        // Subscribe to topics
        String currentUid = getUid();
        if (!currentUid.isEmpty()) {
            MQTT_TOPIC_SETUP = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/setup/value";
            MQTT_TOPIC_SCHEDULE = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/schedule/value";
            MQTT_TOPIC_DATA = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/data";
            MQTT_TOPIC_STATUS = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/status";
            MQTT_TOPIC_FIRMWARE = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/firmware/update";
            MQTT_TOPIC_OTA_STATUS = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/ota/status";
            MQTT_TOPIC_CMD_SETTINGS = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/cmd/settings";
            MQTT_TOPIC_CMD_SCHEDULE = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/cmd/schedule";

            mqttClient.subscribe(MQTT_TOPIC_SETUP.c_str());
            mqttClient.subscribe(MQTT_TOPIC_SCHEDULE.c_str());
            mqttClient.subscribe(MQTT_TOPIC_STATUS.c_str());
            mqttClient.subscribe(MQTT_TOPIC_DATA.c_str());
            mqttClient.subscribe(MQTT_TOPIC_FIRMWARE.c_str());
            mqttClient.subscribe(MQTT_TOPIC_CMD_SETTINGS.c_str(), 1);  // QoS 1
            mqttClient.subscribe(MQTT_TOPIC_CMD_SCHEDULE.c_str(), 1);  // QoS 1

            DBG_PRINT("Subscribed cmd/settings: [");
            DBG_PRINT(MQTT_TOPIC_CMD_SETTINGS);
            DBG_PRINTLN("]");
            DBG_PRINT("Subscribed cmd/schedule: [");
            DBG_PRINT(MQTT_TOPIC_CMD_SCHEDULE);
            DBG_PRINTLN("]");
        }
        return true;
    } else {
        isMqttConnected = false;
        connectMqtt = 0;
        mqttFailCount++;  // Increment fail counter
        DBG_PRINT("MQTT connection failed, fail count: ");
        DBG_PRINTLN(mqttFailCount);
        return false;
    }
}


String connectSuccess() {
  return "success";
}

String connectError() {
  return "error";
}

wl_status_t statusWifi() {
  return WiFi.status();
}

int mqttStatus() {
  return connectMqtt;
}


// Function to clear all EEPROM
void clearEEPROM() {
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
}

// Function to write a string to EEPROM
void writeStringToEEPROM(int addr, const String &str) {
  int len = str.length();
  for (int i = 0; i < len; i++) {
    EEPROM.write(addr + i, str[i]);
  }
  EEPROM.write(addr + len, '\0'); // Null-terminate the string
  EEPROM.commit();
}

void writeInfo(String ssid, String password, String userid) {
  String content = ssid + KEY_SPLIT;
  content = content + password;
  content = content + KEY_SPLIT;
  content = content + userid;
  writeStringToEEPROM(0, content);
}

void saveWifiBroadcastSSID(const String& ssid) {
  preferences.begin("wifi_config", false);
  preferences.putString("broadcast_ssid", ssid);
  preferences.end();
}

String loadWifiBroadcastSSID() {
  preferences.begin("wifi_config", true);
  String ssid = preferences.getString("broadcast_ssid", WIFI_BROADCAST_SSID);
  preferences.end();
  return ssid;
}

void publishOTAStatus(const String& status, const String& message = "", int progress = -1) {
  // Print OTA status to Serial for monitoring
  DBG_PRINT("OTA Status: ");
  DBG_PRINT(status);
  if (!message.isEmpty()) {
    DBG_PRINT(" - ");
    DBG_PRINT(message);
  }
  if (progress >= 0) {
    DBG_PRINT(" (");
    DBG_PRINT(progress);
    DBG_PRINT("%)");
  }
  DBG_PRINTLN();
  
  if (!mqttClient.connected() || MQTT_TOPIC_OTA_STATUS.isEmpty()) {
    return;
  }
  
  String jsonStatus = "{\"status\":\"" + status + "\"";
  
  if (!message.isEmpty()) {
    jsonStatus += ",\"message\":\"" + message + "\"";
  }
  
  if (progress >= 0) {
    jsonStatus += ",\"progress\":" + String(progress);
  }
  // Add timestamp
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)) {
    char timeStr[30];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    jsonStatus += ",\"timestamp\":\"" + String(timeStr) + "\"";
  }
  
  jsonStatus += "}";
  
  bool published = mqttClient.publish(MQTT_TOPIC_OTA_STATUS.c_str(), jsonStatus.c_str());
  DBG_PRINT("MQTT OTA status published: ");
  DBG_PRINTLN(published ? "Success" : "Failed");
}

bool updateFirmwareVersion(const String& firmwareVersion) {

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  String currentUid = getUid();
  if (currentUid.isEmpty()) {
    return false;
  }
  String url = "https://giabao-inverter.com/api/inverter-device/data/" + currentUid + "/" + wifiBroadcastSSID + "/firmware";
  
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  // Create JSON payload with correct format
  String jsonPayload = "{\"firmwareVersion\":\"" + firmwareVersion + "\"}";
  
  int httpResponseCode = http.sendRequest("PATCH", jsonPayload);
  String response = "";
  bool success = false;
  
  if (httpResponseCode > 0) {
    response = http.getString();
    
    if (httpResponseCode == 200 || httpResponseCode == 204) {
      success = true;
    } else {
    }
  } else {
  }
  
  http.end();
  return success;
}

String getFirmwareS3URL(const String& version = "latest") {
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }
  
  String currentUid = getUid();
  if (currentUid.isEmpty()) {
    return "";
  }
  
  String url = "https://giabao-inverter.com/api/firmware";
  url += "?deviceId=" + wifiBroadcastSSID;
  
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.GET();
  String response = "";
  String s3URL = "";
  
  if (httpResponseCode > 0) {
    response = http.getString();
    
    if (httpResponseCode == 200) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      if (!error) {
        if (doc.containsKey("url")) {
          s3URL = doc["url"].as<String>();
        } else if (doc.containsKey("downloadUrl")) {
          s3URL = doc["downloadUrl"].as<String>();
        } else {
        }
      } else {
      }
    }
  } else {
  }
  
  http.end();
  return s3URL;
}

bool performFOTAUpdate(const String& firmwareURL) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  HTTPUpdate httpUpdate;
  httpUpdate.onStart([]() {
    publishOTAStatus("installing", "Firmware download completed, installing...", 0);
  });
  httpUpdate.onEnd([]() {
    publishOTAStatus("installing", "Firmware installation completed", 100);
  });
  httpUpdate.onProgress([](int cur, int total) {
    int progress = (cur * 100) / total;
    
    // Publish progress less frequently to reduce network load
    static int lastProgress = -1;
    if (progress >= lastProgress + 25) {
      publishOTAStatus("downloading", "Downloading firmware", progress);
      lastProgress = progress;
    }
  });
  httpUpdate.onError([](int err) {
    publishOTAStatus("failed", "Firmware update error: " + String(err));
  });
  
  WiFiClientSecure client;
  client.setInsecure();    // 30 second timeout
  
  t_httpUpdate_return ret = httpUpdate.update(client, firmwareURL);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      publishOTAStatus("failed", "Update failed - connection lost");
      return false;
      
    case HTTP_UPDATE_NO_UPDATES:
      publishOTAStatus("failed", "No updates available");
      return false;
      
    case HTTP_UPDATE_OK:
      publishOTAStatus("success", "Update completed, rebooting...");
      delay(1000); // Brief delay before restart
      return true;
      
    default:
      publishOTAStatus("failed", "Unknown update error");
      return false;
  }
}


void handleFirmwareUpdate() {
  DBG_PRINTLN("=== FIRMWARE UPDATE STARTED ===");
  
  // Publish starting status
  publishOTAStatus("starting", "Requesting firmware URL from server");
  
  String s3URL = getFirmwareS3URL();
  DBG_PRINT("S3 URL received: ");
  DBG_PRINTLN(s3URL.isEmpty() ? "EMPTY" : s3URL);
  
  if (s3URL.isEmpty()) {
    publishOTAStatus("failed", "Failed to get firmware URL from server");
    trackLogError("FOTA_URL_FAILED", "Failed to get firmware URL from server");
    return;
  }
  
  // Publish downloading status
  publishOTAStatus("downloading", "Starting firmware download");
  
  bool updateResult = performFOTAUpdate(s3URL);
  DBG_PRINT("Update result: ");
  DBG_PRINTLN(updateResult ? "SUCCESS" : "FAILED");
  
  if (updateResult) {
    DBG_PRINTLN("=== FIRMWARE UPDATE SUCCESSFUL ===");
    publishOTAStatus("success", "Firmware update completed successfully, rebooting...");
  } else {
    DBG_PRINTLN("=== FIRMWARE UPDATE FAILED ===");
    publishOTAStatus("failed", "Firmware download or installation failed");
    trackLogError("FOTA_FAILED", "Firmware download or installation failed");
  }
}


// Function to get device settings from API
String getDeviceSettings(const String& deviceUid, const String& deviceSSID) {
  if (WiFi.status() != WL_CONNECTED) {
    // Load from storage if WiFi not connected
    if (lastSetupValue.isEmpty()) {
      lastSetupValue = loadSettingFromStorage();
    }
    // Send stored value to STM32 even when offline
    if (!lastSetupValue.isEmpty()) {
      testSerial.write(lastSetupValue.c_str());
    }
    return "";
  }

  // HTTPClient http;
  String url = "https://giabao-inverter.com/api/inverter-setting/data/" + deviceUid + "/" + deviceSSID + "?source=hardware";


  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.GET();
  String response = "";

  if (httpResponseCode > 0) {
    response = http.getString();

    if (httpResponseCode == 200) {
      // Parse the JSON response to extract settings
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);

      if (!error) {
        // Extract settings values if they exist
        String value = doc["value"].as<String>();
        DBG_PRINT("Setting value: ");
        DBG_PRINTLN(value);
        String newSetupValue = convertSetupValue(value);

        // Save to storage only if value has changed
        if (!newSetupValue.isEmpty() && newSetupValue != lastSetupValue) {
          saveSettingToStorage(newSetupValue);
          lastSetupValue = newSetupValue;
        }
      } else {
      }
    }
  } else {
    // Server request failed - load from storage as fallback
    if (lastSetupValue.isEmpty()) {
      lastSetupValue = loadSettingFromStorage();
    }
    // Send stored value to STM32 when server fails
    if (!lastSetupValue.isEmpty()) {
      testSerial.write(lastSetupValue.c_str());
    }
  }

  http.end();
  return response;
}

// Function to get schedule settings from API
String getScheduleSettings(const String& deviceUid, const String& deviceSSID) {
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }
  
  // HTTPClient http;
  String url = "https://giabao-inverter.com/api/inverter-schedule/data/" + deviceUid + "/" + deviceSSID + "?source=hardware";
  
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.GET();
  String response = "";
  
  if (httpResponseCode > 0) {
    response = http.getString();
    
    if (httpResponseCode == 200) {
      // Parse the JSON response to extract schedule data
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      
      if (!error) {
        // Extract schedule data if it exists
        String value = doc["schedule"].as<String>();
        parseScheduleData(value);        
      } else {
        testSerial.write(lastSetupValue.c_str());
      }
    }
  } else {
  }
  
  http.end();
  return response;
}

// Function to register new device via API
bool registerDevice(const String& deviceId, const String& deviceName, const String& userId) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  // HTTPClient http;
  String url = "https://giabao-inverter.com/api/inverter-device/data";
  
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  // Create JSON payload
  String jsonPayload = "{";
  jsonPayload += "\"deviceId\":\"" + deviceId + "\",";
  jsonPayload += "\"deviceName\":\"" + deviceName + "\",";
  jsonPayload += "\"userId\":\"" + userId + "\"";
  jsonPayload += "}";
  
  
  int httpResponseCode = http.POST(jsonPayload);
  String response = "";
  bool success = false;
  
  if (httpResponseCode > 0) {
    response = http.getString();
    
    if (httpResponseCode == 200 || httpResponseCode == 201) {
      success = true;
    } else {
    }
  } else {
  }
  
  http.end();
  return success;
}

void setup() {
  Serial.begin(9600);
  DBG_PRINTLN("\n\n=== ESP32 Inverter Controller ===");
  DBG_PRINT("Firmware Version: ");
  DBG_PRINTLN(currentFirmwareVersion);

  testSerial.begin(9600, EspSoftwareSerial::SWSERIAL_8N1, RX, TX);
  // testSerial.setTimeout(100);
  DBG_PRINT("STM32 Serial: RX=");
  DBG_PRINT(RX);
  DBG_PRINT(", TX=");
  DBG_PRINTLN(TX);

  WiFi.mode(WIFI_AP_STA);
  EEPROM.begin(512);
  
  // Load WIFI_BROADCAST_SSID from Preferences
  // Once saved in Preferences, it will NEVER change, even on code uploads
  wifiBroadcastSSID = loadWifiBroadcastSSID();
  
  // Only save hardcoded value if Preferences is completely empty (first install only)
  preferences.begin("wifi_config", true);
  if (!preferences.isKey("broadcast_ssid")) {
    preferences.end();
    wifiBroadcastSSID = WIFI_BROADCAST_SSID;
    saveWifiBroadcastSSID(wifiBroadcastSSID);
  } else {
    preferences.end();
  }
  
  // Final safety check
  if (wifiBroadcastSSID.isEmpty()) {
    wifiBroadcastSSID = WIFI_BROADCAST_SSID;
  }
  
  WiFi.softAP(wifiBroadcastSSID.c_str(), "", 6);
  DBG_PRINT("Access Point SSID: ");
  DBG_PRINTLN(wifiBroadcastSSID);

  pinMode(STM_READY, INPUT);
  pinMode(STM_START, OUTPUT);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  // clearEEPROM();
  readWifi();

  // Load last setting from storage on startup
  lastSetupValue = loadSettingFromStorage();
  if (!lastSetupValue.isEmpty()) {
    DBG_PRINT("Loaded setting from storage: ");
    DBG_PRINTLN(lastSetupValue);
  }
  http.setReuse(true);
  http.setTimeout(3000);  // 3 second timeout to prevent long blocking

  // Initialize MQTT
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setKeepAlive(60);  // 60 second keep-alive for stability
  DBG_PRINT("MQTT Server: ");
  DBG_PRINT(MQTT_SERVER);
  DBG_PRINT(":");
  DBG_PRINTLN(MQTT_PORT);

  mqttClient.setCallback([](char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (int i = 0; i < length; i++) {
      message += (char)payload[i];
    }

    String topicStr = String(topic);
    DBG_PRINTLN("=== MQTT MESSAGE RECEIVED ===");
    DBG_PRINT("Topic: ");
    DBG_PRINTLN(topicStr);
    DBG_PRINT("Message: ");
    DBG_PRINTLN(message);
    // Handle firmware update topic
    if (topicStr == MQTT_TOPIC_FIRMWARE) {
      handleFirmwareUpdate();
    }
    // Command topics: payload is just "{}" - react to the topic name only.
    // Set a debounced flag; the actual HTTP fetch runs from loop().
    else if (topicStr.endsWith("/cmd/settings")) {
      cmdSettingsPending = true;
      cmdSettingsAt = millis();
    }
    else if (topicStr.endsWith("/cmd/schedule")) {
      cmdSchedulePending = true;
      cmdScheduleAt = millis();
    }
  });

  // Serve the WiFi setup page. UID comes from the URL query, e.g. "/connect?uid=abc".
  server.on("/connect", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Return the most recent WiFi scan result (JSON) to the setup page.
  // The actual scan is driven from loop(); requesting this endpoint just marks
  // the setup page as active so loop() keeps scanning.
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    lastScanRequest = millis();
    // "?refresh=1" asks loop() to run one fresh scan; plain GET just polls the
    // cached result (so the client can read results without re-scanning).
    if (request->hasParam("refresh")) {
      scanRequested = true;
    }
    request->send(200, "application/json", scannedNetworksJson);
  });

  server.on("/wifi", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam(PARAM_INPUT_1) && request->hasParam(PARAM_INPUT_2)) {
      param_ssid = request->getParam(PARAM_INPUT_1)->value();
      param_password = request->getParam(PARAM_INPUT_2)->value();
      uid = request->getParam(PARAM_INPUT_3)->value();
      isStartConnect = true;
      lastScanRequest = 0;  // stop scanning so it can't disrupt the connection
      WiFi.disconnect();
      request->send_P(200, "text/plain", connectSuccess().c_str());

    } else {
      request->send_P(200, "text/plain", connectError().c_str());
    }
  });

  server.on("/wifi-status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
    }
    
    request->send_P(200, "text/plain", String(statusWifi()).c_str());
  });

  server.on("/mqtt-status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(mqttStatus()).c_str());
  });

  // Read-only status for the setup page to poll after Connect is pressed.
  // wifi: wl_status_t (3 = connected). mqtt: -1 pending, 0 failed, 1 success.
  server.on("/connect-status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"wifi\":" + String((int)WiFi.status()) +
                  ",\"mqtt\":" + String(connectMqtt) +
                  ",\"result\":" + String(wifiConnectResult) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/change-mode-wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    isStartChangeModeWifi = true;
    request->send_P(200, "text/plain", connectSuccess().c_str());
  });

  // start server
  server.begin();
}

void loop() {

  unsigned long currentMillis = millis();

  if (!isUpdateVersion) {
    if (WiFi.status() == WL_CONNECTED && isMqttConnected) {
      bool res =  updateFirmwareVersion(currentFirmwareVersion);
      publishOTAStatus("installing", "Firmware installation completed", 100);
      if (res) {
        isUpdateVersion = true;
      }
    } 
  }


  // MQTT loop - always call to detect disconnection
  mqttClient.loop();

  // Sync isMqttConnected flag with actual connection state
  if (isMqttConnected && !mqttClient.connected()) {
    isMqttConnected = false;
    connectMqtt = 0;
  }

  if (isStartChangeModeWifi) {
    WiFi.softAPdisconnect(true);  // Disconnect all clients and stop AP
    delay(1000);
    WiFi.mode(WIFI_STA);          // Station mode only (no AP)
    delay(3000);
    WiFi.softAP(wifiBroadcastSSID.c_str(), "12345678", 6);  // Restart AP
    WiFi.mode(WIFI_AP_STA);       // Back to AP+STA mode
    isStartChangeModeWifi = false;
  }

  // listenButtonEvent();
  // put your main code here, to run repeatedly:
  if (isStartConnect && param_ssid.isEmpty() == false) {
    scanRequested = false;  // don't let a pending scan disrupt the connection
    WiFi.disconnect();
    // Disable auto-reconnect for the attempt: on a wrong password it would
    // otherwise rescan/retry forever, knocking the softAP off-channel and making
    // this page unreachable. One clean attempt keeps the AP stable.
    WiFi.setAutoReconnect(false);
    WiFi.begin(param_ssid.c_str(), param_password.c_str());
    WiFi.mode(WIFI_AP_STA);
    isStartConnect = false;
    isStartMqtt = true;
    wifiConnectResult = -1;               // attempt in progress
    connectAttemptStart = currentMillis;  // start the timeout clock
  }

  // Resolve the setup-page connect attempt (success, or failure/timeout).
  if (connectAttemptStart != 0 && wifiConnectResult == -1) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      wifiConnectResult = 1;
      connectAttemptStart = 0;
      WiFi.setAutoReconnect(true);   // restore normal reconnect behaviour
    } else if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL ||
               currentMillis - connectAttemptStart >= connectTimeout) {
      wifiConnectResult = 0;         // wrong password / not found / out of range
      connectAttemptStart = 0;
      isStartMqtt = false;           // don't keep waiting for MQTT on failure
      WiFi.disconnect();             // keep the STA idle so the softAP is stable
    }
  }

  // Check WiFi connection and connect MQTT when WiFi is connected
  if (WiFi.status() == WL_CONNECTED && isStartMqtt) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    writeInfo(param_ssid, param_password, uid);

    if (connectToMqtt()) {
      isMqttConnected = true;
      isStartMqtt = false;
    } else {
    }
    isStartRegisterDevice = true;
  }
  // register device via API and MQTT when connected
  if (isStartRegisterDevice && WiFi.status() == WL_CONNECTED) {
    String currentUid = getUid();
    if (!currentUid.isEmpty()) {
      // Register device via API first
      bool apiRegistered = registerDevice(wifiBroadcastSSID, wifiBroadcastSSID, currentUid);

      if (apiRegistered) {
      } else {
        trackLogError("DEVICE_REGISTER_FAILED", "Failed to register device via API");
      }
    }
    isStartRegisterDevice = false;
  }

  if (currentMillis - previousMillisSetting >= invertalSetting) {
    previousMillisSetting = currentMillis;
    getDeviceSettings(getUid(), wifiBroadcastSSID);
  }

  if (currentMillis - previousMillisSchedule >= intervalSchedule) {
    previousMillisSchedule = currentMillis;
    getScheduleSettings(getUid(), wifiBroadcastSSID);
  }

  // Process debounced MQTT command syncs (server asked us to re-fetch on change).
  if (cmdSettingsPending && (currentMillis - cmdSettingsAt >= cmdDebounce)) {
    cmdSettingsPending = false;
    DBG_PRINTLN("[CMD] settings sync requested -> fetching setting");
    getDeviceSettings(getUid(), wifiBroadcastSSID);
  }
  if (cmdSchedulePending && (currentMillis - cmdScheduleAt >= cmdDebounce)) {
    cmdSchedulePending = false;
    DBG_PRINTLN("[CMD] schedule sync requested -> fetching schedule");
    getScheduleSettings(getUid(), wifiBroadcastSSID);
  }

  // WiFi scan for the setup page. Runs ON DEMAND (page open / refresh) rather
  // than periodically: a scan briefly suspends the softAP (single radio), so
  // scanning repeatedly would make the portal unresponsive to the client.
  // The 3s throttle guards against rapid refresh spam.
  if (scanRequested && (previousMillisScan == 0 || currentMillis - previousMillisScan >= 3000)) {
    scanRequested = false;
    previousMillisScan = currentMillis;

    // If the STA is mid-association the scan fails with -2 (WIFI_SCAN_FAILED).
    // Disconnect (without erasing credentials) to free the radio.
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect(false);
      delay(50);
    }

    WiFi.scanDelete();
    // Bounded per-channel dwell (120ms) keeps the scan — and the softAP
    // outage — short (~1.5s) so the client isn't starved for long.
    int n = WiFi.scanNetworks(false /*async*/, true /*show_hidden*/, false /*passive*/, 120 /*ms per channel*/);
    DBG_PRINT("WiFi scan found ");
    DBG_PRINT(n);
    DBG_PRINTLN(" networks");

    String json = "[";
    for (int i = 0; i < n; i++) {
      if (i) json += ",";
      json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
      json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
      json += "\"secure\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1) + "}";
    }
    json += "]";
    scannedNetworksJson = json;
    WiFi.scanDelete();
  }

  // check MQTT connection and publish data
  if (currentMillis - previousMillis >= interval && isMqttConnected) {

    previousMillis = currentMillis;

    // Debug: Check if data is available from STM32
    DBG_PRINTLN("=== Reading STM32 Data ===");
    int available = testSerial.available();
    DBG_PRINT("Bytes available: ");
    DBG_PRINTLN(available);

    String res = testSerial.readString();
    // String res = dataExample; // For testing, replace with testSerial.readString();
    res.trim();

    DBG_PRINT("Raw data: [");
    DBG_PRINT(res);
    DBG_PRINTLN("]");
    DBG_PRINT("Length: ");
    DBG_PRINTLN(res.length());

    if (res.isEmpty()) {
      DBG_PRINTLN("No data from STM32");
      DBG_PRINTLN("=======================");
    } else {
      int indexOf = res.indexOf("*");
      DBG_PRINT("Index of '*': ");
      DBG_PRINTLN(indexOf);

          res = res.substring(0, indexOf);
          DBG_PRINT("Data after trim: ");
          DBG_PRINTLN(res);

          String currentUid = getUid();
          
          long double pAfter = 0;
          long double p2After = 0;
          
          // Parse the string to find 9th and 10th values
          int startIndex = 0;
          int tokenCount = 0;
          
          while (startIndex < res.length()) {
              int endIndex = res.indexOf('#', startIndex);
              if (endIndex == -1) endIndex = res.length();
              
              tokenCount++;
              String token = res.substring(startIndex, endIndex);
              
              if (tokenCount == 9) {
                  pAfter = fabs(token.toDouble());
              } else if (tokenCount == 10) {
                  p2After = fabs(token.toDouble());
                  break; // Found both values, exit loop
              }
              
              startIndex = endIndex + 1;
          }

          long double increament1 = pAfter;
          long double incremeant2 = p2After;
              
          // Use higher precision calculation to avoid floating point errors    
          totalA = totalA + increament1 / 1000000.0;
          totalA2 = totalA2 + incremeant2 / 1000000.0;
        
          // Publish data via MQTT
          String jsonString = "{\"value\":\"" + res + "\",\"totalA2Capacity\":\"" + String((double)totalA2) + "\",\"totalACapacity\":\"" + String((double)totalA)  + "\"}";

          DBG_PRINTLN("=== Publishing to MQTT ===");
          DBG_PRINT("Topic: ");
          DBG_PRINTLN(MQTT_TOPIC_DATA);
          DBG_PRINT("Payload: ");
          DBG_PRINTLN(jsonString);

          if (!MQTT_TOPIC_DATA.isEmpty()) {
            String signedJsonString = createSignedMessage(jsonString);
            bool dataPublished = mqttClient.publish(MQTT_TOPIC_DATA.c_str(), signedJsonString.c_str());
            DBG_PRINT("Publish result: ");
            DBG_PRINTLN(dataPublished ? "SUCCESS" : "FAILED");
            if (dataPublished) {
              DBG_PRINTLN("Data sent to MQTT successfully");
            } else {
              DBG_PRINTLN("Failed to send data to MQTT");
            }
          } else {
            DBG_PRINTLN("MQTT_TOPIC_DATA is empty!");
          }
          DBG_PRINTLN("=======================");
    }

  }

  // // WiFi connection monitoring and reconnection
  if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillisWifi >= intervalWifi)) {
    WiFi.reconnect();
    previousMillisWifi = currentMillis;

    // Reset MQTT connection status when WiFi disconnects
    isMqttConnected = false;
    connectMqtt = 0;
  }

  if ((currentMillis - previousMillisMqtt >= intervalMqtt)) {
    previousMillisMqtt = currentMillis;
    if (isMqttConnected && mqttClient.connected()) {
      struct tm timeinfo;
      if(!getLocalTime(&timeinfo)){
        return;
      }

      // Publish status update via MQTT
      char timeStringBuff[50];
      strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

      if (!MQTT_TOPIC_STATUS.isEmpty()) {
        String statusMsg = "{\"updatedAt\":\"" + String(timeStringBuff) + "\",\"status\":\"online\"}";
        String signedStatusMsg = createSignedMessage(statusMsg);
        if (mqttClient.connected()) {
          bool statusPublished = mqttClient.publish(MQTT_TOPIC_STATUS.c_str(), signedStatusMsg.c_str());
          if (statusPublished) {
          } else {
          }
        } else {
        }
      }
    } else {
      // Don't update previousMillisMqtt here so we keep trying
    }

  }

  // Ensure MQTT connection is maintained - check WiFi first, then connect MQTT with interval
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected() && (currentMillis - previousMillisMqttReconnect >= intervalMqttReconnect)) {
      DBG_PRINTLN("=== MQTT Reconnection Required ===");
      DBG_PRINT("MQTT State: ");
      DBG_PRINTLN(mqttClient.state());
      DBG_PRINT("isMqttConnected: ");
      DBG_PRINTLN(isMqttConnected ? "true" : "false");
      DBG_PRINT("Fail count: ");
      DBG_PRINTLN(mqttFailCount);

      // Reset WiFi after too many MQTT failures to clear DNS cache
      if (mqttFailCount >= MQTT_MAX_FAIL_BEFORE_RESET) {
        DBG_PRINTLN("Too many MQTT failures, resetting WiFi...");
        trackLogError("MQTT_FAILED", "MQTT connection failed " + String(mqttFailCount) + " times, resetting WiFi");
        resetWiFiConnection();
        mqttFailCount = 0;  // Reset counter after WiFi reset
      }

      isMqttConnected = false; // Reset flag before attempting reconnection
      if (connectToMqtt()) {
        DBG_PRINTLN("MQTT Reconnection successful");
      } else {
        DBG_PRINTLN("MQTT Reconnection failed, will retry in 10s");
      }
      previousMillisMqttReconnect = currentMillis;
      DBG_PRINTLN("===================================");
    }
  } else {
    if (isMqttConnected) {
      DBG_PRINTLN("WiFi down, cannot maintain MQTT connection");
      isMqttConnected = false;
    }
  }

}

