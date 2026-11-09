// Display settings. The names follow the original app's environment
// variables (see docs/04-configuration.md). A settings web page will
// replace this file in a later step.
#pragma once

// Station to show departures from (CRS code)
#define DEPARTURE_STATION "PAD"

// Only show trains calling at these stations: comma-separated CRS codes,
// e.g. "WAT" or "WAT,CLJ". Empty = all departures.
#define DESTINATION_STATIONS ""

// Only show this platform ("" = all). numericPlatformsOnly drops lettered/no platforms.
#define SCREEN1_PLATFORM ""
#define NUMERIC_PLATFORMS_ONLY false

// Second display on CS GPIO 7 (sharing SCLK, SDIN, DC and RST), with its
// own platform filter
#define DUAL_SCREEN false
#define SCREEN2_PLATFORM ""

// Debug screen: 0 = off, 1 = always, 2+ = seconds at start-up
#define DEBUG_SCREEN 0

// Seconds between data refreshes
#define REFRESH_TIME 120

// Minutes to offset the board by (-120 to 119)
#define TIME_OFFSET 0

// Hours to fetch data, e.g. "8-22" ("" = always). Outside them the
// welcome screen shows OUT_OF_HOURS_NAME.
#define OPERATING_HOURS ""
#define OUT_OF_HOURS_NAME "London Paddington"

// Hours to switch the display off, e.g. "1-6" ("" = never)
#define SCREEN_BLANK_HOURS ""

#define INDIVIDUAL_STATION_DEPARTURE_TIME false
#define SHOW_DEPARTURE_NUMBERS false
#define FIRST_DEPARTURE_BOLD true

// UK time with automatic BST changes
#define TZ_INFO "GMT0BST,M3.5.0/1,M10.5.0"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.cloudflare.com"

// Keep showing the last good board for this long if fetches fail
#define STALE_DATA_SECONDS 600
