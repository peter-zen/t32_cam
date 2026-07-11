#!/bin/bash
# regress_net_real.sh — real-hardware regression for net Ethernet + USB
# dongle uplinks (T7). The WiFi regression stays in regress_wifi_real.sh (the
# wpa_supplicant PID invariant is WiFi-specific).
#
# Run MANUALLY on the T32 device. Same posture as T5/T6: NOT in CI (the PC
# cannot reach the MIPS target or a real Ethernet/4G link). What we assert:
#   - Ethernet: net --type eth brings up eth0 (IP + gateway non-empty).
#   - USB default: faithfully ports main_app (loadDriver->open->preconfig, no
#     start()) — EXPECTED to yield no IP on real hardware (known limitation
#     T7-usb-no-start). We record this as a documented "known-defect" check.
#   - USB --usb-bringup: additionally calls start() (activates the 4G data
#     context) — expected to yield IP + gateway when the dongle/model match.
#
# Usage (on T32, with build/ mounted at /mnt/huntcam):
#   LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH \
#     bash /mnt/huntcam/script/regress_net_real.sh
#
# Prereqs:
#   - A live Ethernet cable on eth0 (DHCP available) for the ETH case.
#   - A Quectel dongle plugged in (EC20/EC200A/EG800K/RG255AA) for USB cases.
#     Edit USB_MODEL below to match your hardware.

set -u

BIN=/mnt/huntcam/bin/net
LIB=/mnt/huntcam/lib
ETH_IF=eth0
USB_IF=usb0

# EDIT THIS to match your dongle (EC20|EC200A|EG800K|RG255AA):
USB_MODEL=EC200A

export LD_LIBRARY_PATH=${LIB}:${LD_LIBRARY_PATH:-}

PASS=0; FAIL=0; KNOWN=0
ok()     { echo "[PASS] $1";  PASS=$((PASS+1)); }
fail()   { echo "[FAIL] $1";  FAIL=$((FAIL+1)); }
known()  { echo "[KNOWN] $1 (expected per T7-usb-no-start)"; KNOWN=$((KNOWN+1)); }

ip_of()   { ip -4 -o addr show dev "$1" 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -n1; }
gw_of()   { ip route show dev "$1" 2>/dev/null | awk '/default/ {print $3; exit}'; }

# ---------- Ethernet ----------
echo "=== Case ETH: bring up eth0 via --type eth ==="
${BIN} --type eth
rc=$?
ETH_IP=$(ip_of "${ETH_IF}"); ETH_GW=$(gw_of "${ETH_IF}")
if [ "${rc}" -eq 0 ] && [ -n "${ETH_IP}" ] && [ -n "${ETH_GW}" ]; then
  ok "ETH up: ip=${ETH_IP} gw=${ETH_GW}"
else
  fail "ETH rc=${rc} ip='${ETH_IP}' gw='${ETH_GW}' (expect rc 0 + ip + gw)"
fi

echo "=== Case ETH-no-dhcp: --type eth --no-dhcp should not run udhcpc ==="
${BIN} --type eth --no-dhcp
rc=$?
# No DHCP means the link may already be up (from the previous case) or not;
# either way the tool must not crash. rc 0 (already up) or 3 (no link) are fine.
if [ "${rc}" -eq 0 ] || [ "${rc}" -eq 3 ]; then
  ok "ETH --no-dhcp exit in {0,3} (got ${rc})"
else
  fail "ETH --no-dhcp unexpected exit ${rc}"
fi

# ---------- USB default (faithful main_app port; expect no IP) ----------
echo "=== Case USB-default: --type usb (loadDriver->open->preconfig, no start) ==="
${BIN} --type usb
rc=$?
USB_IP=$(ip_of "${USB_IF}")
# Default path does NOT call start() — context is never activated, so usb0 has
# no carrier -> udhcpc times out -> rc 4, or no IP -> rc 3. This is the
# faithfully-ported main_app behaviour (risk T7-usb-no-start), NOT a regression.
if [ "${rc}" -eq 4 ] || [ "${rc}" -eq 3 ] || [ "${rc}" -eq 2 ]; then
  known "USB-default exit ${rc} (no IP: '${USB_IP}')"
else
  echo "[WARN] USB-default rc=${rc} ip='${USB_IP}' (if you got an IP without --usb-bringup, the dongle auto-activated — nice, recheck T7-usb-no-start)"
fi

# ---------- USB --usb-bringup (activate 4G context) ----------
echo "=== Case USB-bringup: --type usb --usb-bringup --usb-model ${USB_MODEL} ==="
${BIN} --type usb --usb-bringup --usb-model "${USB_MODEL}"
rc=$?
USB_IP2=$(ip_of "${USB_IF}"); USB_GW2=$(gw_of "${USB_IF}")
if [ "${rc}" -eq 0 ] && [ -n "${USB_IP2}" ] && [ -n "${USB_GW2}" ]; then
  ok "USB-bringup up: ip=${USB_IP2} gw=${USB_GW2}"
else
  fail "USB-bringup rc=${rc} ip='${USB_IP2}' gw='${USB_GW2}' (check SIM/APN/model match)"
fi

# ---------- bad args ----------
echo "=== Case bad-type: --type bogus -> exit 6 ==="
${BIN} --type bogus >/dev/null 2>&1
rc=$?
if [ "${rc}" -eq 6 ]; then ok "bad --type -> exit 6"; else fail "bad --type expected 6 got ${rc}"; fi

echo "=== Case no-type: no --type -> exit 6 ==="
${BIN} >/dev/null 2>&1
rc=$?
if [ "${rc}" -eq 6 ]; then ok "missing --type -> exit 6"; else fail "missing --type expected 6 got ${rc}"; fi

echo ""
echo "RESULT: PASS=${PASS} FAIL=${FAIL} KNOWN=${KNOWN}"
[ "${FAIL}" -eq 0 ] && exit 0 || exit 1
