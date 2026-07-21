#include "display.h"
#include "app_config.h"
#include "ui_pages.h"
#include <M5CoreInk.h>

namespace
{

bool sprite_ready = false;
constexpr size_t kFrameBufferSize = (app_config::kScreenWidth * app_config::kScreenHeight) / 8;
uint8_t frame_buffer[kFrameBufferSize];
uint8_t last_frame_buffer[kFrameBufferSize];
bool clip_enabled = false;
int clip_x = 0;
int clip_y = 0;
int clip_w = app_config::kScreenWidth;
int clip_h = app_config::kScreenHeight;

void drawPixelClipped(int x, int y, bool black);

Ink_eSPI_font_t* fontPtr(coreink_gfx::Font font)
{
    return font == coreink_gfx::Font::Large ? &AsciiFont24x48 : &AsciiFont8x16;
}

void drawDegreeGlyph(int x, int y, Ink_eSPI_font_t* font, bool inverse)
{
    const int radius = font->_width >= 24 ? 4 : 2;
    const int center_x = x + (font->_width / 2);
    const int center_y = y + (font->_height >= 48 ? 8 : 3);

    for (uint16_t yy = 0; yy < font->_height; ++yy)
    {
        for (uint16_t xx = 0; xx < font->_width; ++xx)
        {
            const int dx = static_cast<int>(x + xx) - center_x;
            const int dy = static_cast<int>(y + yy) - center_y;
            const int dist = (dx * dx) + (dy * dy);
            const bool glyph_on = dist >= ((radius - 1) * (radius - 1)) && dist <= (radius * radius);
            drawPixelClipped(x + xx, y + yy, inverse ? !glyph_on : glyph_on);
        }
    }
}

void drawPixelClipped(int x, int y, bool black)
{
    if (!sprite_ready || x < 0 || y < 0 || x >= app_config::kScreenWidth || y >= app_config::kScreenHeight)
    {
        return;
    }
    if (clip_enabled && (x < clip_x || y < clip_y || x >= clip_x + clip_w || y >= clip_y + clip_h))
    {
        return;
    }
    const uint32_t pix_num = static_cast<uint32_t>(app_config::kScreenWidth) * static_cast<uint32_t>(y) + static_cast<uint32_t>(x);
    const uint8_t mask = 0x80 >> (pix_num % 8);
    if (black)
    {
        frame_buffer[pix_num / 8] &= ~mask;
    }
    else
    {
        frame_buffer[pix_num / 8] |= mask;
    }
}

} // namespace

namespace coreink_gfx
{

void fillRect(int x, int y, int w, int h, bool black)
{
    for (int yy = 0; yy < h; ++yy)
    {
        for (int xx = 0; xx < w; ++xx)
        {
            drawPixelClipped(x + xx, y + yy, black);
        }
    }
}

void drawRect(int x, int y, int w, int h, bool black)
{
    drawHLine(x, y, w, black);
    drawHLine(x, y + h - 1, w, black);
    for (int yy = 0; yy < h; ++yy)
    {
        drawPixelClipped(x, y + yy, black);
        drawPixelClipped(x + w - 1, y + yy, black);
    }
}

void drawHLine(int x, int y, int w, bool black)
{
    for (int xx = 0; xx < w; ++xx)
    {
        drawPixelClipped(x + xx, y, black);
    }
}

void drawVLine(int x, int y, int h, bool black)
{
    for (int yy = 0; yy < h; ++yy)
    {
        drawPixelClipped(x, y + yy, black);
    }
}

void drawPixel(int x, int y, bool black)
{
    drawPixelClipped(x, y, black);
}

void drawText(int x, int y, const char* text, Font font, bool inverse)
{
    if (!text)
    {
        return;
    }

    Ink_eSPI_font_t* selected_font = fontPtr(font);
    int cursor_x = x;
    while (*text != '\0')
    {
        const unsigned char ch = static_cast<unsigned char>(*text++);
        if (ch == 0xB0 || (ch == 0xC2 && static_cast<unsigned char>(*text) == 0xB0))
        {
            if (ch == 0xC2)
            {
                ++text;
            }
            drawDegreeGlyph(cursor_x, y, selected_font, inverse);
            cursor_x += selected_font->_width;
            continue;
        }

        if (ch < 0x20 || ch > 0x7E)
        {
            cursor_x += selected_font->_width;
            continue;
        }

        const uint16_t glyph_index = ch - 0x20;
        const uint8_t* glyph = selected_font->_fontptr + glyph_index * selected_font->_fontSize;
        for (uint16_t yy = 0; yy < selected_font->_height; ++yy)
        {
            for (uint16_t xx = 0; xx < selected_font->_width; ++xx)
            {
                const uint16_t bit_index = yy * selected_font->_width + xx;
                const uint8_t mask = 0x80 >> (bit_index % 8);
                const bool glyph_on = (glyph[bit_index / 8] & mask) != 0;
                drawPixelClipped(cursor_x + xx, y + yy, inverse ? !glyph_on : glyph_on);
            }
        }
        cursor_x += selected_font->_width;
    }
}

void setClipRect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
    {
        clip_enabled = true;
        clip_x = 0;
        clip_y = 0;
        clip_w = 0;
        clip_h = 0;
        return;
    }

