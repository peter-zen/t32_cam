#pragma once
// wm_time — wm 启动时间获取链（wm-app-spec §6.1）+ 关机 MCU 回写（§7）。
// 链代码自 src/app/time_test.cpp lift，已 T32 真机验证（7/7 GREEN，见
// reviews/2026-06-23-time-chain-hw-verification.md）。
#include <string>

namespace app_workmode {

// spec §6.1 全链：system(plausible)→RTC→MCU→NTP。NTP 成功则置 ntpSynced=true
// 并无条件写 RTC。返回 source："system"|"rtc"|"mcu"|"ntp"|"none"。
// 注：commonStartup 的 S4.5 已先跑 syncSystemTime(RTC→MCU，幂等)；本函数补 NTP 步 +
// ntpSynced 标志，是 wm 的权威链。
std::string acquireTimeChain(const std::string& ntpServer, bool& ntpSynced);

// spec §7 关机 MCU 回写（仅时间）。ntpSynced=false 时先补一次 NTP（失败用当前 sys 时间）；
// 结果 plausible 才 MCU::setDatetime，否则跳过（不写垃圾进 MCU）。
void writebackMcuTime(bool ntpSynced, const std::string& ntpServer);

}  // namespace app_workmode
