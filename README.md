# Train Departure Display for ESP32-S3

A port of Chris Crocker-White's [Train Departure Display](https://github.com/chrisys/train-departure-display) that runs natively on an ESP32-S3. It drives the same SSD1322 256x64 OLED, but needs no Raspberry Pi and no balena account.

It shows live UK departures from the Rail Data Marketplace, and has a settings web page, a Wi-Fi setup hotspot, over-the-air firmware updates, the debug screen and support for a second display. It uses the original app's screens, fonts, layout and scrolling.

Run the commands below from the folder that holds this README.

## Board

The port is built for an ESP32-S3 micro/mini board with the ESP32-S3FH4R2 chip, which has 4 MB flash and 2 MB quad PSRAM built in. Other ESP32-S3 boards with PSRAM should also work.

## Wiring

Set the display to 4-wire SPI first, using the jumper described in the original project's [display guide](https://github.com/chrisys/train-departure-display/blob/main/docs/02-connecting-the-display-to-the-pi.md).

| Display pin | Signal | ESP32-S3 |
|---|---|---|
| 1 | GND | GND |
| 2 | V+ (3.3 V) | 3V3 |
| 4 | D0/SCLK | GPIO 12 |
| 5 | D1/SDIN | GPIO 11 |
| 14 | DC | GPIO 9 |
| 15 | RST | GPIO 8 |
| 16 | CS | GPIO 10 |

### Second display (optional)

A second display shares SCLK, SDIN, DC, RST and power with the first. Only its CS pin has its own wire:

| Display 2 pin | Signal | ESP32-S3 |
|---|---|---|
| 1 | GND | GND |
| 2 | V+ (3.3 V) | 3V3 |
| 4 | D0/SCLK | GPIO 12 (shared) |
| 5 | D1/SDIN | GPIO 11 (shared) |
| 14 | DC | GPIO 9 (shared) |
| 15 | RST | GPIO 8 (shared) |
| 16 | CS | GPIO 7 |

Turn on "Second display" on the settings page and, if you want, give it its own platform. Both displays show the same station. GPIO 7 is held high when the second display is off, so a connected display ignores the first one's traffic. With two displays, the frame rate drops from about 63 fps to about 34 fps, because each frame is drawn and sent twice. The scrolling stays at the same speed.

## Building

You need the Arduino ESP32 core 3.x and the U8g2 and ArduinoJson (7.x) libraries.

```sh
arduino-cli compile --upload -p /dev/cu.usbmodemXXXX \
  --fqbn esp32:esp32:esp32s3:PartitionScheme=min_spiffs,PSRAM=enabled,USBMode=hwcdc,CDCOnBoot=cdc \
  TrainDisplay
```

In the Arduino IDE, choose these settings:

- Board: ESP32S3 Dev Module
- Partition Scheme: Minimal SPIFFS
- PSRAM: QSPI PSRAM
- USB CDC On Boot: Enabled

Serial output runs at 115200 baud over the USB cable.

## Setup

