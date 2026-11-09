// Wi-Fi, setup hotspot, web server, over-the-air updates and rollback
#pragma once
#include <Arduino.h>

extern bool portalActive;
extern char apSsid[32];

void startNetwork();      // in setup()
void serviceNetwork();    // every loop()
bool wifiUp();
