#include "ui_pages.h"

#include "app_config.h"
#include "display.h"
#include "ui_theme.h"

#include <M5CoreInk.h>
#include <WiFi.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace
{

void text(int x, int y, const char* value, uint8_t size = 1)
{
    coreink_gfx::drawText(x, y, value, size >= 3 ? coreink_gfx::Font::Large : coreink_gfx::Font::Small);
}

int fontWidth(coreink_gfx::Font font)
{
    return font == coreink_gfx::Font::Large ? 24 : 8;
}

int textWidth(const char* value, coreink_gfx::Font font)
{
    return value ? static_cast<int>(strlen(value)) * fontWidth(font) : 0;
}

bool isUpperAscii(char ch)
{
    return ch >= 'A' && ch <= 'Z';
}

int clippedLengthAvoidPartialStateSuffix(const char* value, int max_chars)
{
    if (!value || max_chars <= 0)
    {
        return 0;
    }

    const int len = static_cast<int>(strlen(value));
    if (len <= max_chars)
    {
        return len;
    }

    int clipped_len = max_chars;

    // Avoid rendering a dangling one-letter state/province suffix such as
    // "Example O" when the full value is "Example OR". If clipping lands
    // inside a trailing two-letter uppercase token, drop that whole token.
    const int token_start = clipped_len - 2;
    if (token_start >= 0 &&
        value[token_start] == ' ' &&
        isUpperAscii(value[token_start + 1]) &&
        isUpperAscii(value[token_start + 2]) &&
        value[token_start + 3] == '\0')
    {
        clipped_len = token_start;
    }

    while (clipped_len > 0 && value[clipped_len - 1] == ' ')
    {
        --clipped_len;
    }
    return clipped_len;
}

void drawTextClipped(int x, int y, const char* value, coreink_gfx::Font font, int max_width, bool inverse = false)
{
    if (!value || max_width <= 0)
    {
        return;
    }

    const int max_chars = max_width / fontWidth(font);
    if (max_chars <= 0)
    {
        return;
    }

    char buffer[64] = {};
    snprintf(buffer, sizeof(buffer), "%.*s", max_chars, value);
    coreink_gfx::drawText(x, y, buffer, font, inverse);
}

void drawLocationClipped(int x, int y, const char* value, coreink_gfx::Font font, int max_width, bool inverse = false)
{
    if (!value || max_width <= 0)
    {
        return;
    }

    const int max_chars = max_width / fontWidth(font);
    const int fit_chars = clippedLengthAvoidPartialStateSuffix(value, max_chars);
    if (fit_chars <= 0)
    {
        return;
    }

    char buffer[64] = {};
    snprintf(buffer, sizeof(buffer), "%.*s", fit_chars, value);
    coreink_gfx::drawText(x, y, buffer, font, inverse);
}

void drawTextRightAligned(int right_x, int y, const char* value, coreink_gfx::Font font, int max_width, bool inverse = false)
{
    if (!value || max_width <= 0)
    {
        return;
    }

    const int width = textWidth(value, font);
    const int clipped_width = width > max_width ? max_width : width;
    drawTextClipped(right_x - clipped_width, y, value, font, max_width, inverse);
}

void textf(int x, int y, uint8_t size, const char* format, ...)
{
    char buffer[64] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    text(x, y, buffer, size);
}

void wifiIcon(int x, int y, const DeviceStatus& s, bool inverse)
{
    for (int i = 0; i < 4; ++i)
    {
        const int bar_h = 3 + (i * 3);
        const int bar_x = x + (i * 4);
        const int bar_y = y + 12 - bar_h;
        coreink_gfx::drawRect(bar_x, bar_y, 3, bar_h, inverse);
        if (s.wifi_connected && i < s.wifi_strength)
        {
            coreink_gfx::fillRect(bar_x + 1, bar_y + 1, 1, bar_h - 2, inverse);
        }
    }
    if (!s.wifi_connected)
    {
        coreink_gfx::drawHLine(x, y + 12, 15, inverse);
        coreink_gfx::drawHLine(x + 3, y + 9, 9, inverse);
    }
}

void batteryIcon(int x, int y, const DeviceStatus& s, bool inverse)
{
    coreink_gfx::drawRect(x, y + 2, 18, 10, inverse);
    coreink_gfx::fillRect(x + 18, y + 5, 2, 4, inverse);
    if (!s.battery_valid)
    {
        coreink_gfx::drawHLine(x + 3, y + 7, 12, inverse);
        return;
    }
    const int percent = s.battery_percent;
    const int fill = (14 * percent) / 100;
    if (fill > 0)
    {
        coreink_gfx::fillRect(x + 2, y + 4, fill, 6, inverse);
    }
}

void drawTextCentered(int center_x, int y, const char* value, coreink_gfx::Font font, int max_width, bool inverse = false);

int wifiBarsFromRssi(int rssi)
{
    if (rssi > -60) return 4;
    if (rssi > -70) return 3;
    if (rssi > -80) return 2;
    if (rssi > -90) return 1;
    return 0;
}

void dashboardWifiIndicator(int x, int y)
{
    const int rssi = WiFi.RSSI();
    const int bars = WiFi.status() == WL_CONNECTED ? wifiBarsFromRssi(rssi) : 0;
    for (int i = 0; i < 4; ++i)
    {
        const int bar_w = 4;
        const int bar_h = 4 + (i * 4);
        const int bar_x = x + (i * 6);
        const int bar_y = y + 16 - bar_h;
        coreink_gfx::drawRect(bar_x, bar_y, bar_w, bar_h, true);
        if (i < bars)
        {
            coreink_gfx::fillRect(bar_x + 1, bar_y + 1, bar_w - 2, bar_h - 2, true);
        }
    }
}

int clampPercent(int percent)
{
    if (percent < 0) return 0;
    if (percent > 100) return 100;
    return percent;
}

void dashboardBatteryIndicator(int x, int y, const DeviceStatus& s)
{
    constexpr int body_w = 34;
    constexpr int body_h = 16;
    constexpr int terminal_w = 3;
    constexpr int terminal_h = 6;
    constexpr int inner_pad = 1;
    constexpr int pct_y = 3;

    char pct[8] = {};
    if (s.battery_valid)
    {
        snprintf(pct, sizeof(pct), "%d%%", clampPercent(s.battery_percent));
    }
    else
    {
        snprintf(pct, sizeof(pct), "--%%");
    }

    if (!s.battery_valid)
    {
        drawTextCentered(x + (body_w / 2), pct_y, pct, coreink_gfx::Font::Small, body_w - 2);
        coreink_gfx::drawRect(x, y, body_w, body_h, true);
        coreink_gfx::fillRect(x + body_w, y + 5, terminal_w, terminal_h, true);
        coreink_gfx::drawHLine(x + 7, y + 8, body_w - 14, true);
        return;
    }

    const int percent = clampPercent(s.battery_percent);
    const int fill_w = ((body_w - (inner_pad * 2)) * percent) / 100;
    const int fill_x = x + inner_pad;
    const int fill_y = y + inner_pad;
    const int fill_h = body_h - (inner_pad * 2);

    if (fill_w > 0)
    {
        coreink_gfx::fillRect(fill_x, fill_y, fill_w, fill_h, true);
        coreink_gfx::setClipRect(fill_x, fill_y, fill_w, fill_h);
        drawTextCentered(x + (body_w / 2), pct_y, pct, coreink_gfx::Font::Small, body_w - 2, true);
    }

    const int empty_x = fill_x + fill_w;
    const int empty_w = (body_w - (inner_pad * 2)) - fill_w;
    if (empty_w > 0)
    {
        coreink_gfx::setClipRect(empty_x, y, empty_w + inner_pad, body_h);
        drawTextCentered(x + (body_w / 2), pct_y, pct, coreink_gfx::Font::Small, body_w - 2);
    }

    coreink_gfx::clearClipRect();
    coreink_gfx::drawRect(x, y, body_w, body_h, true);
    coreink_gfx::fillRect(x + body_w, y + 5, terminal_w, terminal_h, true);

}

const char* refreshIntervalText(uint32_t interval_ms)
{
    switch (interval_ms)
    {
    case 3000U: return "3 sec";
    case 5000U: return "5 sec";
    case 10000U: return "10 sec";
    case 60000U: return "1 min";
    case 300000U: return "5 min";
    default: return "1 min";
    }
}

void soundIcon(int x, int y, const BasementStatus& b, bool inverse)
{
    coreink_gfx::drawRect(x, y + 4, 4, 6, inverse);
    coreink_gfx::drawHLine(x + 4, y + 3, 3, inverse);
    coreink_gfx::drawHLine(x + 4, y + 10, 3, inverse);
    coreink_gfx::drawVLine(x + 7, y + 3, 8, inverse);
    if (b.alarm_muted)
    {
        coreink_gfx::drawHLine(x + 10, y + 4, 7, inverse);
        coreink_gfx::drawHLine(x + 10, y + 10, 7, inverse);
        coreink_gfx::drawVLine(x + 13, y + 5, 5, inverse);
    }
    else if (b.alarm_active)
    {
        coreink_gfx::drawVLine(x + 11, y + 3, 8, inverse);
        coreink_gfx::drawVLine(x + 15, y + 1, 12, inverse);
    }
}

void mutedBellIcon(int x, int y, bool inverse)
{
    coreink_gfx::drawHLine(x + 3, y + 11, 11, inverse);
    coreink_gfx::drawVLine(x + 4, y + 5, 6, inverse);
    coreink_gfx::drawVLine(x + 13, y + 5, 6, inverse);
    coreink_gfx::drawHLine(x + 6, y + 3, 5, inverse);
    coreink_gfx::drawPixel(x + 8, y + 13, inverse);
    coreink_gfx::drawHLine(x + 2, y + 2, 14, inverse);
    coreink_gfx::drawHLine(x + 1, y + 14, 16, inverse);
}

const char* batteryStateText(BatteryState state)
{
    switch (state)
    {
    case BatteryState::Critical: return "CRITICAL";
    case BatteryState::Low: return "LOW";
    case BatteryState::Ok: return "OK";
    case BatteryState::Full: return "FULL";
    case BatteryState::Unknown:
    default: return "UNKNOWN";
    }
}

const char* timeSourceText(TimeSource source)
{
    switch (source)
    {
    case TimeSource::Ntp: return "NTP";
    case TimeSource::RtcClock: return "RTC";
    case TimeSource::None:
    default: return "NO TIME";
    }
}

void bluetoothIcon(int x, int y, const DeviceStatus& s, bool inverse)
{
    if (!s.bluetooth_supported)
    {
        return;
    }
    coreink_gfx::drawVLine(x + 4, y, 13, inverse);
    coreink_gfx::drawHLine(x + 2, y + 3, 5, inverse);
    coreink_gfx::drawHLine(x + 2, y + 9, 5, inverse);
    if (!s.bluetooth_enabled)
    {
        coreink_gfx::drawHLine(x, y + 12, 9, inverse);
    }
}

void header(const char* title, const AppState& state)
{
    coreink_gfx::fillRect(0, 0, app_config::kScreenWidth, ui_theme::kHeaderHeight, true);

    const DeviceStatus& s = state.status();
    char battery_pct[8] = {};
    if (s.battery_valid)
    {
        snprintf(battery_pct, sizeof(battery_pct), "%d%%", s.battery_percent);
    }
    else
    {
        snprintf(battery_pct, sizeof(battery_pct), "--%%");
    }

    // Keep non-home page headers readable on the 200px display: reserve a
    // left title slot, center the clock, and put battery percentage in the
    // top-right corner. This prevents longer titles such as NETWORK/BEELINK/
    // HELP 1/2 from running directly into the time text.
    drawTextClipped(ui_theme::kMargin, 6, title, coreink_gfx::Font::Small, 68, true);
    drawTextCentered(100, 6, s.time_text, coreink_gfx::Font::Small, 40, true);

    wifiIcon(122, 6, s, true);
    if (s.bluetooth_supported)
    {
        bluetoothIcon(140, 7, s, true);
    }
    if (state.shouldShowAlarmReminder())
    {
        mutedBellIcon(148, 6, true);
    }
    else
    {
        soundIcon(148, 6, state.basement(), true);
    }
    drawTextRightAligned(app_config::kScreenWidth - 4, 6, battery_pct, coreink_gfx::Font::Small, 32, true);

}

void footer(const char* hint)
{
    coreink_gfx::drawHLine(0, app_config::kScreenHeight - ui_theme::kFooterHeight, app_config::kScreenWidth);
    text(ui_theme::kMargin, app_config::kScreenHeight - 14, hint, 1);
}

void card(int x, int y, int w, int h, const char* label, const char* value)
{
    coreink_gfx::drawRect(x, y, w, h);
    text(x + 6, y + 6, label, 1);
    text(x + 6, y + 23, value, 1);
}

void progressBar(int x, int y, int w, int h, int percent)
{
    coreink_gfx::drawRect(x, y, w, h);
    if (percent < 0)
    {
        coreink_gfx::drawHLine(x + 3, y + h / 2, w - 6);
        return;
    }
    if (percent > 100) percent = 100;
    const int fill = ((w - 4) * percent) / 100;
    if (fill > 0)
    {
        coreink_gfx::fillRect(x + 2, y + 2, fill, h - 4, true);
    }
}

void homeBatteryBar(int x, int center_y, int w, int h, const DeviceStatus& s)
{
    coreink_gfx::drawHLine(x, center_y, w);
    if (!s.battery_valid)
    {
        return;
    }

    int percent = s.battery_percent;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    const int fill = (w * percent) / 100;
    if (fill > 0)
    {
        const int fill_x = x + ((w - fill) / 2);
        coreink_gfx::fillRect(fill_x, center_y - (h / 2), fill, h, true);
    }
}

void formatUptime(uint32_t uptime_ms, char* out, size_t out_len)
{
    const uint32_t total_s = uptime_ms / 1000U;
    const uint32_t hours = total_s / 3600U;
    const uint32_t minutes = (total_s / 60U) % 60U;
    snprintf(out, out_len, "%luh %02lum", static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes));
}

