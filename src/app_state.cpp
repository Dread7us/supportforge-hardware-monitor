#include <app_state.h>
#include <secrets.h>
#include <app_config.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5CoreInk.h>
#include <WiFi.h>
#include <algorithm>
#include <ctime>
#include <esp_adc_cal.h>

namespace
{

constexpr int kBatteryAdcPin = 35;
constexpr float kBatteryDivider = 25.1f / 5.1f;
constexpr float kBatteryMinValidVoltage = 3.0f;
constexpr float kBatteryMaxValidVoltage = 4.35f;

bool hasText(const char* value)
{
    return value && value[0] != '\0';
}

int percentFromVoltage(float voltage)
{
    if (voltage < 2.0f || voltage > 5.5f)
    {
        return -1;
    }
    if (voltage <= 3.35f)
    {
        return 0;
    }
    if (voltage >= 4.20f)
    {
        return 100;
    }
    return static_cast<int>(((voltage - 3.35f) * 100.0f / 0.85f) + 0.5f);
}

BatteryState batteryStateFromPercent(int percent)
{
    if (percent < 0) return BatteryState::Unknown;
    if (percent <= 5) return BatteryState::Critical;
    if (percent <= 20) return BatteryState::Low;
    if (percent >= 95) return BatteryState::Full;
    return BatteryState::Ok;
}

int strengthFromRssi(int rssi)
{
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

const char* weatherConditionFromCode(int code)
{
    if (code == 0) return "CLEAR";
    if (code == 1 || code == 2) return "PARTLY CLOUDY";
    if (code == 3) return "CLOUDY";
    if (code == 45 || code == 48) return "FOG";
    if ((code >= 51 && code <= 57) || (code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "RAIN";
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return "SNOW";
    if (code >= 95 && code <= 99) return "STORM";
    return "WEATHER";
}

void copyField(char* dest, size_t len, const char* value)
{
    if (!dest || len == 0 || !value)
    {
        return;
    }
    snprintf(dest, len, "%s", value);
}

} // namespace

void AppState::begin()
{
    preferences.begin("forge", false);
    temp_is_f = preferences.getBool("is_f", false);
    basement_.configured = hasText(app_config::kBeelinkLhmUrl);
    weather_.configured = true;
    M5.Speaker.begin();
    M5.Speaker.mute();
    copyField(weather_.location, sizeof(weather_.location), app_config::kWeatherCity);
    status_.wifi_configured = hasText(app_config::kWifiSsid);
    connectWifiIfNeeded();
    configureTimeIfNeeded();
    update();
}

void AppState::update()
{
    const uint32_t now = millis();
    status_.uptime_ms = now;
    status_.free_heap = ESP.getFreeHeap();
    connectWifiIfNeeded();
    configureTimeIfNeeded();
    readBattery();
    readWireless();
    scanWifiIfDue();
    syncNetworkTimeIfDue();
    readTime();
    fetchWeatherIfDue();
    fetchBasementStatusIfDue();
    updateChargeAnimation();
    last_status_update_ms_ = millis();
}

void AppState::updateChargeAnimation()
{
    const uint32_t now = millis();
    if (status_.charging && page_ == Page::Dashboard && (now - last_anim_toggle_) > 10000U)
    {
        charge_anim_state_ = !charge_anim_state_;
        last_anim_toggle_ = now;
        charge_anim_display_changed_ = true;
    }
    else if (!status_.charging)
    {
        charge_anim_state_ = false;
        last_anim_toggle_ = now;
    }
}

void AppState::nextPage()
{
    const uint8_t next = (static_cast<uint8_t>(page_) + 1U) % static_cast<uint8_t>(Page::Count);
    page_ = static_cast<Page>(next);
}

void AppState::previousPage()
{
    const uint8_t count = static_cast<uint8_t>(Page::Count);
    const uint8_t current = static_cast<uint8_t>(page_);
    page_ = static_cast<Page>((current + count - 1U) % count);
}

void AppState::setPage(Page page)
{
    page_ = page;
}

void AppState::noteRefresh()
{
    ++status_.refresh_count;
}

void AppState::forceNetworkRefresh()
{
    connectWifiIfNeeded();
    configureTimeIfNeeded();
    syncNetworkTimeIfDue(true);
    scanWifiIfDue(true);
    fetchWeatherIfDue(true);
    fetchBasementStatusIfDue(true);
}

void AppState::toggleServerAlarmMuteIfOffline()
{
    if (basement_.server_status == ServerStatus::Offline || basement_.alarm_active)
    {
        basement_.alarm_muted = !basement_.alarm_muted;
        if (basement_.alarm_muted)
        {
            M5.Speaker.mute();
        }
        Serial.printf("BEELINK alarm %s by button\n", basement_.alarm_muted ? "muted" : "unmuted");
    }
}

void AppState::updateServerAlarm()
{
    M5.Speaker.update();

    if (!basement_.alarm_active || basement_.alarm_muted)
    {
        return;
    }

    static uint32_t last_beep_ms = 0;
    const uint32_t now = millis();
    if (last_beep_ms == 0 || (now - last_beep_ms) >= 900U)
    {
        M5.Speaker.setVolume(2);
        M5.Speaker.tone(1800, 160);
        last_beep_ms = now;
    }
}

void AppState::toggleTempUnit()
{
    temp_is_f = !temp_is_f;
    preferences.putBool("is_f", temp_is_f);
    updateBeelinkTempFormats();
}

bool AppState::consumeBatteryDisplayChanged()
{
    const bool changed = battery_display_changed_;
    battery_display_changed_ = false;
    return changed;
}

bool AppState::consumeChargeAnimDisplayChanged()
{
    const bool changed = charge_anim_display_changed_;
    charge_anim_display_changed_ = false;
    return changed;
}

bool AppState::shouldFullClear() const
{
    return app_config::kFullClearEveryRefreshes > 0 &&
           status_.refresh_count > 0 &&
           (status_.refresh_count % app_config::kFullClearEveryRefreshes) == 0;
}

void AppState::readBattery()
{
    analogSetPinAttenuation(kBatteryAdcPin, ADC_11db);
    esp_adc_cal_characteristics_t adc_chars{};
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 3600, &adc_chars);

    uint32_t raw_total = 0;
    uint16_t raw_min = UINT16_MAX;
    uint16_t raw_max = 0;
    constexpr int samples = 16;
    for (int i = 0; i < samples; ++i)
    {
        const uint16_t sample = analogRead(kBatteryAdcPin);
        raw_total += sample;
        raw_min = std::min(raw_min, sample);
        raw_max = std::max(raw_max, sample);
        delay(1);
    }

    const uint32_t raw = (raw_total - raw_min - raw_max) / (samples - 2);
    const uint32_t millivolts = esp_adc_cal_raw_to_voltage(raw, &adc_chars);
    status_.battery_raw_adc = static_cast<uint16_t>(raw);
    status_.battery_adc_mv = static_cast<uint16_t>(millivolts);
    status_.battery_voltage = static_cast<float>(millivolts) * kBatteryDivider / 1000.0f;
    status_.battery_valid = false;
    status_.battery_percent = -1;

    if (raw < 20 || millivolts < 20)
    {
        copyField(status_.battery_fault, sizeof(status_.battery_fault), "BAT ADC ZERO");
    }
    else if (raw > 4080)
    {
        copyField(status_.battery_fault, sizeof(status_.battery_fault), "BAT ADC SATURATED");
    }
    else if (status_.battery_voltage < kBatteryMinValidVoltage || status_.battery_voltage > kBatteryMaxValidVoltage)
    {
        copyField(status_.battery_fault, sizeof(status_.battery_fault), "BAT VOLTAGE RANGE");
    }
    else
    {
        status_.battery_percent = percentFromVoltage(status_.battery_voltage);
        if (status_.battery_percent > 100) status_.battery_percent = 100;
        status_.battery_valid = status_.battery_percent >= 0;
        copyField(status_.battery_fault, sizeof(status_.battery_fault), status_.battery_valid ? "" : "BAT PCT INVALID");
    }
    status_.battery_state = batteryStateFromPercent(status_.battery_percent);

    // Charging voltage can wobble several ADC counts while USB is connected.
    // Treat charging as a stable UI state, and only request battery-driven
    // redraws when the displayed integer percentage changes or this boolean
    // actually flips. Raw voltage/ADC drift must not spam e-paper refreshes.
    const bool was_charging = status_.charging;
    if (status_.battery_valid)
    {
        if (!status_.charging && status_.battery_voltage >= 4.18f)
        {
            status_.charging = true;
        }
        else if (status_.charging && status_.battery_voltage <= 4.08f)
        {
            status_.charging = false;
        }
    }
    else
    {
        status_.charging = false;
    }

    const int current_battery_percentage = status_.battery_percent;
    const bool is_charging = status_.charging;
    if (!has_battery_display_sample_)
    {
        last_battery_percentage_ = current_battery_percentage;
        last_charging_ = is_charging;
        has_battery_display_sample_ = true;
    }
    else if (current_battery_percentage != last_battery_percentage_ || is_charging != last_charging_)
    {
        battery_display_changed_ = true;
        last_battery_percentage_ = current_battery_percentage;
        last_charging_ = is_charging;
    }

    if (was_charging != status_.charging)
    {
        Serial.printf("BAT charging=%d\n", status_.charging);
    }

    static uint32_t last_log_ms = 0;
    if (last_log_ms == 0 || millis() - last_log_ms > 30000U)
    {
        Serial.printf("BAT raw=%u adc_mv=%u pack=%.2f pct=%d state=%u\n",
                      status_.battery_raw_adc,
                      status_.battery_adc_mv,
                      static_cast<double>(status_.battery_voltage),
                      status_.battery_percent,
                      static_cast<unsigned>(status_.battery_state));
        if (!status_.battery_valid)
        {
            Serial.printf("BAT fault=%s\n", status_.battery_fault);
        }
        last_log_ms = millis();
    }
}

void AppState::connectWifiIfNeeded()
{
    status_.wifi_configured = hasText(app_config::kWifiSsid);
    if (!status_.wifi_configured || WiFi.status() == WL_CONNECTED)
    {
        return;
    }

    const uint32_t now = millis();
    if (last_wifi_attempt_ms_ != 0 && (now - last_wifi_attempt_ms_) < app_config::kWifiReconnectMs)
    {
        return;
    }

    last_wifi_attempt_ms_ = now;
    WiFi.mode(WIFI_STA);
    WiFi.begin(app_config::kWifiSsid, app_config::kWifiPassword);
}

void AppState::configureTimeIfNeeded()
{
    if (time_configured_ || WiFi.status() != WL_CONNECTED)
    {
        return;
    }
    configTzTime(app_config::kTimezone, app_config::kNtpServer);
    time_configured_ = true;
    last_time_sync_attempt_ms_ = 0;
}

void AppState::readTime()
{
    RTC_TimeTypeDef rtc_time{};
    RTC_DateTypeDef rtc_date{};
    M5.rtc.GetTime(&rtc_time);
    M5.rtc.GetDate(&rtc_date);

    status_.rtc_valid = rtc_time.Hours >= 0 && rtc_time.Hours <= 23 &&
                        rtc_time.Minutes >= 0 && rtc_time.Minutes <= 59 &&
                        rtc_time.Seconds >= 0 && rtc_time.Seconds <= 59 &&
                        rtc_date.Year >= 2024 && rtc_date.Month >= 1 && rtc_date.Month <= 12 &&
                        rtc_date.Date >= 1 && rtc_date.Date <= 31;

    if (status_.rtc_valid)
    {
        status_.time_source = status_.time_synced ? TimeSource::Ntp : TimeSource::RtcClock;
        const int hour12 = (rtc_time.Hours % 12) == 0 ? 12 : (rtc_time.Hours % 12);
        snprintf(status_.time_text, sizeof(status_.time_text), "%d:%02d", hour12, rtc_time.Minutes);
        snprintf(status_.second_text, sizeof(status_.second_text), "%02d", rtc_time.Seconds);
        snprintf(status_.meridiem_text, sizeof(status_.meridiem_text), "%s", rtc_time.Hours >= 12 ? "PM" : "AM");
        snprintf(status_.date_text, sizeof(status_.date_text), "%02d-%02d-%04d", rtc_date.Month, rtc_date.Date, rtc_date.Year);
        copyField(status_.rtc_fault, sizeof(status_.rtc_fault), "");
        return;
    }

    status_.time_source = TimeSource::None;
    status_.time_synced = false;
    snprintf(status_.time_text, sizeof(status_.time_text), "--:--");
    snprintf(status_.second_text, sizeof(status_.second_text), "--");
    snprintf(status_.meridiem_text, sizeof(status_.meridiem_text), "");
    snprintf(status_.date_text, sizeof(status_.date_text), "RTC NOT SET");
    copyField(status_.rtc_fault, sizeof(status_.rtc_fault), "RTC INVALID/UNSET");
}

void AppState::syncNetworkTimeIfDue(bool force)
{
    configureTimeIfNeeded();
    if (!time_configured_ || WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    const uint32_t now = millis();
    const uint32_t interval = status_.time_synced ? app_config::kNtpResyncMs : app_config::kNtpInitialRetryMs;
    if (!force && last_time_sync_attempt_ms_ != 0 && (now - last_time_sync_attempt_ms_) < interval)
    {
        return;
    }

    last_time_sync_attempt_ms_ = now;
    struct tm timeinfo{};
    if (!getLocalTime(&timeinfo, 250))
    {
        copyField(status_.rtc_fault, sizeof(status_.rtc_fault), "NTP WAITING");
        Serial.println("TIME NTP not ready yet");
        return;
    }

    syncRtcFromSystemTime(timeinfo);
    status_.rtc_valid = true;
    status_.time_synced = true;
    status_.time_source = TimeSource::Ntp;
    status_.last_ntp_sync_ms = now;
}

void AppState::readWireless()
{
    status_.wifi_connected = WiFi.status() == WL_CONNECTED;
    status_.wifi_rssi = status_.wifi_connected ? WiFi.RSSI() : 0;
    status_.wifi_strength = status_.wifi_connected ? strengthFromRssi(status_.wifi_rssi) : 0;
    snprintf(status_.wifi_ssid, sizeof(status_.wifi_ssid), "%s", status_.wifi_connected ? WiFi.SSID().c_str() : app_config::kWifiSsid);
    snprintf(status_.wifi_ip, sizeof(status_.wifi_ip), "%s", status_.wifi_connected ? WiFi.localIP().toString().c_str() : "0.0.0.0");

#if defined(CONFIG_BT_ENABLED)
    status_.bluetooth_supported = true;
    status_.bluetooth_enabled = true;
#else
    status_.bluetooth_supported = false;
    status_.bluetooth_enabled = false;
#endif
}

void AppState::scanWifiIfDue(bool force)
{
    if (WiFi.getMode() == WIFI_OFF)
    {
        return;
    }
    const uint32_t now = millis();
    if (!force && last_wifi_scan_ms_ != 0 && (now - last_wifi_scan_ms_) < app_config::kWifiScanMs)
    {
        return;
    }
    last_wifi_scan_ms_ = now;
    status_.wifi_scan_count = WiFi.scanNetworks(false, true);
    Serial.printf("WIFI connected=%d ssid=%s ip=%s rssi=%d scan_count=%d\n",
                  status_.wifi_connected,
                  status_.wifi_ssid,
                  status_.wifi_ip,
                  status_.wifi_rssi,
                  status_.wifi_scan_count);
}

void AppState::syncRtcFromSystemTime(const tm& timeinfo)
{
    RTC_TimeTypeDef rtc_time{};
    rtc_time.Hours = timeinfo.tm_hour;
    rtc_time.Minutes = timeinfo.tm_min;
    rtc_time.Seconds = timeinfo.tm_sec;
    M5.rtc.SetTime(&rtc_time);

    RTC_DateTypeDef rtc_date{};
    rtc_date.Year = timeinfo.tm_year + 1900;
    rtc_date.Month = timeinfo.tm_mon + 1;
    rtc_date.Date = timeinfo.tm_mday;
    rtc_date.WeekDay = timeinfo.tm_wday;
    M5.rtc.SetDate(&rtc_date);
    Serial.printf("TIME synced RTC from NTP: %04d-%02d-%02d %02d:%02d:%02d\n",
                  rtc_date.Year,
                  rtc_date.Month,
                  rtc_date.Date,
                  rtc_time.Hours,
                  rtc_time.Minutes,
                  rtc_time.Seconds);
}

void AppState::fetchBasementStatusIfDue(bool force)
{
    basement_.configured = hasText(app_config::kBeelinkLhmUrl);
    const uint32_t now = millis();
    if (!force && basement_.last_attempt_ms != 0 && (now - basement_.last_attempt_ms) < app_config::kNetworkRefreshMs)
    {
        return;
    }
    basement_.last_attempt_ms = now;

    if (!basement_.configured || WiFi.status() != WL_CONNECTED)
    {
        noteBasementFailure(basement_.configured ? "wifi offline" : "endpoint not configured");
        Serial.printf("BEELINK skipped: %s\n", basement_.error);
        return;
    }

    HTTPClient http;
    http.setTimeout(app_config::kHttpTimeoutMs);
    if (!http.begin(app_config::kBeelinkLhmUrl))
    {
        noteBasementFailure("HTTP begin failed");
        Serial.println("BEELINK HTTP begin failed");
        return;
    }
    http.addHeader("x-guardian-telemetry-token", app_config::kAuthToken);

    basement_.http_code = http.GET();
    if (basement_.http_code != HTTP_CODE_OK)
    {
        char error[32] = {};
        snprintf(error, sizeof(error), "HTTP %d", basement_.http_code);
        noteBasementFailure(error, basement_.http_code);
        if (basement_.http_code == HTTP_CODE_UNAUTHORIZED || basement_.http_code == HTTP_CODE_FORBIDDEN)
        {
            Serial.printf("supportFORGE authorization failed: HTTP %d\n", basement_.http_code);
        }
        Serial.printf("BEELINK HTTP error: %d\n", basement_.http_code);
        http.end();
        return;
    }

    StaticJsonDocument<3072> doc;
    doc.clear();
    const DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err)
    {
        char error[48] = {};
        snprintf(error, sizeof(error), "JSON %s", err.c_str());
        noteBasementFailure(error, basement_.http_code);
        Serial.printf("BEELINK JSON error: %s\n", err.c_str());
        return;
    }

    basement_.cpu_temp_f = doc["cpu_temp"].as<float>();
    basement_.nvme_temp_f = doc["nvme_temp"].as<float>();
    basement_.cpu_temp_c = (basement_.cpu_temp_f - 32.0f) * (5.0f / 9.0f);
    basement_.nvme_temp_c = (basement_.nvme_temp_f - 32.0f) * (5.0f / 9.0f);
    const long uptime_seconds = doc["uptime_seconds"].as<long>();
    const long uptime_days = uptime_seconds / 86400L;
    const long uptime_hours = (uptime_seconds / 3600L) % 24L;
    const long uptime_minutes = (uptime_seconds / 60L) % 60L;
    basement_.cpu_load = doc["cpu_load"].as<float>();
    basement_.memory_used = doc["ram_used_gb"].as<float>();
    basement_.memory_total = doc["ram_total_gb"].as<float>();

    basement_.disk_count = 0;
    for (uint8_t i = 0; i < BasementStatus::kMaxDisks; ++i)
    {
        basement_.disks[i] = BasementStatus::DiskStatus{};
    }
    JsonArrayConst disks = doc["disks"].as<JsonArrayConst>();
    for (JsonObjectConst disk : disks)
    {
        if (basement_.disk_count >= BasementStatus::kMaxDisks)
        {
            break;
        }

        BasementStatus::DiskStatus& target = basement_.disks[basement_.disk_count];
        const char* mount = disk["mount"] | disk["name"] | disk["drive"] | "";
        copyField(target.mount, sizeof(target.mount), mount);
        target.sizeBytes = disk["sizeBytes"] | disk["size_bytes"] | disk["totalBytes"] | disk["total_bytes"] | 0ULL;
        target.usedBytes = disk["usedBytes"] | disk["used_bytes"] | 0ULL;
        target.usedPercent = disk["usedPercent"] | disk["used_percent"] | 0.0f;
        ++basement_.disk_count;
    }

    snprintf(basement_.cpu, sizeof(basement_.cpu), "%.0f%%", static_cast<double>(basement_.cpu_load));
    snprintf(basement_.memory, sizeof(basement_.memory), "%.1fGB", static_cast<double>(basement_.memory_used));
    updateBeelinkTempFormats();
    snprintf(basement_.uptime,
             sizeof(basement_.uptime),
             "%ldd %ldh %ldm",
             uptime_days,
             uptime_hours,
             uptime_minutes);

    basement_.has_host = true;
    basement_.has_service = true;
    basement_.has_summary = true;
    copyField(basement_.host, sizeof(basement_.host), "Beelink");
    copyField(basement_.service, sizeof(basement_.service), "supportFORGE");
    copyField(basement_.summary, sizeof(basement_.summary), "supportFORGE API");
    basement_.has_cpu = true;
    basement_.has_memory = true;
    basement_.has_disk_c = false;
    basement_.has_disk_d = false;
    basement_.has_disk = true;
    basement_.has_uptime = true;
    setBasementOnline();
    basement_.last_success_ms = now;
    Serial.printf("BEELINK ok: online=%d cpu=%s mem=%s temps=%s uptime=%s\n",
                  basement_.online,
                  basement_.cpu,
                  basement_.memory,
                  basement_.disk,
                  basement_.uptime);
}

void AppState::noteBasementFailure(const char* error, int http_code)
{
    basement_.online = false;
    if (basement_.consecutive_failures < 255)
    {
        ++basement_.consecutive_failures;
    }
    basement_.alarm_active = basement_.consecutive_failures >= app_config::kBasementAlarmFailureThreshold;
    basement_.server_status = basement_.alarm_active ? ServerStatus::Offline : ServerStatus::Online;
    basement_.http_code = http_code;
    basement_.has_cpu = false;
    basement_.has_memory = false;
    basement_.has_disk = false;
    basement_.has_uptime = false;
    basement_.has_disk_c = false;
    basement_.has_disk_d = false;
    copyField(basement_.error, sizeof(basement_.error), hasText(error) ? error : "offline");
}

void AppState::setBasementOnline()
{
    basement_.online = true;
    basement_.server_status = ServerStatus::Online;
    basement_.alarm_muted = false;
    basement_.alarm_active = false;
    basement_.consecutive_failures = 0;
    copyField(basement_.error, sizeof(basement_.error), "");
}

void AppState::updateBeelinkTempFormats()
{
    const float cpu_temp = temp_is_f ? basement_.cpu_temp_f : basement_.cpu_temp_c;
    const float nvme_temp = temp_is_f ? basement_.nvme_temp_f : basement_.nvme_temp_c;
    const char unit = temp_is_f ? 'F' : 'C';
    snprintf(basement_.disk,
             sizeof(basement_.disk),
             "CPU:%d\xB0%c NV:%d\xB0%c",
             static_cast<int>(cpu_temp + (cpu_temp >= 0.0f ? 0.5f : -0.5f)),
             unit,
             static_cast<int>(nvme_temp + (nvme_temp >= 0.0f ? 0.5f : -0.5f)),
             unit);
}

void AppState::fetchWeatherIfDue(bool force)
{
    weather_.configured = true;
    weather_.gps_location = false;
    copyField(weather_.location, sizeof(weather_.location), app_config::kWeatherCity);

    if (WiFi.status() != WL_CONNECTED)
    {
        weather_.online = false;
        copyField(weather_.error, sizeof(weather_.error), "wifi offline");
        return;
    }

    const uint32_t now = millis();
    if (!force && weather_.last_attempt_ms != 0 && (now - weather_.last_attempt_ms) < app_config::kWeatherRefreshMs)
    {
        return;
    }
    weather_.last_attempt_ms = now;

    char url[220] = {};
    snprintf(url,
             sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code&temperature_unit=fahrenheit&timezone=auto",
             app_config::kFallbackWeatherLatitude,
             app_config::kFallbackWeatherLongitude);

    HTTPClient http;
    http.setTimeout(app_config::kHttpTimeoutMs);
    if (!http.begin(url))
    {
        weather_.online = false;
        copyField(weather_.error, sizeof(weather_.error), "HTTP begin failed");
        Serial.println("WEATHER HTTP begin failed");
        return;
    }

    weather_.http_code = http.GET();
    if (weather_.http_code != HTTP_CODE_OK)
    {
        weather_.online = false;
        snprintf(weather_.error, sizeof(weather_.error), "HTTP %d", weather_.http_code);
        Serial.printf("WEATHER HTTP error: %d\n", weather_.http_code);
        http.end();
        return;
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err)
    {
        weather_.online = false;
        snprintf(weather_.error, sizeof(weather_.error), "JSON %s", err.c_str());
        Serial.printf("WEATHER JSON error: %s\n", err.c_str());
        return;
    }

    JsonVariantConst current = doc["current"];
    if (!current["temperature_2m"].is<float>() || !current["weather_code"].is<int>())
    {
        weather_.online = false;
        copyField(weather_.error, sizeof(weather_.error), "missing current weather");
        Serial.println("WEATHER missing current weather fields");
        return;
    }

    weather_.temperature_f = current["temperature_2m"].as<float>();
    weather_.weather_code = current["weather_code"].as<int>();
    copyField(weather_.condition, sizeof(weather_.condition), weatherConditionFromCode(weather_.weather_code));
    copyField(weather_.error, sizeof(weather_.error), "");
    weather_.online = true;
    weather_.last_success_ms = now;
    Serial.printf("WEATHER ok: %.1fF code=%d condition=%s location=%s\n",
                  static_cast<double>(weather_.temperature_f),
                  weather_.weather_code,
                  weather_.condition,
                  weather_.location);
}
