# M5 CoreInk UI

A standalone PlatformIO/Arduino firmware project for the **M5Stack CoreInk**.

This project targets the CoreInk hardware and focuses on a polished monochrome e-paper UI with button navigation, battery-friendly refresh behavior, and simple device status pages.

## Hardware

- M5Stack CoreInk
- ESP32
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
```

## Build

Open this folder directly in VS Code or PlatformIO, then build the default environment:

```cmd
pio run
```

## Wi-Fi and Internet Time

This firmware is preconfigured for the IoT network:

- SSID: `TP-Link_IoT`
- Password: `internet1`

On power-up the CoreInk starts Wi-Fi, configures SNTP with `pool.ntp.org`, and syncs the ESP32/CoreInk RTC when internet time becomes available. To keep the device responsive and battery-friendly, time sync is intentionally incremental:

- It retries every **30 seconds** until the first successful NTP sync.
- After a successful sync, it checks about every **6 hours**.
- Pressing the **middle/select** button forces an immediate Wi-Fi scan, NTP sync attempt, and Beelink fetch.

Timezone defaults to Pacific time via `COREINK_TZ` in `include/app_config.h`:

```cpp
PST8PDT,M3.2.0,M11.1.0
```

You can still override Wi-Fi, password, NTP server, timezone, or Beelink endpoint from PlatformIO `build_flags` if needed.

## Boot Splash and E-paper Refresh

At startup the device now performs a true full-screen e-paper clean, then shows a bordered **COREINK UI** splash screen while Wi-Fi and clock services initialize. This is intended to fix the previous boot artifact where only a tiny quarter of the display appeared at the bottom and the rest of the panel stayed dark until buttons were pressed.

## Device Status Bar

The top bar now shows:

- Local time from SNTP when Wi-Fi is available, falling back to the CoreInk BM8563 RTC reading.
- The CoreInk battery as an icon plus percentage, using the factory-test ADC path on GPIO 35.
- Wi-Fi signal strength bars from the CoreInk ESP32 station connection.
- A Bluetooth capability icon when ESP32 Bluetooth support is compiled in.

## Functional Pages

The CoreInk UI now cycles through functional pages instead of placeholders:

- **Dashboard**: large clock, weather status, and device health badges.
- **Clock**: large `HH:MM`, date, and clear `NTP` / `RTC` / `NO TIME` source label.
- **Power**: hardware ADC battery gauge, raw ADC, calibrated ADC mV, pack voltage, and battery health state.
- **Network**: Wi-Fi configured/connected state, SSID, IP, RSSI, signal bars, scan count, and Bluetooth capability.
- **Beelink**: Libre Hardware Monitor online/offline state, CPU/memory/disk metrics, and latest endpoint error/summary.
- **System**: firmware, heap, uptime, wireless, Bluetooth, and battery diagnostics.

The middle/select button forces a Wi-Fi scan and Beelink fetch. Serial monitor now prints `BAT`, `WIFI`, `TIME`, and `BEELINK` diagnostic lines so hardware issues are visible immediately after flashing.

## Wi-Fi and Beelink Libre Hardware Monitor Status

Default Wi-Fi credentials are set in `include/app_config.h` for `TP-Link_IoT`. Configure the optional basement Beelink status endpoint with PlatformIO build flags. If you override credentials, keep secrets out of git by putting these in a local untracked environment override or editing them only on your machine:

```ini
build_flags =
    -std=gnu++17
    -DCORE_DEBUG_LEVEL=0
    -DCOREINK_WIFI_SSID=\"TP-Link_IoT\"
    -DCOREINK_WIFI_PASSWORD=\"internet1\"
    -DCOREINK_BEELINK_LHM_URL=\"http://192.168.0.123:8085/data.json\"
    -DCOREINK_NTP_SERVER=\"pool.ntp.org\"
    -DCOREINK_TZ=\"PST8PDT,M3.2.0,M11.1.0\"
```

The firmware fetches `COREINK_BEELINK_LHM_URL` about every 30 seconds and expects the nested Libre Hardware Monitor `data.json` tree from port `8085`. It recursively extracts `CPU Total`, memory load, and storage used-space percentages for C: and D: drives.

When the Beelink HTTP request fails, times out, returns non-200, or the Libre Hardware Monitor metrics are missing, the Beelink page remains OFFLINE and the CoreInk speaker pulses until the top/UP button dismisses the alarm. The server status remains OFFLINE for the UI even after the alarm is muted. A successful later fetch returns the state to ONLINE and automatically arms the alarm for the next failure.

## Hardware Diagnostics

After uploading, open the serial monitor:

```cmd
pio device monitor -b 115200
```

Look for lines like:

```text
BAT raw=1840 adc_mv=1610 pack=3.96 pct=72 state=3
WIFI connected=1 ssid=YourWifi ip=192.168.1.50 rssi=-58 scan_count=12
TIME synced RTC from NTP: 2026-07-16 20:11
BEELINK ok: online=1 cpu=18% mem=42% disk=61% summary=Stack OK
```

If the battery still reads wrong, the `BAT raw` and `adc_mv` values are the key hardware facts needed to tune the divider/calibration for your exact CoreInk revision.

## Upload

Connect the CoreInk over USB and run:

```cmd
pio run -t upload
```

## Serial Monitor

```cmd
pio device monitor -b 115200
```

## UI Controls

- **Front/PWR button**: next page; also wakes/navigates away from the sleep page.
- **Top/UP button**: previous page.
- **Middle/MID button**: force refresh. This immediately rechecks Wi-Fi, rescans nearby networks, tries an NTP time sync, and fetches the Beelink Libre Hardware Monitor endpoint if configured.
- **Bottom/DOWN button**: sleep/static page to reduce refresh activity.

The exact physical mapping can be adjusted in `src/buttons.cpp` if a CoreInk hardware revision reports different button names.

## E-paper UI Notes

The firmware avoids continuous animation. It refreshes when page content changes, on a periodic status update, or when the user presses a button. A full clear is performed periodically to reduce ghosting.