void formatAge(uint32_t now_ms, uint32_t then_ms, char* out, size_t out_len)
{
    if (then_ms == 0)
    {
        snprintf(out, out_len, "WAITING");
        return;
    }
    const uint32_t age_s = (now_ms - then_ms) / 1000U;
    if (age_s < 90U)
    {
        snprintf(out, out_len, "JUST NOW");
    }
    else if (age_s < 3600U)
    {
        snprintf(out, out_len, "%lum AGO", static_cast<unsigned long>(age_s / 60U));
    }
    else
    {
        snprintf(out, out_len, "%luh AGO", static_cast<unsigned long>(age_s / 3600U));
    }
}

void formatAgeCompact(uint32_t now_ms, uint32_t then_ms, char* out, size_t out_len)
{
    if (then_ms == 0)
    {
        snprintf(out, out_len, "--");
        return;
    }
    const uint32_t age_s = (now_ms - then_ms) / 1000U;
    if (age_s < 90U)
    {
        snprintf(out, out_len, "NOW");
    }
    else if (age_s < 3600U)
    {
        snprintf(out, out_len, "%lum", static_cast<unsigned long>(age_s / 60U));
    }
    else
    {
        snprintf(out, out_len, "%luh", static_cast<unsigned long>(age_s / 3600U));
    }
}

