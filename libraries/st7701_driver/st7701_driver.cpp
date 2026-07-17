#include "st7701_driver.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "t_panel_config.h"

namespace st7701_driver {
namespace {

constexpr uint32_t kSpiClockHalfPeriodUs = 1;
constexpr uint32_t kBacklightFrequencyHz = 2000;
constexpr uint32_t kBacklightMaxDuty = (1U << 13) - 1;
constexpr ledc_mode_t kBacklightSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;

struct InitCommand {
  uint8_t command;
  std::array<uint8_t, 16> data;
  size_t data_size;
  uint32_t delay_ms;
};

static constexpr InitCommand kInitSequence[] = {
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, {0x08}, 1, 0},
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, {0x3B, 0x00}, 2, 0},
    {0xC1, {0x0B, 0x02}, 2, 0},
    {0xC2, {0x30, 0x02, 0x37}, 3, 0},
    {0xCC, {0x10}, 1, 0},
    {0xB0,
        {0x00, 0x0F, 0x16, 0x0E, 0x11, 0x07, 0x09, 0x09, 0x08, 0x23, 0x05,
            0x11, 0x0F, 0x28, 0x2D, 0x18},
        16, 0},
    {0xB1,
        {0x00, 0x0F, 0x16, 0x0E, 0x11, 0x07, 0x09, 0x08, 0x09, 0x23, 0x05,
            0x11, 0x0F, 0x28, 0x2D, 0x18},
        16, 0},
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, {0x4D}, 1, 0},
    {0xB1, {0x33}, 1, 0},
    {0xB2, {0x87}, 1, 0},
    {0xB5, {0x4B}, 1, 0},
    {0xB7, {0x8C}, 1, 0},
    {0xB8, {0x20}, 1, 0},
    {0xC1, {0x78}, 1, 0},
    {0xC2, {0x78}, 1, 0},
    {0xD0, {0x88}, 1, 0},
    {0xE0, {0x00, 0x00, 0x02}, 3, 0},
    {0xE1, {0x02, 0xF0, 0x00, 0x00, 0x03, 0xF0, 0x00, 0x00, 0x00, 0x44,
               0x44},
        11, 0},
    {0xE2,
        {0x10, 0x10, 0x40, 0x40, 0xF2, 0xF0, 0x00, 0x00, 0xF2, 0xF0, 0x00,
            0x00},
        12, 0},
    {0xE3, {0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, {0x44, 0x44}, 2, 0},
    {0xE5,
        {0x07, 0xEF, 0xF0, 0xF0, 0x09, 0xF1, 0xF0, 0xF0, 0x03, 0xF3, 0xF0,
            0xF0, 0x05, 0xED, 0xF0, 0xF0},
        16, 0},
    {0xE6, {0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, {0x44, 0x44}, 2, 0},
    {0xE8,
        {0x08, 0xF0, 0xF0, 0xF0, 0x0A, 0xF2, 0xF0, 0xF0, 0x04, 0xF4, 0xF0,
            0xF0, 0x06, 0xEE, 0xF0, 0xF0},
        16, 0},
    {0xEB, {0x00, 0x00, 0xE4, 0xE4, 0x44, 0x88, 0x40}, 7, 0},
    {0xEC, {0x78, 0x00}, 2, 0},
    {0xED,
        {0x20, 0xF9, 0x87, 0x76, 0x65, 0x54, 0x4F, 0xFF, 0xFF, 0xF4, 0x45,
            0x56, 0x67, 0x78, 0x9F, 0x02},
        16, 0},
    {0xEF, {0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}, 6, 0},
    {0x3A, {0x55}, 1, 0},
    {0x36, {0x08}, 1, 0},
    {0x11, {}, 0, 120},
    {0x29, {}, 0, 20},
};

void DelayUs(uint32_t delay_us) {
  if (delay_us >= 1000) {
    vTaskDelay(pdMS_TO_TICKS(delay_us / 1000));
  } else if (delay_us > 0) {
    esp_rom_delay_us(delay_us);
  }
}

bool SetSpiLine(int pin, bool level) {
  return gpio_set_level(static_cast<gpio_num_t>(pin), level ? 1 : 0) == ESP_OK;
}

bool ConfigureSpiPins() {
  gpio_config_t config = {};
  config.pin_bit_mask =
      (1ULL << t_panel_lite::gpio::st7701::kSpiCs) |
      (1ULL << t_panel_lite::gpio::st7701::kSpiSclk) |
      (1ULL << t_panel_lite::gpio::st7701::kSpiMosi);
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&config) != ESP_OK) {
    printf("Configure ST7701 SPI pins failed\n");
    return false;
  }

  return SetSpiLine(t_panel_lite::gpio::st7701::kSpiCs, true) &&
         SetSpiLine(t_panel_lite::gpio::st7701::kSpiSclk, false) &&
         SetSpiLine(t_panel_lite::gpio::st7701::kSpiMosi, false);
}

