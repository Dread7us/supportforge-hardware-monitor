#include "app_config.h"
#include "app_state.h"
#include "buttons.h"
#include "display.h"
#include <Arduino.h>
#include <M5CoreInk.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

namespace
{

AppState app;
ButtonController buttons;
Display display;

constexpr uint32_t kManualFullRefreshCooldownMs = 2500U;
uint32_t last_render_ms = 0;
uint32_t last_full_refresh_ms = 0;
bool force_render = true;
bool force_full_clear = true;
bool manual_full_refresh_requested = false;
Page last_rendered_page = Page::Dashboard;

void enterLowPowerLightSleep(uint32_t sleep_ms)
{
    if (sleep_ms < 1000U)
    {
        delay(20);
        return;
    }

    digitalWrite(LED_EXT_PIN, LOW);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleep_ms) * 1000ULL);

    const gpio_num_t wake_buttons[] = {
        static_cast<gpio_num_t>(BUTTON_UP_PIN),
        static_cast<gpio_num_t>(BUTTON_DOWN_PIN),
        static_cast<gpio_num_t>(BUTTON_MID_PIN),
        static_cast<gpio_num_t>(BUTTON_PWR_PIN),
        static_cast<gpio_num_t>(BUTTON_EXT_PIN),
    };
    for (gpio_num_t pin : wake_buttons)
    {
        gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();

    Serial.printf("LOW POWER light sleep %lu ms\n", static_cast<unsigned long>(sleep_ms));
    esp_light_sleep_start();

    for (gpio_num_t pin : wake_buttons)
    {
        gpio_wakeup_disable(pin);
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
}

uint32_t refreshIntervalFor(Page page)
{
    switch (page)
    {
    case Page::Dashboard:
        return app.refresh_interval_ms;
    case Page::Clock:
        return app_config::kClockRefreshMs;
    case Page::System:
        return app_config::kSystemRefreshMs;
    case Page::Sleep:
    case Page::LowPowerActive:
    case Page::Alarm:
        return 0;
    case Page::Power:
    case Page::Network:
    case Page::Beelink:
    case Page::BeelinkCpuDetail:
    case Page::BeelinkMemDetail:
    case Page::BeelinkTempDetail:
    case Page::BeelinkUptimeDetail:
    case Page::BeelinkSpeedTestDetail:
    default:
        return app.refresh_interval_ms;
    }
}

bool handleRefreshButton(ButtonEvent event)
{
    if (event == ButtonEvent::FastRefresh)
    {
        force_render = true;
        force_full_clear = false;
        return true;
    }

    if (event == ButtonEvent::FullRefresh)
    {
        manual_full_refresh_requested = true;
        return true;
    }

    return false;
}

void renderWithAntiGhosting(uint32_t now)
{
    const Page current_page = app.page();
    const bool needs_full_clear = force_full_clear;

    display.render(app, needs_full_clear);
    app.noteRefresh();
    last_render_ms = now;
    last_rendered_page = current_page;
    force_render = false;
    force_full_clear = false;
}

void handleBeelinkButton(ButtonEvent event)
{
    BasementStatus& basement = app.basement();
    switch (event)
    {
    case ButtonEvent::Next:
        if (basement.beelink_cursor >= 5)
        {
            basement.beelink_cursor = 0;
            app.nextPage();
        }
        else
        {
            ++basement.beelink_cursor;
        }
        force_render = true;
        break;
    case ButtonEvent::Previous:
        if (basement.beelink_cursor == 0)
        {
            app.previousPage();
        }
        else
        {
            --basement.beelink_cursor;
        }
        force_render = true;
        break;
    case ButtonEvent::Select:
        if (basement.beelink_cursor == 0)
        {
            app.setPage(Page::BeelinkCpuDetail);
        }
        else if (basement.beelink_cursor == 1)
        {
            app.setPage(Page::BeelinkMemDetail);
        }
        else if (basement.beelink_cursor == 2)
        {
            app.setPage(Page::BeelinkTempDetail);
        }
        else if (basement.beelink_cursor == 3)
        {
            app.setPage(Page::BeelinkUptimeDetail);
        }
        else if (basement.beelink_cursor == 4)
        {
            app.setPage(Page::BeelinkSpeedTestDetail);
        }
        else if (basement.beelink_cursor == 5)
        {
            app.cycleRefreshInterval();
            force_full_clear = false;
        }
        else
        {
            app.forceNetworkRefresh();
            force_full_clear = true;
        }
        force_render = true;
        break;
    case ButtonEvent::FastRefresh:
    case ButtonEvent::FullRefresh:
        handleRefreshButton(event);
        break;
    case ButtonEvent::None:
    default:
        break;
    }
}

void handleButton(ButtonEvent event)
{
    //if (event != ButtonEvent::None) { M5.Speaker.tone(300, 0.01); }

    if (event == ButtonEvent::None)
    {
        return;
    }

    if (app.isAlarmActive())
    {
        switch (event)
        {
        case ButtonEvent::Previous:
            app.snoozeAlarm();
            force_render = true;
            force_full_clear = true;
            return;
        case ButtonEvent::Select:
            app.dismissAlarm();
            force_render = true;
            force_full_clear = true;
            return;
        case ButtonEvent::Next:
            app.muteAlarm();
            force_render = true;
            force_full_clear = false;
            return;
        case ButtonEvent::FastRefresh:
        case ButtonEvent::FullRefresh:
            handleRefreshButton(event);
            return;
        case ButtonEvent::None:
        default:
            return;
        }
    }

    if (app.page() == Page::Sleep)
    {
        switch (event)
        {
        case ButtonEvent::Select:
            if (app.lowPower().settings_focus == 0)
            {
                app.cycleLowPowerInterval();
            }
            else if (app.lowPower().settings_focus == 1)
            {
                app.requestLowPowerMode();
                force_full_clear = false;
            }
            else
            {
                app.setPage(Page::Power);
                force_full_clear = false;
            }
            force_render = true;
            return;
        case ButtonEvent::Next:
            app.moveLowPowerSettingsFocus(1);
            force_render = true;
            force_full_clear = false;
            return;
        case ButtonEvent::Previous:
            app.moveLowPowerSettingsFocus(-1);
            force_render = true;
            force_full_clear = false;
            return;
        case ButtonEvent::FastRefresh:
            app.setPage(Page::Power);
            force_render = true;
            force_full_clear = false;
            return;
        case ButtonEvent::FullRefresh:
            handleRefreshButton(event);
            return;
        case ButtonEvent::None:
        default:
            return;
        }
    }

    if (app.page() == Page::LowPowerActive)
    {
        if (event == ButtonEvent::Next || event == ButtonEvent::FastRefresh || event == ButtonEvent::Previous)
        {
            if (app.isLowPowerArming())
            {
                app.cancelLowPowerArming();
            }
            app.setPage(Page::Sleep);
            force_render = true;
            force_full_clear = false;
        }
        else if (event == ButtonEvent::FullRefresh)
        {
            handleRefreshButton(event);
        }
        return;
    }

    if (app.page() == Page::BeelinkCpuDetail || app.page() == Page::BeelinkMemDetail || app.page() == Page::BeelinkUptimeDetail)
    {
        if (handleRefreshButton(event))
        {
            return;
        }

        app.setPage(Page::Beelink);
        force_render = true;
        return;
    }

    if (app.page() == Page::BeelinkSpeedTestDetail)
    {
        if (event == ButtonEvent::Select)
        {
            app.triggerSpeedTest();
            force_render = true;
            force_full_clear = false;
        }
        else if (event == ButtonEvent::Next || event == ButtonEvent::Previous)
        {
            app.setPage(Page::Beelink);
            force_render = true;
        }
        else if (event == ButtonEvent::FastRefresh || event == ButtonEvent::FullRefresh)
        {
            handleRefreshButton(event);
        }
        return;
    }

    if (app.page() == Page::BeelinkTempDetail)
    {
        if (event == ButtonEvent::Select)
        {
            app.toggleTempUnit();
            force_render = true;
            force_full_clear = true;
        }
        else if (event == ButtonEvent::Next || event == ButtonEvent::Previous)
        {
            app.setPage(Page::Beelink);
            force_render = true;
        }
        else if (event == ButtonEvent::FastRefresh)
        {
            handleRefreshButton(event);
        }
        else if (event == ButtonEvent::FullRefresh)
        {
            handleRefreshButton(event);
        }
        return;
    }

    if (app.page() == Page::Beelink)
    {
        handleBeelinkButton(event);
        return;
    }

    switch (event)
    {
    case ButtonEvent::Next:
        app.nextPage();
        force_render = true;
        break;
    case ButtonEvent::Previous:
        app.toggleServerAlarmMuteIfOffline();
        app.previousPage();
        force_render = true;
        break;
    case ButtonEvent::Select:
        if (app.page() == Page::Dashboard)
        {
            app.cycleDashboardDisplayPrefs();
            force_full_clear = false;
        }
        else
        {
            app.forceNetworkRefresh();
            force_full_clear = true;
        }
        force_render = true;
        break;
    case ButtonEvent::FastRefresh:
    case ButtonEvent::FullRefresh:
        handleRefreshButton(event);
        break;
    case ButtonEvent::None:
    default:
        break;
    }
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(100);

    M5.begin();
    if (!M5.M5Ink.isInit())
    {
        Serial.println("M5Ink init failed");
        while (true)
        {
            delay(1000);
        }
    }

    buttons.begin();
    display.begin();
    display.showSplash("Connecting WiFi...");
    app.begin();

    // Render the first real page once, deliberately, after startup state is
    // populated. Without this, the first loop iteration can immediately redraw
    // over the just-refreshed panel, making e-paper blacks look over-driven.
    const uint32_t now = millis();
    display.render(app, force_full_clear);
    app.noteRefresh();
    last_render_ms = now;
    last_rendered_page = app.page();
    force_render = false;
    force_full_clear = false;

    Serial.println("M5 CoreInk UI started");
}

void loop()
{
    const Page page_before_button = app.page();
    const ButtonEvent event = buttons.poll();
    handleButton(event);

    // Page transitions take priority over background/alarm work. Push the fully
    // rebuilt frame immediately so a network transaction or later state-machine
    // work cannot get ahead of the user's navigation feedback.
    const bool low_power_page_transition = app.page() != page_before_button &&
                                           (app.page() == Page::Sleep || app.page() == Page::LowPowerActive);
    if (low_power_page_transition && force_render)
    {
        renderWithAntiGhosting(millis());
        if (app.page() == Page::LowPowerActive && app.isLowPowerModeActive())
        {
            app.noteLowPowerDisplay(millis());
            app.completeLowPowerActivationAfterDisplay();
        }
        return;
    }

    if (app.isLowPowerArming() && app.page() == Page::LowPowerActive)
    {
        const uint32_t now = millis();
        if (app.updateLowPowerArming(now))
        {
            force_render = true;
            force_full_clear = false;
        }
        if (app.lowPowerArmingProgressPercent(now) >= 100U)
        {
            app.activateLowPowerMode();
            force_render = true;
            force_full_clear = false;
        }
    }

    if (app.isLowPowerModeActive() && app.page() == Page::LowPowerActive)
    {
        if (force_render)
        {
            const uint32_t now = millis();
            renderWithAntiGhosting(now);
            app.noteLowPowerDisplay(now);
            app.completeLowPowerActivationAfterDisplay();
            delay(20);
            return;
        }

        const bool cycle_ran = app.runLowPowerMonitoringCycle();
        if (cycle_ran)
        {
            force_render = true;
            force_full_clear = false;
        }

        // A threshold alarm terminates Active Low Power inside the monitoring
        // cycle. Render the existing alarm frame immediately and service its
        // existing buzzer before any Low Power status refresh or sleep decision
        // can run against the now-obsolete session.
        if (app.isAlarmActive() && app.page() == Page::Alarm)
        {
            app.consumeAlarmDisplayChanged();
            force_render = true;
            force_full_clear = true;
            renderWithAntiGhosting(millis());
            app.updateServerAlarm();
            delay(20);
            return;
        }
        if (app.consumeAlarmDisplayChanged())
        {
            force_render = true;
            force_full_clear = true;
        }
        const uint32_t now = millis();
        if (app.lowPowerMinuteDisplayDue(now))
        {
            force_render = true;
            force_full_clear = false;
        }
        if (force_render)
        {
            renderWithAntiGhosting(now);
            app.noteLowPowerDisplay(now);
        }
        const uint32_t sleep_ms = app.lowPowerSleepDurationMs(millis());
        enterLowPowerLightSleep(sleep_ms);
        return;
    }

    if (manual_full_refresh_requested)
    {
        // Drain any redraw requests that the background task queued before the
        // button press, then do the manual hardware clear + GC/full push, then
        // drain again in case the background task finished while the panel was
        // busy. This keeps the next loop from immediately drawing over it.
        app.consumeDisplayChangeFlags();
        display.renderManualFullRefresh(app);
        app.consumeDisplayChangeFlags();
        app.noteRefresh();
        last_render_ms = millis();
        last_full_refresh_ms = last_render_ms;
        last_rendered_page = app.page();
        force_render = false;
        force_full_clear = false;
        manual_full_refresh_requested = false;
        delay(20);
        return;
    }

    app.updateServerAlarm();

    const uint32_t now = millis();
    const uint32_t interval = refreshIntervalFor(app.page());
    const bool in_full_refresh_cooldown = (last_full_refresh_ms != 0) &&
                                         ((now - last_full_refresh_ms) < kManualFullRefreshCooldownMs);
    app.updateChargeAnimation();
    if (in_full_refresh_cooldown)
    {
        app.consumeDisplayChangeFlags();
        force_render = false;
        force_full_clear = false;
        delay(20);
        return;
    }

    if (app.consumeAlarmDisplayChanged())
    {
        force_render = true;
        force_full_clear = true;
    }
    if (app.consumeAlarmAutoDismissed())
    {
        force_render = true;
        force_full_clear = false;
    }
    if (app.consumeChargeAnimDisplayChanged())
    {
        force_render = true;
        force_full_clear = false;
    }
    if (app.consumeSpeedTestDisplayChanged())
    {
        force_render = true;
    }

    if (interval > 0 && (now - last_render_ms) >= interval)
    {
        force_render = true;
    }

    if (force_render)
    {
        if (app.consumeChargeAnimDisplayChanged())
        {
            force_full_clear = false;
        }
        if (app.consumeBatteryDisplayChanged())
        {
            force_full_clear = false;
        }
        renderWithAntiGhosting(now);
    }

    delay(20);
}
