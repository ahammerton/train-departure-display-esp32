// Train Departure Display - ESP32-S3 port
// Live departures from the Rail Data Marketplace on an SSD1322 256x64 OLED,
// with a settings web page, a setup hotspot and over-the-air updates.
// Screens, positions and scrolling follow the original Pi app (src/main.py).
//
// Wiring (display pin -> ESP32-S3 GPIO), display set to 4-wire SPI:
//   1  GND        -> GND
//   2  V+ (3.3V)  -> 3V3
//   4  D0/SCLK    -> GPIO 12
//   5  D1/SDIN    -> GPIO 11
//   14 DC         -> GPIO 9
//   15 RST        -> GPIO 8
//   16 CS         -> GPIO 10
// A second display (later) shares SCLK/SDIN/DC/RST and uses CS = GPIO 7.
//
// First-boot settings are in config.h, Wi-Fi/API key/passwords in secrets.h
// (copy secrets.example.h). After that, use the web page at
// http://train-display.local/

#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <time.h>

#include "app.h"
#include "config.h"
#include "dmfont.h"
#include "dotmatrix_fonts.h"
#include "net.h"
#include "rail.h"
#include "secrets.h"
#include "settings.h"

#define OLED_SCK   12
#define OLED_MOSI  11
#define OLED_CS    10
#define OLED_DC    9
#define OLED_RST   8
#define OLED2_CS   7   // second display: shares SCLK, SDIN, DC and RST

U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI oled(U8G2_R2, OLED_CS, OLED_DC, OLED_RST);
// The shared reset line is pulsed by the first display's begin()
U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI oled2(U8G2_R2, OLED2_CS, OLED_DC, U8X8_PIN_NONE);
U8G2 *screens[2] = {&oled, &oled2};
bool oled2Ready = false;

U8G2 *g = &oled;  // the display being drawn; all drawing functions use it

const int SCREEN_W = 256;

// The original redraws the scrolling row at most every 20 ms, once per frame
// at a 70 fps target, so in practice it moves one pixel every two frames.
const uint32_t SCROLL_STEP_MS = 28;

enum DataState { DATA_NONE, DATA_BOARD, DATA_WELCOME };

// Scroll state for the "Calling at" row, as in main.py's ScrollState
struct ScrollState {
  int pixelsLeft = 1;
  int pixelsUp = 0;
  bool hasElevated = false;
  int pauseCount = 0;
};
ScrollState scrolls[2];

// ---- Shared state (see app.h) -----------------------------------------------------

Board *boards[2];
volatile int frontBoard = 0;
SemaphoreHandle_t boardLock;
FetchStatus fetchStatus;
volatile float displayFps = 0;
volatile DataState dataState = DATA_NONE;
char welcomeName[64] = "";
TaskHandle_t fetchTaskHandle = nullptr;

// Settings the display loop uses, refreshed when settingsVersion changes
Settings view;
uint32_t viewVersion = 0;

uint32_t debugUntilMs = 0;      // "Show debug screen" from the web page
uint16_t bootDebugSeconds = 0;   // debug setting at start-up (2+ = seconds)

void showDebugFor(uint32_t ms) { debugUntilMs = millis() + ms; }

void requestRefresh() {
  if (fetchTaskHandle) xTaskNotifyGive(fetchTaskHandle);
}

// ---- Time and hours --------------------------------------------------------

bool timeValid() { return time(nullptr) > 1700000000; }

// Port of open.py isRun: is now between start:00 and end:00 (may cross midnight)?
bool isRun(int startHour, int endHour) {
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  int now = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
  int b = startHour * 3600, e = endHour * 3600;
  if (b < e) return now >= b && now <= e;
  return now >= b || now <= e;
}

// "8-22" style hours: false if not set, invalid, or the time isn't known yet
bool inHours(const char *hours) {
  int s, e;
  if (!*hours || !validHours(hours) || sscanf(hours, "%d-%d", &s, &e) != 2) return false;
  return timeValid() && isRun(s, e);
}

// ---- Fetch task (core 0) ----------------------------------------------------------

