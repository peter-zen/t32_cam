# um idle-timeout 纯决策内核（um-app-spec §3/§4）。HAL-free：无 IMP/HAL/network/
# chrono 依赖，时间由 tick(nowMs) 注入。供 um binary（第 2 步 um_app.cpp）link；
# L1 sim 测试 (test_um_idle) 另行直接编译 um_idle.cpp 以保持自包含（与
# test_wm_task_scheduler 对 wm_task_scheduler.cpp 的处理一致）。
add_library(um_idle STATIC um_idle.cpp)

target_include_directories(um_idle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
