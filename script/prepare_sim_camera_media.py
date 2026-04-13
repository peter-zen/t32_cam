#!/usr/bin/env python3
"""
Prepare a diverse simulation camera dataset and rebuild the runtime media DBs.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path
from typing import Optional


PHOTO_EXTS = {".jpg", ".jpeg"}
VIDEO_EXTS = {".mp4", ".mov"}
PHOTO_TYPE = 1
VIDEO_TYPE = 2
THUMB_SCALE_FILTER = "scale=320:-1:force_original_aspect_ratio=decrease"
DEFAULT_FONT_FILE = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")
BASE_START_TIME = datetime(2026, 4, 13, 8, 0, 0)
TIMESTAMP_STEP_SECONDS = 91
COLOR_PALETTE = [
    "0xE85D04",
    "0x2A9D8F",
    "0xE63946",
    "0x264653",
    "0xFFB703",
    "0x457B9D",
    "0xD62828",
    "0x6A4C93",
    "0x1D3557",
    "0x3A86FF",
    "0x43AA8B",
    "0xF72585",
]
SOURCE_VIDEO_CANDIDATES = [
    "build_sim/bin/res/full_frame_camera_no_b_30s.h264",
    "tests/assets/video/full_frame_camera.h264",
    "build_sim/bin/res/sample_video.h264",
]
SOURCE_IMAGE_CANDIDATES = [
    "build_sim/bin/res/sample_image.jpeg",
    "src/hal/simu/res/sample_image.jpeg",
]


@dataclass
class MediaPlanItem:
    kind: str
    sequence: int
    global_index: int
    timestamp_dt: datetime


@dataclass
class MediaRecord:
    file_path: str
    media_type: int
    timestamp: int
    file_size: int
    duration: int
    width: int
    height: int
    thumbnail: bytes


def ensure_tool(name: str) -> None:
    if shutil.which(name):
        return
    print(f"Missing required command: {name}", file=sys.stderr)
    sys.exit(1)


def ensure_file(path: Path, description: str) -> Path:
    if path.exists():
        return path
    print(f"Missing {description}: {path}", file=sys.stderr)
    sys.exit(1)


def run_cmd(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, capture_output=True, text=True, check=True)


def ffprobe_json(path: Path) -> dict:
    result = run_cmd(
        [
            "ffprobe",
            "-v",
            "error",
            "-show_entries",
            "stream=index,codec_type,width,height:format=duration,size",
            "-of",
            "json",
            str(path),
        ]
    )
    return json.loads(result.stdout or "{}")


def parse_media_probe(path: Path) -> tuple[int, int, int]:
    try:
        data = ffprobe_json(path)
    except (subprocess.CalledProcessError, json.JSONDecodeError):
        return 0, 0, 0

    width = 0
    height = 0
    for stream in data.get("streams", []):
        if stream.get("codec_type") == "video":
            width = int(stream.get("width") or 0)
            height = int(stream.get("height") or 0)
            break

    duration = 0
    format_info = data.get("format", {})
    try:
        duration = int(round(float(format_info.get("duration") or 0)))
    except (TypeError, ValueError):
        duration = 0

    return width, height, duration


def is_valid_photo(path: Path) -> bool:
    if path.suffix.lower() not in PHOTO_EXTS or not path.is_file() or path.stat().st_size <= 0:
        return False
    with path.open("rb") as handle:
        return handle.read(2) == b"\xff\xd8"


def is_valid_video(path: Path) -> bool:
    if path.suffix.lower() not in VIDEO_EXTS or not path.is_file() or path.stat().st_size <= 0:
        return False

    try:
        data = ffprobe_json(path)
    except (subprocess.CalledProcessError, json.JSONDecodeError):
        return False

    has_video_stream = any(
        stream.get("codec_type") == "video"
        and int(stream.get("width") or 0) > 0
        and int(stream.get("height") or 0) > 0
        for stream in data.get("streams", [])
    )
    try:
        duration = float(data.get("format", {}).get("duration") or 0)
    except (TypeError, ValueError):
        duration = 0.0
    return has_video_stream and duration > 0


def resolve_path(project_root: Path, raw_path: str) -> Path:
    path = Path(raw_path)
    if path.is_absolute():
        return path
    return (project_root / path).resolve()


def is_writable_target(path: Path) -> bool:
    if path.exists():
        return os.access(path, os.W_OK)

    current = path.parent
    while not current.exists() and current != current.parent:
        current = current.parent
    return current.exists() and os.access(current, os.W_OK)


def choose_target_root(project_root: Path, raw_path: str) -> Path:
    requested = resolve_path(project_root, raw_path)
    if is_writable_target(requested):
        return requested

    fallback = (project_root / "sim_sdcard_runtime").resolve()
    if is_writable_target(fallback):
        print(f"Target {requested} is not writable, falling back to {fallback}")
        return fallback

    print(f"Neither {requested} nor {fallback} is writable", file=sys.stderr)
    sys.exit(1)


def choose_existing(project_root: Path, candidates: list[str], description: str) -> Path:
    for candidate in candidates:
        path = resolve_path(project_root, candidate)
        if path.exists():
            return path
    print(f"Unable to locate {description}", file=sys.stderr)
    for candidate in candidates:
        print(f"  - {resolve_path(project_root, candidate)}", file=sys.stderr)
    sys.exit(1)


def reset_runtime_tree(target_dcim: Path, db_dir: Path) -> None:
    if target_dcim.exists():
        for path in sorted(target_dcim.iterdir()):
            if path.is_file():
                path.unlink()

    for db_path in (
        db_dir / "media_file.db",
        db_dir / "media_file.db-wal",
        db_dir / "media_file.db-shm",
        db_dir / "media_thumb.db",
        db_dir / "media_thumb.db-wal",
        db_dir / "media_thumb.db-shm",
    ):
        if db_path.exists():
            db_path.unlink()


def build_media_plan(photo_count: int, video_count: int) -> list[MediaPlanItem]:
    plan: list[MediaPlanItem] = []
    pattern = ("photo", "photo", "video", "photo", "video")
    photo_index = 0
    video_index = 0
    current_dt = BASE_START_TIME
    global_index = 0

    while photo_index < photo_count or video_index < video_count:
        kind = pattern[global_index % len(pattern)]
        if kind == "photo" and photo_index >= photo_count:
            kind = "video"
        elif kind == "video" and video_index >= video_count:
            kind = "photo"

        current_dt += timedelta(seconds=TIMESTAMP_STEP_SECONDS)
        if kind == "photo":
            photo_index += 1
            sequence = photo_index
        else:
            video_index += 1
            sequence = video_index

        plan.append(MediaPlanItem(kind=kind, sequence=sequence, global_index=global_index, timestamp_dt=current_dt))
        global_index += 1

    return plan


def choose_color(index: int) -> str:
    return COLOR_PALETTE[index % len(COLOR_PALETTE)]


def drawtext_filter(text: str, x: int, y: int, fontsize: int) -> str:
    return (
        "drawtext="
        f"fontfile={DEFAULT_FONT_FILE}:"
        f"text='{text}':"
        f"x={x}:y={y}:"
        f"fontsize={fontsize}:"
        "fontcolor=white:borderw=2:bordercolor=black"
    )


def common_overlay_filters(label: str, subtitle: str, index: int) -> list[str]:
    accent_x = 40 + (index * 83) % 920
    accent_y = 130 + (index * 47) % 320
    return [
        f"drawbox=x=0:y=0:w=iw:h=96:color={choose_color(index)}@0.55:t=fill",
        f"drawbox=x={accent_x}:y={accent_y}:w=280:h=160:color={choose_color(index + 4)}@0.38:t=fill",
        "drawbox=x=18:y=622:w=1244:h=74:color=black@0.28:t=fill",
        drawtext_filter(label, 46, 26, 42),
        drawtext_filter(subtitle, 34, 644, 26),
    ]


def build_photo_filter(photo_index: int, global_index: int, timestamp_dt: datetime) -> str:
    crop_x = (global_index * 97) % 160
    crop_y = (global_index * 61) % 80
    crop_x_wide = (global_index * 53) % 320
    crop_y_wide = (global_index * 37) % 180
    style = global_index % 8
    filters = [
        "scale=1280:720:force_original_aspect_ratio=increase",
        "crop=1280:720",
    ]

    if style == 0:
        filters += [
            f"crop=1120:630:{crop_x}:{crop_y}",
            "scale=1280:720",
            "eq=contrast=1.10:saturation=1.20:brightness=-0.02",
        ]
    elif style == 1:
        filters += ["hflip", f"hue=h={(global_index * 27) % 180 - 90}:s=1.25"]
    elif style == 2:
        filters += ["vflip", "curves=preset=cross_process"]
    elif style == 3:
        filters += [
            f"crop=960:540:{crop_x_wide}:{crop_y_wide}",
            "scale=1280:720",
            "unsharp=5:5:0.8:5:5:0.0",
        ]
    elif style == 4:
        filters += [
            "transpose=1",
            "scale=1280:720:force_original_aspect_ratio=increase",
            "crop=1280:720",
            "eq=brightness=0.05:contrast=1.08",
        ]
    elif style == 5:
        filters += [
            "transpose=2",
            "scale=1280:720:force_original_aspect_ratio=increase",
            "crop=1280:720",
            "vignette",
        ]
    elif style == 6:
        filters += ["hue=h=55:s=0.82", "boxblur=1:1", "eq=contrast=1.16"]
    else:
        filters += ["curves=preset=lighter", "eq=saturation=1.40:gamma=1.04", "vignette"]

    label = f"PHOTO-{photo_index:02d}"
    subtitle = f"SIM-{timestamp_dt.strftime('%Y%m%d-%H%M%S')}"
    filters += common_overlay_filters(label, subtitle, global_index)
    return ",".join(filters)


def build_video_filter(video_index: int, global_index: int, timestamp_dt: datetime) -> str:
    crop_x = (global_index * 41) % 256
    crop_y = (global_index * 29) % 144
    style = global_index % 8
    filters = [
        "scale=1280:720:force_original_aspect_ratio=increase",
        "crop=1280:720",
    ]

    if style == 0:
        filters += [
            f"crop=1152:648:{crop_x // 2}:{crop_y // 2}",
            "scale=1280:720",
            "eq=contrast=1.08:saturation=1.22:brightness=-0.01",
        ]
    elif style == 1:
        filters += ["hflip", f"hue=h={(global_index * 19) % 180 - 90}:s=1.18"]
    elif style == 2:
        filters += ["curves=preset=cross_process", "vignette"]
    elif style == 3:
        filters += [
            "transpose=1",
            "scale=1280:720:force_original_aspect_ratio=increase",
            "crop=1280:720",
            "eq=brightness=0.04:contrast=1.06",
        ]
    elif style == 4:
        filters += [
            f"crop=1024:576:{crop_x}:{crop_y}",
            "scale=1280:720",
            "unsharp=5:5:0.7:5:5:0.0",
        ]
    elif style == 5:
        filters += ["boxblur=1:1", "eq=contrast=1.18:saturation=1.25"]
    elif style == 6:
        filters += ["vflip", "hue=h=-45:s=1.10"]
    else:
        filters += ["curves=preset=lighter", "eq=gamma=1.08:saturation=1.35", "vignette"]

    label = f"VIDEO-{video_index:02d}"
    subtitle = f"SIM-{timestamp_dt.strftime('%Y%m%d-%H%M%S')}"
    filters += common_overlay_filters(label, subtitle, global_index + 100)
    return ",".join(filters)


def make_photo_name(dt: datetime) -> str:
    return dt.strftime("IMG_%Y%m%d_%H%M%S.jpg")


def make_video_name(dt: datetime) -> str:
    return dt.strftime("VID_%Y%m%d_%H%M%S.mp4")


def apply_timestamp(path: Path, timestamp_dt: datetime) -> None:
    ts = int(timestamp_dt.timestamp())
    os.utime(path, (ts, ts))


def prepare_source_video(project_root: Path, temp_dir: Path) -> Path:
    source_video = choose_existing(project_root, SOURCE_VIDEO_CANDIDATES, "simulation source video")
    source_mp4 = temp_dir / "source_video.mp4"
    input_args: list[str] = []
    if source_video.suffix.lower() in {".h264", ".264", ".h265", ".hevc"}:
        input_args += ["-framerate", "30"]

    run_cmd(
        [
            "ffmpeg",
            "-y",
            "-loglevel",
            "error",
            *input_args,
            "-i",
            str(source_video),
            "-vf",
            "scale=1280:720:force_original_aspect_ratio=increase,crop=1280:720",
            "-c:v",
            "libx264",
            "-preset",
            "veryfast",
            "-crf",
            "20",
            "-pix_fmt",
            "yuv420p",
            "-an",
            "-movflags",
            "+faststart",
            str(source_mp4),
        ]
    )
    return ensure_file(source_mp4, "prepared simulation source video")


def prepare_source_image(project_root: Path) -> Path:
    source_image = choose_existing(project_root, SOURCE_IMAGE_CANDIDATES, "simulation source image")
    return ensure_file(source_image, "simulation source image")


def render_photo(source_video: Path, source_image: Path, item: MediaPlanItem, target_dcim: Path) -> Path:
    output_path = target_dcim / make_photo_name(item.timestamp_dt)
    filter_chain = build_photo_filter(item.sequence, item.global_index, item.timestamp_dt)
    use_image = item.sequence % 4 == 0

    if use_image:
        cmd = [
            "ffmpeg",
            "-y",
            "-loglevel",
            "error",
            "-loop",
            "1",
            "-i",
            str(source_image),
            "-frames:v",
            "1",
            "-vf",
            filter_chain,
            "-q:v",
            "2",
            str(output_path),
        ]
    else:
        frame_ts = 0.65 + ((item.global_index * 1.37) % 28.2)
        cmd = [
            "ffmpeg",
            "-y",
            "-loglevel",
            "error",
            "-ss",
            f"{frame_ts:.2f}",
            "-i",
            str(source_video),
            "-frames:v",
            "1",
            "-vf",
            filter_chain,
            "-q:v",
            "2",
            str(output_path),
        ]

    run_cmd(cmd)
    apply_timestamp(output_path, item.timestamp_dt)
    return output_path


def render_video(source_video: Path, item: MediaPlanItem, target_dcim: Path) -> Path:
    output_path = target_dcim / make_video_name(item.timestamp_dt)
    filter_chain = build_video_filter(item.sequence, item.global_index, item.timestamp_dt)
    duration = 4 + (item.sequence % 5)
    max_start = max(0.5, 29.2 - duration)
    start_ts = 0.40 + ((item.global_index * 1.71) % max_start)

    run_cmd(
        [
            "ffmpeg",
            "-y",
            "-loglevel",
            "error",
            "-ss",
            f"{start_ts:.2f}",
            "-t",
            f"{duration:.2f}",
            "-i",
            str(source_video),
            "-vf",
            filter_chain,
            "-r",
            "30",
            "-c:v",
            "libx264",
            "-preset",
            "veryfast",
            "-crf",
            "24",
            "-pix_fmt",
            "yuv420p",
            "-an",
            "-movflags",
            "+faststart",
            str(output_path),
        ]
    )
    apply_timestamp(output_path, item.timestamp_dt)
    return output_path


def generate_media_set(project_root: Path, target_dcim: Path, photo_count: int, video_count: int) -> tuple[list[Path], list[Path]]:
    plan = build_media_plan(photo_count, video_count)
    generated_photos: list[Path] = []
    generated_videos: list[Path] = []

    with tempfile.TemporaryDirectory(prefix="sim_camera_media_") as tmp_dir:
        temp_dir = Path(tmp_dir)
        source_video = prepare_source_video(project_root, temp_dir)
        source_image = prepare_source_image(project_root)

        for item in plan:
            if item.kind == "photo":
                generated_photos.append(render_photo(source_video, source_image, item, target_dcim))
            else:
                generated_videos.append(render_video(source_video, item, target_dcim))

    return generated_photos, generated_videos


def make_thumbnail_bytes(path: Path, media_type: int) -> bytes:
    with tempfile.TemporaryDirectory(prefix="sim_thumb_") as tmp_dir:
        thumb_path = Path(tmp_dir) / "thumb.jpg"
        if media_type == PHOTO_TYPE:
            cmd = [
                "ffmpeg",
                "-y",
                "-loglevel",
                "error",
                "-i",
                str(path),
                "-frames:v",
                "1",
                "-vf",
                THUMB_SCALE_FILTER,
                str(thumb_path),
            ]
        else:
            cmd = [
                "ffmpeg",
                "-y",
                "-loglevel",
                "error",
                "-ss",
                "0.20",
                "-i",
                str(path),
                "-frames:v",
                "1",
                "-vf",
                THUMB_SCALE_FILTER,
                str(thumb_path),
            ]
        try:
            run_cmd(cmd)
        except subprocess.CalledProcessError:
            return b""
        if not thumb_path.exists():
            return b""
        return thumb_path.read_bytes()


def build_record(path: Path) -> Optional[MediaRecord]:
    suffix = path.suffix.lower()
    if suffix in PHOTO_EXTS:
        media_type = PHOTO_TYPE
        if not is_valid_photo(path):
            return None
    elif suffix in VIDEO_EXTS:
        media_type = VIDEO_TYPE
        if not is_valid_video(path):
            return None
    else:
        return None

    width, height, duration = parse_media_probe(path)
    stat_result = path.stat()
    return MediaRecord(
        file_path=str(path.resolve()),
        media_type=media_type,
        timestamp=int(stat_result.st_mtime),
        file_size=stat_result.st_size,
        duration=duration if media_type == VIDEO_TYPE else 0,
        width=width,
        height=height,
        thumbnail=make_thumbnail_bytes(path, media_type),
    )


def rebuild_databases(db_dir: Path, target_dcim: Path) -> tuple[int, int, list[MediaRecord]]:
    media_db = db_dir / "media_file.db"
    thumb_db = db_dir / "media_thumb.db"

    media_conn = sqlite3.connect(media_db)
    thumb_conn = sqlite3.connect(thumb_db)
    try:
        media_conn.executescript(
            """
            CREATE TABLE IF NOT EXISTS media_files (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                file_path TEXT NOT NULL UNIQUE,
                type INTEGER NOT NULL,
                timestamp INTEGER NOT NULL,
                file_size INTEGER NOT NULL,
                duration INTEGER DEFAULT 0,
                width INTEGER DEFAULT 0,
                height INTEGER DEFAULT 0,
                is_favorite INTEGER DEFAULT 0,
                is_locked INTEGER DEFAULT 0
            );
            CREATE INDEX IF NOT EXISTS idx_media_time ON media_files(timestamp DESC);
            CREATE INDEX IF NOT EXISTS idx_media_type ON media_files(type);
            CREATE INDEX IF NOT EXISTS idx_media_type_time ON media_files(type, timestamp DESC);
            """
        )
        thumb_conn.executescript(
            """
            CREATE TABLE IF NOT EXISTS thumbnails (
                file_path TEXT PRIMARY KEY,
                data BLOB
            );
            """
        )

        records: list[MediaRecord] = []
        for path in sorted(target_dcim.iterdir()):
            if not path.is_file():
                continue
            record = build_record(path)
            if record is not None:
                records.append(record)

        records.sort(key=lambda item: item.timestamp, reverse=True)

        for record in records:
            media_conn.execute(
                """
                INSERT OR REPLACE INTO media_files
                    (file_path, type, timestamp, file_size, duration, width, height, is_favorite, is_locked)
                VALUES (?, ?, ?, ?, ?, ?, ?, 0, 0)
                """,
                (
                    record.file_path,
                    record.media_type,
                    record.timestamp,
                    record.file_size,
                    record.duration,
                    record.width,
                    record.height,
                ),
            )
            if record.thumbnail:
                thumb_conn.execute(
                    "INSERT OR REPLACE INTO thumbnails (file_path, data) VALUES (?, ?)",
                    (record.file_path, record.thumbnail),
                )

        media_conn.commit()
        thumb_conn.commit()
        thumb_count = thumb_conn.execute("SELECT COUNT(*) FROM thumbnails").fetchone()[0]
        media_count = media_conn.execute("SELECT COUNT(*) FROM media_files").fetchone()[0]
        return media_count, thumb_count, records
    finally:
        media_conn.close()
        thumb_conn.close()


def print_summary(generated_photos: list[Path], generated_videos: list[Path], records: list[MediaRecord], target_root: Path) -> None:
    photo_records = [record for record in records if record.media_type == PHOTO_TYPE]
    video_records = [record for record in records if record.media_type == VIDEO_TYPE]

    print(f"Target runtime root: {target_root}")
    print(f"Generated photos: {len(generated_photos)}")
    print(f"Generated videos: {len(generated_videos)}")
    print(f"Runtime photo records: {len(photo_records)}")
    print(f"Runtime video records: {len(video_records)}")
    print("")
    print("Latest runtime media:")
    for record in records[:12]:
        kind = "PHOTO" if record.media_type == PHOTO_TYPE else "VIDEO"
        print(
            f"  {kind} {Path(record.file_path).name} "
            f"size={record.file_size} ts={record.timestamp} "
            f"duration={record.duration} wh={record.width}x{record.height}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare simulation camera media and rebuild media DBs")
    parser.add_argument("--target-root", default="sim_sdcard_runtime", help="runtime simulation sdcard root")
    parser.add_argument("--photo-count", type=int, default=30, help="number of photo files to generate")
    parser.add_argument("--video-count", type=int, default=20, help="number of video files to generate")
    args = parser.parse_args()

    if args.photo_count < 0 or args.video_count < 0 or (args.photo_count + args.video_count) == 0:
        print("photo-count and video-count must produce at least one media file", file=sys.stderr)
        return 1

    ensure_tool("ffprobe")
    ensure_tool("ffmpeg")
    ensure_file(DEFAULT_FONT_FILE, "drawtext font file")

    project_root = Path(__file__).resolve().parent.parent
    target_root = choose_target_root(project_root, args.target_root)
    target_dcim = target_root / "DCIM"
    db_dir = target_root / "data" / "db"

    target_dcim.mkdir(parents=True, exist_ok=True)
    db_dir.mkdir(parents=True, exist_ok=True)

    reset_runtime_tree(target_dcim, db_dir)

    generated_photos, generated_videos = generate_media_set(
        project_root,
        target_dcim,
        photo_count=args.photo_count,
        video_count=args.video_count,
    )
    media_count, thumb_count, records = rebuild_databases(db_dir, target_dcim)

    print_summary(generated_photos, generated_videos, records, target_root)
    print("")
    print(f"media_file.db rows: {media_count}")
    print(f"media_thumb.db rows: {thumb_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
