#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <cstdint>

enum class Page : uint8_t
{
    Dashboard = 0,
    Beelink,
    Clock,
    System,
    Network,
    Power,
    Sleep,
    Count,
    Alarm,
    BeelinkCpuDetail,
    BeelinkMemDetail,
    BeelinkTempDetail,
    BeelinkUptimeDetail,
};

enum class BatteryState : uint8_t
{
    Unknown = 0,
    Critical,
    Low,
    Ok,
    Full,
};

enum class TimeSource : uint8_t
{
    None = 0,
    RtcClock,
    Ntp,
};

enum class ServerStatus : uint8_t
{
    Offline = 0,
    Online,
};

struct DeviceStatus
{
    uint32_t uptime_ms = 0;
    uint32_t free_heap = 0;
    int battery_percent = -1;
    float battery_voltage = 0.0f;
    uint16_t battery_raw_adc = 0;
    uint16_t battery_adc_mv = 0;
    BatteryState battery_state = BatteryState::Unknown;
    bool battery_supported = true;
    bool battery_valid = false;
    char battery_fault[32] = "not read";
    bool charging = false;
    bool wifi_configured = false;
    bool wifi_connected = false;
    int wifi_rssi = 0;
    int wifi_strength = 0;
    char wifi_ssid[33] = "";
    char wifi_ip[16] = "0.0.0.0";
    int wifi_scan_count = -1;
    bool bluetooth_supported = true;
    bool bluetooth_enabled = false;
    bool time_synced = false;
    bool rtc_valid = false;
    TimeSource time_source = TimeSource::None;
    char time_text[6] = "--:--";
    char second_text[3] = "--";
    char meridiem_text[3] = "";
    char date_text[12] = "NO DATE";
    char rtc_fault[32] = "not read";
    uint32_t last_ntp_sync_ms = 0;
    uint8_t refresh_count = 0;
};

struct WeatherStatus
{
    bool configured = true;
    bool online = false;
    bool gps_location = false;
    int http_code = 0;
    uint32_t last_attempt_ms = 0;
    uint32_t last_success_ms = 0;
    float temperature_c = 0.0f;
    float temperature_f = 0.0f;
    int weather_code = -1;
    char condition[18] = "WAITING";
    char location[24] = "";
    char error[48] = "not fetched";
};

struct BasementStatus
{
    bool configured = false;
    bool online = false;
    ServerStatus server_status = ServerStatus::Offline;
    bool alarm_muted = false;
    bool alarm_active = false;
    uint8_t consecutive_failures = 0;
    int http_code = 0;
    uint32_t last_attempt_ms = 0;
    uint32_t last_success_ms = 0;
    bool has_host = false;
    bool has_service = false;
    bool has_summary = false;
    bool has_cpu = false;
    bool has_memory = false;
    bool has_disk = false;
    bool has_uptime = false;
    bool has_disk_c = false;
    bool has_disk_d = false;
    char host[24] = "";
    char service[24] = "";
    char summary[48] = "";
    char cpu[12] = "";
    char memory[12] = "";
    char disk[32] = "";
    char uptime[32] = "";
    char disk_c[12] = "";
    char disk_d[12] = "";
    char error[48] = "not fetched";
    float cpu_load = 0.0f;
    float cpu_temp_c = 0.0f;
    float cpu_temp_f = 0.0f;
    float nvme_temp_c = 0.0f;
    float nvme_temp_f = 0.0f;
    float memory_used = 0.0f;
    float memory_total = 0.0f;
    uint8_t beelink_cursor = 0;

    struct DiskStatus
    {
        char mount[16] = "";
        uint64_t sizeBytes = 0;
        uint64_t usedBytes = 0;
        float usedPercent = 0.0f;
    };

    static constexpr uint8_t kMaxDisks = 3;
    DiskStatus disks[kMaxDisks]{};
    uint8_t disk_count = 0;
};

struct AlarmStatus
{
    bool is_alarming = false;
    bool is_muted = false;
    bool is_dismissed = false;
    uint32_t snoozed_until_ms = 0;
    char error_title[32] = "";
    char error_details[64] = "";
    uint8_t buzzer_beeps_played = 0;
    uint32_t next_buzzer_ms = 0;
};

