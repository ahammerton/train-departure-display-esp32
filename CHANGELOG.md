# Changelog

## 0.7.0 (2026-11-09)

First release of the ESP32-S3 port. The version follows the original Raspberry Pi app's 0.7.0, whose features it matches.

- Live departures from the Rail Data Marketplace Live Departure Board API (v1.2), with the original app's screens, Dot Matrix fonts, layout and scrolling.
- Station, destination and platform filters, time offset, operating hours, screen-off hours, rotation, brightness and the other options of the original.
- Settings web page with an admin login, and a setup hotspot when no Wi-Fi network can be joined.
- Firmware updates from the web page or the Arduino IDE, with automatic rollback if new firmware can't reach the network.
- The original's debug screen, and an optional second display with its own platform filter.
- The last good board stays on screen for up to 10 minutes if a fetch fails.
