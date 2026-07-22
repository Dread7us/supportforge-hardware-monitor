#include "buttons.h"

#include "app_config.h"

#include <M5CoreInk.h>

void ButtonController::begin()
{
    last_event_ms_ = 0;
    button_lockout_ = false;
}

ButtonEvent ButtonController::poll()
{
    M5.update();

    const uint32_t now = millis();
    const bool any_button_down = M5.BtnUP.isPressed() ||
                                 M5.BtnDOWN.isPressed() ||
                                 M5.BtnMID.isPressed() ||
                                 M5.BtnPWR.isPressed() ||
                                 M5.BtnEXT.isPressed();

    ButtonEvent event = ButtonEvent::None;

    if (M5.BtnUP.wasPressed())
    {
        event = ButtonEvent::Previous;
    }
    else if (M5.BtnDOWN.wasPressed())
    {
        event = ButtonEvent::Next;
    }
    else if (M5.BtnMID.wasPressed())
    {
        event = ButtonEvent::Select;
    }
    else if (M5.BtnPWR.wasPressed())
    {
        event = ButtonEvent::FastRefresh;
    }
    else if (M5.BtnEXT.wasPressed())
    {
        event = ButtonEvent::FullRefresh;
    }

    if (button_lockout_)
    {
        if (!any_button_down && (now - last_event_ms_) >= app_config::kDebounceMs)
        {
            button_lockout_ = false;
        }
        return ButtonEvent::None;
    }

    if (event != ButtonEvent::None && (now - last_event_ms_) >= app_config::kDebounceMs)
    {
        last_event_ms_ = now;
        button_lockout_ = true;
        return event;
    }

    return ButtonEvent::None;
}
