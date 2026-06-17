---
contract: evidence
contract_version: "1"
task_id: T5
node: implementer
flow: bug
status: success
summary: |
  WiFi 驱动/连接状态复用实现完成。Misc 加两个只读 POSIX 状态探测器(isWifiDriverLoaded
  /isWifiConnected),作 connectWifi/startDHCP 入口短路;删进程内标志 already_inited_wifi;
  main_app 退出路径加 0 代码改动注释。双平台编译 exit 0。行尾噪音已修正:Misc.cpp/Misc.h
  已恢复 HEAD 原始混合行尾,逻辑改动用字节级精确 apply(保持各行原行尾),`git diff --stat`
  现为 Misc.cpp 46 行 / Misc.h 4 行(此前 CRLF 规范化导致的 440/24 噪音已消除)。
---

# T5 — Implementer 证据 (evidence)

status: **success** — 双平台 build 均 exit 0,改动严格贴合 plan,无超范围。行尾噪音已修正:`git diff --stat` 现为 Misc.cpp 46 行 / Misc.h 4 行(纯逻辑改动)。

## 改动文件

- `src/common/misc/Misc.h` — 加 2 声明,删 `already_inited_wifi`
- `src/common/misc/Misc.cpp` — 加 2 探测器实现,改 connectWifi/startDHCP 入口,删 `already_inited_wifi`
- `src/app/main_app.cpp` — performCleanup / main_exit 各加 1 段注释(0 代码改动)

## 关键 diff(忽略行尾噪音后的真实逻辑改动,`git diff --ignore-all-space`)

### Misc.h
```diff
     static bool connectWifi(const std::string &ssid, const std::string &password);
     static bool startDHCP(const std::string &ifname="");
+    // Read-only POSIX probes (SIM-safe, idempotent, no side effects).
+    static bool isWifiDriverLoaded();
+    static bool isWifiConnected(const std::string &ifname="wlan0");
     static bool ntpSync(const std::string& ntp_server);
...
     static bool already_insmod_mmc;
-    static bool already_inited_wifi;
 };
```

### Misc.cpp
```diff
@@ static init @@
 bool Misc::syscall_inited = false;
 bool Misc::already_insmod_mmc = false;
-bool Misc::already_inited_wifi = false;

@@ after getGatewayAddress, before getMACAddress @@
+bool Misc::isWifiDriverLoaded()
+{
+    // grep /proc/modules for the compiled-in WiFi module name.
+#if defined(WIFI_TYPE_CYW43012)
+    std::string command = "grep -q '^cywdhd' /proc/modules";
+#elif defined(WIFI_TYPE_RTL8189FS)
+    std::string command = "grep -q '^8189fs' /proc/modules";
+#else
+    #error "Unknown WiFi type"
+#endif
+    return syscall(command.c_str(), 1000) == 0;
+}
+
+bool Misc::isWifiConnected(const std::string &ifname)
+{
+    return !getIPAddress(ifname).empty() && !getGatewayAddress(ifname).empty();
+}

@@ connectWifi @@
-    if (!already_inited_wifi) {
+    if (isWifiConnected()) {
+        Logger::log(LogLevel::INFO, "WiFi already connected, skip connectWifi");
+        return true;
+    }
+
+    if (!isWifiDriverLoaded()) {
         ... (宏分支 insmod 命令原样保留) ...
         ret = syscall((char*)command.c_str(), 10000);
         if(ret < 0) { ... return false; }
-        already_inited_wifi = true;
+        // Re-check after insmod to absorb the EEXIST race ...
+        if (!isWifiDriverLoaded()) {
+            Logger::log(LogLevel::ERROR, "WiFi driver failed to load (%s)", command.c_str());
+            return false;
+        }
     }
     ... wpa_conn 调用(原样不动,仍传 driver_loaded=1) ...

@@ startDHCP @@
 {
     std::string netif = ifname.empty() ? netifname : ifname;
+    if (!getIPAddress(netif).empty()) {
+        Logger::log(LogLevel::INFO, "%s already has IP, skip DHCP", netif.c_str());
+        return true;
+    }
     int ret;
```

### main_app.cpp(纯注释,+32 行,0 代码改动)
- `performCleanup`(:886-):加 NOTE 说明故意不卸载驱动/不 kill wpa/不清 `/tmp/wpa_supplicant`,状态驱动重入使复用安全。
- `main_exit`(:1832):同上 NOTE。
- 调用点 `connectWifi`(main_app.cpp:1375/1574)、`startDHCP`(:1403/1578)**逻辑零改动**(仅 grep 显示在注释中)。

## grep 校验