const char* weatherMark(const WeatherStatus& w)
{
    if (!w.online)
    {
        return "?";
    }
    if (w.weather_code == 0)
    {
        return "*";
    }
    if (w.weather_code == 1 || w.weather_code == 2 || w.weather_code == 3)
    {
        return "~";
    }
    if ((w.weather_code >= 51 && w.weather_code <= 67) || (w.weather_code >= 80 && w.weather_code <= 82))
    {
        return "/";
    }
    if ((w.weather_code >= 71 && w.weather_code <= 77) || (w.weather_code >= 85 && w.weather_code <= 86))
    {
        return "+";
    }
    if (w.weather_code >= 95 && w.weather_code <= 99)
    {
        return "!";
    }
    return "*";
}

void drawDashboardFrame(int outer_x, int outer_y, int outer_size)
{
    // Fixed Home frame only. Battery state is drawn separately by the centered
    // home battery bar below the clock.
    coreink_gfx::drawRect(outer_x, outer_y, outer_size, outer_size);
}

void drawDashboardFrame(int outer_x, int outer_y, int w, int h)
{
    // Fixed Home frame only. Battery state is drawn separately by the centered
    // home battery bar below the clock.
    coreink_gfx::drawRect(outer_x, outer_y, w, h);
}

void statusPill(int x, int y, const char* label, bool active)
{
    coreink_gfx::drawRect(x, y, 38, 16);
    const int label_width = textWidth(label, coreink_gfx::Font::Small);
    const int label_x = x + ((38 - label_width) / 2);
    if (active)
    {
        coreink_gfx::fillRect(x + 2, y + 2, 34, 12, true);
        coreink_gfx::drawText(label_x, y + 1, label, coreink_gfx::Font::Small, true);
    }
    else
    {
        coreink_gfx::drawText(label_x, y + 1, label, coreink_gfx::Font::Small);
    }
}

