#pragma once

#include <Arduino.h>

// EEPROM helpers (WiFi credentials + UID blob at address 0).
String readStringFromEEPROM(int addr);
void   writeStringToEEPROM(int addr, const String& str);
void   clearEEPROM();
void   writeInfo(String ssid, String password, String userid);

// Parse the EEPROM blob into param_ssid / param_password / uid.
void   readWifi();

// Cached UID accessor (lazily parses EEPROM on first call).
String getUid();

// Base setting value persistence (NVS).
void   saveSettingToStorage(const String& value);
String loadSettingFromStorage();

// Broadcast SSID persistence (NVS) — set once, survives firmware uploads.
void   saveWifiBroadcastSSID(const String& ssid);
String loadWifiBroadcastSSID();