```
=== already_inited_wifi 残留检查 (应为空) ===
OK: 无残留

=== 新探测器声明/定义 ===
src/common/misc/Misc.h:34:    static bool isWifiDriverLoaded();
src/common/misc/Misc.h:35:    static bool isWifiConnected(const std::string &ifname="wlan0");
src/common/misc/Misc.cpp:284:bool Misc::isWifiDriverLoaded()
src/common/misc/Misc.cpp:300:bool Misc::isWifiConnected(const std::string &ifname)
src/common/misc/Misc.cpp:429:    if (isWifiConnected()) {              # connectWifi 入口短路
src/common/misc/Misc.cpp:434:    if (!isWifiDriverLoaded()) {          # 驱动守卫
src/common/misc/Misc.cpp:450:        if (!isWifiDriverLoaded()) {      # insmod 后复检

=== connectWifi / startDHCP 入口短路 ===
430:  Logger::log(LogLevel::INFO, "WiFi already connected, skip connectWifi");
478:  Logger::log(LogLevel::INFO, "%s already has IP, skip DHCP", netif.c_str());

=== wpa_conn.cpp 改动检查 (git diff --stat 应为空) ===
--- (空=未改) ---

=== main_app 仅注释改动 ===
 src/app/main_app.cpp | 32 ++++++++++++++++++++++++++++++++
 1 file changed, 32 insertions(+)
```

## 双平台 build 结果(success / exit 0)

> 行尾修正后重新双平台编译,均 exit 0(与首次实现编译结果一致,逻辑未变)。

### T32 cross-build(`cmake --build build -j$(nproc)`)
```
[100%] Linking CXX executable ../../bin/htc_main_app
Flattening library symlinks for target deployment
Done flattening symlinks in /home/zengping/project/huntcam/code/t32_cam/build/lib
[100%] Built target htc_main_app
T32_BUILD_EXIT=0
```
仅有预先存在的无关警告(SnapImgSize unused / main_app 内 static 函数 unused),非本次改动引入。

### PC SIM build(`cmake --build build_sim -j$(nproc)`)
```
[100%] Linking CXX executable ../../bin/htc_main_app
[100%] Built target htc_main_app
SIM_BUILD_EXIT=0
```
仅有预先存在的无关警告(Logger deprecated / syncWithMCU unused),非本次改动引入。

## 约束符合性

- 双平台编译通过(T32 exit 0 + SIM exit 0)。**未运行** T32 硬件二进制。
- 新探测器纯 POSIX(`getifaddrs` via getIPAddress、读 `/proc/net/route` via getGatewayAddress、读 `/proc/modules` via grep),SIM-safe,**未加**新 `#ifndef BUILD_FOR_SIMULATION`。
- `src/platform/tool/wpa_conn.cpp` **零改动**(`git diff --stat` 为空)。
- main_app 调用点(:1375/1403/1574/1578)**逻辑零改动**。
- 未加 `pkill wpa_supplicant`。
- 复用 `Misc::getIPAddress` / `Misc::getGatewayAddress`,未新造网络检测。
- 未 commit / push。

## 行尾说明(非逻辑改动)— 已修正

**修正前的问题**:`Misc.cpp` / `Misc.h` 在 HEAD 中即为**混合行尾**(Misc.cpp: 406 行 CRLF + 198 行 LF;
Misc.h: 43 行 CRLF + 10 行 LF)。上一轮 Edit 工具写入时整文件被规范化成纯 CRLF(Misc.cpp 644 CR),
导致原本 LF 的行全部变 CRLF,`git diff --stat` 出现 440/24 的行尾噪音。

**修正方法**:用 `git checkout HEAD --` 恢复两个文件的原始混合行尾,再用 Python 字节级脚本
(读 bytes → 精确匹配 old_string → replace → 写 bytes)重新 apply 逻辑改动。新插入的代码块
逐块匹配其所在上下文的行尾(LF 区用 LF,如 static init / 新探测器函数;CRLF 区用 CRLF,如
connectWifi / startDHCP 入口与 Misc.h 声明),**不再整文件规范化**。

**修正后验证**:
```
$ git diff --stat -- src/common/misc/Misc.cpp src/common/misc/Misc.h
 src/common/misc/Misc.cpp | 46 +++++++++++++++++++++++++++++++++++++++++++---
 src/common/misc/Misc.h   |  4 +++-
 2 files changed, 46 insertions(+), 4 deletions(-)
```
逐行抽查 `git diff` 无任何 `-foo/+foo` 同内容噪音(见上方关键 diff,均为真实逻辑改动)。
逻辑改动与首次实现**完全一致**(`git diff --ignore-all-space` 输出不变)。

行尾特征保留:Misc.cpp 现 422 CRLF + 222 LF(新探测器函数为 LF 区,故 LF 行数增加;connectWifi/
startDHCP 的短路块为 CRLF 区,故 CRLF 行数小幅增加),Misc.h 现 45 CRLF + 10 LF。混合行尾特征与 HEAD 一致。
`main_app.cpp` / `RtspServer.*` / `IngenicVideo.cpp` HEAD 为纯 LF,本次修正**未动**这些文件。

## 未运行 / 遗留风险(留设备侧)

- 设备真实回归(htc_main_app -m 连跑 ≥6 次不 oops、第 2 次起出现短路日志、退出后 wpa_supplicant/8189fs 仍在)
  无法在 PC 验,按 plan 验证项 2-4 留 T32 设备侧执行。
- 模块名取 `/proc/modules` 报告名(`8189fs`/`cywdhd`),用 `^name` 前缀匹配兜底变体。