void setFetchError(const char *msg) {
  strlcpy(fetchStatus.lastError, msg, sizeof fetchStatus.lastError);
}

void fetchTask(void *) {
  bool haveGood = false;
  for (;;) {
    if (!wifiUp()) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }
    Settings s = getSettings();

    if (*s.operatingHours && timeValid() && !inHours(s.operatingHours)) {
      xSemaphoreTake(boardLock, portMAX_DELAY);
      strlcpy(welcomeName, s.outOfHoursName, sizeof welcomeName);
      dataState = DATA_WELCOME;
      xSemaphoreGive(boardLock);
      haveGood = false;
      setFetchError("outside operating hours");
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));
      continue;
    }

    int back = 1 - frontBoard;
    String err;
    uint32_t t0 = millis();
    bool ok = fetchBoard(s, *boards[back], err);
    fetchStatus.lastTryMs = millis();
    fetchStatus.lastOk = ok;

    if (ok) {
      xSemaphoreTake(boardLock, portMAX_DELAY);
      frontBoard = back;
      dataState = DATA_BOARD;
      xSemaphoreGive(boardLock);
      haveGood = true;
      fetchStatus.everOk = true;
      fetchStatus.lastOkMs = millis();
      fetchStatus.tookMs = millis() - t0;
      fetchStatus.replyBytes = lastReplyBytes;
      setFetchError("");
      const Board &b = *boards[back];
      Serial.printf("Fetched %s: %d departures, %u bytes in %lu ms (free heap %u)\n", b.stationName, b.count,
                    (unsigned)lastReplyBytes, (unsigned long)fetchStatus.tookMs, (unsigned)ESP.getFreeHeap());
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(s.refreshTime * 1000UL));
    } else {
      Serial.printf("Fetch failed: %s\n", err.c_str());
      setFetchError(err.c_str());
      // Keep the last good board for a while; after that, show the welcome screen
      if (!haveGood || millis() - fetchStatus.lastOkMs > STALE_DATA_SECONDS * 1000UL) {
        xSemaphoreTake(boardLock, portMAX_DELAY);
        strlcpy(welcomeName, s.outOfHoursName, sizeof welcomeName);
        dataState = DATA_WELCOME;
        xSemaphoreGive(boardLock);
      }
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
    }
  }
}

// ---- Drawing helpers ----------------------------------------------------------

// Column widths, measured as the original does with font.getlength()
int statusW, platformW, callingW;

// Text clipped to a box: the original draws each piece into a fixed-size
// snapshot, so anything beyond the box is cut off.
void drawClipped(const DMFont &font, int boxX, int boxY, int boxW, int boxH,
                 int textX, int textY, const char *text) {
  g->setClipWindow(boxX, boxY, boxX + boxW, boxY + boxH);
  dmDrawText(*g, font, boxX + textX, boxY + textY, text);
  g->setMaxClipWindow();
}

void drawCentred(const DMFont &font, int y, const char *text) {
  drawClipped(font, 0, y, SCREEN_W, 10, (SCREEN_W - dmTextWidth(font, text)) / 2, 0, text);
}

// Port of renderTime: large HH:MM with smaller seconds, centred, in a 14 px row
void drawClock(int y) {
  if (!timeValid()) return;
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  char hm[8], sec[8];
  snprintf(hm, sizeof hm, "%02d:%02d", lt.tm_hour, lt.tm_min);
  snprintf(sec, sizeof sec, ":%02d", lt.tm_sec);
  int w1 = dmBoxWidth(dmBoldLarge, hm);
  int w2 = dmBoxWidth(dmBoldTall, ":00");
  int x = (SCREEN_W - w1 - w2) / 2;
  drawClipped(dmBoldLarge, 0, y, SCREEN_W, 14, x, 0, hm);
  drawClipped(dmBoldTall, 0, y, SCREEN_W, 14, x + w1, 5, sec);
}

// Messages from the web server and OTA updates (called inside loop()),
// shown on every active display
int activeScreens() { return view.dualScreen && oled2Ready ? 2 : 1; }

