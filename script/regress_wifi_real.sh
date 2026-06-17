#!/bin/bash
# regress_wifi_real.sh — real-hardware regression for htc_wifi_app (T6).
#
# Run MANUALLY on the T32 device. Same posture as T5/T4: NOT in CI (PC cannot
# reach the MIPS target or a real WiFi link). The critical invariant under test
# is the T5 fix: wpa_supplicant MUST stay resident across SSID switches. We
# assert its PID never changes (proving htc_wifi_app's reconnectSSID path is
# truly graceful — no kill/respawn).
#
# Usage (on T32, with build/ mounted at /mnt/huntcam):
#   LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH \
#     bash /mnt/huntcam/script/regress_wifi_real.sh
#
# Prereqs:
#   - /system/bin/wifi/wpa_cli, wpa_supplicant, wpa_passphrase present
#     (ls /system/bin/wifi/ first if unsure).
#   - Two reachable SSIDs S1/P1 and S2/P2 (edit below).
#   - Optionally a test MCU value you can restore.

set -u

BIN=/mnt/huntcam/bin/htc_wifi_app
LIB=/mnt/huntcam/lib
IF=wlan0

# EDIT THESE for your test network:
S1="TEST_SSID_A"; P1="TEST_PASS_A"
S2="TEST_SSID_B"; P2="TEST_PASS_B"

export LD_LIBRARY_PATH=${LIB}:${LD_LIBRARY_PATH:-}

PASS=0; FAIL=0
ok()   { echo "[PASS] $1"; PASS=$((PASS+1)); }
fail() { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }

wpa_pid()   { pgrep wpa_supplicant | head -n1; }
cur_ssid()  { iwgetid -r "${IF}" 2>/dev/null; }

BASE_PID_BEFORE=$(wpa_pid)
echo "baseline wpa_supplicant PID = ${BASE_PID_BEFORE}"

# ---------- Case A: not connected -> connect S1 ----------
echo "=== Case A: fresh connect ${S1} ==="
${BIN} --ssid "${S1}" --pwd "${P1}" --if "${IF}"
rc=$?
if [ "${rc}" -eq 0 ] && [ "$(cur_ssid)" = "${S1}" ]; then ok "A fresh-connect to ${S1}"; else fail "A expected exit 0 + ssid ${S1} (got rc=${rc} ssid='$(cur_ssid)')"; fi

# ---------- Case B: connected S1 -> switch S2 (PID MUST NOT change) ----------
echo "=== Case B: graceful switch ${S1} -> ${S2} (PID invariant) ==="
PID_B_BEFORE=$(wpa_pid)
${BIN} --ssid "${S2}" --pwd "${P2}" --if "${IF}"
rc=$?
PID_B_AFTER=$(wpa_pid)
if [ "${rc}" -eq 0 ] && [ "$(cur_ssid)" = "${S2}" ] \
   && [ -n "${PID_B_BEFORE}" ] && [ "${PID_B_BEFORE}" = "${PID_B_AFTER}" ]; then
  ok "B switched to ${S2}, wpa_supplicant PID unchanged (${PID_B_BEFORE})"
else
  fail "B rc=${rc} ssid='$(cur_ssid)' pid_before=${PID_B_BEFORE} pid_after=${PID_B_AFTER}"
fi

# ---------- Case C: write-back to MCU ----------
echo "=== Case C: --write-mcu persists ${S2} ==="
${BIN} --ssid "${S2}" --pwd "${P2}" --if "${IF}" --write-mcu
rc=$?
if [ "${rc}" -eq 0 ]; then ok "C write-mcu exit 0 (verify MCU readUPID==${S2} manually)"; else fail "C expected exit 0 (got ${rc})"; fi

# ---------- Case D: bad password -> connect fail, MCU untouched ----------
echo "=== Case D: wrong password, --write-mcu must NOT write ==="
${BIN} --ssid "${S2}" --pwd "WRONG_PASSWORD_XYZ" --if "${IF}" --write-mcu
rc=$?
if [ "${rc}" -eq 3 ] || [ "${rc}" -eq 5 ]; then ok "D failed-connect exit in {3,5} (got ${rc})"; else fail "D expected 3 or 5 (got ${rc})"; fi

# ---------- Case E: no-arg reads MCU creds ----------
echo "=== Case E: no-arg uses MCU credentials ==="
${BIN} --if "${IF}"
rc=$?
if [ "${rc}" -eq 0 ] || [ "${rc}" -eq 6 ]; then ok "E no-arg exit in {0,6} (got ${rc})"; else fail "E expected 0 or 6 (got ${rc})"; fi

# ---------- Case F: T5 regression — multi-round A->B->A, PID constant ----------
echo "=== Case F: multi-round A->B->A, PID constant, no oops ==="
PID_F0=$(wpa_pid); F_OK=1
for pair in "${S1}|${P1}" "${S2}|${P2}" "${S1}|${P1}" "${S2}|${P2}" "${S1}|${P1}"; do
  s="${pair%%|*}"; p="${pair##*|}"
  ${BIN} --ssid "${s}" --pwd "${p}" --if "${IF}" >/dev/null 2>&1
  pid_now=$(wpa_pid)
  if [ -z "${pid_now}" ] || [ "${pid_now}" != "${PID_F0}" ]; then
    F_OK=0; fail "F PID changed at ${s}: ${PID_F0} -> ${pid_now}"; break
  fi
done
if [ "${F_OK}" -eq 1 ]; then ok "F 5 rounds A<->B, wpa_supplicant PID constant (${PID_F0})"; fi

# ---------- dmesg oops scan (best-effort) ----------
echo "=== dmesg oops scan (DbusProcess / ctrl_iface) ==="
if dmesg | tail -200 | grep -iE "DbusProcess|ctrl_iface.*oops|wpa_supplicant.*crash" >/dev/null; then
  fail "dmesg shows wpa_supplicant oops — T5 regression"
else
  ok "no wpa_supplicant oops in recent dmesg"
fi

echo ""
echo "RESULT: PASS=${PASS} FAIL=${FAIL}"
[ "${FAIL}" -eq 0 ] && exit 0 || exit 1
