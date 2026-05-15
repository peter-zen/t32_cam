#!/usr/bin/env python3
"""
Prepare simulation camera media from a real source video and rebuild the runtime media DBs.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import shutil
import sqlite3
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from datetime import date, datetime, time, timedelta, timezone
from pathlib import Path
from typing import Optional


PHOTO_EXTS = {".jpg", ".jpeg"}
VIDEO_EXTS = {".mp4", ".mov"}
PHOTO_TYPE = 1
VIDEO_TYPE = 2
THUMB_SCALE_FILTER = "scale=320:-1:force_original_aspect_ratio=decrease"
VIDEO_SCALE_FILTER = "scale=1280:720:force_original_aspect_ratio=increase,crop=1280:720"
DEFAULT_SEED = 20260427
DEFAULT_VIDEO_SEGMENT_SECONDS = 60
DEFAULT_PHOTO_INTERVAL_SECONDS = 30
DEFAULT_VIDEO_WINDOW_DAYS = 5
DEFAULT_PHOTO_WINDOW_DAYS = 7
DEFAULT_VIDEO_FPS = 30
DEFAULT_VIDEO_GOP_FRAMES = 60
DEFAULT_VIDEO_GOP_MS = int(round(DEFAULT_VIDEO_GOP_FRAMES * 1000 / DEFAULT_VIDEO_FPS))
SOURCE_VIDEO_CANDIDATES = [
    "res/Transformers： Optimus Prime & Bumblebee Vs Pacific Rim Robot War (Filme 2023) [AIRXV99OQmY].mp4",
    "res/剪映Agent终于来了！AI自动剪辑～【小白必备】 [IdylAyrQxl0] 2160p.mp4",
    "build_sim/bin/res/full_frame_camera_no_b_30s.h264",
    "tests/assets/video/full_frame_camera.h264",
    "build_sim/bin/res/sample_video.h264",
]


@dataclass
class VideoPlanItem:
    sequence: int
    timestamp_dt: datetime
    start_seconds: int
    duration_seconds: int


@dataclass
class PhotoPlanItem:
    sequence: int
    timestamp_dt: datetime
    capture_seconds: int


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
    container_type: str = ""
    playback_capable: int = 0
    playback_reason: str = ""
    playback_token: str = ""
    range_supported: int = 0
    seek_support: str = ""
    seek_granularity_ms: int = 0
    effective_gop_frames: int = 0
    effective_gop_ms: int = 0
    fragment_index_path: str = ""


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


def parse_source_duration_seconds(path: Path) -> float:
    try:
        data = ffprobe_json(path)
    except (subprocess.CalledProcessError, json.JSONDecodeError) as exc:
        print(f"Failed to probe source video {path}: {exc}", file=sys.stderr)
        sys.exit(1)

    try:
        return float(data.get("format", {}).get("duration") or 0.0)
    except (TypeError, ValueError):
        return 0.0


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


def choose_source_video(project_root: Path, raw_path: Optional[str]) -> Path:
    if raw_path:
        return ensure_file(resolve_path(project_root, raw_path), "source video")

    for candidate in SOURCE_VIDEO_CANDIDATES:
        candidate_path = resolve_path(project_root, candidate)
        if candidate_path.exists():
            return candidate_path

    print("Unable to locate source video", file=sys.stderr)
    for candidate in SOURCE_VIDEO_CANDIDATES:
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


def day_start(day: date) -> datetime:
    return datetime.combine(day, time.min, tzinfo=timezone.utc)


def day_end(day: date) -> datetime:
    return datetime.combine(day, time.max.replace(microsecond=0), tzinfo=timezone.utc)


def default_today(raw_value: Optional[str]) -> date:
    if raw_value:
        return date.fromisoformat(raw_value)
    return datetime.now(timezone.utc).date()


def build_time_window(today: date, days_before_today: int) -> tuple[datetime, datetime]:
    if days_before_today <= 0:
        raise ValueError("window days must be > 0")
    start_day = today - timedelta(days=days_before_today)
    end_day = today - timedelta(days=1)
    return day_start(start_day), day_end(end_day)


def random_timestamps(start_dt: datetime, end_dt: datetime, count: int, seed: int) -> list[datetime]:
    if count <= 0:
        return []
    span_seconds = int((end_dt - start_dt).total_seconds())
    if span_seconds < 0:
        raise ValueError("invalid time window: end before start")
    if count > span_seconds + 1:
        raise ValueError("time window is too small for unique timestamps")

    offsets = sorted(random.Random(seed).sample(range(span_seconds + 1), count))
    return [start_dt + timedelta(seconds=offset) for offset in offsets]


def clamp_requested_count(requested_count: int, available_count: int, label: str) -> int:
    if available_count <= 0:
        print(f"No {label} can be generated from the source video", file=sys.stderr)
        sys.exit(1)
    if requested_count <= 0:
        return available_count
    return min(requested_count, available_count)


def build_video_plan(
    source_duration_seconds: float,
    segment_seconds: int,
    requested_count: int,
    window_start: datetime,
    window_end: datetime,
    seed: int,
) -> list[VideoPlanItem]:
    available_count = int(math.floor(source_duration_seconds / segment_seconds))
    video_count = clamp_requested_count(requested_count, available_count, "video fragments")
    timestamps = random_timestamps(window_start, window_end, video_count, seed)

    return [
        VideoPlanItem(
            sequence=index + 1,
            timestamp_dt=timestamps[index],
            start_seconds=index * segment_seconds,
            duration_seconds=segment_seconds,
        )
        for index in range(video_count)
    ]


def build_photo_plan(
    source_duration_seconds: float,
    interval_seconds: int,
    requested_count: int,
    window_start: datetime,
    window_end: datetime,
    seed: int,
) -> list[PhotoPlanItem]:
    available_offsets = list(range(0, int(source_duration_seconds), interval_seconds))
    photo_count = clamp_requested_count(requested_count, len(available_offsets), "photos")
    timestamps = random_timestamps(window_start, window_end, photo_count, seed)

    return [
        PhotoPlanItem(
            sequence=index + 1,
            timestamp_dt=timestamps[index],
            capture_seconds=available_offsets[index],
        )
        for index in range(photo_count)
    ]


def make_photo_name(dt: datetime) -> str:
    return dt.strftime("IMG_%Y%m%d_%H%M%S.jpg")


def make_video_name(dt: datetime) -> str:
    return dt.strftime("VID_%Y%m%d_%H%M%S.mp4")


def apply_timestamp(path: Path, timestamp_dt: datetime) -> None:
    ts = int(timestamp_dt.timestamp())
    os.utime(path, (ts, ts))


def render_photo(source_video: Path, item: PhotoPlanItem, target_dcim: Path) -> Path:
    output_path = target_dcim / make_photo_name(item.timestamp_dt)
    run_cmd(
        [
            "ffmpeg",
            "-y",
            "-loglevel",
            "error",
            "-ss",
            str(item.capture_seconds),
            "-i",
            str(source_video),
            "-frames:v",
            "1",
            "-vf",
            VIDEO_SCALE_FILTER,
            "-q:v",
            "2",
            str(output_path),
        ]
    )
    apply_timestamp(output_path, item.timestamp_dt)
    return output_path


def render_video(source_video: Path, item: VideoPlanItem, target_dcim: Path) -> Path:
    output_path = target_dcim / make_video_name(item.timestamp_dt)
    run_cmd(
        [
            "ffmpeg",
            "-y",
            "-loglevel",
            "error",
            "-ss",
            str(item.start_seconds),
            "-t",
            str(item.duration_seconds),
            "-i",
            str(source_video),
            "-vf",
            VIDEO_SCALE_FILTER,
            "-r",
            str(DEFAULT_VIDEO_FPS),
            "-c:v",
            "libx264",
            "-preset",
            "veryfast",
            "-crf",
            "22",
            "-pix_fmt",
            "yuv420p",
            "-g",
            str(DEFAULT_VIDEO_GOP_FRAMES),
            "-keyint_min",
            str(DEFAULT_VIDEO_GOP_FRAMES),
            "-sc_threshold",
            "0",
            "-an",
            "-movflags",
            "frag_keyframe+empty_moov+default_base_moof",
            str(output_path),
        ]
    )
    apply_timestamp(output_path, item.timestamp_dt)
    return output_path


def generate_media_set(
    source_video: Path,
    target_dcim: Path,
    video_plan: list[VideoPlanItem],
    photo_plan: list[PhotoPlanItem],
) -> tuple[list[Path], list[Path]]:
    generated_videos = [render_video(source_video, item, target_dcim) for item in video_plan]
    generated_photos = [render_photo(source_video, item, target_dcim) for item in photo_plan]
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
    record = MediaRecord(
        file_path=str(path.resolve()),
        media_type=media_type,
        timestamp=int(stat_result.st_mtime),
        file_size=stat_result.st_size,
        duration=duration if media_type == VIDEO_TYPE else 0,
        width=width,
        height=height,
        thumbnail=make_thumbnail_bytes(path, media_type),
    )
    if media_type == VIDEO_TYPE and suffix == ".mp4":
        record.container_type = "fmp4"
        record.playback_capable = 1
        record.range_supported = 1
        record.seek_support = "keyframe"
        record.seek_granularity_ms = DEFAULT_VIDEO_GOP_MS
        record.effective_gop_frames = DEFAULT_VIDEO_GOP_FRAMES
        record.effective_gop_ms = DEFAULT_VIDEO_GOP_MS
    return record


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
                is_locked INTEGER DEFAULT 0,
                container_type TEXT DEFAULT '',
                playback_capable INTEGER DEFAULT 0,
                playback_reason TEXT DEFAULT '',
                playback_token TEXT DEFAULT '',
                range_supported INTEGER DEFAULT 0,
                seek_support TEXT DEFAULT '',
                seek_granularity_ms INTEGER DEFAULT 0,
                effective_gop_frames INTEGER DEFAULT 0,
                effective_gop_ms INTEGER DEFAULT 0,
                fragment_index_path TEXT DEFAULT ''
            );
            CREATE INDEX IF NOT EXISTS idx_media_time ON media_files(timestamp DESC);
            CREATE INDEX IF NOT EXISTS idx_media_type ON media_files(type);
            CREATE INDEX IF NOT EXISTS idx_media_type_time ON media_files(type, timestamp DESC);
            CREATE INDEX IF NOT EXISTS idx_media_playback ON media_files(type, playback_capable);
            CREATE INDEX IF NOT EXISTS idx_media_playback_token ON media_files(playback_token);
            PRAGMA user_version = 2;
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
                INSERT OR REPLACE INTO media_files (
                    file_path, type, timestamp, file_size, duration, width, height, is_favorite, is_locked,
                    container_type, playback_capable, playback_reason, playback_token, range_supported,
                    seek_support, seek_granularity_ms, effective_gop_frames, effective_gop_ms, fragment_index_path
                ) VALUES (?, ?, ?, ?, ?, ?, ?, 0, 0, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    record.file_path,
                    record.media_type,
                    record.timestamp,
                    record.file_size,
                    record.duration,
                    record.width,
                    record.height,
                    record.container_type,
                    record.playback_capable,
                    record.playback_reason,
                    record.playback_token,
                    record.range_supported,
                    record.seek_support,
                    record.seek_granularity_ms,
                    record.effective_gop_frames,
                    record.effective_gop_ms,
                    record.fragment_index_path,
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


def print_summary(
    source_video: Path,
    source_duration_seconds: float,
    generated_photos: list[Path],
    generated_videos: list[Path],
    records: list[MediaRecord],
    target_root: Path,
    today: date,
    video_window: tuple[datetime, datetime],
    photo_window: tuple[datetime, datetime],
) -> None:
    photo_records = [record for record in records if record.media_type == PHOTO_TYPE]
    video_records = [record for record in records if record.media_type == VIDEO_TYPE]

    print(f"Source video: {source_video}")
    print(f"Source duration seconds: {source_duration_seconds:.3f}")
    print(f"Reference today: {today.isoformat()}")
    print(f"Video time window: {video_window[0].isoformat()} -> {video_window[1].isoformat()}")
    print(f"Photo time window: {photo_window[0].isoformat()} -> {photo_window[1].isoformat()}")
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
            f"duration={record.duration} wh={record.width}x{record.height} "
            f"container={record.container_type or '-'} playback={record.playback_capable}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare simulation camera media and rebuild media DBs")
    parser.add_argument("--target-root", default="sim_sdcard_runtime", help="runtime simulation sdcard root")
    parser.add_argument("--source-video", default="", help="source video path relative to project root or absolute")
    parser.add_argument("--today", default="", help="reference date in YYYY-MM-DD, default is current UTC date")
    parser.add_argument(
        "--video-segment-seconds",
        type=int,
        default=DEFAULT_VIDEO_SEGMENT_SECONDS,
        help="video fragment duration in seconds",
    )
    parser.add_argument(
        "--photo-interval-seconds",
        type=int,
        default=DEFAULT_PHOTO_INTERVAL_SECONDS,
        help="capture one photo every N seconds from the source video",
    )
    parser.add_argument(
        "--video-window-days",
        type=int,
        default=DEFAULT_VIDEO_WINDOW_DAYS,
        help="spread video timestamps across the N days before today",
    )
    parser.add_argument(
        "--photo-window-days",
        type=int,
        default=DEFAULT_PHOTO_WINDOW_DAYS,
        help="spread photo timestamps across the N days before today",
    )
    parser.add_argument(
        "--video-count",
        type=int,
        default=0,
        help="optional cap on generated video fragments, 0 means use all full segments",
    )
    parser.add_argument(
        "--photo-count",
        type=int,
        default=0,
        help="optional cap on generated photos, 0 means use all capture points",
    )
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED, help="random seed for timestamp distribution")
    args = parser.parse_args()

    if args.video_segment_seconds <= 0 or args.photo_interval_seconds <= 0:
        print("video-segment-seconds and photo-interval-seconds must be > 0", file=sys.stderr)
        return 1
    if args.video_window_days <= 0 or args.photo_window_days <= 0:
        print("video-window-days and photo-window-days must be > 0", file=sys.stderr)
        return 1
    if args.video_count < 0 or args.photo_count < 0:
        print("video-count and photo-count must be >= 0", file=sys.stderr)
        return 1

    ensure_tool("ffprobe")
    ensure_tool("ffmpeg")

    project_root = Path(__file__).resolve().parent.parent
    target_root = choose_target_root(project_root, args.target_root)
    source_video = choose_source_video(project_root, args.source_video or None)
    source_duration_seconds = parse_source_duration_seconds(source_video)
    if source_duration_seconds <= 0:
        print(f"Source video has invalid duration: {source_video}", file=sys.stderr)
        return 1

    today = default_today(args.today or None)
    try:
        video_window = build_time_window(today, args.video_window_days)
        photo_window = build_time_window(today, args.photo_window_days)
        video_plan = build_video_plan(
            source_duration_seconds=source_duration_seconds,
            segment_seconds=args.video_segment_seconds,
            requested_count=args.video_count,
            window_start=video_window[0],
            window_end=video_window[1],
            seed=args.seed,
        )
        photo_plan = build_photo_plan(
            source_duration_seconds=source_duration_seconds,
            interval_seconds=args.photo_interval_seconds,
            requested_count=args.photo_count,
            window_start=photo_window[0],
            window_end=photo_window[1],
            seed=args.seed + 1,
        )
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    target_dcim = target_root / "DCIM"
    db_dir = target_root / "data" / "db"

    target_dcim.mkdir(parents=True, exist_ok=True)
    db_dir.mkdir(parents=True, exist_ok=True)

    reset_runtime_tree(target_dcim, db_dir)

    generated_photos, generated_videos = generate_media_set(
        source_video=source_video,
        target_dcim=target_dcim,
        video_plan=video_plan,
        photo_plan=photo_plan,
    )
    media_count, thumb_count, records = rebuild_databases(db_dir, target_dcim)

    print_summary(
        source_video=source_video,
        source_duration_seconds=source_duration_seconds,
        generated_photos=generated_photos,
        generated_videos=generated_videos,
        records=records,
        target_root=target_root,
        today=today,
        video_window=video_window,
        photo_window=photo_window,
    )
    print("")
    print(f"media_file.db rows: {media_count}")
    print(f"media_thumb.db rows: {thumb_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