bool SpiWriteByte(int dc_bit, uint8_t data) {
  uint16_t packet = (static_cast<uint16_t>(dc_bit) << 8) | data;
  for (int bit = 8; bit >= 0; --bit) {
    if (!SetSpiLine(t_panel_lite::gpio::st7701::kSpiSclk, false) ||
        !SetSpiLine(t_panel_lite::gpio::st7701::kSpiMosi,
            (packet & (1U << bit)) != 0)) {
      return false;
    }
    DelayUs(kSpiClockHalfPeriodUs);
    if (!SetSpiLine(t_panel_lite::gpio::st7701::kSpiSclk, true)) {
      return false;
    }
    DelayUs(kSpiClockHalfPeriodUs);
  }
  return true;
}

bool SpiWritePackage(bool is_command, uint8_t data) {
  if (!SetSpiLine(t_panel_lite::gpio::st7701::kSpiCs, false)) {
    return false;
  }
  DelayUs(kSpiClockHalfPeriodUs);
  if (!SpiWriteByte(is_command ? 0 : 1, data)) {
    return false;
  }
  if (!SetSpiLine(t_panel_lite::gpio::st7701::kSpiSclk, false) ||
      !SetSpiLine(t_panel_lite::gpio::st7701::kSpiMosi, false)) {
    return false;
  }
  DelayUs(kSpiClockHalfPeriodUs);
  if (!SetSpiLine(t_panel_lite::gpio::st7701::kSpiCs, true)) {
    return false;
  }
  DelayUs(kSpiClockHalfPeriodUs);
  return true;
}

bool SendCommand(const InitCommand& init_command) {
  if (!SpiWritePackage(true, init_command.command)) {
    return false;
  }
  for (size_t i = 0; i < init_command.data_size; ++i) {
    if (!SpiWritePackage(false, init_command.data[i])) {
      return false;
    }
  }
  if (init_command.delay_ms > 0) {
    vTaskDelay(pdMS_TO_TICKS(init_command.delay_ms));
  }
  return true;
}

bool InitSt7701BySpi() {
  if (!ConfigureSpiPins()) {
    return false;
  }
  for (const auto& init_command : kInitSequence) {
    if (!SendCommand(init_command)) {
      printf("Send ST7701 init command 0x%02X failed\n", init_command.command);
      return false;
    }
  }
  return true;
}

bool InitRgbPanel(esp_lcd_panel_handle_t* panel_handle) {
  esp_lcd_rgb_panel_config_t rgb_config = {
      .clk_src = LCD_CLK_SRC_DEFAULT,
      .timings =
          {
              .pclk_hz = t_panel_lite::device::st7701::kPixelClockHz,
              .h_res = t_panel_lite::device::st7701::kWidth,
              .v_res = t_panel_lite::device::st7701::kHeight,
              .hsync_pulse_width =
                  t_panel_lite::device::st7701::kHsyncPulseWidth,
              .hsync_back_porch =
                  t_panel_lite::device::st7701::kHsyncBackPorch,
              .hsync_front_porch =
                  t_panel_lite::device::st7701::kHsyncFrontPorch,
              .vsync_pulse_width =
                  t_panel_lite::device::st7701::kVsyncPulseWidth,
              .vsync_back_porch =
                  t_panel_lite::device::st7701::kVsyncBackPorch,
              .vsync_front_porch =
                  t_panel_lite::device::st7701::kVsyncFrontPorch,
              .flags =
                  {
                      .hsync_idle_low =
                          t_panel_lite::device::st7701::kHsyncIdleLow,
                      .vsync_idle_low =
                          t_panel_lite::device::st7701::kVsyncIdleLow,
                      .de_idle_high =
                          t_panel_lite::device::st7701::kDeIdleHigh,
                      .pclk_active_neg =
                          t_panel_lite::device::st7701::kPclkActiveNeg,
                      .pclk_idle_high =
                          t_panel_lite::device::st7701::kPclkIdleHigh,
                  },
          },
      .data_width = 16,
      .bits_per_pixel = 16,
      .num_fbs = t_panel_lite::device::st7701::kFrameBufferCount,
      .bounce_buffer_size_px =
          t_panel_lite::device::st7701::kWidth *
          t_panel_lite::device::st7701::kBounceBufferHeight,
      .sram_trans_align = 8,
      .psram_trans_align = 64,
      .hsync_gpio_num = t_panel_lite::gpio::st7701::kHsync,
      .vsync_gpio_num = t_panel_lite::gpio::st7701::kVsync,
      .de_gpio_num = t_panel_lite::gpio::st7701::kDe,
      .pclk_gpio_num = t_panel_lite::gpio::st7701::kPclk,
      .disp_gpio_num = GPIO_NUM_NC,
      .data_gpio_nums =
          {
              t_panel_lite::gpio::st7701::kR0,
              t_panel_lite::gpio::st7701::kR1,
              t_panel_lite::gpio::st7701::kR2,
              t_panel_lite::gpio::st7701::kR3,
              t_panel_lite::gpio::st7701::kR4,
              t_panel_lite::gpio::st7701::kG0,
              t_panel_lite::gpio::st7701::kG1,
              t_panel_lite::gpio::st7701::kG2,
              t_panel_lite::gpio::st7701::kG3,
              t_panel_lite::gpio::st7701::kG4,
              t_panel_lite::gpio::st7701::kG5,
              t_panel_lite::gpio::st7701::kB0,
              t_panel_lite::gpio::st7701::kB1,
              t_panel_lite::gpio::st7701::kB2,
              t_panel_lite::gpio::st7701::kB3,
              t_panel_lite::gpio::st7701::kB4,
          },
      .flags =
          {
              .disp_active_low = 0,
              .refresh_on_demand = 0,
              .fb_in_psram = 1,
              .double_fb = 0,
              .no_fb = 0,
              .bb_invalidate_cache = 0,
          },
  };

  if (esp_lcd_new_rgb_panel(&rgb_config, panel_handle) != ESP_OK) {
    printf("New RGB panel failed\n");
    return false;
  }
  if (esp_lcd_panel_reset(*panel_handle) != ESP_OK) {
    printf("Reset RGB panel failed\n");
    return false;
  }
  if (esp_lcd_panel_init(*panel_handle) != ESP_OK) {
    printf("Init RGB panel failed\n");
    return false;
  }
  return true;
}

