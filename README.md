# ESP-WordClock8x8

Firmware for an English **8×8 WS2812 word clock**. Time is synchronized from NTP over Wi-Fi. Configuration is available from a self-hosted web dashboard and from a serial console at 115200 baud.

The primary hardware target is the **M5 Atom S3 Lite** (ESP32-S3). The same source tree builds for generic **ESP32** and **ESP8266** boards.

[![Platform](https://img.shields.io/badge/platform-ESP32--S3%20%7C%20ESP32%20%7C%20ESP8266-informational)](platformio.ini)
[![Framework](https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-blue)](https://platformio.org/)
[![Firmware](https://img.shields.io/badge/firmware-0.8.2-blue)](include/config.h)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![LEDs](https://img.shields.io/badge/matrix-WS2812%208%C3%978-lightgrey)](#hardware)
[![MQTT](https://img.shields.io/badge/mqtt-Home%20Assistant-orange)](#mqtt-and-home-assistant)

<p align="center">
  <img src="images/WordClock.jpg" alt="3D-printed 8x8 English word clock showing TWENTY HALF PAST ONE" width="520">
</p>

<p align="center"><em>Reference build: 3D-printed enclosure, stencil letter face, 8×8 WS2812 matrix. The face reads TWENTY HALF PAST ONE.</em></p>

| | |
|---|---|
| **Repository** | [github.com/markusvankempen/ESP-WordClock8x8](https://github.com/markusvankempen/ESP-WordClock8x8) |
| **Project** | ESP-WordClock8x8 |
| **Author** | Markus Van Kempen |
| **License** | [MIT](LICENSE) — Copyright (c) 2026 Markus Van Kempen |
| **Collaborators** | None. Sole author. |
| **Dashboard** | `http://wordclock8x8.local` or the device IP |
| **Setup access point** | `WordClock8x8` (2.4 GHz) |
| **Firmware** | 0.8.2 |

---

## Contents

1. [Overview](#overview)
2. [Features](#features)
3. [Hardware](#hardware)
4. [ESP8266 support](#esp8266-support)
5. [Web dashboard](#web-dashboard)
6. [Live face simulator](#live-face-simulator)
7. [First boot](#first-boot)
8. [OTA updates](#ota-updates)
9. [MQTT and Home Assistant](#mqtt-and-home-assistant)
10. [Settings backup](#settings-backup)
11. [Serial console](#serial-console)
12. [3D-printed enclosure](#3d-printed-enclosure)
13. [Build and flash](#build-and-flash)
14. [Troubleshooting](#troubleshooting)
15. [Topics](#topics)
16. [License](#license)

---

## Overview

ESP-WordClock8x8 drives an 8×8 addressable LED matrix behind a stencil letter plate. Minutes are shown as English words (five, ten, quarter, twenty, half, past, to) plus optional 1–4 minute dots. Hours share letters where the plate requires it (TWELVE / ELEVEN, FIVE / EIGHT, PAST / TO, ONE, TWO, SEVEN).

Typical workflow:

1. Flash firmware and complete the one-time Wi-Fi setup.
2. Read the time on the physical face.
3. Open the dashboard for timezone, colours, night schedule, matrix alignment, MQTT, and firmware updates.

Until NTP succeeds, the unused `*` cells breathe so a missing time lock is visible and the panel does not look unpowered.

---

## Features

| Area | Capability |
|---|---|
| Time | NTP synchronization, POSIX timezone presets, manual preview, live date in the header |
| Appearance | Brightness, hour / solid / rainbow / warm colour modes, RGB picker, seconds effects, minute dots |
| Simulator | Live 8×8 word-clock face in the dashboard, including colours and seconds animations |
| Schedule | Night dimming, blackout window, daily alarm with configurable ring duration |
| Face | Home plate, ATWENTYD plate, or a custom 8×8 letter map |
| Alignment | Mirror X/Y, zigzag, column-major order, corner calibration, wiring presets |
| Network | Setup access point, in-dashboard scan and join with rollback, mDNS hostname |
| Updates | Browser `.bin` upload and ArduinoOTA (`hostname.local`) |
| MQTT | Retained state topic, command topic, optional Home Assistant discovery |
| Backup | JSON export and import of settings (passwords excluded) |
| Console | USB serial menu at 115200 baud covering the same settings as the dashboard |

---

## Hardware

| Item | Notes |
|---|---|
| Microcontroller | M5 Atom S3 Lite (default), generic ESP32, or ESP8266 (D1 mini / NodeMCU) |
| Display | 8×8 WS2812 / NeoPixel matrix, 5 V, single data line |
| Power | USB for flash and modest brightness; a dedicated 5 V supply for a full-white matrix |

### M5 Atom S3 Lite

Grove port: red **5 V**, black **GND**, yellow G1, white **G2**.

| Matrix | Atom S3 Lite |
|---|---|
| DIN | **G2** (Grove white), GPIO 2 |
| 5 V | Grove 5 V, or an external 5 V supply |
| GND | Common ground with the Atom |
| Button | GPIO 41 (short press cycles brightness; hold ~1.5 s runs an LED walk) |

Download mode: hold the side **RESET** button for about two seconds until the green LED lights, then release.

### ESP8266 D1 mini

| Matrix | D1 mini |
|---|---|
| DIN | **D2 / GPIO 4** (GPIO 2 is often the onboard LED) |
| 5 V | 5 V (do not power the matrix from 3.3 V) |
| GND | GND |
| Button | FLASH / GPIO 0 (optional) |

Pins can be overridden at compile time with `-DWC_LED_PIN=n` and `-DWC_BUTTON_PIN=n`.

### Default letter plate

Row 0 is the top of the clock:

```text
T W E N T Y H *
Q U A R T E R M
F I V E H A L F
S E T P A S T O
F I V E I G H T
S I X T H R E E
T W E L E V E N
F O U R N I N E
```

---

## ESP8266 support

The LED, NTP, dashboard, serial, MQTT, and OTA paths are shared. Differences:

| | ESP32-S3 (Atom S3 Lite) | ESP8266 (D1 mini) |
|---|---|---|
| PlatformIO environment | `atoms3lite` | `esp8266` |
| Default DIN | GPIO 2 | GPIO 4 |
| Default button | GPIO 41 | GPIO 0 |
| Flash / OTA | 8 MB, dual OTA slots | 4 MB (`4m1m`) |
| RAM | Sufficient for the full dashboard | Limited; prefer serial for large JSON imports |

Wi-Fi is 2.4 GHz on all supported chips.

---

## Web dashboard

Open `http://wordclock8x8.local/` or the IP address printed on the serial console. The header includes a live 8×8 face that mirrors the physical matrix.

### Time

![Time tab](images/dashboard-time.png)

Timezone presets or a custom POSIX string, NTP server, and a manual time preview.

### Look

![Look tab](images/dashboard-look.png)

Word colour, seconds effect, brightness, and extra-minute dots.

### Night

![Night tab](images/dashboard-night.png)

Night dimming, blackout schedule, and the daily alarm. Time windows may wrap midnight.

### Align

![Align tab](images/dashboard-align.png)

Corner calibration and wiring options (mirror, zigzag, column-major).

### About

![About tab](images/dashboard-about.png)

Firmware version, license, settings backup, and OTA install. MQTT is configured on the Wi-Fi tab.

---

## Live face simulator

Open the dashboard **Live** tab (it is the default page). The header also shows a compact 8×8 face. Both match the LEDs on the device: letter plate, colours, seconds animations, minute dots, night dimming, blackout, alarm, and alignment previews.

| Item | Detail |
|---|---|
| Where | **Live** tab (default), header preview, and Face tab grid |
| Source | `GET /api/face` about five times per second |
| Payload | `letters` (64 characters), `rgb` (64 × RRGGBB), `words` (spoken time), `mode`, `fx` |
| Animations | Star blink, pulse, trail, tick, sparkle, NTP wait breathe |

Example:

```text
GET /api/face
{"ok":true,"mode":"day","fx":"star","words":"TWENTY PAST ONE +2","letters":"TWENTYH*...","rgb":"FFD24A..."}
```

Use this view to confirm wiring, colour, and effect settings without standing in front of the enclosure.

---

## First boot

1. Flash firmware (`./upload.sh` or `pio run -e atoms3lite -t upload`).
2. Join the **WordClock8x8** access point (2.4 GHz).
3. Open `http://192.168.4.1` and enter the home network.
4. Open `http://wordclock8x8.local/` or the LAN address shown by the portal.
5. The matrix displays the time after NTP succeeds.

Default timezone is Atlantic Time with daylight saving (`AST4ADT`). Change it on the Time tab or with `tzlist` / `tz ast` on the serial console.

---

## OTA updates

After the device is on the LAN:

- **Browser:** About tab → select a `.bin` → Install firmware.
- **PlatformIO:** `pio run -e atoms3lite -t upload --upload-port wordclock8x8.local`
- **Serial:** `ota` reports whether ArduinoOTA is listening.

---

## MQTT and Home Assistant

Enable MQTT on the Wi-Fi tab or from the serial console:

```text
mqtt on
mqtt host 192.168.0.10
mqtt port 1883
mqtt user home
mqtt pass secret
mqtt prefix wordclock
```

| Topic | Direction | Payload |
|---|---|---|
| `{prefix}/state` | Device → broker (retained) | JSON: `time`, `mode`, `bright`, `color`, `night`, `blackout`, `alarm` |
| `{prefix}/cmd` | Broker → device | JSON or text (`bright 24`, `night off`, `alarm stop`, `{"color":"warm"}`) |

Optional Home Assistant discovery publishes `homeassistant/sensor/<hostname>/state/config`.

---

## Settings backup

- About tab: export or import JSON
- `GET /api/settings` and `POST /api/settings`
- Serial: `export` / `import { ... }`

Wi-Fi passwords are not included.

---

## Serial console

115200 baud. Type `help`.

| Command | Function |
|---|---|
| `status` | Full status |
| `fetch` / `ntp <host>` | NTP sync |
| `tz <id>` / `tzlist` | Timezone |
| `bright` / `color` / `effect` / `mdots` | Appearance |
| `night` / `sleep` / `alarm` / `alen` | Schedule |
| `letters` / `rowset` / `plate` | Face |
| `align` / `cal` / `flipx` / `zigzag` | Wiring |
| `mqtt` / `export` / `import` / `ota` | Integration and backup |
| `scan` / `join` / `hostname` | Network |

---

## 3D-printed enclosure

This repository does not include STL files. Search Printables, Thingiverse, MakerWorld, or Cults3D for **word clock 8x8**, **word clock stencil**, or **word clock enclosure**.

After assembly, use the Align tab so the lit corners match the printed letters.

| Site | Search |
|---|---|
| [Printables](https://www.printables.com/search/models?q=word%20clock) | word clock 8x8 |
| [Thingiverse](https://www.thingiverse.com/search?q=word%20clock) | word clock |
| [MakerWorld](https://makerworld.com/en/search/models?keyword=word%20clock) | word clock |
| [Cults3D](https://cults3d.com/en/search?q=word+clock) | word clock |

---

## Build and flash

```bash
cd ~/projects/pax/ESP-WordClock8x8

# M5 Atom S3 Lite (default)
./upload.sh
# pio run -e atoms3lite -t upload

# Generic ESP32 (DIN GPIO 13, button GPIO 0)
pio run -e esp32 -t upload

# ESP8266 D1 mini (DIN D2 / GPIO 4)
pio run -e esp8266 -t upload
```

```bash
pio device monitor -b 115200
```

---

## Troubleshooting

| Symptom | Action |
|---|---|
| Matrix is dark | `fill` on serial; check 5 V, GND, and the DIN GPIO for the board |
| Incorrect colours | `order rgb` |
| Words in the wrong place | Align tab or `align mirrorxz` |
| Time offset by hours | `tzlist` then `tz ast` (or the local zone) |
| Breathing `*` only | NTP is not locked; confirm Wi-Fi and run `fetch` |
| ESP8266 dashboard instability | Heap is limited; use serial for large imports |

---

## Topics

Suggested GitHub topics so the repository can be found:

`esp32` `esp32-s3` `esp8266` `arduino` `platformio` `word-clock` `wordclock` `led-matrix` `ws2812` `neopixel` `8x8` `m5stack` `m5-atom` `atom-s3` `iot` `ntp` `mqtt` `home-assistant` `homeassistant` `ota` `wifi` `3d-printing` `diy-clock` `firmware` `embedded` `smart-home` `wled-alternative` `desk-clock` `wall-clock`

When the repository is on GitHub:

```bash
gh repo edit --add-topic esp32,esp32-s3,esp8266,arduino,platformio,word-clock,wordclock,led-matrix,ws2812,neopixel,8x8,m5stack,m5-atom,atom-s3,iot,ntp,mqtt,home-assistant,ota,3d-printing,diy-clock,firmware,embedded,smart-home
```

---

## License

[MIT License](LICENSE). Copyright (c) 2026 Markus Van Kempen.

This project has a **single author** and **no collaborators**. See [`NOTICE`](NOTICE).
