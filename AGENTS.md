# AGENTS.md

This file provides guidelines for agentic coding agents working in this T32 firmware project.

## Build Commands

### Target Hardware (Ingenic T32 MIPS)
```bash
mkdir -p build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake ..
make -j$(nproc)
```

### PC Simulation (x86_64)
```bash
mkdir -p build_sim && cd build_sim
cmake -DBUILD_FOR_SIMULATION=ON ..
make -j$(nproc)
```

### Run Single Test
```bash
# Build and run RTSP audio/video test
./build_sim/bin/test_rtsp_av_simple <video_file> <audio_file>
# Example: ./build_sim/bin/test_rtsp_av_simple sim_sdcard/video/test.h264 sim_sdcard/video/test.pcm

# Test with RTSP client
./build_sim/bin/htc_main_app -rs  # Start RTSP server
ffplay rtsp://localhost:554/live   # Play stream
```

### Quick Rebuild
```bash
cd build_sim && make -j$(nproc)
```

## Code Style Guidelines

### File Organization
- Headers: `.h` for C++ headers
- Sources: `.cpp` for C++ implementation, `.c` for C code
- Place platform-specific code behind `#ifdef SIMULATION_MODE`
- Use `namespace media` for media-related components

### Naming Conventions
- **Classes**: PascalCase (e.g., `RtspServer`, `AudioSource`)
- **Methods/Functions**: camelCase (e.g., `setParams()`, `pullPacket()`)
- **Member Variables**: camelCase with `m_` prefix or just camelCase (be consistent within class)
- **Local Variables**: camelCase (e.g., `videoFile`, `audioSource`)
- **Constants/Enums**: UPPER_SNAKE_CASE (e.g., `RTSP_SENSOR_CHN_NUM`, `MAX_FRAMES`)
- **Macros**: UPPER_SNAKE_CASE (e.g., `SIMULATION_MODE`, `BUILD_FOR_SIMULATION`)
- **File Names**: PascalCase for class files (e.g., `RtspServer.h`, `AudioSource.h`)

### Formatting
- **Indentation**: 4 spaces, no tabs
- **Brackets**: K&R style (opening brace on same line)
  ```cpp
  bool open() {
      if (running) {
          return true;
      }
  }
  ```
- **Line Length**: ~120 characters max
- **Spacing**: Space after keywords, before/after operators

### Include Ordering
```cpp
// 1. System headers
#include <thread>
#include <mutex>
#include <vector>

// 2. Third-party libraries
#include <elog.h>

// 3. Local headers
#include "Common.h"
#include "Logger.h"
#include "AudioSource.h"
```

### Error Handling
- **Return Codes**: 0 = success, < 0 = error
- **Use specific codes**: `-1` for retryable errors, `-2` for EOF
  ```cpp
  int pullPacket(void** data, size_t* size);
  // Returns: 0 on success, -1 on retryable error, -2 on EOF
  ```
- **Check return values**: Always check IMP API returns
- **Logging**: Log errors on failure with context

### Logging (Use EasyLogger)
- **Priority**: Use `elog_i` (info), `elog_d` (debug), `elog_e` (error), `elog_w` (warn)
- **Tags**: Uppercase, module-specific (e.g., `"RTSP"`, `"AUDIO"`)
- **Format**: Printf-style
  ```cpp
  elog_i("RTSP", "Server started on port %d", port);
  elog_e("AUDIO", "Failed to open device: %d", ret);
  ```
- **Note**: Legacy `Logger` class is deprecated - do not use in new code

### Memory Management
- Prefer `std::shared_ptr` and `std::unique_ptr`
- Use `std::make_shared<T>()` and `std::make_unique<T>()`
- Manual memory allocation with `malloc`/`free` only for C interop
- RAII for resource management

### Threading
- Use `std::thread`, `std::mutex`, `std::condition_variable`
- Guard shared data with `std::lock_guard<std::mutex>` or `std::unique_lock`
- Use `std::atomic<bool>` for flags
- Pattern for thread loops:
  ```cpp
  std::atomic<bool> running{false};
  std::thread worker([&]() {
      while (running) {
          // Do work
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
  });
  ```

### Platform-Specific Code
- Wrap T32 hardware calls in `#ifndef SIMULATION_MODE`
- Wrap simulation HAL calls in `#ifdef SIMULATION_MODE`
- Example:
  ```cpp
  #ifdef SIMULATION_MODE
      hal_enc_polling_stream(chn, timeout);
  #else
      IMP_Encoder_PollingStream(chn, timeout);
  #endif
  ```

