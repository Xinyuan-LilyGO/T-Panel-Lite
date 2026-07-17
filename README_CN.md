<h1 align="center">T-Panel-Lite</h1>

## [English](./README.md) | **中文**

[![License](https://img.shields.io/github/license/Xinyuan-LilyGO/T-Panel-Lite?style=flat-square)](./LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.4%2B-ff6f00?style=flat-square)](https://github.com/espressif/esp-idf)
[![C++](https://img.shields.io/badge/C%2B%2B-17%2B-00599c?style=flat-square)](https://isocpp.org/)

<p align="center">
  <img src="image/14.jpg" alt="T-Panel-Lite" width="720">
</p>

## 概览

T-Panel-Lite 是 T-Panel 的精简无触摸版本，主控为 **ESP32-S3**，搭载
**480 x 480 ST7701 RGB 屏幕**、MicroSD 卡槽和三个物理按键。本分支已改为
与 T-Panel 相同的 ESP-IDF 工程布局和代码风格，并按 Lite 板的直连 GPIO
硬件进行适配。

> [!NOTE]
> T-Panel-Lite **没有** CST3240 触摸屏、XL9535 GPIO 扩展、ESP32-H2、
> RS485 和 CAN 硬件，因此工程中不包含这些功能。

## 目录

- [概览](#概览)
- [硬件版本](#硬件版本)
- [预览](#预览)
- [支持框架](#支持框架)
- [快速开始](#快速开始)
- [硬件模块](#硬件模块)
- [引脚总览](#引脚总览)
- [项目资料](#项目资料)
- [常见问题](#常见问题)

## 硬件版本

| 版本 | 日期 | 说明 |
| :---: | :---: | --- |
| T-Panel-Lite V1.0 | 2023-11-23 | ESP32-S3、16 MB Flash、8 MB PSRAM |

## 预览

<p align="center">
  <img src="image/12.jpg" alt="T-Panel-Lite 预览图 1" width="49%">
  <img src="image/13.jpg" alt="T-Panel-Lite 预览图 2" width="49%">
</p>

## 支持框架

| 框架 | 状态 | 版本 |
| --- | --- | --- |
| ESP-IDF | 推荐 | `>= v5.5.4` |

## 快速开始

### 使用 ESP-IDF 构建

请先安装 ESP-IDF。环境安装与配置可以参考官方说明：
[ESP-IDF 入门指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/index.html)

```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py flash monitor
```

在 `menuconfig` 中选择下面的示例程序，然后重新构建工程。

```text
Example Configuration
`-- Select the example to build
```

| 示例 | 说明 |
| --- | --- |
| [`screen`](./main/examples/screen) | 基础 RGB LCD 点屏示例 |
| [`screen_lvgl`](./main/examples/screen_lvgl) | LVGL 9.5 显示启动示例 |
| [`sd`](./main/examples/sd) | SD 卡挂载和文件系统测试 |
| [`general_test`](./main/examples/general_test) | 综合出厂测试 UI |

以下是已经编译好的固件。

烧录预编译固件时，可参考乐鑫官方 [ESP 固件在线烧录平台说明](https://docs.espressif.com/projects/esp-techpedia/zh_CN/latest/esp-friends/get-started/try-firmware/try-firmware-platform.html)。

| 固件 | 烧录地址 | 说明 |
| --- | --- | --- |
| [`general_test`](<./firmware/[t-panel-lite_v1.0][general_test]_firmware_202607171157.bin>) | `0x0` | T-Panel-Lite V1.0 `general_test` 出厂测试固件 |

## 硬件模块

### MCU

- ESP32-S3
- Flash：16 MB
- PSRAM：8 MB（Quad SPI）

### 显示屏

- 型号：YDP395BT001
- 尺寸：3.95 英寸
- 分辨率：480 x 480
- 驱动 IC：ST7701S
- 接口：ESP32-S3 直连 9-bit SPI 初始化 + 16-bit RGB 数据总线
- 触摸：未安装

### 存储与按键

- MicroSD（SPI 总线）
- KEY1、KEY2 和 BOOT 三个物理按键

## 引脚总览

全部定义集中在
[`t_panel_config.h`](./libraries/private_library/t_panel_config.h)。

## 项目资料

| 资料 | 说明 |
| --- | --- |
| [`T-Panel_Lite_V1.0.pdf`](./project/T-Panel_Lite_V1.0.pdf) | 硬件项目文档 |
| [`YDP395BT001-V2.pdf`](./docs/YDP395BT001-V2.pdf) | 屏幕模组规格书 |
| [`ST7701S_SPEC_V1.4.pdf`](./docs/ST7701S_SPEC_V1.4.pdf) | ST7701S 规格书 |

## 常见问题

<details>
<summary>Q. 为什么我的板子一直烧录失败？</summary>

A. 请按住 `BOOT` 按键，然后重新下载程序。

</details>
