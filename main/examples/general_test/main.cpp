/*
 * @Description: Integrated LVGL factory test for T-Panel-Lite
 * @Author: LILYGO_L
 * @Date: 2026-05-29
 * @License: GPL 3.0
 */
#include <sys/lock.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <dirent.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "draw/sw/lv_draw_sw.h"
#include "lvgl.h"
#include "lvgl_startup_images.h"
#include "nvs_flash.h"
#include "sd_protocol_defs.h"
#include "sdmmc_cmd.h"
#include "st7701_driver.h"
#include "t_panel_config.h"

namespace {

constexpr int kLvglTickPeriodMs = 1;
constexpr int kLvglTaskStackSize = 14 * 1024;
constexpr int kLvglTaskPriority = 2;
constexpr int kLvglRefreshPeriodMs = 20;
constexpr int kLvglTaskMinDelayMs = kLvglRefreshPeriodMs;
constexpr int kLvglTaskMaxDelayMs = 500;
constexpr int kButtonTaskStackSize = 3 * 1024;
constexpr int kButtonTaskPriority = 1;
constexpr int kButtonPollMs = 20;
constexpr int kButtonDebounceMs = 80;
constexpr int kLvglDrawBufferLines = 20;
constexpr int kBacklightFadeTimeMs = 800;
constexpr int kBytesPerPixel = 2;

constexpr int kWifiConnectTimeoutMs = 10000;
constexpr char kWifiSsid[] = "LilyGo-AABB";
constexpr char kWifiPassword[] = "xinyuandianzi";
constexpr int kSdTaskStackSize = 8 * 1024;
constexpr int kSdTaskPriority = 1;
constexpr int kSdSpiMaxFreqKhz = 10000;
constexpr int kMaxSdTreeDepth = 4;
constexpr int kMaxSdTreeEntries = 32;
constexpr char kSdMountPoint[] = "/sdcard";
constexpr int kPageCount = 4;
constexpr int kDotHitSize = 42;
constexpr int kDotGap = 4;
constexpr int kDotRowWidth = kDotHitSize * kPageCount + kDotGap * (kPageCount - 1);
constexpr int kIndicatorHeight = 72;
constexpr int kPageWidth = t_panel_lite::device::st7701::kWidth;
constexpr int kPageHeight = t_panel_lite::device::st7701::kHeight;
constexpr int kCardWidth = 410;
constexpr int kCardHeight = 330;
constexpr int kPrimaryStatusPanelHeight = 190;
constexpr int kDisplayTestEnterPixelClockHz = 11000000;
constexpr uint32_t kColorWindow = 0xF4F6FB;
constexpr uint32_t kColorSurface = 0xFFFBFE;
constexpr uint32_t kColorSurfaceContainer = 0xF0F3FA;
constexpr uint32_t kColorSurfaceContainerHigh = 0xE9EDF5;
constexpr uint32_t kColorPrimary = 0x2F6BFF;
constexpr uint32_t kColorPrimaryPressed = 0x1D56D6;
constexpr uint32_t kColorPrimaryContainer = 0xDCE7FF;
constexpr uint32_t kColorOnSurface = 0x1D1B20;
constexpr uint32_t kColorOnSurfaceVariant = 0x5E6470;
constexpr uint32_t kColorOutline = 0xD6DAE3;

enum class Page : int {
  kDisplay = 0,
  kRotation,
  kWifiTime,
  kSd,
};

enum class ButtonFunction {
  kNone,
  kPreviousPage,
  kConfirm,
  kNextPage,
};

_lock_t g_lvgl_api_lock;

lv_display_t* g_display = nullptr;
esp_lcd_panel_handle_t g_panel_handle = nullptr;
lv_obj_t* g_root = nullptr;
lv_obj_t* g_tileview = nullptr;
lv_obj_t* g_tiles[kPageCount] = {};
lv_obj_t* g_page_dots[kPageCount] = {};
lv_obj_t* g_action_buttons[kPageCount] = {};
lv_obj_t* g_page_name_label = nullptr;
lv_obj_t* g_fullscreen_display = nullptr;
lv_obj_t* g_rotation_status = nullptr;
lv_obj_t* g_wifi_status = nullptr;
lv_obj_t* g_sd_status = nullptr;

Page g_active_page = Page::kDisplay;
int g_display_pattern = 0;
bool g_wifi_ready = false;
TaskHandle_t g_wifi_task_handle = nullptr;
bool g_sd_spi_bus_ready = false;
bool g_sd_mounted = false;
sdmmc_card_t* g_sd_card = nullptr;
TaskHandle_t g_sd_task_handle = nullptr;
lv_display_rotation_t g_display_rotation = LV_DISPLAY_ROTATION_180;
void* g_rotated_flush_buffer = nullptr;
size_t g_rotated_flush_buffer_size = 0;

const lv_image_dsc_t* const kWallpaperTable[] = {
    &kLvglStartupImage1,
    &kLvglStartupImage2,
    &kLvglStartupImage3,
};

void SetLabel(lv_obj_t* label, const std::string& text) {
  printf("%s\n", text.c_str());
  _lock_acquire(&g_lvgl_api_lock);
  if (label != nullptr) {
    lv_label_set_text(label, text.c_str());
  }
  _lock_release(&g_lvgl_api_lock);
}

void SetLabelUnlocked(lv_obj_t* label, const char* text) {
  printf("%s\n", text);
  if (label != nullptr) {
    lv_label_set_text(label, text);
  }
}

void SetLabelFmt(lv_obj_t* label, const char* fmt, ...) {
  char buffer[512] = {};
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  SetLabel(label, buffer);
}

bool NotifyLvglFlushReady(esp_lcd_panel_handle_t panel,
    const esp_lcd_rgb_panel_event_data_t* event_data, void* user_ctx) {
  lv_display_t* display = static_cast<lv_display_t*>(user_ctx);
  lv_display_flush_ready(display);
  return false;
}

void LvglFlush(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
  esp_lcd_panel_handle_t panel_handle =
      static_cast<esp_lcd_panel_handle_t>(lv_display_get_user_data(display));

  const lv_area_t* draw_area = area;
  uint8_t* draw_map = px_map;
  lv_area_t rotated_area = {};
  const lv_display_rotation_t rotation = lv_display_get_rotation(display);

  if (rotation != LV_DISPLAY_ROTATION_0) {
    const int32_t src_width = lv_area_get_width(area);
    const int32_t src_height = lv_area_get_height(area);
    const uint32_t src_stride =
        lv_draw_buf_width_to_stride(src_width, LV_COLOR_FORMAT_RGB565);

    rotated_area = *area;
    lv_display_rotate_area(display, &rotated_area);

    const uint32_t dest_stride = lv_draw_buf_width_to_stride(
        lv_area_get_width(&rotated_area), LV_COLOR_FORMAT_RGB565);
    const size_t rotated_size =
        dest_stride * static_cast<size_t>(lv_area_get_height(&rotated_area));

    if (g_rotated_flush_buffer_size < rotated_size) {
      if (g_rotated_flush_buffer != nullptr) {
        heap_caps_free(g_rotated_flush_buffer);
      }
      g_rotated_flush_buffer =
          heap_caps_malloc(rotated_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      g_rotated_flush_buffer_size =
          g_rotated_flush_buffer != nullptr ? rotated_size : 0;
    }

    if (g_rotated_flush_buffer == nullptr) {
      printf("Allocate rotated LVGL flush buffer failed\n");
      lv_display_flush_ready(display);
      return;
    }

    lv_draw_sw_rotate(px_map, g_rotated_flush_buffer, src_width, src_height,
        src_stride, dest_stride, rotation, LV_COLOR_FORMAT_RGB565);
    draw_area = &rotated_area;
    draw_map = static_cast<uint8_t*>(g_rotated_flush_buffer);
  }

  if (esp_lcd_panel_draw_bitmap(panel_handle, draw_area->x1, draw_area->y1,
          draw_area->x2 + 1, draw_area->y2 + 1, draw_map) != ESP_OK) {
    printf("LVGL flush failed\n");
    lv_display_flush_ready(display);
  }
}

void IncreaseLvglTick(void* arg) { lv_tick_inc(kLvglTickPeriodMs); }

const char* RotationName(lv_display_rotation_t rotation) {
  switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
      return "0";
    case LV_DISPLAY_ROTATION_90:
      return "90";
    case LV_DISPLAY_ROTATION_180:
      return "180";
    case LV_DISPLAY_ROTATION_270:
      return "270";
    default:
      return "?";
  }
}

lv_display_rotation_t NextClockwiseRotation(lv_display_rotation_t rotation) {
  switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
      return LV_DISPLAY_ROTATION_90;
    case LV_DISPLAY_ROTATION_90:
      return LV_DISPLAY_ROTATION_180;
    case LV_DISPLAY_ROTATION_180:
      return LV_DISPLAY_ROTATION_270;
    case LV_DISPLAY_ROTATION_270:
    default:
      return LV_DISPLAY_ROTATION_0;
  }
}

