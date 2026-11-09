// Departure data from the Rail Data Marketplace "Live Departure Board" API.
// The text building is a port of src/trains.py (ProcessDepartures).
#pragma once
#include <Arduino.h>

#include "settings.h"

const int MAX_DEPARTURES = 20;

struct Departure {
  char std[6];             // aimed departure time "hh:mm"
  char etd[16];            // "On time", "Cancelled", "Delayed" or "hh:mm"
  char platform[8];        // "" if not known
  char destination[96];
  char callingAt[1024];    // the scrolling "Calling at" text
};

struct Board {
  Departure deps[MAX_DEPARTURES];
  int count;
  char stationName[64];
};

// Size of the last API reply, for the serial log
extern size_t lastReplyBytes;

// Fetch and build the board for the station and destination filters in
// settings. Returns false on a network/API error, with a message in err.
bool fetchBoard(const Settings &settings, Board &out, String &err);