void drawMessage(const char *line1, const char *line2) {
  for (int i = 0; i < activeScreens(); i++) {
    g = screens[i];
    g->setPowerSave(0);
    g->clearBuffer();
    drawCentred(dmBold, 18, line1);
    drawCentred(dmRegular, 32, line2);
    g->sendBuffer();
  }
}

void drawUpdateProgress(int pct) {
  static int lastPct = -1;
  if (pct == lastPct) return;
  lastPct = pct;
  char buf[8];
  snprintf(buf, sizeof buf, "%d%%", pct);
  for (int i = 0; i < activeScreens(); i++) {
    g = screens[i];
    g->setPowerSave(0);
    g->clearBuffer();
    drawCentred(dmBold, 12, "Updating firmware");
    g->drawFrame(28, 28, 200, 9);
    g->drawBox(30, 30, 196 * pct / 100, 5);
    drawCentred(dmRegular, 42, buf);
    g->sendBuffer();
  }
}

// ---- Screens ----------------------------------------------------------------

// drawStartup: name, version, NRE attribution
void drawStartup() {
  drawCentred(dmBold, 0, "UK Train Departure Display");
  drawCentred(dmRegular, 12, "v" FW_VERSION);
  drawCentred(dmBold, 24, "Powered by");
  drawCentred(dmBold, 36, "National Rail Enquiries");
}

// Setup hotspot instructions
void drawSetup() {
  char line[64];
  drawCentred(dmBold, 0, "Wi-Fi setup");
  snprintf(line, sizeof line, "Join %s", apSsid);
  drawCentred(dmRegular, 14, line);
  snprintf(line, sizeof line, "Password %s", SETUP_AP_PASS);
  drawCentred(dmRegular, 26, line);
  snprintf(line, sizeof line, "then open http://%s", WiFi.softAPIP().toString().c_str());
  drawCentred(dmRegular, 38, line);
}

// drawBlankSignage: shown out of hours, with no departures, or after errors
void drawWelcome(const char *station) {
  drawCentred(dmBold, 0, "Welcome to");
  drawCentred(dmBold, 12, station);
  dmDrawText(*g, dmBold, 0, 24, ".  .  .");
  drawClock(50);
}

const char *serviceStatus(const Departure &d) {
  static char buf[24];
  if (!strcmp(d.etd, "On time") || !strcmp(d.etd, "Cancelled") || !strcmp(d.etd, "Delayed"))
    return d.etd;
  if (!strcmp(d.std, d.etd)) return "On time";
  snprintf(buf, sizeof buf, "Exp %s", d.etd);
  return buf;
}

void drawDepartureRow(const Departure &d, int y, const DMFont &font, const char *ordinal, int destW) {
  char line[128];
  if (view.showNumbers)
    snprintf(line, sizeof line, "%s  %s  %s", ordinal, d.std, d.destination);
  else
    snprintf(line, sizeof line, "%s  %s", d.std, d.destination);
  drawClipped(font, 0, y, destW, 10, 0, 0, line);

  // Status: right-aligned in its column
  const char *status = serviceStatus(d);
  drawClipped(dmRegular, SCREEN_W - statusW, y, statusW, 10,
              statusW - dmBoxWidth(dmRegular, status), 0, status);

  // Platform: left-aligned in its column
  if (d.platform[0]) {
    char plat[16];
    if (!strcasecmp(d.platform, "bus")) strcpy(plat, "BUS");
    else snprintf(plat, sizeof plat, "Plat %s", d.platform);
    drawClipped(dmRegular, SCREEN_W - statusW - platformW, y, platformW, 10, 0, 0, plat);
  }
}

