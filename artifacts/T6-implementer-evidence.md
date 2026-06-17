---
task_id: T6
node: implementer
kind: evidence
created: 2026-06-17
---

# T6 implementer evidence (pointer-style)

## API verification (actual code, vs planner claims)

| planner claim | actual in code | note |
|---|---|---|
| popen helper `popencall` at Misc.cpp:542 | `Misc::popencall(char *cmd, char *out, int max_size, int timeout_ms)` (`Misc.h:47`, `Misc.cpp:542`) | name matches; used by `wifi_reconnect.cpp` via `Misc::popencall(...)` |
| MCU `getInstance()` | `static std::shared_ptr<MCU> getInstance()` (`MCU.h:10`) | returns shared_ptr; called as `MCU::getInstance()->readUPID()` (matches `main_app.cpp:272` idiom) |
| `readUPID/readUPWD/writeUPID/writeUPWD` | `std::string readUPID()` / `std::string readUPWD()` / `bool writeUPID(const std::string&)` / `bool writeUPWD(const std::string&)` (`MCU.h:46-50`, `MCU.cpp:492/515/554/570`) | signatures match; empty-string write returns false |
| connectWifi RTL branch spawns wpa_supplicant | `Misc.cpp:459-464` shells `wpa_conn wlan0 ... 1` -> `wpa_conn.cpp:155` `wpa_supplicant ... -C /tmp/wpa_supplicant &` | confirmed — reason we do NOT route SSID-switch through connectWifi |
| ctrl_iface path fixed `/tmp/wpa_supplicant` | `wpa_conn.cpp:155` (`-C`), `172/201` (`-p`) | reused verbatim in `wifi_reconnect.cpp` |
| `system_call`/`popen_call` | `int system_call(char*,int)` / `int popen_call(char*,char*,int,int)` (`system_call.h:18/32`) | consumed via `Misc::syscall`/`Misc::popencall` |

No deviation from planner; `popencall` name was correct.

## Build verification

### 1. sim build (`htc_wifi_app`)
```
cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S .        # configure exit 0
cmake --build build_sim -j$(nproc) --target htc_wifi_app  # exit 0
```
Tail: `[100%] Linking CXX executable ../../bin/htc_wifi_app` / `[100%] Built target htc_wifi_app`.
Binary: `build_sim/bin/htc_wifi_app` (246352 B, x86-64).
Only warnings emitted: `-Wunused-function`/`-Wunused-variable` for the T32-only
helpers in `wifi_reconnect.cpp` (capture/runIgnore/parseSsidFromStatus/
wpaStateCompleted + kWpaCli/kWpaPassphrase/kCtrlIfacePath) — these are dead under
SIM (the SIM stub short-circuits before them) but live on T32. Expected, harmless.
No `error:` lines.

### 2. T32 cross build (`htc_wifi_app`)
```
cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S .  # configure exit 0
cmake --build build -j$(nproc) --target htc_wifi_app         # exit 0
```
Tail: `[100%] Linking CXX executable ../../bin/htc_wifi_app` / `[100%] Built target htc_wifi_app`.
Binary: `build/bin/htc_wifi_app` (22692 B, `ELF 32-bit LSB executable, MIPS`).
Rebuild re-check: `grep -iE "error|to_string|stoi|undefined reference"` -> NONE
(T32 uclibc portability: code uses snprintf/Logger, no std::to_string/stoi).

### 3. sim unit test (`test_wifi_app_logic`)
```
cmake --build build_sim -j$(nproc) --target test_wifi_app_logic   # exit 0
cd build_sim && LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/test_wifi_app_logic
```
Output: `test_wifi_app_logic: ALL PASS` / exit 0.
Coverage: decide() ABORT/FRESH_CONNECT/RECONNECT/RECONNECT(unknown ssid)/REUSE/
case-sensitive; decisionExitCode mapping (6/3/3/0); mayWriteBack gate (5 negative
+ 1 positive); normalizeSsid (trim-only, case preserved, whitespace->empty);
end-to-end decision->gate scenario.

## Sim smoke
```
./build_sim/bin/htc_wifi_app --help    # exit 0, prints usage + exit-code table
./build_sim/bin/htc_wifi_app           # exit 6 ("no target SSID available"), no segfault
```
(sim MCU readUPID/readUPWD return empty via IIC bypass -> target.hasCredentials=false
-> ABORT -> exit 6, exactly as planned.)

## Grep audits

### A. T5 safety — kill/rmmod MUST be empty
```
grep -rn "killall\|kill.*wpa_supplicant\|rmmod.*8189fs\|pkill" \
    src/app/wifi_app.cpp src/app/wifi_app_logic.h src/app/wifi_app_logic.cpp \
    src/app/wifi_reconnect.h src/app/wifi_reconnect.cpp
```
Result: **empty** (grep exit 1). PASS — T5 fix not regressed.

### B. graceful reconnect primitives present
```
grep -n "currentSSID\|reconnectSSID\|wpa_cli.*-p /tmp/wpa_supplicant\|iwgetid" src/app/wifi_reconnect.cpp
```
Hits: `currentSSID` def (L85), `reconnectSSID` def (L127), `iwgetid -r` (L100).
(T32 path also emits `wpa_cli -i %s -p /tmp/wpa_supplicant ...` in capture/snprintf.)

### C. main delegates to bottom layer (no self-rolled WiFi shell)
```
grep -n "currentSSID\|reconnectSSID\|connectWifi\|startDHCP" src/app/wifi_app.cpp
```
Hits: currentSSID (L167/225/246), reconnectSSID (L210), connectWifi (L192),
startDHCP (L233). All delegation; no raw shell in main.

### D. writeUPID/writeUPWD only after gate
```
grep -n "writeUPID\|writeUPWD\|mayWriteBack" src/app/wifi_app.cpp
```
Order in file: `mayWriteBack(...)` gate at L248 -> `writeUPID` at L255 ->
`writeUPWD` at L256. Writes appear ONLY after the gate; on gate miss we
`return EXIT_WRITE_GATED (5)` before any write. PASS.

## Forbidden-zone diff (git)
```
git status --short src/hal/ src/app/main_app.cpp \
    src/hardware/mcu/MCU.h src/hardware/mcu/MCU.cpp \
    src/platform/tool/wpa_conn.cpp
```
Result: **empty**. None of the protected files touched.

## Changed/new files
New: `src/app/wifi_app.cpp`, `src/app/wifi_app_logic.{h,cpp}`, `src/app/wifi_reconnect.{h,cpp}`,
`tests/test_wifi_app_logic.cpp`, `script/regress_wifi_real.sh`, `reviews/2026-06-17-htc_wifi_app.md`.
Modified: `src/app/CMakeLists.txt` (add target + dual-platform link + include common/misc),
`tests/CMakeLists.txt` (add test_wifi_app_logic).
Not committed (no permission).

## Real-hardware regression
Left as manual script `script/regress_wifi_real.sh` (cases A-F, T32 only). Key
invariant asserted: `pgrep wpa_supplicant` PID constant across SSID switches
(cases B and F) + dmesg oops scan. PC cannot run MIPS / reach real WiFi, so this
is out of CI scope (same posture as T5/T4).
