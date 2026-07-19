#pragma once

#include <Arduino.h>
#include <cstdint>

enum class ButtonEvent : uint8_t
{
    None = 0,
    Next,
    Previous,
    Select,
    FastRefresh,
    FullRefresh,
};

class ButtonController
{
  public:
    void begin();
    ButtonEvent poll();

  private:
    uint32_t last_event_ms_ = 0;
};
