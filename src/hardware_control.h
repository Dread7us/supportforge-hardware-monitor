#pragma once

#include <Arduino.h>
#include <M5CoreInk.h>

inline void setStatusLedEnabled(bool enabled)
{
    // Physical CoreInk verification: LED_EXT_PIN is active-low.
    pinMode(LED_EXT_PIN, OUTPUT);
    digitalWrite(LED_EXT_PIN, enabled ? LOW : HIGH);
}
