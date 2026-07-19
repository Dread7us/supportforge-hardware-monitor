#include "buttons.h"

#include "app_config.h"

#include <M5CoreInk.h>

void ButtonController::begin()
{
    last_event_ms_ = millis();
}

ButtonEvent ButtonController::poll()
{
    M5.update();

    const uint32_t now = millis();
    if ((now - last_event_ms_) < app_config::kDebounceMs)
    {
        return ButtonEvent::None;
    }

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

    if (event != ButtonEvent::None)
    {
        last_event_ms_ = now;
    }

    return event;
}
