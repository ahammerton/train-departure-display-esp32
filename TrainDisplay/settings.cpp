#include "settings.h"

#include <Preferences.h>

#include "config.h"
#include "secrets.h"

static Settings current;
static SemaphoreHandle_t lock = nullptr;
volatile uint32_t settingsVersion = 0;
static Preferences prefs;

static void defaults(Settings &s) {
  memset(&s, 0, sizeof s);
  strlcpy(s.wifiSsid, WIFI_SSID, sizeof s.wifiSsid);
  strlcpy(s.wifiPass, WIFI_PASS, sizeof s.wifiPass);
  strlcpy(s.apiKey, RAILDATA_API_KEY, sizeof s.apiKey);
  strlcpy(s.adminPass, ADMIN_PASS, sizeof s.adminPass);
  strlcpy(s.station, DEPARTURE_STATION, sizeof s.station);
  strlcpy(s.destinations, DESTINATION_STATIONS, sizeof s.destinations);
  strlcpy(s.platform, SCREEN1_PLATFORM, sizeof s.platform);
  s.dualScreen = DUAL_SCREEN;
  strlcpy(s.platform2, SCREEN2_PLATFORM, sizeof s.platform2);
  s.numericPlatformsOnly = NUMERIC_PLATFORMS_ONLY;
  s.refreshTime = REFRESH_TIME;
  s.timeOffset = TIME_OFFSET;
  strlcpy(s.operatingHours, OPERATING_HOURS, sizeof s.operatingHours);
  strlcpy(s.outOfHoursName, OUT_OF_HOURS_NAME, sizeof s.outOfHoursName);
  strlcpy(s.screenBlankHours, SCREEN_BLANK_HOURS, sizeof s.screenBlankHours);
  s.individualTimes = INDIVIDUAL_STATION_DEPARTURE_TIME;
  s.showNumbers = SHOW_DEPARTURE_NUMBERS;
  s.firstBold = FIRST_DEPARTURE_BOLD;
  s.rotation = 2;
  s.contrast = 255;
  s.debug = DEBUG_SCREEN;
}

static void getStr(const char *key, char *dst, size_t size) {
  if (prefs.isKey(key)) prefs.getString(key, dst, size);
}

void loadSettings() {
  if (!lock) lock = xSemaphoreCreateMutex();
  Settings s;
  defaults(s);
  prefs.begin("tdd", true);
  getStr("ssid", s.wifiSsid, sizeof s.wifiSsid);
  getStr("pass", s.wifiPass, sizeof s.wifiPass);
  getStr("apikey", s.apiKey, sizeof s.apiKey);
  getStr("admin", s.adminPass, sizeof s.adminPass);
  getStr("station", s.station, sizeof s.station);
  getStr("dests", s.destinations, sizeof s.destinations);
  getStr("plat", s.platform, sizeof s.platform);
  s.dualScreen = prefs.getBool("dual", s.dualScreen);
  getStr("plat2", s.platform2, sizeof s.platform2);
  s.numericPlatformsOnly = prefs.getBool("numplat", s.numericPlatformsOnly);
  s.refreshTime = prefs.getUShort("refresh", s.refreshTime);
  s.timeOffset = prefs.getShort("offset", s.timeOffset);
  getStr("ophours", s.operatingHours, sizeof s.operatingHours);
  getStr("oohname", s.outOfHoursName, sizeof s.outOfHoursName);
  getStr("blank", s.screenBlankHours, sizeof s.screenBlankHours);
  s.individualTimes = prefs.getBool("indtimes", s.individualTimes);
  s.showNumbers = prefs.getBool("numbers", s.showNumbers);
  s.firstBold = prefs.getBool("bold", s.firstBold);
  s.rotation = prefs.getUChar("rotation", s.rotation);
  s.contrast = prefs.getUChar("contrast", s.contrast);
  s.debug = prefs.getUShort("debug", s.debug);
  prefs.end();

  xSemaphoreTake(lock, portMAX_DELAY);
  current = s;
  settingsVersion = settingsVersion + 1;
  xSemaphoreGive(lock);
}

Settings getSettings() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Settings s = current;
  xSemaphoreGive(lock);
  return s;
}

// ---- Validation ---------------------------------------------------------------

static bool isCrs(const char *s, size_t len) {
  if (len != 3) return false;
  for (size_t i = 0; i < 3; i++)
    if (!isupper((unsigned char)s[i])) return false;
  return true;
}

