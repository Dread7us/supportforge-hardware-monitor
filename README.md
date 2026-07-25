# 📟 M5 CoreInk UI

![PlatformIO](https://img.shields.io/badge/PlatformIO-orange?style=flat&logo=PlatformIO)
![C++](https://img.shields.io/badge/C%2B%2B-00599C?style=flat&logo=c%2B%2B)
![ESP32](https://img.shields.io/badge/ESP32-Hardware-blue?style=flat)

> A standalone PlatformIO/Arduino firmware project for the **M5Stack CoreInk**. 
> Focuses on a polished monochrome e-paper UI with button navigation, battery-friendly refresh behavior, and dynamic device status pages.

---

## 📸 Screenshots

| Dashboard | Network | Server Telemetry |
| :---: | :---: | :---: |
| <img src="https://github.com/user-attachments/assets/ff72749e-d9ce-4d79-a058-cd2c7f96f7b4" width="250" alt="Dashboard View" /> | <img src="https://github.com/user-attachments/assets/79e5c50f-af99-4fdb-8fc6-526d63a609e7" width="250" alt="Network View" /> | <img src="https://github.com/user-attachments/assets/174a6117-47c4-437b-ad56-65f38a8cda42" width="250" alt="Server Telemetry View" /> |

---

## 🧰 Hardware

- **M5Stack CoreInk** (ESP32)
- **Display:** 200 × 200 monochrome e-paper (SPI)
- **Controls:** CoreInk front/side multi-function buttons
- **Power:** Built-in battery/PMIC support exposed by the `M5Core-Ink` library

## 📂 Project Structure

```text
m5-coreink-ui/
  platformio.ini
  README.md
  include/
    app_config.h
    ui_theme.h
  src/
    main.cpp
    app_state.h
    app_state.cpp
    buttons.h
    buttons.cpp
    display.h
    display.cpp
    ui_pages.h
    ui_pages.cpp
```

## 🔒 Configuration & Environment Setup

To keep your personal network credentials, server endpoints, and location coordinates completely secure and out of version control, this project uses a detached local configuration file. 

### 1. Configure Local Secrets

Create the active local secrets file at `src/secrets.h`. Copy the template below and update the values with your local network infrastructure, telemetry endpoints, and local coordinates. The primary endpoint is attempted first. The fallback endpoint is optional, is used only after a connection-level primary failure, and any successful fallback is remembered temporarily while the primary endpoint is periodically retried:

```cpp
#ifndef SECRETS_H
#define SECRETS_H

// Local Network Credentials
#ifndef COREINK_WIFI_SSID
#define COREINK_WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef COREINK_WIFI_PASSWORD
#define COREINK_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

// Primary supportFORGE telemetry endpoint
#ifndef COREINK_BASEMENT_STATUS_URL
#define COREINK_BASEMENT_STATUS_URL \
    "http://YOUR_PRIMARY_SERVER_IP:PORT"
#endif

#ifndef COREINK_BEELINK_LHM_URL
#define COREINK_BEELINK_LHM_URL COREINK_BASEMENT_STATUS_URL
#endif

// Optional secondary endpoint used after a connection-level primary failure.
// Leave empty when no fallback server address is available.
#ifndef COREINK_BEELINK_LHM_FALLBACK_URL
#define COREINK_BEELINK_LHM_FALLBACK_URL \
    "http://YOUR_FALLBACK_SERVER_IP:PORT"
#endif

// Application Authentication Token
#ifndef SUPPORTFORGE_AUTH_TOKEN
#define SUPPORTFORGE_AUTH_TOKEN "YOUR_SECURE_AUTH_TOKEN"
#endif

// Geographic Fallbacks for Weather Services
#ifndef WEATHER_LAT
#define WEATHER_LAT 0.0000
#endif

#ifndef WEATHER_LON
#define WEATHER_LON 0.0000
#endif

#ifndef WEATHER_CITY_NAME
#define WEATHER_CITY_NAME "Your City, Region"
#endif

#endif // SECRETS_H
```

### 2. Verify Version Control Safety

Ensure your root `.gitignore` file contains a dedicated rule for `src/secrets.h` to prevent accidental commits of your private configuration data. Real credentials, authentication tokens, SSIDs, and private server addresses must never be committed.

---

## 🚀 Build & Upload

Open this folder directly in VS Code or PlatformIO, then build the default environment:

```cmd
pio run
```
To build and push the firmware to the device over USB:
```cmd
pio run -t upload
```

---

## 🌐 Wi-Fi and Internet Time

On power-up the CoreInk starts Wi-Fi, configures SNTP with `pool.ntp.org`, and syncs the ESP32/CoreInk RTC when internet time becomes available. To keep the device responsive and battery-friendly, time sync is intentionally incremental:

- 🔄 Retries every **30 seconds** until the first successful NTP sync.
- ⏳ After a successful sync, it checks about every **6 hours**.
- 🔘 Pressing the **middle/select** button forces an immediate Wi-Fi scan, NTP sync attempt, and a manual telemetry fetch from the monitoring host.

Timezone defaults to Pacific time via `COREINK_TZ` in `include/app_config.h`:
```cpp
PST8PDT,M3.2.0,M11.1.0
```
*(You can override Wi-Fi, passwords, NTP servers, timezones, or server endpoints globally via PlatformIO `build_flags` in `platformio.ini`)*

---

## 🎨 UI & Rendering Architecture

### Boot Splash and E-paper Refresh
At startup, the device performs a true full-screen e-paper clean, then shows a bordered **COREINK UI** splash screen while Wi-Fi and clock services initialize. This eliminates the traditional e-paper boot artifact where only a fractional segment of the display updates on initial power-up.

### Device Status Bar
The top status bar provides system status at a glance:
- **Time:** Parsed from SNTP when Wi-Fi is available, falling back to the CoreInk BM8563 RTC.
- **Battery:** Responsive gauge icon + percentage, using the factory-calibrated ADC path (GPIO 35).
- **Wi-Fi:** Signal strength bars computed directly from the active station connection RSSI metrics.
- **Bluetooth:** Capability icon indicating when ESP32 BLE support layers are active.

### 📑 Functional Pages
The interface handles multi-page navigation across several distinct diagnostic arrays:

- 🏠 **Dashboard**: Large digital clock display, localized weather data, and critical device health badges.
- 🕒 **Clock**: High-contrast `HH:MM` layout, full calendar date, and active time source label (`NTP`, `RTC`, or `NO TIME`).
- 🔋 **Power**: Hardware ADC metrics, real-time millivolt readings, pack voltage, and automated battery health diagnostics.
- 📶 **Network**: Wi-Fi states, active SSID, local IP, RSSI readings, scanned network count, and BLE compile states.
- 🖥️ **Server Telemetry**: Live metrics fetched from the host server tracking CPU load, RAM usage, storage allocations, and API request summaries.
- ⚙️ **System**: Firmware versioning, heap memory allocation, system uptime counters, and wireless stack performance.

---

## 🕹️ UI Controls

| Button | Action |
| :--- | :--- |
| **Front / PWR** | Cycles to the next functional page; wakes device from sleep state. |
| **Top / UP** | Reverses navigation to the previous page. |
| **Middle / MID** | Forces a full hardware refresh. Reinitializes Wi-Fi, scans cells, syncs NTP, and pulls fresh telemetry. |
| **Bottom / DOWN** | Drops the device into a low-refresh static sleep view to conserve power. |

---

## 🩺 Hardware Diagnostics

To view low-level telemetry logs, launch the PlatformIO serial monitor immediately after flashing:

```cmd
pio device monitor -b 115200
```

The system streams standardized debugging blocks to help you monitor runtime processes and tune hardware configurations:

```text
BAT raw=1840 adc_mv=1610 pack=3.96 pct=72 state=3
WIFI connected=1 ssid=YourWifi ip=192.168.1.50 rssi=-58 scan_count=12
TIME synced RTC from NTP: 2026-07-16 20:11
TELEMETRY ok: online=1 cpu=18% mem=42% disk=61% summary=Stack OK
```