lv_display_rotation_t LvglRotationForLite(
    lv_display_rotation_t interface_rotation) {
  // Lite 屏幕的物理安装方向与参考板相差 180 度。
  switch (interface_rotation) {
    case LV_DISPLAY_ROTATION_0:
      return LV_DISPLAY_ROTATION_180;
    case LV_DISPLAY_ROTATION_90:
      return LV_DISPLAY_ROTATION_270;
    case LV_DISPLAY_ROTATION_180:
      return LV_DISPLAY_ROTATION_0;
    case LV_DISPLAY_ROTATION_270:
      return LV_DISPLAY_ROTATION_90;
    default:
      return LV_DISPLAY_ROTATION_0;
  }
}

void ApplyDisplayRotationUnlocked(lv_display_rotation_t rotation) {
  g_display_rotation = rotation;
  if (g_display != nullptr) {
    lv_display_set_rotation(g_display, LvglRotationForLite(rotation));
    lv_obj_invalidate(lv_display_get_screen_active(g_display));
  }

  char status[128] = {};
  std::snprintf(status, sizeof(status),
      "Current rotation: %s degrees\n"
      "Press the center key to rotate the interface clockwise.",
      RotationName(rotation));
  SetLabelUnlocked(g_rotation_status, status);
  printf("LVGL display rotation set to %s degrees\n", RotationName(rotation));
}

void RotateDisplayEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ApplyDisplayRotationUnlocked(NextClockwiseRotation(g_display_rotation));
}

void LvglTask(void* arg) {
  printf("LvglTask start\n");
  while (true) {
    _lock_acquire(&g_lvgl_api_lock);
    uint32_t delay_ms = lv_timer_handler();
    _lock_release(&g_lvgl_api_lock);

    if (delay_ms < kLvglTaskMinDelayMs) {
      delay_ms = kLvglTaskMinDelayMs;
    } else if (delay_ms > kLvglTaskMaxDelayMs) {
      delay_ms = kLvglTaskMaxDelayMs;
    }
    usleep(delay_ms * 1000);
  }
}