class AppState
{
  public:
    void begin();
    void update();
    void serviceBackground();
    void updateChargeAnimation();

    Page page() const { return page_; }
    const DeviceStatus& status() const { return status_; }
    const BasementStatus& basement() const { return basement_; }
    BasementStatus& basement() { return basement_; }
    const WeatherStatus& weather() const { return weather_; }
    const AlarmStatus& alarm() const { return alarm_; }
    uint32_t lastStatusUpdateMs() const { return last_status_update_ms_; }
    uint32_t refresh_interval_ms = 60000;

    void nextPage();
    void previousPage();
    void setPage(Page page);
    void noteRefresh();
    void forceNetworkRefresh();
    void toggleServerAlarmMuteIfOffline();
    void updateServerAlarm();
    void muteAlarm();
    void dismissAlarm();
    void snoozeAlarm();
    bool isAlarmActive() const { return alarm_.is_alarming; }
    bool shouldShowAlarmReminder() const;
    void toggleTempUnit();
    bool hostTempIsF() const { return host_is_f_; }
    bool dashboardTempIsF() const { return dash_is_f_; }
    bool is24HourTime() const { return is_24h_; }
    void cycleDashboardDisplayPrefs();
    void cycleRefreshInterval();
    bool consumeBatteryDisplayChanged();
    uint8_t chargeAnimPhase() const { return charge_anim_phase_; }
    bool consumeChargeAnimDisplayChanged();
    bool consumeAlarmDisplayChanged();
    bool consumeAlarmAutoDismissed();
    void consumeDisplayChangeFlags();

    bool shouldFullClear() const;

  private:
    void connectWifiIfNeeded();
    void configureTimeIfNeeded();
    void readBattery();
    void readTime();
    void syncNetworkTimeIfDue(bool force = false);
    void readWireless();
    void scanWifiIfDue(bool force = false);
    void fetchBasementStatusIfDue(bool force = false);
    void fetchWeatherIfDue(bool force = false);
    void noteBasementFailure(const char* error, int http_code = 0);
    void setBasementOnline();
    void triggerAlarm(const char* title, const char* details);
    void resetAlarmBuzzerForNewIncident();
    void playConnectionChime();
    void silenceAlarmBuzzer();
    void resetAlarmState();
    void updateBeelinkTempFormats();
    uint32_t sanitizeRefreshInterval(uint32_t interval_ms) const;
    void syncRtcFromSystemTime(const tm& timeinfo);
    static void backgroundTask(void* context);

    Page page_ = Page::Dashboard;
    Page page_before_alarm_ = Page::Dashboard;
    Preferences preferences;
    bool host_is_f_ = false;
    bool dash_is_f_ = true;
    bool is_24h_ = false;
    DeviceStatus status_{};
    BasementStatus basement_{};
    WeatherStatus weather_{};
    AlarmStatus alarm_{};
    int last_battery_percentage_ = -2;
    bool last_charging_ = false;
    bool has_battery_display_sample_ = false;
    bool battery_display_changed_ = false;
    static constexpr uint8_t kBatteryVoltageWindowSize = 8;
    float battery_voltage_samples_[kBatteryVoltageWindowSize]{};
    uint8_t battery_voltage_sample_count_ = 0;
    uint8_t battery_voltage_sample_index_ = 0;
    int last_forced_battery_percentage_ = -2;
    uint32_t last_charge_percent_increment_ms_ = 0;
    uint8_t charge_anim_phase_ = 0;
    bool charge_anim_display_changed_ = false;
    bool alarm_display_changed_ = false;
    bool alarm_auto_dismissed_ = false;
    bool background_task_started_ = false;
    bool was_host_connected_ = false;
    unsigned long last_anim_toggle_ = 0;
    uint32_t last_status_update_ms_ = 0;
    uint32_t last_wifi_attempt_ms_ = 0;
    uint32_t last_wifi_scan_ms_ = 0;
    uint32_t last_time_sync_attempt_ms_ = 0;
    uint32_t last_battery_read_ms_ = 0;
    bool time_configured_ = false;
};