// Port of renderStations: slide the text up into view, pause, scroll it left
// until it has gone, then repeat.
void drawCallingAt(const char *stations, ScrollState &s, bool step) {
  int boxX = callingW, boxW = SCREEN_W - callingW;
  int txtW = dmBoxWidth(dmRegular, stations);
  int txtH = dmBoxHeight(dmRegular, stations);

  if (s.hasElevated) {
    drawClipped(dmRegular, boxX, 12, boxW, 10, s.pixelsLeft - 1, 0, stations);
    if (!step) return;
    if (-s.pixelsLeft > txtW && s.pauseCount < 8) {
      s.pauseCount++;
      s.pixelsLeft = 0;
      s.hasElevated = false;
    } else {
      s.pauseCount = 0;
      s.pixelsLeft--;
    }
  } else {
    drawClipped(dmRegular, boxX, 12, boxW, 10, 0, txtH - s.pixelsUp, stations);
    if (!step) return;
    if (s.pixelsUp == txtH) {
      s.pauseCount++;
      if (s.pauseCount > 20) {
        s.hasElevated = true;
        s.pixelsUp = 0;
      }
    } else {
      s.pixelsUp++;
    }
  }
}

// Port of platform_filter
bool platformMatches(const Departure &d, const char *platform) {
  if (view.numericPlatformsOnly) {
    if (!d.platform[0]) return false;
    for (const char *p = d.platform; *p; p++)
      if (!isdigit((unsigned char)*p)) return false;
    return true;
  }
  if (!platform[0]) return true;
  return !strcmp(d.platform, platform);
}

// drawSignage: the departure board. Returns false if no departures match.
bool drawBoard(const Board &b, const char *platform, ScrollState &s, bool step) {
  const Departure *deps[3];
  int n = 0;
  for (int i = 0; i < b.count && n < 3; i++)
    if (platformMatches(b.deps[i], platform)) deps[n++] = &b.deps[i];
  if (n == 0) return false;

  const DMFont &firstFont = view.firstBold ? dmBold : dmRegular;
  drawDepartureRow(*deps[0], 0, firstFont, "1st", SCREEN_W - statusW - platformW - 5);
  dmDrawText(*g, dmRegular, 0, 12, "Calling at: ");
  drawCallingAt(deps[0]->callingAt, s, step);
  if (n > 1) drawDepartureRow(*deps[1], 24, dmRegular, "2nd", SCREEN_W - statusW - platformW);
  if (n > 2) drawDepartureRow(*deps[2], 36, dmRegular, "3rd", SCREEN_W - statusW - platformW);
  drawClock(50);
  return true;
}

// Port of drawDebugScreen: settings, version and addresses in a box
void drawDebug(int screen, bool showTime) {
  const char *platform = screen == 0 ? view.platform : view.platform2;
  char l1[96], l2[48], l3[32], l4[32];
  int n = snprintf(l1, sizeof l1, "= %s", view.station);
  if (view.destinations[0]) n += snprintf(l1 + n, sizeof l1 - n, "->%s", view.destinations);
  if (platform[0]) n += snprintf(l1 + n, sizeof l1 - n, " (Plat%s) ", platform);
  else n += snprintf(l1 + n, sizeof l1 - n, " (PlatAll) ");
  n += snprintf(l1 + n, sizeof l1 - n, "%us ", (unsigned)view.refreshTime);
  if (view.operatingHours[0]) snprintf(l1 + n, sizeof l1 - n, "%sh", view.operatingHours);
  snprintf(l2, sizeof l2, "= T_D_D:  %s", FW_VERSION);
  snprintf(l3, sizeof l3, "= %s", WiFi.macAddress().c_str());
  snprintf(l4, sizeof l4, "IP=%s", (portalActive ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str());

  g->drawFrame(1, 1, 254, 45);
  dmDrawText(*g, dmRegular, 5, 5, "Display");
  dmDrawText(*g, dmRegular, 45, 5, l1);
  dmDrawText(*g, dmRegular, 5, 18, "Script");
  dmDrawText(*g, dmRegular, 45, 18, l2);
  dmDrawText(*g, dmRegular, 5, 31, "Address");
  dmDrawText(*g, dmRegular, 45, 31, l3);
  dmDrawText(*g, dmRegular, 140, 31, l4);
  if (showTime) drawClock(50);
}

// ---- Setup and main loop ---------------------------------------------------------

void applyDisplaySettings() {
  if (view.dualScreen && !oled2Ready) {
    oled2.begin();
    oled2Ready = true;
  }
  for (int i = 0; i < (oled2Ready ? 2 : 1); i++) {
    screens[i]->setDisplayRotation(view.rotation == 0 ? U8G2_R0 : U8G2_R2);
    screens[i]->setContrast(view.contrast);
  }
  // A second display that has been switched off is blanked
  if (oled2Ready && !view.dualScreen) oled2.setPowerSave(1);
}

void setup() {
  Serial.begin(115200);
  loadSettings();
  view = getSettings();
  viewVersion = settingsVersion;
  bootDebugSeconds = view.debug >= 2 ? view.debug : 0;

  // Keep the second display deselected, even when it isn't in use
  pinMode(OLED2_CS, OUTPUT);
  digitalWrite(OLED2_CS, HIGH);

  SPI.begin(OLED_SCK, -1, OLED_MOSI, OLED_CS);
  oled.setBusClock(10000000);  // SSD1322 SPI maximum
  oled2.setBusClock(10000000);
  oled.begin();                // also resets the second display (shared RST)
  applyDisplaySettings();

  statusW = dmTextWidth(dmRegular, "Exp 00:00");
  platformW = dmTextWidth(dmRegular, "Plat 888");
  callingW = dmTextWidth(dmRegular, "Calling at: ");

  for (int i = 0; i < activeScreens(); i++) {
    g = screens[i];
    g->clearBuffer();
    drawStartup();
    g->sendBuffer();
  }

  Serial.println("Train Departure Display v" FW_VERSION);
  Serial.printf("PSRAM: %u bytes\n", (unsigned)ESP.getPsramSize());

  for (int i = 0; i < 2; i++) {
    boards[i] = (Board *)heap_caps_calloc(1, sizeof(Board), MALLOC_CAP_SPIRAM);
    if (!boards[i]) boards[i] = (Board *)calloc(1, sizeof(Board));
  }
  boardLock = xSemaphoreCreateMutex();

  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);
  startNetwork();

  // Networking and parsing run on core 0; the display loop has core 1
  xTaskCreatePinnedToCore(fetchTask, "fetch", 16384, nullptr, 1, &fetchTaskHandle, 0);
}

