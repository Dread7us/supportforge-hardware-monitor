#include <app_state.h>
#include <secrets.h>
#include <app_config.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5CoreInk.h>
#include <WiFi.h>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <esp_adc_cal.h>

namespace
{

constexpr int kBatteryAdcPin = 35;
constexpr float kBatteryDivider = 25.1f / 5.1f;
constexpr float kBatteryMinValidVoltage = 3.0f;
constexpr float kBatteryMaxValidVoltage = 4.35f;
constexpr float kBatteryEmptyVoltage = 3.30f;
constexpr float kBatteryFullVoltage = 4.15f;
constexpr uint32_t kRefreshIntervalsMs[] = {3000U, 5000U, 10000U, 60000U, 300000U};
constexpr uint8_t kRefreshIntervalCount = sizeof(kRefreshIntervalsMs) / sizeof(kRefreshIntervalsMs[0]);
constexpr uint32_t kChargeAnimationMs = 4000U;
constexpr float kChargeFloatVoltage = 4.12f;
constexpr float kChargeHysteresisVoltage = 4.05f;
constexpr float kChargeRiseThresholdV = 0.006f;
constexpr float kChargeFallThresholdV = -0.012f;
constexpr uint32_t kBatteryChargePercentStepMs = 90000U;
constexpr uint32_t kBatteryPollIntervalMs = 60000U;
constexpr uint8_t kBatteryClusterSamples = 5;
constexpr uint32_t kBatteryClusterDelayMs = 10U;
constexpr int kBatteryForceRefreshThresholdPercent = 5;
constexpr uint32_t kSpeedTestPollMs = 2000U;
constexpr uint32_t kSpeedTestAnimMs = 700U;

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
    if (voltage >= kBatteryFullVoltage)
    {
        return 100;
    }
    if (voltage <= kBatteryEmptyVoltage)
    {
        return 0;
    }
    return static_cast<int>(((voltage - kBatteryEmptyVoltage) * 100.0f / (kBatteryFullVoltage - kBatteryEmptyVoltage)) + 0.5f);
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

float firstFloat(JsonVariantConst json, const char* a, const char* b, const char* c = nullptr)
{
    if (a && !json[a].isNull()) return json[a].as<float>();
    if (b && !json[b].isNull()) return json[b].as<float>();
    if (c && !json[c].isNull()) return json[c].as<float>();
    return 0.0f;
}

bool firstBool(JsonVariantConst json, const char* a, const char* b = nullptr)
{
    if (a && !json[a].isNull()) return json[a].as<bool>();
    if (b && !json[b].isNull()) return json[b].as<bool>();
    return false;
}

const char* firstText(JsonVariantConst json, const char* a, const char* b = nullptr, const char* c = nullptr)
{
    if (a && json[a].is<const char*>()) return json[a].as<const char*>();
    if (b && json[b].is<const char*>()) return json[b].as<const char*>();
    if (c && json[c].is<const char*>()) return json[c].as<const char*>();
    return "";
}

void formatSpeedTestShortDate(const char* last_run, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (!hasText(last_run))
    {
        snprintf(out, out_len, "--/--");
        return;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (sscanf(last_run, "%d-%d-%d", &year, &month, &day) == 3 && month >= 1 && month <= 12 && day >= 1 && day <= 31)
    {
        snprintf(out, out_len, "%02d/%02d", month, day);
        return;
    }

    snprintf(out, out_len, "%.*s", static_cast<int>(out_len - 1), last_run);
}

void speedTestEndpoint(char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    char base[220] = {};
    snprintf(base, sizeof(base), "%s", app_config::kBeelinkLhmUrl);
    char* query = strchr(base, '?');
    if (query)
    {
        *query = '\0';
    }
    char* telemetry = strstr(base, "/telemetry");
    if (telemetry)
    {
        telemetry[1] = '\0';
        snprintf(out, out_len, "%sspeedtest?token=%s", base, app_config::kAuthToken);
        return;
    }
    snprintf(out, out_len, "%s/speedtest?token=%s", base, app_config::kAuthToken);
}

} // namespace

void AppState::begin()
{
    preferences.begin("forge", false);
    host_is_f_ = preferences.getBool("host_is_f", false);
    dash_is_f_ = preferences.getBool("dash_is_f", true);
    is_24h_ = preferences.getBool("is_24h", false);
    refresh_interval_ms = sanitizeRefreshInterval(preferences.getUInt("refresh_ms", app_config::kDefaultRefreshIntervalMs));
    basement_.configured = hasText(app_config::kBeelinkLhmUrl);
    weather_.configured = true;
    M5.Speaker.begin();
    M5.Speaker.mute();
    pinMode(SPEAKER_PIN, OUTPUT);
    digitalWrite(SPEAKER_PIN, LOW);
    copyField(weather_.location, sizeof(weather_.location), app_config::kWeatherCity);
    status_.wifi_configured = hasText(app_config::kWifiSsid);
    connectWifiIfNeeded();
    configureTimeIfNeeded();
    update();

    if (!background_task_started_)
    {
        background_task_started_ = xTaskCreatePinnedToCore(
                                       backgroundTask,
                                       "app-bg",
                                       8192,
                                       this,
                                       1,
                                       nullptr,
                                       0) == pdPASS;
    }
}

void AppState::update()
{
    serviceBackground();
}

void AppState::serviceBackground()
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
    updateSpeedTestPolling(now);
    updateChargeAnimation();
    last_status_update_ms_ = millis();
}

