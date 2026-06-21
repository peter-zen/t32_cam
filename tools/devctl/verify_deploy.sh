#!/bin/bash
# verify_deploy.sh — confirm the device is running the binary you just built.
# Codifies the 3-step check from .claude/CLAUDE.md "Verifying the build".
# The three "I rebuilt, why is the device still buggy?" failure modes all show
# up as md5 mismatches here.
#
# Usage (from project root, broker running):
#   tools/devctl/verify_deploy.sh htc_workmode_app
#   tools/devctl/verify_deploy.sh htc_workmode_app --maps libmedia_recorder
set -u
DEVCTL="$(dirname "$0")/devctl"
APP="${1:?usage: verify_deploy.sh <app> [--maps <so-substr>]}"
shift || true
MAPS_SO=""
[ "${1:-}" = "--maps" ] && MAPS_SO="${2:-}"

HOST_BIN="build/bin/$APP"
DEV_BIN="/mnt/huntcam/bin/$APP"

ok()   { echo "[PASS] $*"; }
fail() { echo "[FAIL] $*"; exit 1; }

[ -x "$HOST_BIN" ] || fail "host binary missing: $HOST_BIN (build first)"

# 1. host md5
HOST_MD5=$(md5sum "$HOST_BIN" | awk '{print $1}')
echo "host   $HOST_BIN  $HOST_MD5"

# 2. device md5 (via serial broker)
DEV_OUT=$("$DEVCTL" run "md5sum $DEV_BIN" --timeout 15) \
  || fail "devctl run md5sum failed — is the broker up and NFS mounted?"
DEV_MD5=$(echo "$DEV_OUT" | awk '{print $1}')
echo "device $DEV_BIN  ${DEV_MD5:-<empty>}"
[ -n "$DEV_MD5" ] || fail "empty device md5 (got: $DEV_OUT)"
[ "$HOST_MD5" = "$DEV_MD5" ] \
  && ok "host == device md5" \
  || fail "host != device md5 — NFS serving stale binary. Remount with noac (script/mount_nfs.sh)."

# 3. (optional) confirm the process actually loads the .so from /mnt/huntcam/lib,
#    not a stale /usr/lib copy. Launches the app briefly, so needs the HW stack up.
if [ -n "$MAPS_SO" ]; then
  LAUNCH="LD_LIBRARY_PATH=/mnt/huntcam/lib:\$LD_LIBRARY_PATH $DEV_BIN >/dev/null 2>&1 & echo \$!"
  PIDOUT=$("$DEVCTL" run "$LAUNCH" --timeout 12) || fail "could not launch $APP"
  PID=$(echo "$PIDOUT" | grep -oE '[0-9]+' | tail -1)
  [ -n "$PID" ] || fail "no PID from launch (got: $PIDOUT)"
  sleep 1
  MAPS=$("$DEVCTL" run "cat /proc/$PID/maps 2>/dev/null | grep $MAPS_SO | head -1" --timeout 10) || MAPS=""
  "$DEVCTL" run "kill $PID 2>/dev/null" --timeout 5 >/dev/null 2>&1 || true
  case "$MAPS" in
    */mnt/huntcam/lib/*) ok "loaded $MAPS_SO from /mnt/huntcam/lib" ;;
    "")                  fail "$MAPS_SO not found in /proc/$PID/maps" ;;
    *)                   fail "$MAPS_SO loaded from unexpected path: $MAPS" ;;
  esac
fi

echo "RESULT: deploy verified"
