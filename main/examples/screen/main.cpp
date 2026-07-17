/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-26 16:17:47
 * @LastEditTime: 2026-05-28 14:42:59
 * @License: GPL 3.0
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "material_16bit_480x480px.h"
#include "st7701_driver.h"
#include "t_panel_config.h"

namespace {

constexpr int kImagePlayIntervalMs = 1000;
constexpr int kBacklightFadeTimeMs = 800;

const uint8_t* const kImageTable[] = {
    gImage_1,
    gImage_2,
    gImage_3,
    gImage_4,
    gImage_5,
};

constexpr size_t kImageCount = sizeof(kImageTable) / sizeof(kImageTable[0]);

bool ShowImage(esp_lcd_panel_handle_t panel_handle, size_t image_index) {
  if (esp_lcd_panel_draw_bitmap(panel_handle, 0, 0,
          t_panel_lite::device::st7701::kWidth,
          t_panel_lite::device::st7701::kHeight,
          kImageTable[image_index]) != ESP_OK) {
    printf("Show image %u failed\n", static_cast<unsigned>(image_index));
    return false;
  }

  return true;
}

}  // namespace

extern "C" void app_main(void) {
  printf("Ciallo\n");

  esp_lcd_panel_handle_t panel_handle = nullptr;

  if (!st7701_driver::InitSt7701(&panel_handle)) {
    printf("InitSt7701 failed\n");
    return;
  }
  if (!st7701_driver::InitBacklight()) {
    printf("InitBacklight failed\n");
    return;
  }

  if (!ShowImage(panel_handle, 0)) {
    printf("ShowImage failed\n");
    return;
  }
  st7701_driver::StartBacklightGradient(100, kBacklightFadeTimeMs);

  size_t image_index = 1;
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(kImagePlayIntervalMs));
    ShowImage(panel_handle, image_index);
    image_index = (image_index + 1) % kImageCount;
  }
}