lv_obj_t* CreateButton(
    lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user_data) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_set_height(button, 40);
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_left(button, 18, 0);
  lv_obj_set_style_pad_right(button, 18, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(kColorPrimary), 0);
  lv_obj_set_style_transform_width(button, -3, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(button, -3, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(kColorPrimaryPressed), LV_STATE_PRESSED);
  lv_obj_set_style_outline_width(button, 3, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_pad(button, 3, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_color(
      button, lv_color_hex(kColorPrimaryPressed), LV_STATE_FOCUSED);
  lv_obj_set_style_shadow_width(button, 10, 0);
  lv_obj_set_style_shadow_opa(button, LV_OPA_20, 0);
  lv_obj_set_style_shadow_offset_y(button, 4, 0);
  lv_obj_set_style_shadow_width(button, 2, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_offset_y(button, 1, LV_STATE_PRESSED);
  lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_center(label);
  return button;
}

lv_obj_t* CreateWrappedLabel(lv_obj_t* parent, const char* text) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_width(label, LV_PCT(100));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(kColorOnSurfaceVariant), 0);
  lv_label_set_text(label, text);
  return label;
}

lv_obj_t* CreateCardTitle(lv_obj_t* parent, const char* text) {
  lv_obj_t* header = lv_obj_create(parent);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(header, LV_PCT(100));
  lv_obj_set_height(header, 34);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_set_style_pad_column(header, 10, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_layout(header, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);

  lv_obj_t* accent = lv_obj_create(header);
  lv_obj_remove_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(accent, 5, 24);
  lv_obj_set_style_radius(accent, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(accent, 0, 0);
  lv_obj_set_style_bg_color(accent, lv_color_hex(kColorPrimary), 0);

  lv_obj_t* label = lv_label_create(header);
  lv_obj_set_width(label, 320);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(kColorOnSurface), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_text(label, text);
  return label;
}

lv_obj_t* CreateStatusPanel(
    lv_obj_t* parent, const char* text, int height, bool scrollable = false) {
  lv_obj_t* panel = lv_obj_create(parent);
  if (scrollable) {
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
  } else {
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
  }
  lv_obj_set_width(panel, LV_PCT(100));
  lv_obj_set_height(panel, height);
  lv_obj_set_style_radius(panel, 12, 0);
  lv_obj_set_style_pad_left(panel, 14, 0);
  lv_obj_set_style_pad_right(panel, 14, 0);
  lv_obj_set_style_pad_top(panel, 12, 0);
  lv_obj_set_style_pad_bottom(panel, 12, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(kColorSurfaceContainerHigh), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(kColorOutline), 0);

  lv_obj_t* label = CreateWrappedLabel(panel, text);
  lv_obj_set_style_text_color(label, lv_color_hex(kColorOnSurfaceVariant), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_line_space(label, 4, 0);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
  return label;
}

lv_obj_t* CreatePageCard(lv_obj_t* tile) {
  lv_obj_t* card = lv_obj_create(tile);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, kCardWidth, kCardHeight);
  lv_obj_set_style_radius(card, 18, 0);
  lv_obj_set_style_pad_left(card, 20, 0);
  lv_obj_set_style_pad_right(card, 20, 0);
  lv_obj_set_style_pad_top(card, 16, 0);
  lv_obj_set_style_pad_bottom(card, 24, 0);
  lv_obj_set_style_pad_row(card, 10, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(kColorSurface), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(kColorOutline), 0);
  lv_obj_set_style_shadow_width(card, 14, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
  lv_obj_set_style_shadow_offset_y(card, 5, 0);
  lv_obj_set_layout(card, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
      card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);
  return card;
}

const char* ApplyDisplayPatternTo(lv_obj_t* target) {
  if (target == nullptr) {
    return "";
  }

  lv_obj_clean(target);
  lv_obj_set_style_bg_opa(target, LV_OPA_COVER, 0);
  const char* name = "";

  switch (g_display_pattern) {
    case 0:
      lv_obj_set_style_bg_color(target, lv_palette_main(LV_PALETTE_RED), 0);
      name = "Red";
      break;
    case 1:
      lv_obj_set_style_bg_color(target, lv_palette_main(LV_PALETTE_GREEN), 0);
      name = "Green";
      break;
    case 2:
      lv_obj_set_style_bg_color(target, lv_palette_main(LV_PALETTE_BLUE), 0);
      name = "Blue";
      break;
    case 3:
      lv_obj_set_style_bg_color(target, lv_color_white(), 0);
      name = "White";
      break;
    case 4:
      lv_obj_set_style_bg_color(target, lv_color_black(), 0);
      name = "Black";
      break;
    default: {
      const size_t image_index = static_cast<size_t>(g_display_pattern - 5);
      lv_obj_set_style_bg_color(target, lv_color_black(), 0);
      lv_obj_t* image = lv_image_create(target);
      lv_image_set_src(image, kWallpaperTable[image_index]);
      lv_obj_center(image);
      name = (image_index == 0) ? "Wallpaper 1"
                                : (image_index == 1) ? "Wallpaper 2"
                                                     : "Wallpaper 3";
      break;
    }
  }

  return name;
}

void ExitFullscreenDisplayTest() {
  if (g_panel_handle != nullptr) {
    esp_err_t err = esp_lcd_rgb_panel_set_pclk(
        g_panel_handle, t_panel_lite::device::st7701::kPixelClockHz);
    if (err == ESP_OK) {
      printf("Display test exit RGB PCLK set to %d Hz\n",
          t_panel_lite::device::st7701::kPixelClockHz);
    } else {
      printf("Set display test exit RGB PCLK to %d Hz failed: %s\n",
          t_panel_lite::device::st7701::kPixelClockHz, esp_err_to_name(err));
    }
  }

  if (g_fullscreen_display != nullptr) {
    lv_obj_delete(g_fullscreen_display);
    g_fullscreen_display = nullptr;
  }
}

void AdvanceFullscreenDisplayTest() {
  if (g_display_pattern >= 7) {
    ExitFullscreenDisplayTest();
    return;
  }

  g_display_pattern++;
  ApplyDisplayPatternTo(g_fullscreen_display);
}

void EnterFullscreenDisplayTest() {
  if (g_root == nullptr) {
    return;
  }
  if (g_panel_handle != nullptr) {
    esp_err_t err = esp_lcd_rgb_panel_set_pclk(
        g_panel_handle, kDisplayTestEnterPixelClockHz);
    if (err == ESP_OK) {
      printf("Display test RGB PCLK set to %d Hz\n",
          kDisplayTestEnterPixelClockHz);
    } else {
      printf("Set display test RGB PCLK to %d Hz failed: %s\n",
          kDisplayTestEnterPixelClockHz, esp_err_to_name(err));
    }
  }

  if (g_fullscreen_display != nullptr) {
    lv_obj_delete(g_fullscreen_display);
  }

  g_display_pattern = 0;
  g_fullscreen_display = lv_obj_create(g_root);
  lv_obj_remove_flag(g_fullscreen_display, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(g_fullscreen_display, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(g_fullscreen_display, kPageWidth, kPageHeight);
  lv_obj_set_pos(g_fullscreen_display, 0, 0);
  lv_obj_set_style_radius(g_fullscreen_display, 0, 0);
  lv_obj_set_style_border_width(g_fullscreen_display, 0, 0);
  lv_obj_set_style_pad_all(g_fullscreen_display, 0, 0);
  ApplyDisplayPatternTo(g_fullscreen_display);
  lv_obj_move_foreground(g_fullscreen_display);
}

void StartDisplayTestEvent(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    EnterFullscreenDisplayTest();
  }
}

void WifiTask(void* arg) {
  SetLabel(g_wifi_status, "WiFi: init...");

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    SetLabelFmt(g_wifi_status, "NVS init failed: %s", esp_err_to_name(err));
    g_wifi_task_handle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  if (!g_wifi_ready) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_init_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      SetLabelFmt(
          g_wifi_status, "WiFi init failed: %s", esp_err_to_name(err));
      g_wifi_task_handle = nullptr;
      vTaskDelete(nullptr);
      return;
    }
    g_wifi_ready = true;
  }

  esp_wifi_set_mode(WIFI_MODE_STA);
  err = esp_wifi_start();
  if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
    SetLabelFmt(
        g_wifi_status, "WiFi start failed: %s", esp_err_to_name(err));
    g_wifi_task_handle = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  esp_wifi_disconnect();

  SetLabel(g_wifi_status, "WiFi: scanning...");
  uint16_t ap_count = 0;
  wifi_scan_config_t scan_config = {};
  err = esp_wifi_scan_start(&scan_config, true);
  if (err == ESP_OK) {
    esp_wifi_scan_get_ap_num(&ap_count);
  }

  wifi_config_t wifi_config = {};
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid),
      kWifiSsid, sizeof(wifi_config.sta.ssid));
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password),
      kWifiPassword, sizeof(wifi_config.sta.password));
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

  esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  SetLabelFmt(g_wifi_status, "WiFi: %u APs found\nConnecting to %s...",
      static_cast<unsigned>(ap_count), kWifiSsid);
  err = esp_wifi_connect();
  if (err != ESP_OK) {
    SetLabelFmt(g_wifi_status, "WiFi connect failed: %s", esp_err_to_name(err));
    g_wifi_task_handle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  const int64_t start_us = esp_timer_get_time();
  wifi_ap_record_t ap_info = {};
  while ((esp_timer_get_time() - start_us) <
         static_cast<int64_t>(kWifiConnectTimeoutMs) * 1000) {
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }

  if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
    SetLabelFmt(g_wifi_status,
        "WiFi: connection timeout\nSSID: %s\nAPs found: %u",
        kWifiSsid, static_cast<unsigned>(ap_count));
    g_wifi_task_handle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  setenv("TZ", "CST-8", 1);
  tzset();
  if (!esp_sntp_enabled()) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");
    esp_sntp_init();
  }

  tm time_info = {};
  bool time_ok = false;
  for (int i = 0; i < 20; ++i) {
    time_t now = time(nullptr);
    localtime_r(&now, &time_info);
    if (time_info.tm_year >= (2024 - 1900)) {
      time_ok = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  if (time_ok) {
    char time_text[96] = {};
    ::strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S",
        &time_info);
    SetLabelFmt(g_wifi_status,
        "WiFi: connected\nSSID: %s\nRSSI: %d dBm\nTime: %s",
        kWifiSsid, ap_info.rssi, time_text);
  } else {
    SetLabelFmt(g_wifi_status,
        "WiFi: connected\nSSID: %s\nRSSI: %d dBm\nNTP time failed",
        kWifiSsid, ap_info.rssi);
  }

  g_wifi_task_handle = nullptr;
  vTaskDelete(nullptr);
}

