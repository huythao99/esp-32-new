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

#define WIFI_BROADCAST_SSID "GTIControl415"

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
String currentFirmwareVersion = "1.0.1"; // Variable to store current firmware version

int connectMqtt = -1; // -1: pending; 0: failed; 1: success
bool isMqttConnected = false;
unsigned long lastMqttReconnectAttempt = 0;

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
const long invertalSetting = 3000;
const long intervalSchedule = 3000;

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

ScheduleItem schedules[3]; // Array to hold up to 3 schedule items
int scheduleCount = 0;

String readStringFromEEPROM(int addr) {
  char data[100]; // Adjust size as needed
  int len = 0;
  unsigned char k;
  k = EEPROM.read(addr);
  while (k != '\0' && len < sizeof(data) - 1 && k <= 127 && k >= 32) {
    data[len] = k;
    len++;
    k = EEPROM.read(addr + len);

  }
  data[len] = '\0';
  return String(data);
}

String getUid() {
  if (uid.isEmpty() == false) {
    return uid;
  }
  String strText = readStringFromEEPROM(0); 

  if (strText.length() == 0 || strText.isEmpty()) {
    return "";
  }
  String key_split = '\0' + KEY_SPLIT;
  int index_split = strText.indexOf(key_split);
  param_ssid = strText.substring(0, index_split);
  strText.replace(param_ssid + key_split, "");
  index_split = strText.indexOf(key_split);
  param_password = strText.substring(0, index_split);
  strText.replace(param_password + key_split, "");
  uid = strText;
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


String createSignedMessage(const String& payload) {
    // unsigned long timestamp = millis();
    // String message = payload + "|" + String(timestamp);
    
    // String signedMessage = "{\"payload\":\"" + payload + "\",\"timestamp\":" + String(timestamp) + ",\"signature\":\"" + signature + "\"}";
    return payload;
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
        return;
    }
    
    char timeStr[6];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    String currentTime = String(timeStr);
    
    scheduleCount = 0;
    int startIndex = 0;
    int endIndex = 0;
    bool foundMatchingSchedule = false;
    
    // Loop through the string to find each schedule
    while (startIndex < scheduleData.length() && scheduleCount < 3 && !foundMatchingSchedule) {
        // Find the next schedule using # as separator
        endIndex = scheduleData.indexOf('#', startIndex);
        if (endIndex == -1) endIndex = scheduleData.length();
        
        String schedule = scheduleData.substring(startIndex, endIndex);
        
        // Parse start time
        int startPos = schedule.indexOf("start=");
        int endPos = schedule.indexOf("&", startPos);
        if (startPos != -1) {
            schedules[scheduleCount].startTime = schedule.substring(startPos + 6, endPos);
        }
        
        // Parse end time
        startPos = schedule.indexOf("end=");
        endPos = schedule.indexOf("&", startPos);
        if (startPos != -1) {
            schedules[scheduleCount].endTime = schedule.substring(startPos + 4, endPos);
        }
        
        // Parse value
        startPos = schedule.indexOf("value=");
        if (startPos != -1) {
            schedules[scheduleCount].value = schedule.substring(startPos + 6);
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
  String key_split = '\0' + KEY_SPLIT;
  int index_split = strText.indexOf(key_split);
  param_ssid = strText.substring(0, index_split);
    // param_ssid = "10S05-Bedroom";
  strText.replace(param_ssid + key_split, "");
  index_split = strText.indexOf(key_split);
  param_password = strText.substring(0, index_split);
    // param_password = "123456789";
  strText.replace(param_password + key_split, "");
  uid = strText;
  // uid = "Y8Lg4tiveSWmnkrzRqo98ngpTwH3";
  isStartConnect = true;
}

// MQTT connection function
bool connectToMqtt() {
    
    lastMqttReconnectAttempt = millis();
    
    
    String clientId = "esp32-" + WiFi.macAddress();
    
    
    if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
        isMqttConnected = true;
        connectMqtt = 1;
        
        // Subscribe to topics
        String currentUid = getUid();
        if (!currentUid.isEmpty()) {
            MQTT_TOPIC_SETUP = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/setup/value";
            MQTT_TOPIC_SCHEDULE = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/schedule/value";
            MQTT_TOPIC_DATA = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/data";
            MQTT_TOPIC_STATUS = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/status";
            MQTT_TOPIC_FIRMWARE = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/firmware/update";
            MQTT_TOPIC_OTA_STATUS = "inverter/" + currentUid + "/" + wifiBroadcastSSID + "/ota/status";
            
            mqttClient.subscribe(MQTT_TOPIC_SETUP.c_str());
            mqttClient.subscribe(MQTT_TOPIC_SCHEDULE.c_str());
            mqttClient.subscribe(MQTT_TOPIC_STATUS.c_str());
            mqttClient.subscribe(MQTT_TOPIC_DATA.c_str());
            mqttClient.subscribe(MQTT_TOPIC_FIRMWARE.c_str());

        }
        return true;
    } else {
        isMqttConnected = false;
        connectMqtt = 0;
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
  // EEPROM.writeString(addr, str);
  EEPROM.commit();
}

void writeInfo(String ssid, String password, String userid) {
  String content = ssid + KEY_SPLIT;
  content = content + password;
  content = content + KEY_SPLIT;
  content = content + userid;
  writeStringToEEPROM(0, content);

}

void writeAStorage(double totalA, double totalA2) {
  String content = String(totalA) + KEY_SPLIT;
  content = content + String(totalA2);
  writeStringToEEPROM(100, content);
}

void getAStorage() {
  String strText = readStringFromEEPROM(100); 

  if (strText.length() == 0 || strText.isEmpty()) {
    return;
  }
  String key_split = '\0' + KEY_SPLIT;
  int index_split = strText.indexOf(key_split);
  String totalAByString = strText.substring(0, index_split);
  String totalA2ByString = strText.substring(index_split + key_split.length());
  totalA = totalAByString.toDouble();
  totalA2 = totalA2ByString.toDouble();
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
  Serial.print("OTA Status: ");
  Serial.print(status);
  if (!message.isEmpty()) {
    Serial.print(" - ");
    Serial.print(message);
  }
  if (progress >= 0) {
    Serial.print(" (");
    Serial.print(progress);
    Serial.print("%)");
  }
  Serial.println();
  
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
  Serial.print("MQTT OTA status published: ");
  Serial.println(published ? "Success" : "Failed");
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
  Serial.println("=== FIRMWARE UPDATE STARTED ===");
  
  // Publish starting status
  publishOTAStatus("starting", "Requesting firmware URL from server");
  
  String s3URL = getFirmwareS3URL();
  Serial.print("S3 URL received: ");
  Serial.println(s3URL.isEmpty() ? "EMPTY" : s3URL);
  
  if (s3URL.isEmpty()) {
    publishOTAStatus("failed", "Failed to get firmware URL from server");
    return;
  }
  
  // Publish downloading status
  publishOTAStatus("downloading", "Starting firmware download");
  
  bool updateResult = performFOTAUpdate(s3URL);
  Serial.print("Update result: ");
  Serial.println(updateResult ? "SUCCESS" : "FAILED");
  
  if (updateResult) {
    Serial.println("=== FIRMWARE UPDATE SUCCESSFUL ===");
    publishOTAStatus("success", "Firmware update completed successfully, rebooting...");
  } else {
    Serial.println("=== FIRMWARE UPDATE FAILED ===");
    publishOTAStatus("failed", "Firmware download or installation failed");
  }
}


// Function to get device settings from API
String getDeviceSettings(const String& deviceUid, const String& deviceSSID) {
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }
  
  // HTTPClient http;
  String url = "https://giabao-inverter.com/api/inverter-setting/data/" + deviceUid + "/" + deviceSSID;
  
  
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
        lastSetupValue = convertSetupValue(value);
      } else {
      }
    }
  } else {
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
  String url = "https://giabao-inverter.com/api/inverter-schedule/data/" + deviceUid + "/" + deviceSSID;
  
  
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
  testSerial.begin(9600, EspSoftwareSerial::SWSERIAL_8N1, RX, TX);
  // testSerial.setTimeout(100);
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
  pinMode(STM_READY, INPUT);
  pinMode(STM_START, OUTPUT);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  // clearEEPROM();
  readWifi();
  getAStorage();
  http.setReuse(true);
  
  // Initialize MQTTn
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback([](char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (int i = 0; i < length; i++) {
      message += (char)payload[i];
    }
    
    String topicStr = String(topic);
    
    // Handle firmware update topic
    if (topicStr == MQTT_TOPIC_FIRMWARE) {
      handleFirmwareUpdate();
    }
  });

  server.on("/wifi", HTTP_POST, [](AsyncWebServerRequest *request){    
    if (request->hasParam(PARAM_INPUT_1) && request->hasParam(PARAM_INPUT_2)) {
      param_ssid = request->getParam(PARAM_INPUT_1)->value();
      param_password = request->getParam(PARAM_INPUT_2)->value();
      uid = request->getParam(PARAM_INPUT_3)->value();
      isStartConnect = true;
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


  // MQTT loop
  if (mqttClient.connected()) {
    mqttClient.loop();
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
    WiFi.disconnect();
    WiFi.begin(param_ssid, param_password);
    WiFi.mode(WIFI_AP_STA);
    isStartConnect = false;
    isStartMqtt = true;
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
      }
    }
    isStartRegisterDevice = false;
  }

  if (currentMillis - previousMillisStorage >= 1000 * 60 * 60) {
    writeAStorage(totalA, totalA2);
  }

  if (currentMillis - previousMillisSetting >= invertalSetting) {
    previousMillisSetting = currentMillis;
    getDeviceSettings(getUid(), wifiBroadcastSSID);
  }

  if (currentMillis - previousMillisSchedule >= intervalSchedule) {
    previousMillisSchedule = currentMillis;
    getScheduleSettings(getUid(), wifiBroadcastSSID);
  }

  // check MQTT connection and publish data
  if (currentMillis - previousMillis >= interval && isMqttConnected) {

    previousMillis = currentMillis;    
    String res = testSerial.readString();
    // String res = dataExample; // For testing, replace with testSerial.readString();
    res.trim();
    if (res.isEmpty()) {
    } else {
      int indexOf = res.indexOf("*");

          res = res.substring(0, indexOf);

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
          if (!MQTT_TOPIC_DATA.isEmpty()) {
            String signedJsonString = createSignedMessage(jsonString);
            bool dataPublished = mqttClient.publish(MQTT_TOPIC_DATA.c_str(), signedJsonString.c_str());
            if (dataPublished) {
            }
          }
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
      isMqttConnected = false; // Reset flag before attempting reconnection
      if (connectToMqtt()) {
      } else {
      }
      previousMillisMqttReconnect = currentMillis;
    }
  }

}