void AppState::backgroundTask(void* context)
{
    AppState* state = static_cast<AppState*>(context);
    while (state)
    {
        state->serviceBackground();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void AppState::updateChargeAnimation()
{
    const uint32_t now = millis();
    if (status_.charging && page_ == Page::Dashboard && (now - last_anim_toggle_) > kChargeAnimationMs)
    {
        charge_anim_phase_ = static_cast<uint8_t>((charge_anim_phase_ + 1U) % 3U);
        last_anim_toggle_ = now;
        charge_anim_display_changed_ = true;
    }
    else if (!status_.charging)
    {
        charge_anim_phase_ = 0;
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

bool AppState::triggerSpeedTest()
{
    basement_.configured = hasText(app_config::kBeelinkLhmUrl);
    if (!basement_.configured || WiFi.status() != WL_CONNECTED)
    {
        copyField(basement_.speedtest.error, sizeof(basement_.speedtest.error), basement_.configured ? "wifi offline" : "endpoint not set");
        speedtest_display_changed_ = true;
        return false;
    }

    char url[260] = {};
    speedTestEndpoint(url, sizeof(url));
    HTTPClient http;
    http.setTimeout(app_config::kHttpTimeoutMs);
    if (!http.begin(url))
    {
        copyField(basement_.speedtest.error, sizeof(basement_.speedtest.error), "HTTP begin failed");
        speedtest_display_changed_ = true;
        return false;
    }
    http.addHeader("x-guardian-telemetry-token", app_config::kAuthToken);
    basement_.speedtest.http_code = http.POST("");
    http.end();

    if (basement_.speedtest.http_code < 200 || basement_.speedtest.http_code >= 300)
    {
        snprintf(basement_.speedtest.error, sizeof(basement_.speedtest.error), "HTTP %d", basement_.speedtest.http_code);
        Serial.printf("SPEEDTEST trigger failed: HTTP %d\n", basement_.speedtest.http_code);
        speedtest_display_changed_ = true;
        return false;
    }

    basement_.speedtest.is_running = true;
    basement_.speedtest.trigger_pending = false;
    basement_.speedtest.anim_phase = 0;
    basement_.speedtest.error[0] = '\0';
    speedtest_was_running_ = true;
    last_speedtest_poll_ms_ = 0;
    last_speedtest_anim_ms_ = 0;
    speedtest_display_changed_ = true;
    fetchBasementStatusIfDue(true);
    Serial.printf("SPEEDTEST triggered: HTTP %d\n", basement_.speedtest.http_code);
    return true;
}

void AppState::toggleServerAlarmMuteIfOffline()
{
    if (basement_.server_status == ServerStatus::Offline || basement_.alarm_active)
    {
        muteAlarm();
    }
}

void AppState::updateServerAlarm()
{
    M5.Speaker.update();

    if (!alarm_.is_alarming || alarm_.is_muted)
    {
        return;
    }

    const uint32_t now = millis();
    if (alarm_.next_buzzer_ms == 0 || static_cast<int32_t>(now - alarm_.next_buzzer_ms) >= 0)
    {
        M5.Speaker.setVolume(2);
        M5.Speaker.tone(1800, app_config::kAlarmBeepMs);
        ++alarm_.buzzer_beeps_played;
        if (alarm_.buzzer_beeps_played >= app_config::kAlarmBeepsPerPattern)
        {
            alarm_.buzzer_beeps_played = 0;
            alarm_.next_buzzer_ms = now + app_config::kAlarmPatternPauseMs;
        }
        else
        {
            alarm_.next_buzzer_ms = now + app_config::kAlarmBeepMs + app_config::kAlarmBeepGapMs;
        }
    }
}

void AppState::muteAlarm()
{
    if (!basement_.alarm_active && !alarm_.is_alarming)
    {
        return;
    }
    alarm_.is_muted = true;
    basement_.alarm_muted = true;
    silenceAlarmBuzzer();
    Serial.println("BEELINK alarm muted");
}

void AppState::dismissAlarm()
{
    if (!basement_.alarm_active && !alarm_.is_alarming)
    {
        return;
    }
    alarm_.is_alarming = false;
    alarm_.is_muted = true;
    alarm_.is_dismissed = true;
    alarm_.snoozed_until_ms = 0;
    basement_.alarm_muted = true;
    silenceAlarmBuzzer();
    setPage(Page::Dashboard);
    Serial.println("BEELINK alarm dismissed");
}

void AppState::snoozeAlarm()
{
    if (!basement_.alarm_active && !alarm_.is_alarming)
    {
        return;
    }
    alarm_.is_alarming = false;
    alarm_.is_muted = true;
    alarm_.is_dismissed = false;
    alarm_.snoozed_until_ms = millis() + app_config::kAlarmSnoozeMs;
    basement_.alarm_muted = true;
    silenceAlarmBuzzer();
    setPage(Page::Dashboard);
    Serial.println("BEELINK alarm snoozed");
}

bool AppState::shouldShowAlarmReminder() const
{
    return basement_.alarm_active && !alarm_.is_alarming &&
           (alarm_.is_muted || alarm_.is_dismissed || alarm_.snoozed_until_ms != 0);
}

void AppState::toggleTempUnit()
{
    host_is_f_ = !host_is_f_;
    preferences.putBool("host_is_f", host_is_f_);
    updateBeelinkTempFormats();
}

void AppState::cycleDashboardDisplayPrefs()
{
    if (!is_24h_ && dash_is_f_)
    {
        is_24h_ = true;
        dash_is_f_ = true;
    }
    else if (is_24h_ && dash_is_f_)
    {
        is_24h_ = true;
        dash_is_f_ = false;
    }
    else if (is_24h_ && !dash_is_f_)
    {
        is_24h_ = false;
        dash_is_f_ = false;
    }
    else
    {
        is_24h_ = false;
        dash_is_f_ = true;
    }

    preferences.putBool("is_24h", is_24h_);
    preferences.putBool("dash_is_f", dash_is_f_);
}

uint32_t AppState::sanitizeRefreshInterval(uint32_t interval_ms) const
{
    for (uint8_t i = 0; i < kRefreshIntervalCount; ++i)
    {
        if (kRefreshIntervalsMs[i] == interval_ms)
        {
            return interval_ms;
        }
    }
    return app_config::kDefaultRefreshIntervalMs;
}

void AppState::cycleRefreshInterval()
{
    uint8_t current_index = 0;
    refresh_interval_ms = sanitizeRefreshInterval(refresh_interval_ms);
    for (uint8_t i = 0; i < kRefreshIntervalCount; ++i)
    {
        if (kRefreshIntervalsMs[i] == refresh_interval_ms)
        {
            current_index = i;
            break;
        }
    }
    refresh_interval_ms = kRefreshIntervalsMs[(current_index + 1U) % kRefreshIntervalCount];
    preferences.putUInt("refresh_ms", refresh_interval_ms);
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

bool AppState::consumeAlarmDisplayChanged()
{
    const bool changed = alarm_display_changed_;
    alarm_display_changed_ = false;
    return changed;
}

bool AppState::consumeAlarmAutoDismissed()
{
    const bool changed = alarm_auto_dismissed_;
    alarm_auto_dismissed_ = false;
    return changed;
}

bool AppState::consumeSpeedTestDisplayChanged()
{
    const bool changed = speedtest_display_changed_;
    speedtest_display_changed_ = false;
    return changed;
}

void AppState::consumeDisplayChangeFlags()
{
    battery_display_changed_ = false;
    charge_anim_display_changed_ = false;
    alarm_display_changed_ = false;
    alarm_auto_dismissed_ = false;
    speedtest_display_changed_ = false;
}

bool AppState::shouldFullClear() const
{
    return app_config::kFullClearEveryRefreshes > 0 &&
           status_.refresh_count > 0 &&
           (status_.refresh_count % app_config::kFullClearEveryRefreshes) == 0;
}

void AppState::readBattery()
{
    const uint32_t read_start_ms = millis();
    if (last_battery_read_ms_ != 0 && (read_start_ms - last_battery_read_ms_) < kBatteryPollIntervalMs)
    {
        return;
    }
    last_battery_read_ms_ = read_start_ms;

    analogSetPinAttenuation(kBatteryAdcPin, ADC_11db);
    esp_adc_cal_characteristics_t adc_chars{};
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 3600, &adc_chars);

    uint32_t cluster_raw_total = 0;
    uint32_t cluster_mv_total = 0;
    float cluster_voltage_total = 0.0f;

    for (uint8_t cluster = 0; cluster < kBatteryClusterSamples; ++cluster)
    {
        uint32_t raw_total = 0;
        uint16_t raw_min = UINT16_MAX;
        uint16_t raw_max = 0;
        constexpr int samples = 8;
        for (int i = 0; i < samples; ++i)
        {
            const uint16_t sample = analogRead(kBatteryAdcPin);
            raw_total += sample;
            raw_min = std::min(raw_min, sample);
            raw_max = std::max(raw_max, sample);
            delay(1);
        }

        const uint32_t raw_sample = (raw_total - raw_min - raw_max) / (samples - 2);
        const uint32_t millivolts_sample = esp_adc_cal_raw_to_voltage(raw_sample, &adc_chars);
        cluster_raw_total += raw_sample;
        cluster_mv_total += millivolts_sample;
        cluster_voltage_total += static_cast<float>(millivolts_sample) * kBatteryDivider / 1000.0f;

        if (cluster + 1U < kBatteryClusterSamples)
        {
            delay(kBatteryClusterDelayMs);
        }
    }

    const uint32_t raw = cluster_raw_total / kBatteryClusterSamples;
    const uint32_t millivolts = cluster_mv_total / kBatteryClusterSamples;
    status_.battery_raw_adc = static_cast<uint16_t>(raw);
    status_.battery_adc_mv = static_cast<uint16_t>(millivolts);
    status_.battery_voltage = cluster_voltage_total / static_cast<float>(kBatteryClusterSamples);
    status_.battery_valid = false;
    status_.battery_percent = -1;
    float filtered_voltage = status_.battery_voltage;

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
        battery_voltage_samples_[battery_voltage_sample_index_] = status_.battery_voltage;
        battery_voltage_sample_index_ = static_cast<uint8_t>((battery_voltage_sample_index_ + 1U) % kBatteryVoltageWindowSize);
        if (battery_voltage_sample_count_ < kBatteryVoltageWindowSize)
        {
            ++battery_voltage_sample_count_;
        }

        float filtered_voltage_total = 0.0f;
        for (uint8_t i = 0; i < battery_voltage_sample_count_; ++i)
        {
            filtered_voltage_total += battery_voltage_samples_[i];
        }
        filtered_voltage = filtered_voltage_total / static_cast<float>(battery_voltage_sample_count_);

        status_.battery_percent = percentFromVoltage(filtered_voltage);
        if (status_.battery_percent > 100) status_.battery_percent = 100;
        status_.battery_valid = status_.battery_percent >= 0;
        copyField(status_.battery_fault, sizeof(status_.battery_fault), status_.battery_valid ? "" : "BAT PCT INVALID");
    }
    if (!status_.battery_valid)
    {
        battery_voltage_sample_count_ = 0;
        battery_voltage_sample_index_ = 0;
        last_forced_battery_percentage_ = -2;
        last_charge_percent_increment_ms_ = 0;
    }
    status_.battery_state = batteryStateFromPercent(status_.battery_percent);

    // This M5CoreInk library does not expose a PMIC/USB-power API, so charging
    // is inferred from the filtered battery ADC with a hybrid of trend and USB
    // float voltage. The float check keeps charging true during the CV plateau,
    // while hysteresis prevents flicker until voltage drops far enough to imply
    // USB power has been removed or the pack is no longer being held up.
    const bool was_charging = status_.charging;
    static bool has_voltage_sample = false;
    static float previous_voltage = 0.0f;
    const float trend_voltage = status_.battery_valid ? filtered_voltage : status_.battery_voltage;
    const float voltage_delta = has_voltage_sample ? (trend_voltage - previous_voltage) : 0.0f;
    if (status_.battery_valid)
    {
        const bool active_rise = has_voltage_sample && voltage_delta >= kChargeRiseThresholdV;
        const bool elevated_float = filtered_voltage >= kChargeFloatVoltage;
        const bool below_hysteresis = filtered_voltage < kChargeHysteresisVoltage &&
                                      status_.battery_voltage < kChargeHysteresisVoltage;
        const bool falling_like_unplug = has_voltage_sample && voltage_delta <= kChargeFallThresholdV;

        if (active_rise || elevated_float)
        {
            status_.charging = true;
        }
        else if (status_.charging && (below_hysteresis || falling_like_unplug))
        {
            status_.charging = false;
        }
        previous_voltage = trend_voltage;
        has_voltage_sample = true;
    }
    else
    {
        status_.charging = false;
        has_voltage_sample = false;
    }

    // Keep the internal state tied to the latest averaged reading. The old
    // displayed-percent throttle intentionally delayed upward movement while
    // charging, but it also made unplugged discharge appear stuck until a power
    // state transition. Redraw throttling is handled below instead.
    last_charge_percent_increment_ms_ = status_.charging ? last_charge_percent_increment_ms_ : 0;
    status_.battery_state = batteryStateFromPercent(status_.battery_percent);

    const int current_battery_percentage = status_.battery_percent;
    const bool is_charging = status_.charging;
    if (!has_battery_display_sample_)
    {
        last_battery_percentage_ = current_battery_percentage;
        last_forced_battery_percentage_ = current_battery_percentage;
        last_charging_ = is_charging;
        has_battery_display_sample_ = true;
    }
    else if (current_battery_percentage != last_battery_percentage_ || is_charging != last_charging_)
    {
        const bool significant_battery_drop = current_battery_percentage >= 0 &&
                                             last_forced_battery_percentage_ >= 0 &&
                                             (last_forced_battery_percentage_ - current_battery_percentage) >= kBatteryForceRefreshThresholdPercent;
        battery_display_changed_ = battery_display_changed_ || is_charging != last_charging_ || significant_battery_drop;
        last_battery_percentage_ = current_battery_percentage;
        if (battery_display_changed_)
        {
            last_forced_battery_percentage_ = current_battery_percentage;
        }
        last_charging_ = is_charging;
    }

    if (was_charging != status_.charging)
    {
        Serial.printf("BAT charging=%d\n", status_.charging);
    }

    static uint32_t last_log_ms = 0;
    if (last_log_ms == 0 || millis() - last_log_ms > 30000U)
    {
        Serial.printf("BAT raw=%u adc_mv=%u pack=%.2f filt=%.2f delta=%.3f pct=%d state=%u charging=%d\n",
                      status_.battery_raw_adc,
                      status_.battery_adc_mv,
                      static_cast<double>(status_.battery_voltage),
                      static_cast<double>(trend_voltage),
                      static_cast<double>(voltage_delta),
                      status_.battery_percent,
                      static_cast<unsigned>(status_.battery_state),
                      status_.charging);
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
        if (is_24h_)
        {
            snprintf(status_.time_text, sizeof(status_.time_text), "%02d:%02d", rtc_time.Hours, rtc_time.Minutes);
            snprintf(status_.meridiem_text, sizeof(status_.meridiem_text), "");
        }
        else
        {
            const int hour12 = (rtc_time.Hours % 12) == 0 ? 12 : (rtc_time.Hours % 12);
            snprintf(status_.time_text, sizeof(status_.time_text), "%d:%02d", hour12, rtc_time.Minutes);
            snprintf(status_.meridiem_text, sizeof(status_.meridiem_text), "%s", rtc_time.Hours >= 12 ? "PM" : "AM");
        }
        snprintf(status_.second_text, sizeof(status_.second_text), "%02d", rtc_time.Seconds);
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
    if (!force && basement_.last_attempt_ms != 0 && (now - basement_.last_attempt_ms) < refresh_interval_ms)
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

    if (!was_host_connected_)
    {
        playConnectionChime();
        was_host_connected_ = true;
    }

    const bool should_auto_dismiss_alarm = basement_.alarm_active || alarm_.is_alarming || alarm_.is_muted || alarm_.is_dismissed || alarm_.snoozed_until_ms != 0;
    if (should_auto_dismiss_alarm)
    {
        resetAlarmState();
        basement_.consecutive_failures = 0;
        alarm_auto_dismissed_ = true;
        alarm_display_changed_ = true;
        Serial.println("BEELINK alarm auto-dismissed: HTTP 200 restored");
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
    parseSpeedTest(doc["speedtest"]);

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

    const char* telemetry_host = firstText(doc, "hostname", "host", "device_name");
    if (!hasText(telemetry_host))
    {
        telemetry_host = firstText(doc, "deviceName", "name", "machine");
    }
    if (!hasText(telemetry_host))
    {
        telemetry_host = app_config::kTargetHostName;
    }

    basement_.has_host = hasText(telemetry_host);
    basement_.has_service = true;
    basement_.has_summary = true;
    copyField(basement_.host, sizeof(basement_.host), telemetry_host);
    copyField(basement_.service, sizeof(basement_.service), app_config::kAppName);
    copyField(basement_.summary, sizeof(basement_.summary), app_config::kAppName);
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

void AppState::parseSpeedTest(JsonVariantConst speedtest)
{
    const bool was_running = basement_.speedtest.is_running;
    if (speedtest.isNull())
    {
        basement_.speedtest.is_running = false;
        basement_.speedtest.trigger_pending = false;
        if (was_running)
        {
            speedtest_display_changed_ = true;
        }
        return;
    }

    basement_.speedtest.is_running = firstBool(speedtest, "is_running", "running");
    basement_.speedtest.download_mbps = speedtest["down"].as<float>();
    basement_.speedtest.upload_mbps = speedtest["up"].as<float>();
    basement_.speedtest.ping_ms = firstFloat(speedtest, "ping_ms", "pingMs", "ping");

    const char* last_run = firstText(speedtest, "last_run", "lastRun", "timestamp");
    if (!hasText(last_run))
    {
        last_run = firstText(speedtest, "last_run_at", "completed_at", "created_at");
    }
    copyField(basement_.speedtest.last_run, sizeof(basement_.speedtest.last_run), last_run);
    formatSpeedTestShortDate(basement_.speedtest.last_run, basement_.speedtest.last_run_short, sizeof(basement_.speedtest.last_run_short));

    const bool has_metrics = basement_.speedtest.download_mbps > 0.0f || basement_.speedtest.upload_mbps > 0.0f || basement_.speedtest.ping_ms > 0.0f;
    basement_.speedtest.has_result = has_metrics || hasText(basement_.speedtest.last_run);
    if (basement_.speedtest.has_result || basement_.speedtest.is_running)
    {
        basement_.speedtest.error[0] = '\0';
    }

    if (was_running != basement_.speedtest.is_running)
    {
        speedtest_display_changed_ = true;
    }
    if (was_running && !basement_.speedtest.is_running)
    {
        speedtest_was_running_ = false;
        playMarioCoinChime();
    }
}

void AppState::updateSpeedTestPolling(uint32_t now)
{
    if (!basement_.speedtest.is_running)
    {
        return;
    }

    if (last_speedtest_anim_ms_ == 0 || (now - last_speedtest_anim_ms_) >= kSpeedTestAnimMs)
    {
        basement_.speedtest.anim_phase = static_cast<uint8_t>((basement_.speedtest.anim_phase + 1U) % 4U);
        last_speedtest_anim_ms_ = now;
        speedtest_display_changed_ = true;
    }

    if (last_speedtest_poll_ms_ == 0 || (now - last_speedtest_poll_ms_) >= kSpeedTestPollMs)
    {
        last_speedtest_poll_ms_ = now;
        fetchBasementStatusIfDue(true);
    }
}

void AppState::noteBasementFailure(const char* error, int http_code)
{
    was_host_connected_ = false;
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

    if (basement_.alarm_active)
    {
        const uint32_t now = millis();
        const bool snoozed = alarm_.snoozed_until_ms != 0 && static_cast<int32_t>(now - alarm_.snoozed_until_ms) < 0;
        if (!alarm_.is_alarming && !alarm_.is_dismissed && !snoozed)
        {
            triggerAlarm("BEELINK OFFLINE", basement_.error);
        }
    }
}

void AppState::setBasementOnline()
{
    const bool was_alarm_active = basement_.alarm_active || alarm_.is_alarming || alarm_.is_muted || alarm_.is_dismissed || alarm_.snoozed_until_ms != 0;
    basement_.online = true;
    basement_.server_status = ServerStatus::Online;
    resetAlarmState();
    basement_.consecutive_failures = 0;
    copyField(basement_.error, sizeof(basement_.error), "");
    if (was_alarm_active)
    {
        alarm_auto_dismissed_ = true;
        alarm_display_changed_ = true;
        Serial.println("BEELINK alarm auto-dismissed: HTTP 200 restored");
    }
}

void AppState::triggerAlarm(const char* title, const char* details)
{
    resetAlarmBuzzerForNewIncident();

    if (page_ != Page::Alarm)
    {
        page_before_alarm_ = page_;
    }
    copyField(alarm_.error_title, sizeof(alarm_.error_title), hasText(title) ? title : "ALARM");
    copyField(alarm_.error_details, sizeof(alarm_.error_details), hasText(details) ? details : "Attention required");
    alarm_.is_alarming = true;
    alarm_.is_muted = false;
    alarm_.is_dismissed = false;
    alarm_.snoozed_until_ms = 0;
    alarm_.buzzer_beeps_played = 0;
    alarm_.next_buzzer_ms = 0;
    basement_.alarm_muted = false;
    setPage(Page::Alarm);
    alarm_display_changed_ = true;
    Serial.printf("ALARM triggered: %s - %s\n", alarm_.error_title, alarm_.error_details);
}

void AppState::resetAlarmBuzzerForNewIncident()
{
    // A previous mute/dismiss/snooze path may have forced the speaker pin LOW
    // and detached the LEDC channel. Rebuild the buzzer path for each fresh
    // incident so the next alarm cannot inherit an unresponsive hardware state.
    M5.Speaker.end();
    pinMode(SPEAKER_PIN, OUTPUT);
    digitalWrite(SPEAKER_PIN, LOW);
    M5.Speaker.begin();
    M5.Speaker.setVolume(2);
}

void AppState::playConnectionChime()
{
    M5.Speaker.end();
    pinMode(SPEAKER_PIN, OUTPUT);
    digitalWrite(SPEAKER_PIN, LOW);
    M5.Speaker.begin();
    M5.Speaker.setVolume(2);
    M5.Speaker.tone(988, 80);
    delay(80);
    M5.Speaker.tone(1319, 150);
    delay(150);
    M5.Speaker.end();
    pinMode(SPEAKER_PIN, OUTPUT);
    digitalWrite(SPEAKER_PIN, LOW);
}

void AppState::playMarioCoinChime()
{
    M5.Speaker.end();
    pinMode(SPEAKER_PIN, OUTPUT);
    digitalWrite(SPEAKER_PIN, LOW);
    M5.Speaker.begin();
    M5.Speaker.setVolume(2);
    M5.Speaker.tone(1319, 70);
    delay(75);
    M5.Speaker.tone(1760, 90);
    delay(100);
    M5.Speaker.end();
    pinMode(SPEAKER_PIN, OUTPUT);
    digitalWrite(SPEAKER_PIN, LOW);
}

void AppState::silenceAlarmBuzzer()
{
    M5.Speaker.end();
    pinMode(SPEAKER_PIN, OUTPUT);
    digitalWrite(SPEAKER_PIN, LOW);
    alarm_.buzzer_beeps_played = 0;
    alarm_.next_buzzer_ms = 0;
}

void AppState::resetAlarmState()
{
    const bool was_showing_alarm = page_ == Page::Alarm;
    silenceAlarmBuzzer();

    // Network recovery marks the end of the current offline incident. Clear
    // every suppression flag explicitly so mute/dismiss/snooze state cannot
    // leak into the next independent offline incident.
    alarm_.is_muted = false;
    alarm_.is_dismissed = false;
    alarm_.snoozed_until_ms = 0;
    alarm_.is_alarming = false;
    alarm_.buzzer_beeps_played = 0;
    alarm_.next_buzzer_ms = 0;
    alarm_.error_title[0] = '\0';
    alarm_.error_details[0] = '\0';

    basement_.alarm_muted = false;
    basement_.alarm_active = false;
    if (was_showing_alarm)
    {
        setPage(page_before_alarm_ == Page::Alarm ? Page::Dashboard : page_before_alarm_);
    }
}

void AppState::updateBeelinkTempFormats()
{
    const float cpu_temp = host_is_f_ ? basement_.cpu_temp_f : basement_.cpu_temp_c;
    const float nvme_temp = host_is_f_ ? basement_.nvme_temp_f : basement_.nvme_temp_c;
    const char unit = host_is_f_ ? 'F' : 'C';
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
              "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code&temperature_unit=celsius&timezone=auto",
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

    weather_.temperature_c = current["temperature_2m"].as<float>();
    weather_.temperature_f = (weather_.temperature_c * 9.0f / 5.0f) + 32.0f;
    weather_.weather_code = current["weather_code"].as<int>();
    copyField(weather_.condition, sizeof(weather_.condition), weatherConditionFromCode(weather_.weather_code));
    copyField(weather_.error, sizeof(weather_.error), "");
    weather_.online = true;
    weather_.last_success_ms = now;
    Serial.printf("WEATHER ok: %.1fC %.1fF code=%d condition=%s location=%s\n",
                  static_cast<double>(weather_.temperature_c),
                  static_cast<double>(weather_.temperature_f),
                  weather_.weather_code,
                  weather_.condition,
                  weather_.location);
}
