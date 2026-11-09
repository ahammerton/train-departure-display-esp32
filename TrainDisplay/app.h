// State shared between the display loop, the fetch task and the web server
#pragma once
#include <Arduino.h>

#include "rail.h"

#define FW_VERSION "0.7.2"
#define HOSTNAME   "train-display"

// Boards: [frontBoard] is shown, the other is filled by the fetch task
extern Board *boards[2];
extern volatile int frontBoard;
extern SemaphoreHandle_t boardLock;

// Result of the most recent fetch, for the web page
struct FetchStatus {
  bool everOk;
  bool lastOk;
  uint32_t lastOkMs;       // millis() of the last good fetch
  uint32_t lastTryMs;
  uint32_t tookMs;
  uint32_t replyBytes;
  char lastError[96];
};
extern FetchStatus fetchStatus;
extern volatile float displayFps;  // frames per second, averaged over 5 s

void requestRefresh();     // wake the fetch task now
void showDebugFor(uint32_t ms);  // show the debug screen for a while

// Display messages drawn from the web server / OTA (both run in loop())
void drawMessage(const char *line1, const char *line2);
void drawUpdateProgress(int pct);
