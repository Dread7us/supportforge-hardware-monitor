#pragma once

#include <cstdint>
#include "secrets.h"

namespace app_config
{

constexpr const char* kAppName = "supportFORGE";
constexpr const char* kVersion = "1.0.1";

#ifndef COREINK_TARGET_HOST_NAME
#define COREINK_TARGET_HOST_NAME "Host"
#endif

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
constexpr const char* kTargetHostName = COREINK_TARGET_HOST_NAME;
constexpr const char* kNtpServer = COREINK_NTP_SERVER;
constexpr const char* kTimezone = COREINK_TZ;
constexpr const char* kAuthToken = SUPPORTFORGE_AUTH_TOKEN;

constexpr double kFallbackWeatherLatitude = WEATHER_LAT;
constexpr double kFallbackWeatherLongitude = WEATHER_LON;
constexpr const char* kWeatherCity = WEATHER_CITY_NAME;

constexpr uint16_t kScreenWidth = 200;
constexpr uint16_t kScreenHeight = 200;

constexpr uint32_t kDashboardRefreshMs = 60'000;
constexpr uint32_t kDefaultRefreshIntervalMs = 60'000;
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
constexpr uint32_t kAlarmSnoozeMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kAlarmBeepMs = 140;
constexpr uint32_t kAlarmBeepGapMs = 160;
constexpr uint32_t kAlarmPatternPauseMs = 5UL * 1000UL;
constexpr uint8_t kAlarmBeepsPerPattern = 3;
// Set to 0 to avoid periodic heavy full-clears. Full clears are still available
// for explicit actions such as startup and manual refresh commands.
constexpr uint8_t kFullClearEveryRefreshes = 0;

} // namespace app_config
