# T11 implementer — full archive (mirror of report card + evidence)

Task T11, node=implementer, flow=feature. Branch `feature/new-workmode`.
Scope: Lift #1 (NTP) + Lift #2 (mDNS) ONLY. Lift #3 (RTSP) deferred to T12.

This file is the long-form companion to `artifacts/T11-implementer-report.md`.
The report card is the authoritative pointer holder; this file inlines the
moved bodies for the reviewer's byte-identity audit.

---

## 1. Diff summary

### `src/common/misc/Misc.h` (+4 lines)
Added next to `ntpSync`:
```cpp
static bool ntpSync(const std::string& ntp_server);
// Synchronize via ntpd, then block until system time is valid
// (year > YEAR_MIN) or MAX_WAIT_SECONDS elapses. Returns true if time
// became valid, false on ntpSync failure OR timeout. RTC writeback is
// left to the CALLER (keeps common_misc free of a common_time_rtc dep).
static bool ntpSyncAndWait(const std::string& ntp_server);
```

### `src/common/misc/Misc.cpp`
- include block: added `#include <ctime>` and `#include "Common.h"`.
- new method body (verbatim move of HEAD `main_app.cpp` CMD_NTP loop,
  `goto main_exit` -> `return false`):
```cpp
bool Misc::ntpSyncAndWait(const std::string& ntp_server)
{
	if (!Misc::ntpSync(ntp_server)) {
		Logger::log(LogLevel::ERROR, "ntp sync error");
		return false;
	}

	// Wait until system time is synchronized (year > YEAR_MIN(2000))
	const int MAX_WAIT_SECONDS = 30; // Maximum wait time 30 seconds
	const int CHECK_INTERVAL = 2;    // Check every 2 seconds
	int wait_time = 0;
	struct tm* nowtime = nullptr;
	while (wait_time < MAX_WAIT_SECONDS) {
		time_t now = time(nullptr);
		nowtime = localtime(&now);

		// Check if year is greater than YEAR_MIN
		if (nowtime->tm_year + YEAR_OFFSET > YEAR_MIN) {
			Logger::log(LogLevel::INFO, "System time synchronized: %d-%02d-%02d %02d:%02d:%02d",
			           nowtime->tm_year + YEAR_OFFSET, nowtime->tm_mon + MONTH_OFFSET, nowtime->tm_mday,
			           nowtime->tm_hour, nowtime->tm_min, nowtime->tm_sec);
			break;
		}

		Logger::log(LogLevel::INFO, "Waiting for system time synchronization, current year: %d, waited %d seconds",
		           nowtime->tm_year + YEAR_OFFSET, wait_time);
		sleep(CHECK_INTERVAL);
		wait_time += CHECK_INTERVAL;
	}

	if (wait_time >= MAX_WAIT_SECONDS) {
		Logger::log(LogLevel::WARNING, "Timeout waiting for system time synchronization after %d seconds", MAX_WAIT_SECONDS);
		return false;
	}

	return true;
}
```

### `src/common/misc/CMakeLists.txt`
Added `${CMAKE_CURRENT_SOURCE_DIR}/..` to `include_directories` so bare
`Common.h` resolves.

### `src/app/main_app.cpp` CMD_NTP block (after edit)
```cpp
    if (command & CMD_NTP) {
        auto ntp_server_ip = config->get(INI_SECTION_SERVER, INI_KEY_NTP_IP, "");
        auto ntp_server_port = config->get(INI_SECTION_SERVER, INI_KEY_NTP_PORT, 0);
        auto ntp_server = ntp_server_ip + ":" + to_string_custom(ntp_server_port);
        Logger::log(LogLevel::INFO, "ntp server: %s", ntp_server.c_str());
        if (ntp_server.empty()) {
            Logger::log(LogLevel::ERROR, "ntp server is empty");
            goto main_exit;
        }
        if (!Misc::ntpSyncAndWait(ntp_server)) {
            goto main_exit;
        }

        if (is_rtc_work_well) {
            time_t now = time(nullptr);
            struct tm* nowtime = localtime(&now);
            RTC::getInstance()->setTime(*nowtime);
        }
    }
```

### `src/service/discovery/MdnsParams.h` (new) — see file.
### `src/service/discovery/MdnsParams.cpp` (new) — see file.
### `src/service/discovery/CMakeLists.txt` — added MdnsParams.cpp source,
   common_misc + devconf PUBLIC links, four include dirs.

### `src/app/main_app.cpp` mDNS edits
- Deleted 4 statics: `getDefaultMdnsInstanceName`, `getDefaultMdnsHostName`,
  `buildMdnsParams`, `isMdnsEnabled`.
- KEPT `trimConfigString` (scanner helper) and `getConfiguredPort`.
- CMD_MOBILE call sites now:
```cpp
        if (service::isMdnsEnabled(config)) {
            auto mdns_params = service::buildMdnsParams(config, interface_name, ip_address, http_port, rtsp_port);
```
- Added `#include "MdnsParams.h"` after `#include "MdnsService.h"`.

---

## 2. Byte-identity audit (reviewer reference)

Whitespace-stripped per-function body diff vs `git show HEAD:src/app/main_app.cpp`:
- `buildMdnsParams` -> IDENTICAL (logic)
- `isMdnsEnabled`   -> IDENTICAL (logic)
- NTP wait-loop     -> IDENTICAL (logic)
- `trimConfigString`, `getDefaultMdnsInstanceName`, `getDefaultMdnsHostName`
  -> IDENTICAL (logic); the new copies keep their `static` keyword and live
  in `namespace service { namespace { ... } }` (file-local within service).
  HEAD copies were `static` at file scope in main_app.cpp — same linkage
  semantics.

Allowed diffs vs HEAD (per planner §3):
- signature prefix (`Misc::`, `service::`, namespace wrap),
- `goto main_exit` -> `return false` + caller `goto main_exit` (NTP),
- `nowtime` recomputed in caller for RTC writeback (NTP).

No other logic change. No formatting drift inside bodies (indent is tabs in
Misc.cpp matching that file's style; mDNS bodies keep main_app's 4-space
indent verbatim).

---

## 3. Build evidence

```
build_sim/bin/htc_main_app           # produced, exit 0
build/bin/htc_main_app               # produced (T32 uClibc), exit 0
build_sim/.../discovery_service.dir/MdnsParams.cpp.o   # present
build/.../discovery_service.dir/MdnsParams.cpp.o       # present
```

`grep -nE 'std::to_string|std::stoi'` on both new/edited bodies: empty.