uint32_t BacklightDuty(uint8_t percent) {
  const uint32_t clamped = std::min<uint32_t>(percent, 100);
  return clamped * kBacklightMaxDuty / 100;
}

}  // namespace

bool InitSt7701(esp_lcd_panel_handle_t* panel_handle) {
  if (panel_handle == nullptr) {
    printf("Invalid ST7701 panel handle\n");
    return false;
  }
  if (!InitSt7701BySpi()) {
    return false;
  }
  return InitRgbPanel(panel_handle);
}

bool InitBacklight() {
  ledc_timer_config_t timer_config = {};
  timer_config.speed_mode = kBacklightSpeedMode;
  timer_config.duty_resolution = LEDC_TIMER_13_BIT;
  timer_config.timer_num = kBacklightTimer;
  timer_config.freq_hz = kBacklightFrequencyHz;
  timer_config.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&timer_config) != ESP_OK) {
    printf("Configure ST7701 backlight timer failed\n");
    return false;
  }

  ledc_channel_config_t channel_config = {};
  channel_config.gpio_num = t_panel_lite::gpio::st7701::kBacklight;
  channel_config.speed_mode = kBacklightSpeedMode;
  channel_config.channel = kBacklightChannel;
  channel_config.intr_type = LEDC_INTR_DISABLE;
  channel_config.timer_sel = kBacklightTimer;
  channel_config.duty = 0;
  channel_config.hpoint = 0;
  if (ledc_channel_config(&channel_config) != ESP_OK) {
    printf("Configure ST7701 backlight channel failed\n");
    return false;
  }

  const esp_err_t fade_result = ledc_fade_func_install(0);
  if (fade_result != ESP_OK && fade_result != ESP_ERR_INVALID_STATE) {
    printf("Install ST7701 backlight fade service failed\n");
    return false;
  }
  return true;
}

bool SetBacklight(uint8_t duty) {
  const uint32_t raw_duty = BacklightDuty(duty);
  if (ledc_set_duty(kBacklightSpeedMode, kBacklightChannel, raw_duty) !=
          ESP_OK ||
      ledc_update_duty(kBacklightSpeedMode, kBacklightChannel) != ESP_OK) {
    printf("Set ST7701 backlight failed\n");
    return false;
  }
  return true;
}

bool StartBacklightGradient(uint8_t target_duty, int32_t time_ms) {
  const uint32_t fade_time_ms = std::max<int32_t>(time_ms, 0);
  if (ledc_set_fade_with_time(kBacklightSpeedMode, kBacklightChannel,
          BacklightDuty(target_duty), fade_time_ms) != ESP_OK ||
      ledc_fade_start(kBacklightSpeedMode, kBacklightChannel,
          LEDC_FADE_NO_WAIT) != ESP_OK) {
    printf("Start ST7701 backlight gradient failed\n");
    return false;
  }
  return true;
}

}  // namespace st7701_driver