void StartWifiTest(lv_event_t* event) {
  if (g_wifi_task_handle != nullptr) {
    SetLabelUnlocked(g_wifi_status, "WiFi test is already running...");
    return;
  }
  xTaskCreate(WifiTask, "WifiTask", 8 * 1024, nullptr, 3,
      &g_wifi_task_handle);
}

bool InitSdSpiBus(std::string* error) {
  if (g_sd_spi_bus_ready) {
    return true;
  }

  spi_bus_config_t bus_config = {};
  bus_config.mosi_io_num = t_panel_lite::gpio::sd::kMosi;
  bus_config.miso_io_num = t_panel_lite::gpio::sd::kMiso;
  bus_config.sclk_io_num = t_panel_lite::gpio::sd::kSclk;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  bus_config.max_transfer_sz = 4000;

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  esp_err_t err = spi_bus_initialize(static_cast<spi_host_device_t>(host.slot),
      &bus_config, SDSPI_DEFAULT_DMA);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    *error = std::string("SPI bus init failed: ") + esp_err_to_name(err);
    return false;
  }

  g_sd_spi_bus_ready = true;
  return true;
}

const char* SdCardTypeName(const sdmmc_card_t* card) {
  if (card == nullptr) {
    return "Unknown";
  }
  if (card->is_sdio) {
    return "SDIO";
  }
  if (card->is_mmc) {
    return "MMC";
  }
  return (card->ocr & SD_OCR_SDHC_CAP) ? "SDHC/SDXC" : "SDSC";
}

