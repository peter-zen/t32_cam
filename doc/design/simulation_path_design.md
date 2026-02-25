# Simulation Environment Path Design

## 1. Overview
This document defines the file system structure and path conventions for the PC simulation environment of the Huntcam T32 project. The goal is to emulate the T32 hardware environment as closely as possible while accommodating development convenience on PC.

## 2. Simulated SD Card (`build_sim/sdcard`)
The directory `build_sim/sdcard` serves as the root for the simulated SD card. This structure mirrors the physical SD card on the T32 device.

| Path on PC (Relative to Project Root) | Path on T32 Device | Description |
| ------------------------------------- | ------------------ | ----------- |
| `build_sim/sdcard/data/db` | `/sdcard/data/db` | SQLite databases (media.db, thumb.db) |
| `build_sim/sdcard/DCIM` | `/sdcard/DCIM` | Camera recordings (photos/videos) |
| `build_sim/sdcard/log` | `/sdcard/log` | Application logs (app.log) |
| `build_sim/sdcard/configs` | `/sdcard/configs` | Runtime configuration files (rtsp_config.ini, etc.) |
| `build_sim/sdcard/media/video` | (Simulation Only) | Pre-recorded video assets for RTSP simulation |
| `build_sim/sdcard/media/audio` | (Simulation Only) | Pre-recorded audio assets for RTSP simulation |

## 3. Source Assets (`tests/assets`)
Read-only assets used for testing and simulation are stored in `tests/assets`.

| Path | Content |
| ---- | ------- |
| `tests/assets/video` | Source video files (e.g., `full_frame_camera.h264`) |
| `tests/assets/audio` | Source audio files (e.g., `full_frame_camera.pcm`) |
| `tests/assets/configs` | Configuration templates (e.g., `rtsp_config.ini`) |

## 4. Runtime Behavior

### 4.1. RTSP Server Configuration
The RTSP server configuration (`rtsp_config.ini`) determines the media sources.

- **Config Location**: The application looks for `sdcard/configs/rtsp_config.ini`.
- **Asset Paths in Config**: 
  - To support development without constant copying, `tests/assets/configs/rtsp_config.ini` uses **relative paths** to source assets (e.g., `../tests/assets/video/full_frame_camera.h264`).
  - This allows the simulation to read directly from the source tree when running from `build_sim`.

### 4.2. Fallback Mechanism
If `rtsp_config.ini` is missing or paths are invalid, the application falls back to default paths within the simulated SD card:
- Video: `sdcard/media/video/full_frame_camera.h264`
- Audio: `sdcard/media/audio/full_frame_camera.pcm`

### 4.3. Test Scripts (`test_av_full.sh`)
The automated test script initializes the simulation environment:
1. Creates the directory structure in `build_sim/sdcard`.
2. Copies `rtsp_config.ini` from `tests/assets/configs` to `build_sim/sdcard/configs`.
3. Copies video/audio assets from `tests/assets` to `build_sim/sdcard/media` (ensuring fallback paths work if config fails).
4. Executes the application from `build_sim` directory.

## 5. Migration Note
- The legacy directory `sim_sdcard` (in project root) is **deprecated** and has been renamed to `sim_sdcard_bk`.
- All simulation I/O should occur within `build_sim/sdcard`.
