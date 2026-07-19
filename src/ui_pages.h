#pragma once

#include "app_state.h"

namespace ui_pages
{

void renderDashboard(const AppState& state);
void renderClock(const AppState& state);
void renderPower(const AppState& state);
void renderNetwork(const AppState& state);
void renderBeelink(const AppState& state);
void renderBeelinkCpuDetail(const AppState& state);
void renderBeelinkMemDetail(const AppState& state);
void renderBeelinkTempDetail(const AppState& state);
void renderBeelinkUptimeDetail(const AppState& state);
void renderSystem(const AppState& state);
void renderSleep(const AppState& state);

} // namespace ui_pages