void AppendSdDirectoryTree(
    const std::string& path, int depth, int* entry_count, std::string* out) {
  if (depth > kMaxSdTreeDepth || *entry_count >= kMaxSdTreeEntries) {
    return;
  }

  DIR* dir = opendir(path.c_str());
  if (dir == nullptr) {
    return;
  }

  while (*entry_count < kMaxSdTreeEntries) {
    dirent* entry = readdir(dir);
    if (entry == nullptr) {
      break;
    }
    if (std::strcmp(entry->d_name, ".") == 0 ||
        std::strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::string child_path = path;
    if (child_path.back() != '/') {
      child_path += "/";
    }
    child_path += entry->d_name;

    struct stat stat_buffer = {};
    const bool stat_ok = stat(child_path.c_str(), &stat_buffer) == 0;
    const bool is_dir = stat_ok && S_ISDIR(stat_buffer.st_mode);

    out->append(depth * 2, ' ');
    out->append(is_dir ? "[D] " : "[F] ");
    out->append(entry->d_name);
    if (!is_dir && stat_ok) {
      char size_text[32] = {};
      std::snprintf(size_text, sizeof(size_text), " (%ld B)",
          static_cast<long>(stat_buffer.st_size));
      out->append(size_text);
    }
    out->append("\n");
    (*entry_count)++;

    if (is_dir) {
      AppendSdDirectoryTree(child_path, depth + 1, entry_count, out);
    }
  }

  closedir(dir);
}

std::string BuildSdMountedStatusText() {
  std::string text;
  text.reserve(4096);

  const uint64_t card_size_mb =
      static_cast<uint64_t>(g_sd_card->csd.capacity) *
      g_sd_card->csd.sector_size / (1024ULL * 1024ULL);
  char header[192] = {};
  std::snprintf(header, sizeof(header),
      "SD card mounted\nType: %s\nSize: %llu MB\nMount: %s\n\nDirectory:\n",
      SdCardTypeName(g_sd_card), static_cast<unsigned long long>(card_size_mb),
      kSdMountPoint);
  text += header;

  int entry_count = 0;
  AppendSdDirectoryTree(kSdMountPoint, 0, &entry_count, &text);
  if (entry_count == 0) {
    text += "(empty)\n";
  } else if (entry_count >= kMaxSdTreeEntries) {
    text += "... output truncated\n";
  }

  return text;
}

bool MountSdCard(std::string* error) {
  if (!InitSdSpiBus(error)) {
    return false;
  }

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.max_freq_khz = kSdSpiMaxFreqKhz;

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.host_id = static_cast<spi_host_device_t>(host.slot);
  slot_config.gpio_cs = static_cast<gpio_num_t>(t_panel_lite::gpio::sd::kCs);

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = 8;
  mount_config.allocation_unit_size = 16 * 1024;
  mount_config.disk_status_check_enable = true;

  sdmmc_card_t* card = nullptr;
  esp_err_t err = esp_vfs_fat_sdspi_mount(
      kSdMountPoint, &host, &slot_config, &mount_config, &card);
  if (err != ESP_OK) {
    *error = std::string("SD mount failed: ") + esp_err_to_name(err);
    return false;
  }

  g_sd_card = card;
  g_sd_mounted = true;
  sdmmc_card_print_info(stdout, g_sd_card);
  return true;
}

void UnmountSdCard() {
  if (g_sd_mounted && g_sd_card != nullptr) {
    esp_vfs_fat_sdcard_unmount(kSdMountPoint, g_sd_card);
  }
  g_sd_card = nullptr;
  g_sd_mounted = false;
}

bool SdCardStillReadable() {
  if (!g_sd_mounted || g_sd_card == nullptr) {
    return false;
  }
  if (sdmmc_get_status(g_sd_card) != ESP_OK) {
    return false;
  }

  DIR* dir = opendir(kSdMountPoint);
  if (dir == nullptr) {
    return false;
  }
  closedir(dir);
  return true;
}

void SdTask(void* arg) {
  SetLabel(g_sd_status, "SD card scanning...");

  if (g_sd_mounted && !SdCardStillReadable()) {
    UnmountSdCard();
  }

  std::string error;
  if (!g_sd_mounted && !MountSdCard(&error)) {
    SetLabelFmt(g_sd_status, "SD card test failed\n%s\nCheck card and wiring.",
        error.c_str());
  } else {
    SetLabel(g_sd_status, BuildSdMountedStatusText());
  }

  g_sd_task_handle = nullptr;
  vTaskDelete(nullptr);
}

void StartSdTest(lv_event_t* event) {
  if (g_sd_task_handle != nullptr) {
    SetLabelUnlocked(g_sd_status, "SD card test is already running...");
    return;
  }

  if (xTaskCreate(SdTask, "SdTask", kSdTaskStackSize, nullptr, kSdTaskPriority,
          &g_sd_task_handle) != pdPASS) {
    SetLabelUnlocked(g_sd_status, "Create SD card test task failed");
  }
}

void BuildDisplayPage(lv_obj_t* parent) {
  lv_obj_t* card = CreatePageCard(parent);
  CreateCardTitle(card, "Display test");
  CreateStatusPanel(card,
      "The center key starts the full-screen color and wallpaper checks.\n"
      "Press the center key again to advance each test pattern.",
      kPrimaryStatusPanelHeight);
  lv_obj_t* button =
      CreateButton(card, "Start LCD picture test", StartDisplayTestEvent,
          nullptr);
  lv_obj_set_width(button, 230);
  g_action_buttons[static_cast<int>(Page::kDisplay)] = button;
}

void BuildRotationPage(lv_obj_t* parent) {
  lv_obj_t* card = CreatePageCard(parent);
  CreateCardTitle(card, "Screen rotation");

  char status[128] = {};
  std::snprintf(status, sizeof(status),
      "Current rotation: %s degrees\n"
      "Press the center key to rotate the interface clockwise.",
      RotationName(g_display_rotation));
  g_rotation_status =
      CreateStatusPanel(card, status, kPrimaryStatusPanelHeight);

  lv_obj_t* button =
      CreateButton(card, "Rotate clockwise", RotateDisplayEvent, nullptr);
  lv_obj_set_width(button, 230);
  g_action_buttons[static_cast<int>(Page::kRotation)] = button;
}

void BuildWifiPage(lv_obj_t* parent) {
  lv_obj_t* card = CreatePageCard(parent);
  CreateCardTitle(card, "WiFi and time");
  g_wifi_status =
      CreateStatusPanel(card,
          "Press the center key to scan WiFi and get NTP time.",
          kPrimaryStatusPanelHeight);
  g_action_buttons[static_cast<int>(Page::kWifiTime)] =
      CreateButton(card, "Start WiFi time test", StartWifiTest, nullptr);
}

void BuildSdPage(lv_obj_t* parent) {
  lv_obj_t* card = CreatePageCard(parent);
  CreateCardTitle(card, "SD card");
  g_sd_status = CreateStatusPanel(card,
      "Press the center key to mount the SD card and print the directory tree.",
      kPrimaryStatusPanelHeight, true);
  lv_obj_t* button =
      CreateButton(card, "Start SD card scan", StartSdTest, nullptr);
  lv_obj_set_width(button, 230);
  g_action_buttons[static_cast<int>(Page::kSd)] = button;
}

void UpdatePageIndicator() {
  static constexpr const char* kPageNames[kPageCount] = {
      "LCD", "Rotation", "WiFi", "SD"};
  for (int i = 0; i < kPageCount; ++i) {
    if (g_page_dots[i] == nullptr) {
      continue;
    }
    const bool selected = i == static_cast<int>(g_active_page);
    lv_obj_t* dot = lv_obj_get_child(g_page_dots[i], 0);
    if (dot == nullptr) {
      continue;
    }
    lv_obj_set_size(dot, selected ? 18 : 13, selected ? 18 : 13);
    lv_obj_set_style_bg_color(g_page_dots[i],
        selected ? lv_color_hex(kColorPrimaryContainer)
                 : lv_color_hex(kColorSurfaceContainer),
        0);
    lv_obj_set_style_bg_color(dot,
        selected ? lv_color_hex(kColorPrimary) : lv_color_hex(0xAEB6C2),
        0);
    if (g_action_buttons[i] != nullptr) {
      if (selected) {
        lv_obj_add_state(g_action_buttons[i], LV_STATE_FOCUSED);
      } else {
        lv_obj_remove_state(g_action_buttons[i], LV_STATE_FOCUSED);
      }
    }
  }

  if (g_page_name_label != nullptr) {
    lv_label_set_text(g_page_name_label, kPageNames[static_cast<int>(
                                           g_active_page)]);
  }
}

int TileIndexFromObject(lv_obj_t* tile) {
  for (int i = 0; i < kPageCount; ++i) {
    if (g_tiles[i] == tile) {
      return i;
    }
  }
  return 0;
}

void TileviewEvent(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED ||
      g_tileview == nullptr) {
    return;
  }

  g_active_page = static_cast<Page>(
      TileIndexFromObject(lv_tileview_get_tile_active(g_tileview)));
  UpdatePageIndicator();
}

