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

#define WIFI_BROADCAST_SSID "GTIControl401"

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

// MQTT client
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);

// MQTT Topics
String MQTT_TOPIC_DATA;
String MQTT_TOPIC_SETUP;
String MQTT_TOPIC_SCHEDULE;
String MQTT_TOPIC_STATUS;


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

bool taskComplete = false;

int countLCD = 0;

String uid;

bool taskCompleted = false;
int connectMqtt = -1; // -1: pending; 0: failed; 1: success
bool isMqttConnected = false;
unsigned long lastMqttReconnectAttempt = 0;

bool isStartRegisterDevice = false;

bool isStartStreamCharge = false;

bool dataChanged = false;
String param_ssid;
String param_password;

bool isStartConnect = true;
bool isStartChangeMode = false;
bool isStartMqtt = false;

const long interval = 3000; 
const long intervalDigital = 1000;
const long intervalWifi = 60000;
const long intervalMqtt = 3000;
const long intervalMqttReconnect = 10000;
const long invertalSetting = 3000;
const long intervalSchedule = 3000;

String valueSetup = "";

bool isLow = false;

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
  Serial.print("Password: ");
  Serial.println(param_password);
  strText.replace(param_password + key_split, "");
  uid = strText;
  return uid;
}


String convertSetupValue(const String& input) {
    Serial.printf("length string %d", input.length());
    if (input.length() != 8) {
        Serial.println("Invalid setup value format");
        return "";
    }
    
    // Extract values from input (e.g., "99001620")
    int pset = input.substring(0, 4).toInt();  // First 4 digits as Pset
    int vset = input.substring(4, 8).toInt();  // Last 4 digits as Vset
    
    // Create formatted string
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "*%d@%d#", pset, vset);
    
    Serial.printf("Converted setup value: %s -> %s\n", input.c_str(), buffer);
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
        Serial.println("Failed to obtain time");
        return;
    }
    
    char timeStr[6];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    String currentTime = String(timeStr);
    Serial.println("Current time: " + currentTime);
    
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
        Serial.println("Parsing schedule: " + schedule);
        
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
        
        Serial.printf("Schedule %d: Start=%s, End=%s, Value=%s\n",
            scheduleCount + 1,
            schedules[scheduleCount].startTime.c_str(),
            schedules[scheduleCount].endTime.c_str(),
            schedules[scheduleCount].value.c_str()
        );
        
        // Check if current time is within this schedule
        if(isTimeInRange(currentTime, schedules[scheduleCount].startTime, schedules[scheduleCount].endTime)) {
            String valueSetup = convertSetupValue(schedules[scheduleCount].value);
            Serial.println("Found matching schedule. Using value: " + valueSetup);
            testSerial.write(valueSetup.c_str());
            foundMatchingSchedule = true;
            break;
        }
        
        scheduleCount++;
        startIndex = endIndex + 1;
    }
    
    // If no matching schedule found, use the stored setup value
    if(!foundMatchingSchedule && !lastSetupValue.isEmpty()) {
        Serial.println("No matching schedule found. Using stored setup value: " + lastSetupValue);
        testSerial.write(lastSetupValue.c_str());
    }
}

void readWifi() {
  Serial.printf("Read wifi");
  String strText = readStringFromEEPROM(0); 

  if (strText.length() == 0 || strText.isEmpty()) {
    return;
  }
  String key_split = '\0' + KEY_SPLIT;
  int index_split = strText.indexOf(key_split);
  param_ssid = strText.substring(0, index_split);
    // param_ssid = "10S05-Bedroom";
  Serial.print("SSID: ");
  Serial.println(param_ssid);
  strText.replace(param_ssid + key_split, "");
  index_split = strText.indexOf(key_split);
  param_password = strText.substring(0, index_split);
    // param_password = "123456789";
  Serial.print("Password: ");
  Serial.println(param_password);
  strText.replace(param_password + key_split, "");
  uid = strText;
  // uid = "Y8Lg4tiveSWmnkrzRqo98ngpTwH3";
  Serial.print("UID: ");
  Serial.println(uid);
  isStartConnect = true;
}

