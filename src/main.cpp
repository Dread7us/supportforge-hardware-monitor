#include "app_config.h"
#include "app_state.h"
#include "buttons.h"
#include "display.h"
#include <Arduino.h>
#include <M5CoreInk.h>

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
bool last_rendered_speedtest_running = false;

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

bool speedTestViewTransitionRequiresFullClear(Page previous_page,
                                              Page current_page,
                                              bool previous_running,
                                              bool current_running)
{
    const bool was_speed_detail = previous_page == Page::BeelinkSpeedTestDetail;
    const bool is_speed_detail = current_page == Page::BeelinkSpeedTestDetail;
    if (was_speed_detail != is_speed_detail)
    {
        return true;
    }

    // Running -> complete/error and idle/result -> running are materially
    // different speed-test views. Promote them to true full hardware clears;
    // animation phase changes while still running remain partial updates.
    return is_speed_detail && previous_running != current_running;
}

void renderWithAntiGhosting(uint32_t now)
{
    const Page current_page = app.page();
    const bool current_speedtest_running = app.basement().speedtest.is_running;
    const bool page_changed = current_page != last_rendered_page;
    const bool speedtest_transition = speedTestViewTransitionRequiresFullClear(last_rendered_page,
                                                                               current_page,
                                                                               last_rendered_speedtest_running,
                                                                               current_speedtest_running);
    const bool needs_full_clear = force_full_clear || page_changed || speedtest_transition;

    display.render(app, needs_full_clear);
    app.noteRefresh();
    last_render_ms = now;
    last_rendered_page = current_page;
    last_rendered_speedtest_running = current_speedtest_running;
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
            force_full_clear = true;
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
    last_rendered_speedtest_running = app.basement().speedtest.is_running;
    force_render = false;
    force_full_clear = false;

    Serial.println("M5 CoreInk UI started");
}

void loop()
{
    const ButtonEvent event = buttons.poll();
    handleButton(event);

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
        last_rendered_speedtest_running = app.basement().speedtest.is_running;
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