1. Copy `TrainDisplay/secrets.example.h` to `TrainDisplay/secrets.h`, which git ignores. Fill in your Wi-Fi details, your Rail Data Marketplace consumer key, the admin password and the setup hotspot password.
2. To get the key, subscribe to the **Live Departure Board** product (version 1.2) on [raildata.org.uk](https://raildata.org.uk). The key is in the product's Specification tab. A key for another version is rejected with HTTP 401.
3. Optionally, set your station and options in `TrainDisplay/config.h`. These are only first-boot defaults; you can change everything later on the web page.
4. Flash over USB once (see Building).

## Settings web page

Open `http://train-display.local/` (or the display's IP address). Anyone on your network can see the status panel: the departures, time, network, last update and memory. To change settings, log in with the admin password (user name `admin`).

- **Departures:** station, destination filter, platform, refresh time, time offset and per-stop times.
- **Hours:** operating hours, out-of-hours name and screen-off hours.
- **Display:** rotation, brightness, bold first row, numbered departures, the second display and the debug screen.
- **Network and keys:** Wi-Fi, API key and admin password. Saved passwords and keys are never shown; leave a field blank to keep it.
- **Actions:** refresh now, show the debug screen for 30 seconds, restart, and reset settings to the `config.h`/`secrets.h` defaults.

### Debug screen

The debug screen is the original app's: a box showing the station, filters, platform, refresh time and operating hours, the firmware version, and the MAC and IP addresses. Set **Debug screen** to 1 to show it permanently, with the clock below it. Set it to 2 or more to show it for that many seconds in place of the start-up screen. Each display shows its own platform.

Most changes apply straight away and trigger a fresh fetch. Changing Wi-Fi restarts the display. Five wrong passwords in 10 minutes lock that address out for a minute, doubling up to 15 minutes.

## Setup hotspot

If no Wi-Fi network is saved, or the saved one can't be joined within 60 seconds of starting, the display opens a hotspot called `TrainDisplay-Setup-XXXX`. The screen shows its name, password and address (`http://192.168.4.1`). Join it from a phone, and the settings page should open by itself. Log in, set the Wi-Fi details and save. The display restarts and joins your network. An unused hotspot restarts after 5 minutes to try the saved network again.

To wipe the saved settings over USB instead:

```sh
esptool --port /dev/cu.usbmodemXXXX erase-region 0x9000 0x5000
```

## Firmware updates

- **From the web page:** log in, choose `TrainDisplay.ino.bin` (from the build output folder) under Firmware update, and upload. The screen shows the progress.
- **From the Arduino IDE:** choose the `train-display` network port and enter the admin password when asked. With `arduino-cli`, use `-p train-display.local --protocol network --upload-field password=...`.

New firmware starts in a "pending health check" state. It is kept once it has been on the network (or running the setup hotspot) for a minute. If it crashes before then, or hasn't got that far after 10 minutes, the display goes back to the previous firmware.

### Changes from the original

- The data comes from the Rail Data Marketplace JSON API (`GetDepBoardWithDetails`, v1.2) instead of the OpenLDBWS SOAP service. The text built from it is the same.
- If a fetch fails, the last good board stays on screen for up to 10 minutes (`STALE_DATA_SECONDS`). After that, the welcome screen shows. The original showed the welcome screen at the first failure.
- Departures just after midnight always sort after late-evening ones.
- Fetching runs on its own CPU core, so scrolling doesn't pause while new data loads.

## Fonts

`TrainDisplay/dotmatrix_fonts.h` is generated by `tools/fontconv.py` from Daniel Hart's [Dot Matrix Typeface](https://github.com/DanielHartUK/Dot-Matrix-Typeface) TTFs (`Dot Matrix Regular.ttf`, `Dot Matrix Bold.ttf` and `Dot Matrix Bold Tall.ttf`). The script renders each glyph with Pillow, the same way the original Pi app draws its text. It then checks that drawing a set of test strings one glyph at a time matches Pillow's own output: the same pixels, widths and heights. Run it again if the fonts change:

```sh
python3 -m venv tools/.venv
tools/.venv/bin/pip install 'pillow>=10.3'
tools/.venv/bin/python -I tools/fontconv.py path/to/fonts TrainDisplay/dotmatrix_fonts.h
```

## Credits

- [Train Departure Display](https://github.com/chrisys/train-departure-display) by Chris Crocker-White and contributors: the original Raspberry Pi app, whose screens, layout and text this port reproduces.
- [Dot Matrix Typeface](https://github.com/DanielHartUK/Dot-Matrix-Typeface) by Daniel Hart, under the [SIL Open Font License](https://openfontlicense.org). The bitmap fonts in `TrainDisplay/dotmatrix_fonts.h` are made from it.
- Departure data from National Rail Enquiries via the [Rail Data Marketplace](https://raildata.org.uk).
- [U8g2](https://github.com/olikraus/u8g2) and [ArduinoJson](https://arduinojson.org).
