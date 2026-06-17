# 2026-06-17 — htc_wifi_app migration note (T6)

## What landed
New standalone executable `htc_wifi_app`: connect a WiFi network, run DHCP, and
optionally persist credentials to the MCU. Built for both T32 (`build/bin/`) and
PC simulation (`build_sim/bin/`).

New files:
- `src/app/wifi_app.cpp` — `main()`: CLI parse + orchestration + exit codes.
- `src/app/wifi_app_logic.{h,cpp}` — pure-logic decision/write-back-gate/exit-code
  layer (no syscalls); unit-tested in sim.
- `src/app/wifi_reconnect.{h,cpp}` — graceful SSID-switch primitives
  (`currentSSID` / `reconnectSSID`) that talk to the resident wpa_supplicant via
  its ctrl_iface socket WITHOUT restarting the process.
- `tests/test_wifi_app_logic.cpp` — pure-logic unit test (sim only).
- `script/regress_wifi_real.sh` — manual real-hardware regression (cases A-F).

Modified:
- `src/app/CMakeLists.txt` — add `htc_wifi_app` target (dual-platform link,
  mirrors `htc_daemon_app`); added `../common/misc` to include dirs.
- `tests/CMakeLists.txt` — add `test_wifi_app_logic` (sim only, output to bin/).

## Design invariants (do NOT regress)
1. **Never kill/respawn wpa_supplicant, never delete `/tmp/wpa_supplicant`, never
   rmmod.** The graceful reconnect path uses `wpa_cli -i <if> -p /tmp/wpa_supplicant
   reconfigure` (+ add_network/select_network fallback) on the SAME ctrl_iface
   socket that `wpa_conn` uses. This preserves the T5 DbusProcess/ctrl_iface-oops
   fix. Grep audit `killall|kill.*wpa_supplicant|rmmod.*8189fs|pkill` over the new
   sources is intentionally EMPTY.
2. **MCU write-back is strictly gated.** `mayWriteBack()` requires BOTH
   `isWifiConnected()==true` AND `currentSSID()==target`, with a FRESH re-read of
   `currentSSID()` immediately before writing. Any miss -> skip write, exit 5.
3. **Credentials come ONLY from CLI args or MCU registers** (readUPID/readUPWD).
   This binary deliberately does NOT read any ini, so it is unaffected by the
   `Common.h` `INI_KEY_UPWD="PWD"` naming pitfall. If a future change adds ini
   reads, use the macro `INI_KEY_UPWD` (not the literal "UPWD") — see planner T6 §5.

## Exit code contract (stable, scripts/daemons depend on it)
| code | meaning |
|------|---------|
| 0 | success |
| 2 | driver load failure |
| 3 | connection failure (incl. graceful reconnect) |
| 4 | DHCP failure |
| 5 | connected OK but MCU write-back gated/skipped |
| 6 | argument / credential error |

## Where this fits
- `htc_main_app` remains the primary WiFi runtime; this tool is a "connect once
  per invocation" foreground helper (no daemon). Stripping WiFi logic out of
  main_app is left to a future task (planner T6 Non-goals).
- The graceful-reconnect primitives live in a NEW module (`wifi_reconnect`) rather
  than in shared `Misc`, to keep `Misc.{h,cpp}` untouched and the git diff minimal.

## Verification done (sim/PC; see artifacts/T6-implementer-evidence.md)
- Dual-platform compile exit 0 (`build/bin/htc_wifi_app` MIPS, `build_sim/bin/htc_wifi_app` x86-64).
- `test_wifi_app_logic` ALL PASS (Decision: ABORT/FRESH_CONNECT/RECONNECT/REUSE,
  write-back gate, normalizeSsid, exit-code mapping).
- `--help` exit 0; no-arg exit 6 (sim MCU returns empty) — no segfault.
- grep audits clean (no kill/rmmod; writeUPID/writeUPWD only after gate).
- Real-hardware cases A-F left for manual run via `script/regress_wifi_real.sh`
  (key: wpa_supplicant PID must stay constant across SSID switches).

## Untouched (hard constraints respected)
`src/hal/**`, `src/app/main_app.cpp`, `src/hardware/mcu/MCU.{h,cpp}`,
`src/platform/tool/wpa_conn.cpp` — none modified. Not committed (no permission).