void ShowPage(Page page, bool animate = true) {
  g_active_page = page;
  if (g_tileview != nullptr) {
    lv_tileview_set_tile_by_index(g_tileview, static_cast<uint32_t>(page), 0,
        animate ? LV_ANIM_ON : LV_ANIM_OFF);
  }
  UpdatePageIndicator();
}

void DotButtonEvent(lv_event_t* event) {
  int page = static_cast<int>(reinterpret_cast<intptr_t>(
      lv_event_get_user_data(event)));
  ShowPage(static_cast<Page>(page));
}

ButtonFunction ButtonFunctionForPin(int pin) {
  if (pin == t_panel_lite::gpio::key::kKey1) {
    return ButtonFunction::kConfirm;
  }

  const bool use_zero_degree_mapping =
      g_display_rotation == LV_DISPLAY_ROTATION_0;
  if (pin == t_panel_lite::gpio::key::kBoot) {
    return use_zero_degree_mapping ? ButtonFunction::kPreviousPage
                                   : ButtonFunction::kNextPage;
  }
  if (pin == t_panel_lite::gpio::key::kKey2) {
    return use_zero_degree_mapping ? ButtonFunction::kNextPage
                                   : ButtonFunction::kPreviousPage;
  }
  return ButtonFunction::kNone;
}

