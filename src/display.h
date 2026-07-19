#pragma once

#include "app_state.h"

#include <cstdint>

namespace coreink_gfx
{

enum class Font : uint8_t
{
    Small,
    Large,
};

void fillRect(int x, int y, int w, int h, bool black);
void drawRect(int x, int y, int w, int h, bool black = true);
void drawHLine(int x, int y, int w, bool black = true);
void drawVLine(int x, int y, int h, bool black = true);
void drawPixel(int x, int y, bool black = true);
void drawText(int x, int y, const char* text, Font font = Font::Small, bool inverse = false);
void setClipRect(int x, int y, int w, int h);
void clearClipRect();

} // namespace coreink_gfx

class Display
{
  public:
    bool begin();
    void showSplash(const char* status = nullptr);
    void render(const AppState& state, bool force_full_clear = false);

  private:
    void clear(bool full_clear);
    void push(bool full_refresh);
};