void drawTextCentered(int center_x, int y, const char* value, coreink_gfx::Font font, int max_width, bool inverse)
{
    if (!value || max_width <= 0)
    {
        return;
    }

    const int width = textWidth(value, font);
    const int clipped_width = width > max_width ? max_width : width;
    drawTextClipped(center_x - (clipped_width / 2), y, value, font, max_width, inverse);
}

void panel(int x, int y, int w, int h, const char* title = nullptr)
{
    coreink_gfx::drawRect(x, y, w, h);
    if (title && title[0])
    {
        coreink_gfx::fillRect(x + 1, y + 1, w - 2, 16, true);
        drawTextClipped(x + 5, y + 1, title, coreink_gfx::Font::Small, w - 10, true);
    }
}

void labelValueRow(int x, int y, int w, const char* label, const char* value)
{
    drawTextClipped(x, y, label, coreink_gfx::Font::Small, 48);
    drawTextClipped(x + 54, y, value, coreink_gfx::Font::Small, w - 54);
}

void labelValueRowf(int x, int y, int w, const char* label, const char* format, ...)
{
    char buffer[64] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    labelValueRow(x, y, w, label, buffer);
}

void metricRow(int x, int y, int w, const char* label, const char* value)
{
    coreink_gfx::drawRect(x, y, w, 20);
    coreink_gfx::fillRect(x + 1, y + 1, 45, 18, true);
    drawTextCentered(x + 23, y + 3, label, coreink_gfx::Font::Small, 41, true);
    drawTextClipped(x + 52, y + 3, value, coreink_gfx::Font::Small, w - 58);
}

void selectableMetricRow(int x, int y, int w, const char* label, const char* value, bool selected)
{
    if (selected)
    {
        coreink_gfx::fillRect(x, y, w, 20, true);
        coreink_gfx::drawRect(x + 2, y + 2, w - 4, 16, false);
        drawTextCentered(x + 23, y + 3, label, coreink_gfx::Font::Small, 41, true);
        drawTextClipped(x + 52, y + 3, value, coreink_gfx::Font::Small, w - 58, true);
        return;
    }

    metricRow(x, y, w, label, value);
}