void HandleButtonPress(int pin) {
  const ButtonFunction function = ButtonFunctionForPin(pin);
  if (function == ButtonFunction::kConfirm) {
    if (g_fullscreen_display != nullptr) {
      AdvanceFullscreenDisplayTest();
      return;
    }

    lv_obj_t* action =
        g_action_buttons[static_cast<int>(g_active_page)];
    if (action != nullptr) {
      lv_obj_send_event(action, LV_EVENT_CLICKED, nullptr);
    }
    return;
  }

  if (g_fullscreen_display != nullptr) {
    return;
  }

  int page = static_cast<int>(g_active_page);
  if (function == ButtonFunction::kPreviousPage) {
    page = (page + kPageCount - 1) % kPageCount;
  } else if (function == ButtonFunction::kNextPage) {
    page = (page + 1) % kPageCount;
  } else {
    return;
  }
  ShowPage(static_cast<Page>(page));
}

bool InitButtons() {
  gpio_config_t config = {};
  config.pin_bit_mask =
      (1ULL << t_panel_lite::gpio::key::kKey1) |
      (1ULL << t_panel_lite::gpio::key::kKey2) |
      (1ULL << t_panel_lite::gpio::key::kBoot);
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = GPIO_PULLUP_ENABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  const esp_err_t err = gpio_config(&config);
  if (err != ESP_OK) {
    printf("Init T-Panel-Lite buttons failed: %s\n", esp_err_to_name(err));
    return false;
  }
  return true;
}

void ButtonTask(void* arg) {
  static constexpr std::array<int, 3> kButtonPins = {
      t_panel_lite::gpio::key::kKey1,
      t_panel_lite::gpio::key::kKey2,
      t_panel_lite::gpio::key::kBoot,
  };
  std::array<bool, 3> last_pressed = {};
  std::array<TickType_t, 3> last_trigger_tick = {};

  for (size_t i = 0; i < kButtonPins.size(); ++i) {
    last_pressed[i] =
        gpio_get_level(static_cast<gpio_num_t>(kButtonPins[i])) == 0;
  }

  while (true) {
    const TickType_t now = xTaskGetTickCount();
    for (size_t i = 0; i < kButtonPins.size(); ++i) {
      const bool pressed =
          gpio_get_level(static_cast<gpio_num_t>(kButtonPins[i])) == 0;
      if (pressed && !last_pressed[i] &&
          now - last_trigger_tick[i] >= pdMS_TO_TICKS(kButtonDebounceMs)) {
        last_trigger_tick[i] = now;
        _lock_acquire(&g_lvgl_api_lock);
        HandleButtonPress(kButtonPins[i]);
        _lock_release(&g_lvgl_api_lock);
      }
      last_pressed[i] = pressed;
    }
    vTaskDelay(pdMS_TO_TICKS(kButtonPollMs));
  }
}

