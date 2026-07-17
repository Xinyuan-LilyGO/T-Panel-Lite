#pragma once

#include <cstdint>

namespace t_panel_lite {
namespace gpio {
namespace sd {
inline constexpr int kCs = 34;
inline constexpr int kSclk = 36;
inline constexpr int kMosi = 35;
inline constexpr int kMiso = 37;
}  // namespace sd

namespace i2c {
inline constexpr int kSda = 17;
inline constexpr int kScl = 18;
}  // namespace i2c

namespace key {
inline constexpr int kKey1 = 48;
inline constexpr int kKey2 = 47;
inline constexpr int kBoot = 0;
}  // namespace key

namespace st7701 {
inline constexpr int kSpiCs = 14;
inline constexpr int kSpiSclk = 36;
inline constexpr int kSpiMosi = 35;
inline constexpr int kDe = 38;
inline constexpr int kVsync = 40;
inline constexpr int kHsync = 39;
inline constexpr int kPclk = 41;
inline constexpr int kBacklight = 33;
inline constexpr int kB0 = 1;
inline constexpr int kB1 = 2;
inline constexpr int kB2 = 3;
inline constexpr int kB3 = 4;
inline constexpr int kB4 = 5;
inline constexpr int kG0 = 6;
inline constexpr int kG1 = 7;
inline constexpr int kG2 = 8;
inline constexpr int kG3 = 9;
inline constexpr int kG4 = 10;
inline constexpr int kG5 = 11;
inline constexpr int kR0 = 12;
inline constexpr int kR1 = 13;
inline constexpr int kR2 = 42;
inline constexpr int kR3 = 46;
inline constexpr int kR4 = 45;
}  // namespace st7701
}  // namespace gpio

namespace device {
namespace st7701 {
inline constexpr int kWidth = 480;
inline constexpr int kHeight = 480;
inline constexpr int kPixelClockHz = 6000000;
inline constexpr int kHsyncIdleLow = 0;
inline constexpr int kVsyncIdleLow = 0;
inline constexpr int kDeIdleHigh = 0;
inline constexpr int kPclkActiveNeg = 0;
inline constexpr int kPclkIdleHigh = 0;
inline constexpr int kBounceBufferHeight = 10;
inline constexpr int kFrameBufferCount = 1;
inline constexpr int kHsyncPulseWidth = 1;
inline constexpr int kHsyncBackPorch = 1;
inline constexpr int kHsyncFrontPorch = 20;
inline constexpr int kVsyncPulseWidth = 1;
inline constexpr int kVsyncBackPorch = 10;
inline constexpr int kVsyncFrontPorch = 30;
}  // namespace st7701
}  // namespace device
}  // namespace t_panel_lite