// MQTT connection function
bool connectToMqtt() {
    if (millis() - lastMqttReconnectAttempt < 5000) {
        return false;
    }
    
    lastMqttReconnectAttempt = millis();
    
    Serial.println("Attempting MQTT connection...");
    
    String clientId = "esp32-" + WiFi.macAddress();
    
    Serial.printf("Connecting to MQTT broker %s:%d with client ID: %s\n", MQTT_SERVER, MQTT_PORT, clientId.c_str());
    
    if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
        Serial.println("MQTT connected");
        isMqttConnected = true;
        connectMqtt = 1;
        
        // Subscribe to topics
        String currentUid = getUid();
        if (!currentUid.isEmpty()) {
            MQTT_TOPIC_SETUP = "inverter/" + currentUid + "/" + WIFI_BROADCAST_SSID + "/setup/value";
            MQTT_TOPIC_SCHEDULE = "inverter/" + currentUid + "/" + WIFI_BROADCAST_SSID + "/schedule/value";
            MQTT_TOPIC_DATA = "inverter/" + currentUid + "/" + WIFI_BROADCAST_SSID + "/data";
            MQTT_TOPIC_STATUS = "inverter/" + currentUid + "/" + WIFI_BROADCAST_SSID + "/status";
            
            mqttClient.subscribe(MQTT_TOPIC_SETUP.c_str());
            mqttClient.subscribe(MQTT_TOPIC_SCHEDULE.c_str());
            mqttClient.subscribe(MQTT_TOPIC_STATUS.c_str());
            mqttClient.subscribe(MQTT_TOPIC_DATA.c_str());

            Serial.println("MQTT subscribed to topics");
        }
        return true;
    } else {
        Serial.printf("MQTT connection failed, rc=%d\n", mqttClient.state());
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

// Function to get device settings from API
String getDeviceSettings(const String& deviceUid, const String& deviceSSID) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot fetch device settings");
    return "";
  }
  
  HTTPClient http;
  String url = "https://giabao-inverter.com/api/inverter-setting/data/" + deviceUid + "/" + deviceSSID;
  
  Serial.println("Fetching device settings from: " + url);
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.GET();
  String response = "";
  
  if (httpResponseCode > 0) {
    response = http.getString();
    Serial.printf("Device settings response code: %d\n", httpResponseCode);
    Serial.println("Device settings response: " + response);
    
    if (httpResponseCode == 200) {
      // Parse the JSON response to extract settings
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      
      if (!error) {
        // Extract settings values if they exist
        String value = doc["value"].as<String>();
        lastSetupValue = convertSetupValue(value);
      } else {
        Serial.println("Failed to parse device settings JSON");
      }
    }
  } else {
    Serial.printf("Failed to fetch device settings, error: %d\n", httpResponseCode);
  }
  
  http.end();
  return response;
}

// Function to get schedule settings from API
String getScheduleSettings(const String& deviceUid, const String& deviceSSID) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot fetch schedule settings");
    return "";
  }
  
  HTTPClient http;
  String url = "https://giabao-inverter.com/api/inverter-schedule/data/" + deviceUid + "/" + deviceSSID;
  
  Serial.println("Fetching schedule settings from: " + url);
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.GET();
  String response = "";
  
  if (httpResponseCode > 0) {
    response = http.getString();
    Serial.printf("Schedule settings response code: %d\n", httpResponseCode);
    Serial.println("Schedule settings response: " + response);
    
    if (httpResponseCode == 200) {
      // Parse the JSON response to extract schedule data
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      
      if (!error) {
        // Extract schedule data if it exists
        String value = doc["schedule"].as<String>();
        parseScheduleData(value);        
      } else {
        Serial.println("Failed to parse schedule settings JSON");
      }
    }
  } else {
    Serial.printf("Failed to fetch schedule settings, error: %d\n", httpResponseCode);
  }
  
  http.end();
  return response;
}

// Function to register new device via API
bool registerDevice(const String& deviceId, const String& deviceName, const String& userId) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot register device");
    return false;
  }
  
  HTTPClient http;
  String url = "https://giabao-inverter.com/api/inverter-device/data";
  
  Serial.println("Registering device at: " + url);
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  // Create JSON payload
  String jsonPayload = "{";
  jsonPayload += "\"deviceId\":\"" + deviceId + "\",";
  jsonPayload += "\"deviceName\":\"" + deviceName + "\",";
  jsonPayload += "\"userId\":\"" + userId + "\"";
  jsonPayload += "}";
  
  Serial.println("Device registration payload: " + jsonPayload);
  
  int httpResponseCode = http.POST(jsonPayload);
  String response = "";
  bool success = false;
  
  if (httpResponseCode > 0) {
    response = http.getString();
    Serial.printf("Device registration response code: %d\n", httpResponseCode);
    Serial.println("Device registration response: " + response);
    
    if (httpResponseCode == 200 || httpResponseCode == 201) {
      Serial.println("Device registered successfully");
      success = true;
    } else {
      Serial.println("Device registration failed");
    }
  } else {
    Serial.printf("Failed to register device, error: %d\n", httpResponseCode);
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
  WiFi.softAP(WIFI_BROADCAST_SSID, "12345678", 6);
  pinMode(STM_READY, INPUT);
  pinMode(STM_START, OUTPUT);
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  readWifi();
  getAStorage();
  
  // Initialize MQTT
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

  server.on("/wifi", HTTP_POST, [](AsyncWebServerRequest *request){    
    if (request->hasParam(PARAM_INPUT_1) && request->hasParam(PARAM_INPUT_2)) {
      param_ssid = request->getParam(PARAM_INPUT_1)->value();
      param_password = request->getParam(PARAM_INPUT_2)->value();
      uid = request->getParam(PARAM_INPUT_3)->value();
      Serial.println("params: " + param_ssid + " " + param_password + " " + uid);
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
    
    // Close WiFi AP to force client redirect
    WiFi.mode(WIFI_STA);
    delay(5000);
    WiFi.mode(WIFI_AP_STA);
  });

  server.on("/mqtt-status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(mqttStatus()).c_str());
  });

  // start server
  server.begin();

}

