#pragma once

#include <cstdint>

#include "esp_lcd_panel_ops.h"

namespace st7701_driver {

bool InitSt7701(esp_lcd_panel_handle_t* panel_handle);
bool InitBacklight();
bool SetBacklight(uint8_t duty);
bool StartBacklightGradient(uint8_t target_duty, int32_t time_ms);

}  // namespace st7701_driver