// What one display shows this frame
void drawScreen(int screen, DataState state, bool step) {
  uint32_t now = millis();
  bool debugNow = view.debug == 1 || (int32_t)(debugUntilMs - now) > 0;
  bool bootDebug = bootDebugSeconds && now < bootDebugSeconds * 1000UL;

  if (portalActive) {
    drawSetup();
  } else if (debugNow) {
    drawDebug(screen, true);
  } else if (bootDebug) {
    drawDebug(screen, false);  // replaces the start-up screen, as in the original
  } else if (now < 5000 || state == DATA_NONE) {
    drawStartup();  // the attribution shows while the first data loads
  } else if (state == DATA_BOARD) {
    const Board &b = *boards[frontBoard];
    const char *platform = screen == 0 ? view.platform : view.platform2;
    if (!drawBoard(b, platform, scrolls[screen], step)) drawWelcome(b.stationName);
  } else {
    drawWelcome(welcomeName);
  }
}

void loop() {
  static uint32_t lastScrollStep = 0;
  serviceNetwork();
  uint32_t now = millis();

  if (viewVersion != settingsVersion) {
    viewVersion = settingsVersion;
    view = getSettings();
    applyDisplaySettings();
  }

  // Screens off during the blank hours (but not while setting up)
  bool blank = !portalActive && inHours(view.screenBlankHours);
  bool step = now - lastScrollStep >= SCROLL_STEP_MS;
  if (step) lastScrollStep = now;

  for (int i = 0; i < activeScreens(); i++) {
    g = screens[i];
    if (blank) {
      g->setPowerSave(1);
      continue;
    }
    g->setPowerSave(0);
    g->clearBuffer();
    xSemaphoreTake(boardLock, portMAX_DELAY);
    drawScreen(i, dataState, step);
    xSemaphoreGive(boardLock);
    g->sendBuffer();
  }
  if (blank || portalActive) delay(20);

  static uint32_t frames = 0, fpsMark = 0;
  frames++;
  if (now - fpsMark >= 5000) {
    displayFps = frames * 1000.0f / (now - fpsMark);
    frames = 0;
    fpsMark = now;
  }
}
