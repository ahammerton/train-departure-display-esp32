// Settings kept in flash (Preferences). First-boot defaults come from
// config.h and secrets.h; the web page changes them after that.
#pragma once
#include <Arduino.h>

struct Settings {
  // Network and logins
  char wifiSsid[33];
  char wifiPass[64];
  char apiKey[81];
  char adminPass[65];

  // What to show (names follow the original app's environment variables)
  char station[4];            // departureStation (CRS)
  char destinations[24];      // destinationStation: up to 5 CRS codes, comma-separated
  char platform[4];           // screen1Platform ("" = all)
  bool dualScreen;            // dualScreen: second display on CS GPIO 7
  char platform2[4];          // screen2Platform ("" = all)
  bool numericPlatformsOnly;
  uint16_t refreshTime;       // seconds
  int16_t timeOffset;         // minutes
  char operatingHours[6];     // "8-22" or ""
  char outOfHoursName[41];
  char screenBlankHours[6];   // "1-6" or ""
  bool individualTimes;       // individualStationDepartureTime
  bool showNumbers;           // showDepartureNumbers
  bool firstBold;             // firstDepartureBold

  // Display
  uint8_t rotation;           // screenRotation: 0 or 2 (180 degrees)
  uint8_t contrast;           // 0-255
  uint16_t debug;             // 0 off, 1 always, 2+ = seconds at start-up
};

extern volatile uint32_t settingsVersion;  // bumped on every change

void loadSettings();
Settings getSettings();                     // a consistent copy
// Validate and store. Returns "" on success, or an error message.
String applySettings(const Settings &s);
void resetSettings();                       // back to config.h/secrets.h defaults

bool validHours(const char *s);