void loop() {

  unsigned long currentMillis = millis();

  // MQTT loop
  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  // listenButtonEvent();
  // put your main code here, to run repeatedly:
  if (isStartConnect && param_ssid.isEmpty() == false) {
    Serial.println("Start connect wifi: " + param_ssid + " " + param_password);
    WiFi.disconnect();
    WiFi.begin(param_ssid, param_password);
    WiFi.mode(WIFI_AP_STA);
    isStartConnect = false;
    isStartMqtt = true;
  }

  // Check WiFi connection and connect MQTT when WiFi is connected
  if (WiFi.status() == WL_CONNECTED && isStartMqtt) {
    Serial.println("WiFi connected successfully");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    writeInfo(param_ssid, param_password, uid);
    
    Serial.println("Attempting to connect to MQTT...");
    if (connectToMqtt()) {
      Serial.println("MQTT connection successful");
      isMqttConnected = true;
      isStartMqtt = false;
    } else {
      Serial.println("MQTT connection failed, will retry");
    }
    isStartRegisterDevice = true;
  }
  // register device via API and MQTT when connected
  if (isStartRegisterDevice && WiFi.status() == WL_CONNECTED) {
    String currentUid = getUid();
    if (!currentUid.isEmpty()) {
      // Register device via API first
      Serial.println("Registering device via API...");
      bool apiRegistered = registerDevice(WIFI_BROADCAST_SSID, WIFI_BROADCAST_SSID, currentUid);
      
      if (apiRegistered) {
        Serial.println("Device registered via API successfully");
      } else {
        Serial.println("Device registration via API failed");
      }
    }
    isStartRegisterDevice = false;
  }

  if (currentMillis - previousMillisStorage >= 1000 * 60 * 60) {
    writeAStorage(totalA, totalA2);
  }

  if (currentMillis - previousMillisSetting >= invertalSetting) {
    previousMillisSetting = currentMillis;
    getDeviceSettings(getUid(), WIFI_BROADCAST_SSID);
  }

  if (currentMillis - previousMillisSchedule >= intervalSchedule) {
    previousMillisSchedule = currentMillis;
    getScheduleSettings(getUid(), WIFI_BROADCAST_SSID);
  }

  // check MQTT connection and publish data
  if (currentMillis - previousMillis >= interval && isMqttConnected && !taskComplete) {

    previousMillis = currentMillis;    
    taskComplete = true;
    String res = testSerial.readString();
    // String res = dataExample; // For testing, replace with testSerial.readString();
    res.trim();
    Serial.println("Data: " + res);
    if (res.isEmpty()) {
      return;
    }
    int indexOf = res.indexOf("*");

    res = res.substring(0, indexOf);

    String currentUid = getUid();
    Serial.println("Current UID: " + currentUid);
    
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
      taskComplete = false;
      Serial.println("Data published: " + String(dataPublished));
      if (dataPublished) {
        Serial.println("Data published successfully");
      }
    }
  }

  // WiFi connection monitoring and reconnection
  if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillisWifi >= intervalWifi)) {
    Serial.printf("[%lu] WiFi disconnected (status: %d), attempting reconnection...\n", millis(), WiFi.status());
    WiFi.reconnect();
    previousMillisWifi = currentMillis;
    
    // Reset MQTT connection status when WiFi disconnects
    isMqttConnected = false;
    connectMqtt = 0;
  }

  if (isMqttConnected && (currentMillis - previousMillisMqtt >= intervalMqtt)) {
    previousMillisMqtt = currentMillis;
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      Serial.println("Failed to obtain time");
      return;
    }
    
    // Publish status update via MQTT
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    Serial.println(timeStringBuff);
    
    if (!MQTT_TOPIC_STATUS.isEmpty()) {
      String statusMsg = "{\"updatedAt\":\"" + String(timeStringBuff) + "\",\"status\":\"online\"}";
      String signedStatusMsg = createSignedMessage(statusMsg);
      if (mqttClient.connected()) {
        bool statusPublished = mqttClient.publish(MQTT_TOPIC_STATUS.c_str(), signedStatusMsg.c_str());
        if (statusPublished) {
          Serial.println("Status published successfully " + MQTT_TOPIC_STATUS);
        } else {
          Serial.println("Status published unsuccessfully");
        }
      } else {
        Serial.println("MQTT not connected, cannot publish status");
      }
    }
  }

  if (isStartChangeMode) {
    isStartChangeMode = false;
    WiFi.mode(WIFI_STA);
    delay(3000);
    WiFi.mode(WIFI_AP_STA);
  }
  // Ensure MQTT connection is maintained - check WiFi first, then connect MQTT with interval
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected() && (currentMillis - previousMillisMqttReconnect >= intervalMqttReconnect)) {
      Serial.println("WiFi connected but MQTT disconnected, attempting reconnection...");
      connectToMqtt();
      previousMillisMqttReconnect = currentMillis;
    }
  }

}

