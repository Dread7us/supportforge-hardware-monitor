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

uint32_t last_render_ms = 0;
bool force_render = true;
bool force_full_clear = true;

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
        return 0;
    case Page::Power:
    case Page::Network:
    case Page::Beelink:
    case Page::BeelinkCpuDetail:
    case Page::BeelinkMemDetail:
    case Page::BeelinkTempDetail:
    case Page::BeelinkUptimeDetail:
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
        force_render = true;
        force_full_clear = true;
        return true;
    }

    return false;
}

void handleBeelinkButton(ButtonEvent event)
{
    BasementStatus& basement = app.basement();
    switch (event)
    {
    case ButtonEvent::Next:
        if (basement.beelink_cursor >= 4)
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
            if (app.refresh_interval_ms == 15000U)
            {
                app.refresh_interval_ms = 30000U;
            }
            else if (app.refresh_interval_ms == 30000U)
            {
                app.refresh_interval_ms = 60000U;
            }
            else if (app.refresh_interval_ms == 60000U)
            {
                app.refresh_interval_ms = 300000U;
            }
            else
            {
                app.refresh_interval_ms = 15000U;
            }
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
        app.forceNetworkRefresh();
        force_render = true;
        force_full_clear = true;
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
    force_render = false;
    force_full_clear = false;

    Serial.println("M5 CoreInk UI started");
}

void loop()
{
    const ButtonEvent event = buttons.poll();
    handleButton(event);
    app.updateServerAlarm();

    const uint32_t now = millis();
    const uint32_t interval = refreshIntervalFor(app.page());
    app.updateChargeAnimation();
    if (app.consumeChargeAnimDisplayChanged())
    {
        force_render = true;
        force_full_clear = false;
    }

    if (interval > 0 && (now - last_render_ms) >= interval)
    {
        force_render = true;
    }

    if (force_render)
    {
        app.update();
        if (app.consumeChargeAnimDisplayChanged())
        {
            force_full_clear = false;
        }
        if (app.consumeBatteryDisplayChanged())
        {
            force_full_clear = false;
        }
        display.render(app, force_full_clear);
        app.noteRefresh();
        last_render_ms = now;
        force_render = false;
        force_full_clear = false;
    }

    delay(20);
}
