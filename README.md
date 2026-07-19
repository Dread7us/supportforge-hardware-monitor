# M5 CoreInk UI

A standalone PlatformIO/Arduino firmware project for the **M5Stack CoreInk**.

This project targets the CoreInk hardware and focuses on a polished monochrome e-paper UI with button navigation, battery-friendly refresh behavior, and simple device status pages.

## Hardware

- M5Stack CoreInk (ESP32)
- 200 × 200 monochrome e-paper display
- CoreInk front/side buttons
- Built-in battery/PMIC support exposed by the M5Core-Ink library

## Project Structure

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
Configuration & Environment Setup
To keep your personal network credentials, server endpoints, and location coordinates completely secure and out of version control, this project uses a detached local configuration file.

1. Configure Local Secrets
Create a file named secrets.h inside the src/ directory. Copy the template below and update the values with your local network infrastructure, target telemetry endpoints, and local coordinates:

C++
#ifndef SECRETS_H
#define SECRETS_H

// Local Network Credentials
#ifndef COREINK_WIFI_SSID
#define COREINK_WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef COREINK_WIFI_PASSWORD
#define COREINK_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

// Telemetry Host API Configuration
#ifndef COREINK_BASEMENT_STATUS_URL
#define COREINK_BASEMENT_STATUS_URL "http://YOUR_SERVER_IP:PORT/api/v1/telemetry"
#endif

#ifndef COREINK_BEELINK_LHM_URL
#define COREINK_BEELINK_LHM_URL COREINK_BASEMENT_STATUS_URL
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
2. Verify Version Control Safety
Ensure your root .gitignore file contains a dedicated rule for src/secrets.h to prevent accidental commits of your private configuration data.

Build
Open this folder directly in VS Code or PlatformIO, then build the default environment:

DOS
pio run
Wi-Fi and Internet Time
On power-up the CoreInk starts Wi-Fi, configures SNTP with pool.ntp.org, and syncs the ESP32/CoreInk RTC when internet time becomes available. To keep the device responsive and battery-friendly, time sync is intentionally incremental:

It retries every 30 seconds until the first successful NTP sync.

After a successful sync, it checks about every 6 hours.

Pressing the middle/select button forces an immediate Wi-Fi scan, NTP sync attempt, and a manual telemetry fetch from the monitoring host.

Timezone defaults to Pacific time via COREINK_TZ in include/app_config.h:

C++
PST8PDT,M3.2.0,M11.1.0
You can still override Wi-Fi, password, NTP server, timezone, or server endpoints globally from PlatformIO build_flags within your platformio.ini file if needed.

Boot Splash and E-paper Refresh
At startup the device performs a true full-screen e-paper clean, then shows a bordered COREINK UI splash screen while Wi-Fi and clock services initialize. This addresses the traditional e-paper boot artifact where only a fractional segment of the display updates on initial power-up.

Device Status Bar
The top status bar provides system status at a glance:

Local time parsed from SNTP when Wi-Fi is available, falling back automatically to the CoreInk BM8563 RTC hardware module.

CoreInk battery gauge rendered as a responsive icon + percentage, using the factory-calibrated ADC path on GPIO 35.

Wi-Fi signal strength bars computed directly from the active station connection RSSI metrics.

A Bluetooth capability icon indicating when ESP32 BLE support layers are compiled into the firmware.

Functional Pages
The interface handles multi-page navigation across several distinct diagnostic arrays:

Dashboard: Large digital clock display, localized weather data string, and critical device health badges.

Clock: High-contrast HH:MM layout, full calendar date, and an active time source label (NTP, RTC, or NO TIME).

Power: Hardware ADC metrics, real-time millivolt readings, pack voltage calculations, and automated battery health diagnostics.

Network: Wi-Fi connection states, active SSID parameters, local IP assignment, current RSSI readings, total scanned networks, and BLE compile states.

Server Telemetry: Live metrics fetched from the host server tracking CPU load, RAM usage, storage allocations, and detailed API request summaries.

System: Detailed diagnostics covering active firmware versioning, heap memory allocation, system uptime counters, and wireless stack performance.

Hardware Diagnostics
To view low-level telemetry logs, launch the PlatformIO serial monitor immediately after flashing:

DOS
pio device monitor -b 115200
The system streams standardized debugging blocks to help you monitor runtime processes and tune hardware configurations:

Plaintext
BAT raw=1840 adc_mv=1610 pack=3.96 pct=72 state=3
WIFI connected=1 ssid=YourWifi ip=192.168.1.50 rssi=-58 scan_count=12
TIME synced RTC from NTP: 2026-07-16 20:11
TELEMETRY ok: online=1 cpu=18% mem=42% disk=61% summary=Stack OK
Upload
Connect the CoreInk over USB and execute the build pipeline:

DOS
pio run -t upload
UI Controls
Front/PWR button: Cycles to the next functional page; also handles power state wakeups.

Top/UP button: Reverses navigation to the previous page.

Middle/MID button: Forces a full hardware refresh. Instantly reinitializes Wi-Fi configurations, scans adjacent cells, synchronizes the hardware RTC via NTP, and pulls fresh system payloads from the host endpoint.

Bottom/DOWN button: Drops the device into a low-refresh static sleep view to conserve power.