void CreateUi() {
  lv_theme_default_init(g_display, lv_palette_main(LV_PALETTE_BLUE),
      lv_palette_main(LV_PALETTE_RED), false, &lv_font_montserrat_14);

  g_root = lv_display_get_screen_active(g_display);
  lv_obj_set_style_bg_color(g_root, lv_color_hex(kColorWindow), 0);
  lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(g_root, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(g_root, 0, 0);
  lv_obj_set_layout(g_root, LV_LAYOUT_GRID);

  static int32_t root_cols[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static int32_t root_rows[] = {
      46, LV_GRID_FR(1), kIndicatorHeight, LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(g_root, root_cols, root_rows);
  lv_obj_set_style_pad_row(g_root, 4, 0);

  lv_obj_t* header = lv_obj_create(g_root);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(header, LV_PCT(100));
  lv_obj_set_height(header, LV_PCT(100));
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_set_style_pad_row(header, 0, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_layout(header, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);
  lv_obj_set_grid_cell(
      header, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

  lv_obj_t* title = lv_label_create(header);
  lv_label_set_text(title, "T-Panel-Lite General Test");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(kColorOnSurface), 0);

  g_tileview = lv_tileview_create(g_root);
  lv_obj_set_width(g_tileview, LV_PCT(100));
  lv_obj_set_height(g_tileview, LV_PCT(100));
  lv_obj_set_style_pad_all(g_tileview, 0, 0);
  lv_obj_set_style_bg_opa(g_tileview, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_tileview, 0, 0);
  lv_obj_set_scrollbar_mode(g_tileview, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_grid_cell(
      g_tileview, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_add_event_cb(g_tileview, TileviewEvent, LV_EVENT_VALUE_CHANGED,
      nullptr);

  for (int i = 0; i < kPageCount; ++i) {
    lv_dir_t dirs = static_cast<lv_dir_t>(LV_DIR_LEFT | LV_DIR_RIGHT);
    if (i == 0) {
      dirs = LV_DIR_RIGHT;
    } else if (i == kPageCount - 1) {
      dirs = LV_DIR_LEFT;
    }
    g_tiles[i] = lv_tileview_add_tile(g_tileview, i, 0, dirs);
    lv_obj_remove_flag(g_tiles[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(g_tiles[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_tiles[i], 0, 0);
    lv_obj_set_style_pad_all(g_tiles[i], 0, 0);
    lv_obj_set_scrollbar_mode(g_tiles[i], LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(g_tiles[i], LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_tiles[i], LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_tiles[i], LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  }

  BuildDisplayPage(g_tiles[static_cast<int>(Page::kDisplay)]);
  BuildRotationPage(g_tiles[static_cast<int>(Page::kRotation)]);
  BuildWifiPage(g_tiles[static_cast<int>(Page::kWifiTime)]);
  BuildSdPage(g_tiles[static_cast<int>(Page::kSd)]);

  lv_obj_t* indicator = lv_obj_create(g_root);
  lv_obj_set_width(indicator, LV_PCT(100));
  lv_obj_set_height(indicator, kIndicatorHeight);
  lv_obj_remove_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(indicator, 0, 0);
  lv_obj_set_style_pad_row(indicator, 4, 0);
  lv_obj_set_style_border_width(indicator, 0, 0);
  lv_obj_set_style_bg_opa(indicator, LV_OPA_TRANSP, 0);
  lv_obj_set_layout(indicator, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(indicator, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
      indicator, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);
  lv_obj_set_grid_cell(
      indicator, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_END, 2, 1);

  g_page_name_label = lv_label_create(indicator);
  lv_obj_set_style_text_color(
      g_page_name_label, lv_color_hex(kColorOnSurface), 0);

  lv_obj_t* dot_row = lv_obj_create(indicator);
  lv_obj_remove_flag(dot_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(dot_row, kDotRowWidth, kDotHitSize);
  lv_obj_set_style_pad_all(dot_row, 0, 0);
  lv_obj_set_style_pad_column(dot_row, kDotGap, 0);
  lv_obj_set_style_border_width(dot_row, 0, 0);
  lv_obj_set_style_bg_opa(dot_row, LV_OPA_TRANSP, 0);
  lv_obj_set_layout(dot_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(dot_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dot_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);

  for (int i = 0; i < kPageCount; ++i) {
    g_page_dots[i] = lv_obj_create(dot_row);
    lv_obj_add_flag(g_page_dots[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g_page_dots[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(g_page_dots[i], kDotHitSize, kDotHitSize);
    lv_obj_set_style_radius(g_page_dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(g_page_dots[i], 0, 0);
    lv_obj_add_event_cb(g_page_dots[i], DotButtonEvent, LV_EVENT_CLICKED,
        reinterpret_cast<void*>(static_cast<intptr_t>(i)));

    lv_obj_t* dot = lv_obj_create(g_page_dots[i]);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(dot, 13, 13);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_center(dot);
  }

  g_active_page = Page::kDisplay;
  ShowPage(Page::kDisplay, false);
  UpdatePageIndicator();
}

bool InitLvgl(esp_lcd_panel_handle_t panel_handle) {
  lv_init();

  g_display = lv_display_create(
      t_panel_lite::device::st7701::kWidth,
      t_panel_lite::device::st7701::kHeight);
  if (g_display == nullptr) {
    printf("Create LVGL display failed\n");
    return false;
  }
  lv_display_set_user_data(g_display, panel_handle);
  lv_display_set_color_format(g_display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_rotation(
      g_display, LvglRotationForLite(g_display_rotation));
  printf("LVGL display default rotation set to 180 degrees\n");
  lv_timer_t* refresh_timer = lv_display_get_refr_timer(g_display);
  if (refresh_timer != nullptr) {
    lv_timer_set_period(refresh_timer, kLvglRefreshPeriodMs);
  }

  const size_t draw_buffer_size = t_panel_lite::device::st7701::kWidth *
                                  kLvglDrawBufferLines * kBytesPerPixel;
  void* draw_buffer1 =
      heap_caps_malloc(draw_buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  void* draw_buffer2 =
      heap_caps_malloc(draw_buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (draw_buffer1 == nullptr || draw_buffer2 == nullptr) {
    printf("Allocate LVGL draw buffers failed\n");
    if (draw_buffer1 != nullptr) {
      heap_caps_free(draw_buffer1);
    }
    if (draw_buffer2 != nullptr) {
      heap_caps_free(draw_buffer2);
    }
    return false;
  }

  lv_display_set_buffers(g_display, draw_buffer1, draw_buffer2,
      draw_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(g_display, LvglFlush);

  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_color_trans_done = NotifyLvglFlushReady;
  if (esp_lcd_rgb_panel_register_event_callbacks(
          panel_handle, &callbacks, g_display) != ESP_OK) {
    printf("Register RGB panel callbacks failed\n");
    return false;
  }

  const esp_timer_create_args_t tick_timer_args = {
      .callback = IncreaseLvglTick,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lvgl_tick",
      .skip_unhandled_events = false,
  };
  esp_timer_handle_t tick_timer = nullptr;
  if (esp_timer_create(&tick_timer_args, &tick_timer) != ESP_OK) {
    printf("Create LVGL tick timer failed\n");
    return false;
  }
  if (esp_timer_start_periodic(
          tick_timer, kLvglTickPeriodMs * 1000) != ESP_OK) {
    printf("Start LVGL tick timer failed\n");
    return false;
  }

  CreateUi();

  if (!InitButtons()) {
    return false;
  }

  if (xTaskCreate(ButtonTask, "ButtonTask", kButtonTaskStackSize, nullptr,
          kButtonTaskPriority, nullptr) != pdPASS) {
    printf("Create button task failed\n");
    return false;
  }

  if (xTaskCreate(LvglTask, "LvglTask", kLvglTaskStackSize, nullptr,
          kLvglTaskPriority, nullptr) != pdPASS) {
    printf("Create LVGL task failed\n");
    return false;
  }

  return true;
}

}  // namespace

extern "C" void app_main(void) {
  printf("T-Panel-Lite general test start\n");

  esp_lcd_panel_handle_t panel_handle = nullptr;
  printf("Init ST7701 start\n");
  if (!st7701_driver::InitSt7701(&panel_handle)) {
    printf("Init ST7701 failed\n");
    return;
  }
  g_panel_handle = panel_handle;
  printf("Init ST7701 done\n");
  if (!st7701_driver::InitBacklight()) {
    printf("Init backlight failed\n");
    return;
  }
  if (!InitLvgl(panel_handle)) {
    printf("Init LVGL failed\n");
    return;
  }

  st7701_driver::StartBacklightGradient(100, kBacklightFadeTimeMs);
}