    const int x2 = x + w;
    const int y2 = y + h;
    clip_x = x < 0 ? 0 : x;
    clip_y = y < 0 ? 0 : y;
    const int clipped_x2 = x2 > app_config::kScreenWidth ? app_config::kScreenWidth : x2;
    const int clipped_y2 = y2 > app_config::kScreenHeight ? app_config::kScreenHeight : y2;
    clip_w = clipped_x2 > clip_x ? clipped_x2 - clip_x : 0;
    clip_h = clipped_y2 > clip_y ? clipped_y2 - clip_y : 0;
    clip_enabled = true;
}

void clearClipRect()
{
    clip_enabled = false;
}

} // namespace coreink_gfx

bool Display::begin()
{
    memset(frame_buffer, 0xFF, sizeof(frame_buffer));
    memset(last_frame_buffer, 0xFF, sizeof(last_frame_buffer));

    // Do a true full-screen clean at boot. This avoids the CoreInk panel waking
    // in a stale partial-update window, which can leave a tiny quadrant at the
    // bottom with the rest of the screen dark until a button forces a redraw.
    M5.M5Ink.clear(INK_CLENR_MODE0);
    M5.M5Ink.drawBuff(frame_buffer, true);
    sprite_ready = M5.M5Ink.isInit();
    return sprite_ready;
}

void Display::showSplash(const char* status)
{
    clear(false);
    coreink_gfx::drawRect(8, 8, 184, 184);
    coreink_gfx::drawRect(12, 12, 176, 176);
    coreink_gfx::fillRect(28, 32, 144, 34, true);
    coreink_gfx::drawText(32, 41, app_config::kAppName, coreink_gfx::Font::Small, true);
    coreink_gfx::drawText(40, 82, "Starting up", coreink_gfx::Font::Small);
    coreink_gfx::drawHLine(38, 106, 124);
    coreink_gfx::fillRect(38, 102, 72, 8, true);
    coreink_gfx::drawText(28, 126, status && status[0] ? status : "WiFi + clock ready", coreink_gfx::Font::Small);
    coreink_gfx::drawText(32, 158, "PWR next  MID sync", coreink_gfx::Font::Small);
    push(true);
}

void Display::render(const AppState& state, bool force_full_clear)
{
    const bool full_refresh = force_full_clear || state.shouldFullClear();
    clear(full_refresh);

    if (force_full_clear)
    {
        M5.M5Ink.clear();
        delay(850); // Wait for the 0.85s hardware cycle to physically complete
    }

    switch (state.page())
    {
    case Page::Alarm:
        ui_pages::renderAlarmPage(state);
        break;
    case Page::Dashboard:
        ui_pages::renderDashboard(state);
        break;
    case Page::Clock:
        ui_pages::renderClock(state);
        break;
    case Page::Power:
        ui_pages::renderPower(state);
        break;
    case Page::Network:
        ui_pages::renderNetwork(state);
        break;
    case Page::Beelink:
        ui_pages::renderBeelink(state);
        break;
    case Page::BeelinkCpuDetail:
        ui_pages::renderBeelinkCpuDetail(state);
        break;
    case Page::BeelinkMemDetail:
        ui_pages::renderBeelinkMemDetail(state);
        break;
    case Page::BeelinkTempDetail:
        ui_pages::renderBeelinkTempDetail(state);
        break;
    case Page::BeelinkUptimeDetail:
        ui_pages::renderBeelinkUptimeDetail(state);
        break;
    case Page::System:
        ui_pages::renderSystem(state);
        break;
    case Page::Sleep:
        ui_pages::renderSleep(state);
        break;
    default:
        ui_pages::renderDashboard(state);
        break;
    }

    push(full_refresh);
}

void Display::clear(bool full_clear)
{
    memset(frame_buffer, 0xFF, sizeof(frame_buffer));
    if (full_clear)
    {
        memset(last_frame_buffer, 0xFF, sizeof(last_frame_buffer));
    }
}

void Display::push(bool full_refresh)
{
    if (!sprite_ready)
    {
        return;
    }

    if (full_refresh)
    {
        M5.M5Ink.drawBuff(frame_buffer, true);
    }
    else
    {
        if (M5.M5Ink.getMode() != INK_PARTIAL_MODE)
        {
            M5.M5Ink.switchMode(INK_PARTIAL_MODE);
        }
        M5.M5Ink.setDrawAddr(0, 0, app_config::kScreenWidth, app_config::kScreenHeight);
        M5.M5Ink.drawBuff(last_frame_buffer, frame_buffer, sizeof(frame_buffer));
    }
    memcpy(last_frame_buffer, frame_buffer, sizeof(last_frame_buffer));
}