bool validHours(const char *s) {
  if (!*s) return true;
  int a, b;
  char extra;
  return sscanf(s, "%d-%d%c", &a, &b, &extra) == 2 && a >= 0 && a <= 23 && b >= 0 && b <= 23;
}

// Same rule as the original's parsePlatformData: 1-2 digits, optionally
// followed by A-D, or a single letter A-D
static bool validPlatform(const char *p) {
  size_t n = strlen(p);
  if (n == 0) return true;
  if (n == 1) return isdigit((unsigned char)p[0]) || (p[0] >= 'A' && p[0] <= 'D');
  size_t digits = 0;
  while (digits < n && isdigit((unsigned char)p[digits])) digits++;
  if (digits == 0 || digits > 2) return false;
  if (digits == n) return true;
  return digits == n - 1 && p[n - 1] >= 'A' && p[n - 1] <= 'D';
}

static bool printable(const char *s) {
  for (; *s; s++)
    if (*s < 32 || *s > 126) return false;
  return true;
}

static String validate(const Settings &s) {
  if (!*s.wifiSsid) return "Wi-Fi network name is required";
  if (*s.wifiPass && (strlen(s.wifiPass) < 8 || strlen(s.wifiPass) > 63))
    return "Wi-Fi password must be 8-63 characters, or empty for an open network";
  if (!printable(s.apiKey)) return "API key has invalid characters";
  if (strlen(s.adminPass) < 4 || !printable(s.adminPass)) return "Admin password must be at least 4 characters";
  if (!isCrs(s.station, strlen(s.station))) return "Station must be a 3-letter CRS code, e.g. PAD";
  const char *p = s.destinations;
  int count = 0;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    if (!isCrs(p, len)) return "Destinations must be 3-letter CRS codes separated by commas";
    if (++count > 5) return "At most 5 destinations";
    p += len + (comma ? 1 : 0);
  }
  if (!validPlatform(s.platform)) return "Platform must be like 4, 12, 4A or B";
  if (!validPlatform(s.platform2)) return "Screen 2 platform must be like 4, 12, 4A or B";
  if (s.refreshTime < 30 || s.refreshTime > 3600) return "Refresh time must be 30-3600 seconds";
  if (s.timeOffset < -120 || s.timeOffset > 119) return "Time offset must be -120 to 119 minutes";
  if (!validHours(s.operatingHours)) return "Operating hours must look like 8-22";
  if (!validHours(s.screenBlankHours)) return "Screen blank hours must look like 1-6";
  if (!printable(s.outOfHoursName)) return "Out-of-hours name must be plain text";
  if (s.rotation != 0 && s.rotation != 2) return "Rotation must be 0 or 180 degrees";
  if (s.debug > 3600) return "Debug must be 0 (off), 1 (always) or 2-3600 seconds";
  return "";
}

String applySettings(const Settings &s) {
  String err = validate(s);
  if (err.length()) return err;

  prefs.begin("tdd", false);
  prefs.putString("ssid", s.wifiSsid);
  prefs.putString("pass", s.wifiPass);
  prefs.putString("apikey", s.apiKey);
  prefs.putString("admin", s.adminPass);
  prefs.putString("station", s.station);
  prefs.putString("dests", s.destinations);
  prefs.putString("plat", s.platform);
  prefs.putBool("dual", s.dualScreen);
  prefs.putString("plat2", s.platform2);
  prefs.putBool("numplat", s.numericPlatformsOnly);
  prefs.putUShort("refresh", s.refreshTime);
  prefs.putShort("offset", s.timeOffset);
  prefs.putString("ophours", s.operatingHours);
  prefs.putString("oohname", s.outOfHoursName);
  prefs.putString("blank", s.screenBlankHours);
  prefs.putBool("indtimes", s.individualTimes);
  prefs.putBool("numbers", s.showNumbers);
  prefs.putBool("bold", s.firstBold);
  prefs.putUChar("rotation", s.rotation);
  prefs.putUChar("contrast", s.contrast);
  prefs.putUShort("debug", s.debug);
  prefs.end();

  xSemaphoreTake(lock, portMAX_DELAY);
  current = s;
  settingsVersion = settingsVersion + 1;
  xSemaphoreGive(lock);
  return "";
}

void resetSettings() {
  prefs.begin("tdd", false);
  prefs.clear();
  prefs.end();
  loadSettings();
}