void tempMetricRow(int x, int y, int w, const BasementStatus& b)
{
    metricRow(x, y, w, "TEMP", b.online && b.has_disk ? b.disk : "--");
}

void badge(int x, int y, int w, const char* label, bool active)
{
    coreink_gfx::drawRect(x, y, w, 16);
    if (active)
    {
        coreink_gfx::fillRect(x + 2, y + 2, w - 4, 12, true);
        drawTextCentered(x + (w / 2), y + 1, label, coreink_gfx::Font::Small, w - 6, true);
    }
    else
    {
        drawTextCentered(x + (w / 2), y + 1, label, coreink_gfx::Font::Small, w - 6);
    }
}

void metricBox(int x, int y, int w, const char* label, const char* value)
{
    coreink_gfx::drawRect(x, y, w, 34);
    drawTextCentered(x + (w / 2), y + 2, label, coreink_gfx::Font::Small, w - 4);
    coreink_gfx::drawHLine(x + 3, y + 17, w - 6);
    drawTextCentered(x + (w / 2), y + 18, value, coreink_gfx::Font::Small, w - 4);
}

void footerNav(const char* left, const char* right = nullptr)
{
    const int y = app_config::kScreenHeight - ui_theme::kFooterHeight;
    coreink_gfx::drawHLine(0, y, app_config::kScreenWidth);
    if (left && left[0])
    {
        drawTextClipped(ui_theme::kMargin, y + 3, left, coreink_gfx::Font::Small, right && right[0] ? 104 : 184);
    }
    if (right && right[0])
    {
        drawTextRightAligned(app_config::kScreenWidth - ui_theme::kMargin, y + 3, right, coreink_gfx::Font::Small, 78);
    }
}

void pageHint()
{
    footerNav("MID", "DOWN");
}

} // namespace

