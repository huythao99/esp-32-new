#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Pure decision logic + the single STM32 writer.
//
// Locking contract:
//   parseScheduleData(), currentScheduleValue() and buildShareValue() touch the
//   shared schedules[]/lastSetupValue and are NOT self-locking — the caller must
//   hold stateMutex. applyCurrentValue() takes stateMutex itself.
// ---------------------------------------------------------------------------

// "99001620" -> "*9900@1620#". Returns "" for non-8-digit input.
String convertSetupValue(const String& input);

bool isTimeInRange(const String& currentTime, const String& startTime, const String& endTime);

// Parse "#"-separated schedule string into schedules[]. Caller holds stateMutex.
void parseScheduleData(const String& scheduleData);

// Converted value of the matching window, or "". Caller holds stateMutex.
String currentScheduleValue();

// Build "*pset@value#" from lastSetupValue's cap + a clamped share. Caller holds stateMutex.
String buildShareValue(int shareWatts);

// THE single writer to the STM32. Picks share > schedule > setting and writes
// on change plus a keepalive. Takes stateMutex internally.
void applyCurrentValue();

// JSON helpers.
String jsonEscape(const String& in);
String createSignedMessage(const String& payload);

// SNTP sync callback (sets isNtpSynced).
void onNtpSync(struct timeval* tv);