### HAL Layer (Hardware Abstraction)
- For simulation mode, implement HAL stubs in `src/hal/`
- Keep API signatures consistent with IMP SDK
- Return appropriate error codes

### Collaboration Ownership Rules (Multi-Developer)
- `src/hal/**` is PIC-owned (including `src/hal/CMakeLists.txt`, `src/hal/include/`, `src/hal/ingenic/`, `src/hal/simu/`, and all files under this tree).
- Agents must **not** directly modify files under `src/hal/**` during normal tasks.
- If a HAL change is needed, provide a written change proposal first (scope, rationale, impact), and wait for PIC confirmation before applying any code changes.

### Comments and Documentation
- Use Doxygen-style comments for public APIs:
  ```cpp
  /**
   * @brief Pull an audio packet
   * @param data Pointer to buffer pointer
   * @param size Pointer to size variable
   * @return 0 on success, < 0 on error
   */
  ```
- Chinese comments acceptable for internal notes
- Explain platform-specific logic
- **When organizing documentation**:
  - Format content as markdown
  - Use mermaid diagrams for architecture and flow visualization

### Types
- Use `size_t` for sizes and indices
- Use `int` for return codes and small integers
- Use `uint8_t`, `uint16_t`, `uint32_t` for specific bit widths
- Use `std::string` for strings, `const char*` for C strings

### Structs and Classes
- Use `struct` for POD data (public by default)
- Use `class` for objects with encapsulation (private by default)
- Use `std::vector<uint8_t>` for byte arrays
- Define Params structs for configuration:
  ```cpp
  struct Params {
      int sampleRate = 16000;
      int channels = 1;
      bool loop = true;
  };
  ```

### Testing and Debugging
- Enable debug logs: `elog_set_filter_lvl(ELOG_LVL_DEBUG)`
- Check logs in `sim_sdcard/log/app.log`
- Use test scripts: `./test_rtsp_fix.sh`, `./test_client.sh`

### Git Operations Policy
- **IMPORTANT**: Never perform git operations without explicit permission
- **Forbidden**: Do not unilaterally decide to rollback git versions
- **Allowed only when explicitly requested**:
  - Create commits (follow the commit message format below)
  - Push to remote (only when requested)
  - Create pull requests (only when requested)

### Git Commit Messages
- Format: `<type>: <description>`
- Types: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`
- Example: `fix(rtsp): add audio stream retry mechanism`

### Project-Specific Notes
- Dual-platform project: code must compile for both T32 and PC
- File source mode: for PC simulation testing without hardware
- RTSP server port: 554 by default
- Audio default: 16kHz, 16-bit, mono, L16 codec
- Video default: H.264, 25fps

### New Architecture (Refactoring Complete - 2025-01-16)

The RTSP server has been refactored with a clean architecture:

**New Module Structure**:
```
src/media/
├── base/           # Base interfaces (IMediaSource, IVideoSource, IAudioSource)
├── audio/          # Audio source implementations (AudioLiveSource, AudioFileSource)
├── video/          # Video source implementations (VideoLiveSource, VideoFileSource)
├── fifo/           # Unified MediaFIFO for producer-consumer pattern
└── rtsp/           # RTSP protocol layer (RtspServer, MediaSession)
```

**Key Patterns**:
- **IMediaSource**: Unified interface for all media sources
- **MediaFIFO**: Thread-safe FIFO for producer-consumer
- **MediaSession**: Manages producer threads and FIFOs
- **VideoLiveSource**: Encapsulates IMP_Encoder logic
- **AudioLiveSource**: Uses MediaFIFO instead of internal queue

**Legacy Files** (deprecated, moved to new locations):
- `src/media/rtsp/AudioLiveSource.*` → `src/media/audio/`
- `src/media/rtsp/AudioFileSource.*` → `src/media/audio/`
- `src/media/rtsp/VideoFileSource.*` → `src/media/video/`

**When Adding New Media Sources**:
1. Inherit from `IMediaSource` (or `IVideoSource`/`IAudioSource`)
2. Implement `open()`, `close()`, `pullData()`, `releaseData()`
3. Return 0 on success, -1 on retryable error, -2 on EOF
4. Use MediaFIFO for producer-consumer pattern
5. Wrap hardware calls in `#ifdef SIMULATION_MODE`

**See Also**:
- `doc/refactoring_completion_summary.md` - Complete refactoring summary
- `doc/refactoring_implementation_plan.md` - Detailed implementation plan
