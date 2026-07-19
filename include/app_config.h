#pragma once

#include <cstdint>
#include <secrets.h>

namespace app_config
{

constexpr const char* kAppName = "CoreInk UI";
constexpr const char* kVersion = "0.2.0";

#ifndef COREINK_NTP_SERVER
#define COREINK_NTP_SERVER "pool.ntp.org"
#endif

#ifndef COREINK_TZ
#define COREINK_TZ "PST8PDT,M3.2.0,M11.1.0"
#endif

constexpr const char* kWifiSsid = COREINK_WIFI_SSID;
constexpr const char* kWifiPassword = COREINK_WIFI_PASSWORD;
constexpr const char* kBasementStatusUrl = COREINK_BASEMENT_STATUS_URL;
constexpr const char* kBeelinkLhmUrl = COREINK_BEELINK_LHM_URL;
constexpr const char* kNtpServer = COREINK_NTP_SERVER;
constexpr const char* kTimezone = COREINK_TZ;
constexpr const char* SUPPORTFORGE_AUTH_TOKEN = secrets.SUPPORTFORGE_AUTH_TOKEN;

constexpr double kFallbackWeatherLatitude;

#ifndef COREINK_WEATHER_LAT
#define COREINK_WEATHER_LAT 43.3665
#endif

#ifndef COREINK_WEATHER_LON
#define COREINK_WEATHER_LON -124.2179
#endif

#ifndef COREINK_WEATHER_LOCATION
#define COREINK_WEATHER_LOCATION "Coos Bay OR"
#endif

constexpr double kFallbackWeatherLatitude = COREINK_WEATHER_LAT;
constexpr double kFallbackWeatherLongitude = COREINK_WEATHER_LON;
constexpr const char* kFallbackWeatherLocation = COREINK_WEATHER_LOCATION;

constexpr uint16_t kScreenWidth = 200;
constexpr uint16_t kScreenHeight = 200;

constexpr uint32_t kDashboardRefreshMs = 60'000;
constexpr uint32_t kClockRefreshMs = 1'000;
constexpr uint32_t kSystemRefreshMs = 5'000;
constexpr uint32_t kNetworkRefreshMs = 30'000;
constexpr uint32_t kWifiReconnectMs = 15'000;
constexpr uint32_t kWifiScanMs = 300'000;
constexpr uint32_t kWeatherRefreshMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kNtpInitialRetryMs = 30'000;
constexpr uint32_t kNtpResyncMs = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kHttpTimeoutMs = 4'000;
constexpr uint32_t kDebounceMs = 40;
constexpr uint8_t kBasementAlarmFailureThreshold = 3;
// Set to 0 to avoid periodic heavy full-clears. Full clears are still available
// for explicit actions such as startup and manual refresh commands.
constexpr uint8_t kFullClearEveryRefreshes = 0;

} // namespace app_config
