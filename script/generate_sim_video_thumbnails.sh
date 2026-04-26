#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SIM_ROOT="${1:-$PROJECT_ROOT/sim_sdcard_runtime}"
MEDIA_DB="$SIM_ROOT/data/db/media_file.db"
THUMB_DB="$SIM_ROOT/data/db/media_thumb.db"
TMP_DIR="$(mktemp -d /tmp/t32_thumb_gen.XXXXXX)"

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "[FAIL] Missing command: $1" >&2
        exit 1
    fi
}

resolve_ffmpeg() {
    if [[ -n "${FFMPEG_BIN:-}" && -x "${FFMPEG_BIN:-}" ]]; then
        printf '%s\n' "$FFMPEG_BIN"
        return 0
    fi

    require_cmd uv
    uv run --with imageio-ffmpeg python - <<'PY'
import imageio_ffmpeg
print(imageio_ffmpeg.get_ffmpeg_exe())
PY
}

require_cmd sqlite3

if [[ ! -f "$MEDIA_DB" ]]; then
    echo "[FAIL] media db not found: $MEDIA_DB" >&2
    exit 1
fi

if [[ ! -f "$THUMB_DB" ]]; then
    sqlite3 "$THUMB_DB" <<'SQL'
CREATE TABLE IF NOT EXISTS thumbnails (
    file_path TEXT PRIMARY KEY,
    data BLOB
);
SQL
fi

FFMPEG="$(resolve_ffmpeg)"

mapfile -t media_files < <(
    sqlite3 "$MEDIA_DB" "
        ATTACH DATABASE '$THUMB_DB' AS thumb_db;
        SELECT m.file_path
        FROM media_files AS m
        LEFT JOIN thumb_db.thumbnails AS t ON t.file_path = m.file_path
        WHERE m.type = 2
          AND m.file_path LIKE '$SIM_ROOT/DCIM/%'
          AND t.file_path IS NULL
        ORDER BY m.timestamp DESC;
    "
)

if [[ "${#media_files[@]}" -eq 0 ]]; then
    echo "[INFO] no missing video thumbnails under $SIM_ROOT"
    exit 0
fi

count=0
for media_path in "${media_files[@]}"; do
    if [[ ! -f "$media_path" ]]; then
        echo "[WARN] skip missing media: $media_path" >&2
        continue
    fi

    thumb_path="$TMP_DIR/$(basename "$media_path").jpg"
    "$FFMPEG" -hide_banner -loglevel error \
        -ss 1 \
        -i "$media_path" \
        -frames:v 1 \
        -vf "scale=320:-1:force_original_aspect_ratio=decrease" \
        -y "$thumb_path"

    sqlite3 "$THUMB_DB" <<SQL
INSERT OR REPLACE INTO thumbnails (file_path, data)
VALUES ('$media_path', readfile('$thumb_path'));
SQL

    count=$((count + 1))
    echo "[OK] thumbnail saved: $media_path"
done

echo "[DONE] generated $count thumbnails"
