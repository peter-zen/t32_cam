# Camera HTTP API Implementation Plan

## 1. Objective
Implement the RESTful API defined in `doc/design/camera_http_api_design.md` for the Huntcam T32 project. The implementation will use an abstraction layer (`ICameraService`) to ensure compatibility with both T32 hardware and PC simulation environments.

## 2. Prerequisites
- [x] **Path Standardization**: Simulation environment paths defined and implemented (`doc/design/simulation_path_design.md`).
- [ ] **Service Abstraction**: `ICameraService` interface and factory pattern implementation.

## 3. Implementation Phases

### Phase 1: Core Architecture & Basic APIs (Priority: High)
**Goal**: Establish the service layer and implement fundamental camera operations.

1. **Service Layer Implementation**
   - [ ] Define `ICameraService` interface (`src/service/camera/ICameraService.h`).
   - [ ] Implement `CameraServiceSim` for PC simulation (file/log based).
   - [ ] Implement `CameraServiceT32` for T32 hardware (calling SDK/IMP).
   - [ ] Create `CameraServiceFactory` for instantiation.

2. **HTTP Server Integration**
   - [ ] Update `http_server` to support API routing registration.
   - [ ] Create API handler structure (`src/service/http_server/api/`).

3. **Basic APIs**
   - [ ] **Photo**: `POST /api/v1/camera/photo` (Single shot).
   - [ ] **Video**: `POST /api/v1/camera/video/start`, `stop`, `status`.
   - [ ] **Properties**: `GET /api/v1/camera/properties` (Get all).
   - [ ] **Properties**: `POST /api/v1/camera/properties` (Batch set).

### Phase 2: Extended Functionality (Priority: Medium)
**Goal**: Add advanced camera features and preset management.

1. **Advanced Photo Modes**
   - [ ] `POST /api/v1/camera/photo/burst` (Burst mode).
   - [ ] `POST /api/v1/camera/photo/timer` (Timer mode).
   - [ ] `GET /api/v1/camera/photo/status` (Job status tracking).

2. **Property Management**
   - [ ] `GET/POST /api/v1/camera/properties/{name}` (Single property).
   - [ ] `POST /api/v1/camera/properties/reset` (Factory reset).

3. **Presets**
   - [ ] `GET /api/v1/camera/presets` (List presets).
   - [ ] `POST /api/v1/camera/presets/{id}` (Apply preset).

### Phase 3: File & Database Management (Priority: Medium)
**Goal**: Enable client-side file browsing and synchronization.

1. **Database Sync**
   - [ ] `GET /api/v1/camera/database/media` (Download media.db).
   - [ ] `GET /api/v1/camera/database/thumbnail` (Download thumb.db).

2. **File Operations**
   - [ ] `GET /api/v1/camera/photos` (List photos - API fallback).
   - [ ] `GET /api/v1/camera/video/list` (List videos - API fallback).
   - [ ] `POST /api/v1/camera/files/delete` (Delete files).

### Phase 4: Preview & Validation (Priority: Low)
**Goal**: Real-time monitoring and final verification.

1. **Preview**
   - [ ] `GET /api/v1/camera/preview` (MJPEG/JPEG).
   - [ ] `GET /api/v1/camera/thumbnail` (Current frame thumb).

2. **Testing**
   - [ ] Unit tests for `CameraServiceSim`.
   - [ ] Integration tests using `curl` scripts against `build_sim`.
   - [ ] T32 hardware verification.

## 4. Technical Architecture
```
[HTTP Client] <-> [CivetWeb Server] <-> [API Handlers] <-> [ICameraService] <-> [Impl: Sim/T32]
```
- **API Handlers**: Parse JSON, validate input, call Service.
- **ICameraService**: Unified interface for business logic.
- **Impl**: Platform-specific implementation (Hardware calls vs File simulation).

## 5. Next Steps
1. Review and approve this plan.
2. Complete `ICameraService` implementation (currently in progress).
3. Begin Phase 1 API integration.