namespace ui_pages
{

void renderDashboard(const AppState& state)
{
    const BasementStatus& b = state.basement();
    const DeviceStatus& s = state.status();
    const WeatherStatus& w = state.weather();

    drawDashboardFrame(4, 24, 192, 172);

    constexpr int battery_body_w = 34;
    constexpr int battery_terminal_w = 3;
    constexpr int battery_total_w = battery_body_w + battery_terminal_w;
    constexpr int battery_x = app_config::kScreenWidth - 5 - battery_total_w;
    constexpr int battery_y = 3;
    constexpr int wifi_w = 22;
    constexpr int wifi_gap = 7;
    const int wifi_x = battery_x - wifi_gap - wifi_w;

    drawTextCentered(54, 3, s.date_text, coreink_gfx::Font::Small, 88);
    if (state.shouldShowAlarmReminder())
    {
        mutedBellIcon(104, 4, false);
    }
    else
    {
        soundIcon(104, 4, state.basement(), false);
    }
    dashboardWifiIndicator(wifi_x, 3);
    dashboardBatteryIndicator(battery_x, battery_y, s);

    const int time_width = static_cast<int>(strlen(s.time_text)) * 24;
    const int time_x = (app_config::kScreenWidth - time_width - 18) / 2;
    const int safe_time_x = time_x < 8 ? 8 : time_x;
    text(safe_time_x, 38, s.time_text, 3);
    coreink_gfx::drawText(safe_time_x + time_width + 2, 40, s.meridiem_text, coreink_gfx::Font::Small);
    coreink_gfx::drawText(safe_time_x + time_width + 2, 56, s.second_text, coreink_gfx::Font::Small);
    homeBatteryBar(22, 88, 156, 8, s);

    badge(14, 98, 40, "BEE", b.online);
    badge(58, 98, 40, "WIFI", s.wifi_connected);
    badge(102, 98, 40, "PWR", s.battery_valid && s.battery_percent > 20);
    badge(146, 98, 40, "WX", w.online);

    coreink_gfx::fillRect(12, 123, 176, 46, true);
    if (w.online)
    {
        char temp[8] = {};
        const float weather_temp = state.dashboardTempIsF() ? w.temperature_f : w.temperature_c;
        const char weather_unit = state.dashboardTempIsF() ? 'F' : 'C';
        snprintf(temp, sizeof(temp), "%.0f%c", static_cast<double>(weather_temp), weather_unit);
        drawTextClipped(18, 130, weatherMark(w), coreink_gfx::Font::Small, 8, true);
        drawTextClipped(30, 124, temp, coreink_gfx::Font::Large, 72, true);
        drawTextClipped(106, 134, w.condition, coreink_gfx::Font::Small, 78, true);
        drawLocationClipped(106, 150, w.location, coreink_gfx::Font::Small, 78, true);
    }
    else
    {
        drawTextClipped(22, 132, "WEATHER OFFLINE", coreink_gfx::Font::Small, 150, true);
        drawTextClipped(22, 150, w.error, coreink_gfx::Font::Small, 150, true);
    }

    char update_age[8] = {};
    formatAgeCompact(s.uptime_ms, w.last_success_ms, update_age, sizeof(update_age));
    char update_text[32] = {};
    snprintf(update_text, sizeof(update_text), "WX:%s", update_age);
    drawTextClipped(18, 174, update_text, coreink_gfx::Font::Small, 88);
    drawTextRightAligned(184, 174, "MID", coreink_gfx::Font::Small, 88);
}

void renderClock(const AppState& state)
{
    header("CLOCK", state);

    const DeviceStatus& s = state.status();
    panel(10, 34, 180, 68, "LOCAL TIME");
    const int time_width = static_cast<int>(strlen(s.time_text)) * 24;
    const int time_x = 100 - ((time_width + 18) / 2);
    text(time_x < 14 ? 14 : time_x, 52, s.time_text, 3);
    coreink_gfx::drawText(time_x + time_width + 2, 55, s.meridiem_text, coreink_gfx::Font::Small);
    coreink_gfx::drawText(time_x + time_width + 2, 72, s.second_text, coreink_gfx::Font::Small);

    panel(10, 110, 180, 48, "CLOCK STATUS");
    drawTextCentered(100, 130, s.date_text, coreink_gfx::Font::Small, 160);
    drawTextCentered(100, 146, s.rtc_valid ? timeSourceText(s.time_source) : s.rtc_fault, coreink_gfx::Font::Small, 160);
    drawTextCentered(100, 164, s.time_synced ? "NTP sync every 6h" : "NTP retry every 30s", coreink_gfx::Font::Small, 176);

    footerNav("MID", "DOWN");
}

void renderPower(const AppState& state)
{
    header("POWER", state);

    const DeviceStatus& s = state.status();
    panel(10, 34, 180, 58, "BATTERY");
    char pct[16] = {};
    if (s.battery_valid)
    {
        snprintf(pct, sizeof(pct), "%d%%", s.battery_percent);
    }
    else
    {
        snprintf(pct, sizeof(pct), "--%%");
    }
    drawTextCentered(100, 52, pct, coreink_gfx::Font::Small, 80);
    drawTextCentered(100, 68, s.battery_valid ? batteryStateText(s.battery_state) : s.battery_fault, coreink_gfx::Font::Small, 160);

    progressBar(18, 100, 164, 18, s.battery_percent);
    panel(10, 126, 180, 48, "DETAILS");
    labelValueRowf(18, 146, 164, "Pack", "%.2f V", static_cast<double>(s.battery_voltage));
    labelValueRowf(18, 162, 164, "ADC", "%u / %umV", static_cast<unsigned>(s.battery_raw_adc), static_cast<unsigned>(s.battery_adc_mv));

    footerNav("MID", "DOWN");
}

void renderNetwork(const AppState& state)
{
    header("NETWORK", state);

    const DeviceStatus& s = state.status();
    const char* wifi_state = s.wifi_connected ? "CONNECTED" : (s.wifi_configured ? "CONNECTING" : "NOT SET");
    panel(10, 34, 180, 54, "WIFI STATUS");
    drawTextCentered(100, 54, wifi_state, coreink_gfx::Font::Small, 160);
    drawTextCentered(100, 70, s.wifi_ssid[0] ? s.wifi_ssid : "<none>", coreink_gfx::Font::Small, 160);

    panel(10, 96, 180, 78, "NETWORK DETAILS");
    labelValueRow(18, 116, 164, "IP", s.wifi_ip);
    labelValueRowf(18, 132, 164, "RSSI", "%d dBm", s.wifi_rssi);
    labelValueRowf(18, 148, 164, "Bars", "%d / 4", s.wifi_strength);
    labelValueRowf(18, 164, 164, "Scan", "%d nets  NTP %s", s.wifi_scan_count, s.time_synced ? "OK" : "WAIT");

    footerNav("MID", "NTP");
}

void renderBeelink(const AppState& state)
{
    header(app_config::kTargetHostName, state);

    const BasementStatus& b = state.basement();
    char status_title[24] = {};
    if (b.online)
    {
        snprintf(status_title, sizeof(status_title), "ONLINE");
    }
    else if (!b.alarm_active)
    {
        snprintf(status_title, sizeof(status_title), "RETRY %u/%u", static_cast<unsigned>(b.consecutive_failures), static_cast<unsigned>(app_config::kBasementAlarmFailureThreshold));
    }
    else
    {
        snprintf(status_title, sizeof(status_title), "%s", b.alarm_muted ? "OFFLINE MUTED" : "OFFLINE ALARM");
    }
    panel(10, 30, 180, 44, status_title);
    drawTextCentered(100, 48, b.has_host ? b.host : "missing host", coreink_gfx::Font::Small, 160);
    drawTextCentered(100, 62, b.has_service ? b.service : "missing service", coreink_gfx::Font::Small, 160);

    selectableMetricRow(10, 78, 180, "CPU", b.online && b.has_cpu ? b.cpu : "--", b.beelink_cursor == 0);
    selectableMetricRow(10, 99, 180, "RAM", b.online && b.has_memory ? b.memory : "--", b.beelink_cursor == 1);
    selectableMetricRow(10, 120, 180, "TEMP", b.online && b.has_disk ? b.disk : "--", b.beelink_cursor == 2);
    selectableMetricRow(10, 141, 180, "UP", b.online && b.has_uptime ? b.uptime : b.error, b.beelink_cursor == 3);

    char interval[24] = {};
    snprintf(interval, sizeof(interval), "%s", refreshIntervalText(state.refresh_interval_ms));
    selectableMetricRow(10, 162, 180, "INT", interval, b.beelink_cursor == 4);

    footerNav("MID select", "UP/DOWN move");
}

void renderBeelinkCpuDetail(const AppState& state)
{
    header("CPU DETAIL", state);

    const BasementStatus& b = state.basement();
    const char unit = state.hostTempIsF() ? 'F' : 'C';
    const float cpu_temp = state.hostTempIsF() ? b.cpu_temp_f : b.cpu_temp_c;

    panel(10, 38, 180, 94, "CPU DETAIL");
    if (b.online && b.has_cpu)
    {
        labelValueRowf(18, 62, 164, "Use", "%s %.0f%%", app_config::kTargetHostName, static_cast<double>(b.cpu_load));
        labelValueRowf(18, 84, 164, "Temp", "%s %.0f%c%c", app_config::kTargetHostName, static_cast<double>(cpu_temp), 0xB0, unit);
        progressBar(18, 108, 164, 14, static_cast<int>(b.cpu_load + 0.5f));
    }
    else
    {
        drawTextCentered(100, 76, b.error, coreink_gfx::Font::Small, 160);
    }

    footerNav("ANY button", "BACK");
}

void renderBeelinkMemDetail(const AppState& state)
{
    header("RAM DETAIL", state);

    const BasementStatus& b = state.basement();
    const float used = b.online && b.has_memory ? b.memory_used : 0.0f;
    const float total = b.memory_total;
    float free = total - used;
    if (free < 0.0f)
    {
        free = 0.0f;
    }

    panel(10, 38, 180, 102, "RAM DETAIL");
    labelValueRowf(18, 60, 164, "BL", "%.1fGB / %.1fGB", static_cast<double>(used), static_cast<double>(total));
    labelValueRowf(18, 80, 164, "M5", "%luKB Heap Free", static_cast<unsigned long>(ESP.getFreeHeap() / 1024U));
    labelValueRowf(18, 100, 164, "Free", "%.1f GB", static_cast<double>(free));

    int percent = -1;
    if (total > 0.0f)
    {
        percent = static_cast<int>(((used * 100.0f) / total) + 0.5f);
    }
    progressBar(18, 118, 164, 14, percent);

    footerNav("ANY button", "BACK");
}

void renderBeelinkTempDetail(const AppState& state)
{
    header("TEMP DETAIL", state);

    const BasementStatus& b = state.basement();
    const bool is_f = state.hostTempIsF();
    const char unit = is_f ? 'F' : 'C';
    const char inactive_unit = is_f ? 'C' : 'F';
    const float bl_cpu = is_f ? b.cpu_temp_f : b.cpu_temp_c;
    const float bl_nvme = is_f ? b.nvme_temp_f : b.nvme_temp_c;
    const float m5_core_c = temperatureRead();
    const float m5_core = is_f ? ((m5_core_c * 9.0f / 5.0f) + 32.0f) : m5_core_c;

    panel(10, 38, 180, 112, "TEMP DETAIL");
    if (b.online && b.has_disk)
    {
        labelValueRowf(18, 62, 164, "BL CPU", "%.0f%c%c", static_cast<double>(bl_cpu), 0xB0, unit);
        labelValueRowf(18, 84, 164, "BL NVMe", "%.0f%c%c", static_cast<double>(bl_nvme), 0xB0, unit);
    }
    else
    {
        drawTextCentered(100, 72, b.error, coreink_gfx::Font::Small, 160);
    }
    labelValueRowf(18, 106, 164, "M5 Core", "%.0f%c%c", static_cast<double>(m5_core), 0xB0, unit);

    char hint[32] = {};
    snprintf(hint, sizeof(hint), "[Select] Switch to %c", inactive_unit);
    footerNav(hint, "UP/DOWN back");
}

void renderBeelinkUptimeDetail(const AppState& state)
{
    header("UPTIME DETAIL", state);

    const BasementStatus& b = state.basement();
    const uint32_t m5_total_s = millis() / 1000U;
    const uint32_t m5_days = m5_total_s / 86400U;
    const uint32_t m5_hours = (m5_total_s / 3600U) % 24U;
    const uint32_t m5_minutes = (m5_total_s / 60U) % 60U;
    char m5_uptime[24] = {};
    snprintf(m5_uptime,
             sizeof(m5_uptime),
             "%lud %luh %lum",
             static_cast<unsigned long>(m5_days),
             static_cast<unsigned long>(m5_hours),
             static_cast<unsigned long>(m5_minutes));

    panel(10, 38, 180, 94, "UPTIME DETAIL");
    labelValueRow(18, 64, 164, "BL", b.online && b.has_uptime ? b.uptime : b.error);
    labelValueRow(18, 90, 164, "M5", m5_uptime);

    footerNav("ANY button", "BACK");
}

void renderSystem(const AppState& state)
{
    header("SYSTEM", state);

    const DeviceStatus& s = state.status();
    char uptime[20] = {};
    formatUptime(s.uptime_ms, uptime, sizeof(uptime));

    panel(10, 34, 180, 64, "FIRMWARE");
    labelValueRow(18, 54, 164, "App", app_config::kAppName);
    labelValueRow(18, 70, 164, "Ver", app_config::kVersion);
    labelValueRowf(18, 86, 164, "Heap", "%lu", static_cast<unsigned long>(s.free_heap));

    panel(10, 106, 180, 54, "DEVICE HEALTH");
    labelValueRow(18, 126, 164, "Battery", s.battery_valid ? "valid" : s.battery_fault);
    labelValueRowf(18, 142, 164, "WiFi", "%s %d", s.wifi_connected ? "OK" : "--", s.wifi_rssi);
    labelValueRow(18, 158, 164, "RTC", s.rtc_valid ? "valid" : s.rtc_fault);

    progressBar(18, 171, 164, 10, s.battery_percent);

    footerNav("MID", "DOWN");
}

void renderHelpButtons(const AppState& state)
{
    header("HELP 1/2", state);

    panel(10, 34, 180, 60, "BUTTONS");
    labelValueRow(18, 54, 164, "PWR", "next page / wake");
    labelValueRow(18, 70, 164, "UP", "previous page");

    panel(10, 102, 180, 72, "ACTIONS");
    labelValueRow(18, 122, 164, "MID", "home time/WX mode");
    labelValueRow(18, 138, 164, "DOWN", "sleep screen");
    drawTextCentered(100, 158, "USB power may wake", coreink_gfx::Font::Small, 160);

    footerNav("PWR/UP", "2/2");
}

void renderHelpInfo(const AppState& state)
{
    header("HELP 2/2", state);

    panel(10, 34, 180, 76, "SHORT WORDS");
    labelValueRow(18, 54, 164, "WX", "weather age/status");
    labelValueRow(18, 70, 164, "NTP", "internet time");
    labelValueRow(18, 86, 164, "RTC", "device clock");

    panel(10, 118, 180, 56, "STATUS");
    labelValueRow(18, 138, 164, "BAT", "battery percent");
    labelValueRow(18, 154, 164, "PWR", "battery OK badge");

    footerNav("PWR/UP", "MID");
}

void renderSleep(const AppState& state)
{
    header("SLEEP", state);

    panel(14, 42, 172, 112, "LOW POWER");
    drawTextCentered(100, 66, "CoreInk", coreink_gfx::Font::Large, 168);
    drawTextCentered(100, 116, "RESTING", coreink_gfx::Font::Small, 140);
    coreink_gfx::drawRect(28, 132, 144, 18);
    drawTextCentered(100, 133, "PWR Wake", coreink_gfx::Font::Small, 136);

    footerNav("STATIC", "LOW PWR");
}

void renderAlarmPage(const AppState& state)
{
    const AlarmStatus& alarm = state.alarm();
    const char* title = alarm.error_title[0] ? alarm.error_title : "ALARM";
    const char* details = alarm.error_details[0] ? alarm.error_details : "Attention required";

    coreink_gfx::drawRect(4, 4, 192, 192);
    coreink_gfx::drawRect(8, 8, 184, 184);
    coreink_gfx::fillRect(14, 16, 172, 26, true);
    drawTextCentered(100, 21, "ACTIVE ALARM", coreink_gfx::Font::Small, 160, true);

    mutedBellIcon(90, 54, false);
    mutedBellIcon(100, 54, false);
    coreink_gfx::drawHLine(52, 78, 96);
    drawTextCentered(100, 92, title, coreink_gfx::Font::Small, 176);

    panel(14, 116, 172, 38, "DETAILS");
    drawTextCentered(100, 137, details, coreink_gfx::Font::Small, 156);

    const int y = 166;
    coreink_gfx::drawHLine(8, y - 8, 184);
    drawTextCentered(34, y, "[UP]", coreink_gfx::Font::Small, 48);
    drawTextCentered(34, y + 15, "Snooze", coreink_gfx::Font::Small, 54);
    drawTextCentered(100, y, "[MID]", coreink_gfx::Font::Small, 54);
    drawTextCentered(100, y + 15, "Dismiss", coreink_gfx::Font::Small, 60);
    drawTextCentered(166, y, "[DWN]", coreink_gfx::Font::Small, 54);
    drawTextCentered(166, y + 15, "Mute", coreink_gfx::Font::Small, 48);
}

} // namespace ui_pages
