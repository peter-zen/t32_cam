# MediaScanner Modes

## Background

The camera can enter a fast capture or fast recording path before the database layer is ready. In that case the media file must still be written successfully, but metadata and thumbnails cannot be committed to SQLite immediately.

The storage design keeps final thumbnail data in `media_thumb.db`, not in a permanent thumbnail file tree. The pending thumbnail directory is a short-lived recovery queue used only until the database is available.

## Final Storage State

After synchronization completes:

- Original media files stay under the media directory, for example `/sdcard/DCIM`.
- Media metadata is stored in `media_file.db`.
- Thumbnail JPEG blobs are stored in `media_thumb.db`.
- The pending thumbnail directory should be empty or nearly empty.

## Modes

### PendingThumbnails

This is the production-oriented fast mode.

It scans only the pending thumbnail directory, then maps each pending thumbnail back to its media file. This is efficient for the product shape where files are created by the camera itself and users are not expected to copy media into the device manually.

Default paths:

- Media root: `/sdcard/DCIM` on device, `sim_sdcard_runtime/DCIM` in simulation.
- Pending thumbnails: `/sdcard/data/thumb_pending` on device, `sim_sdcard_runtime/data/thumb_pending` in simulation.

Pending thumbnail naming:

```text
<media-file-name>.thumb.jpg
<media-file-name>.thumb.jpeg
```

Examples:

```text
IMG_20260425_120000.jpg.thumb.jpg
VID_20260425_120500.mp4.thumb.jpg
```

Synchronization rules:

- If both pending thumbnail and matching media file exist, insert or update `media_files`, write the thumbnail blob to `media_thumb.db`, then delete the pending thumbnail.
- If the pending thumbnail exists but the matching media file is missing, delete the pending thumbnail and log a warning.
- If the media record already exists but its thumbnail is missing, write the thumbnail and delete the pending file.
- Temporary or hidden pending files are skipped. Producers should write a temporary file first and rename it to the final pending thumbnail name only after the write is complete.

This mode intentionally does not discover media files that have no pending thumbnail and no database record. That is an accepted tradeoff for normal product operation.

### FullScan

This is the maintenance and recovery mode.

It scans the full media tree and reconciles `media_files` with the files on disk. It is used for database rebuilds, migration, repair, factory diagnostics, and development workflows where media files may be copied into the device externally.

Full scan is more expensive when there are thousands of media files, so it should not be the default normal boot path for production.

## Mode Selection

`MediaScanner` supports both modes through `MediaScannerOptions`.

Runtime startup can select the mode through environment variables:

```text
MEDIA_SCANNER_MODE=pending_thumb
MEDIA_SCANNER_MODE=full
THUMB_PENDING_DIR=/sdcard/data/thumb_pending
```

Recognized full scan values are `full`, `full_scan`, and `media`. Any other value uses `PendingThumbnails`.

## Capture And Recording Requirements

Fast capture and recording flows should follow this ordering:

1. Write the original media file to the media directory.
2. Generate a thumbnail JPEG.
3. Write the thumbnail to the pending directory using a temporary name.
4. Rename the temporary thumbnail to `<media-file-name>.thumb.jpg` after the write is complete.

If the database is ready during normal capture or recording, the flow may write directly to `media_file.db` and `media_thumb.db`. The pending thumbnail path remains the fallback and recovery mechanism when the database is not ready or direct DB write fails.
