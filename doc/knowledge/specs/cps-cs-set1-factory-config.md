# CPS-CS-SET1 Factory Config

The first-pass factory config contract supports a structured JSON file while
keeping existing `update_config.ini` behavior as a compatibility path.

## Files

Factory files are read from the TF-card root after mount:

| File | Priority | Behavior |
| --- | --- | --- |
| `camera_factory_config.json` | 1 | Preferred structured config. If present, it is validated and selected. |
| `update_config.ini` | 2 | Compatibility input when JSON is absent. Existing startup behavior may move it into the runtime config path and restart. |

If JSON is present but invalid, import fails and INI fallback is not used. This
prevents a malformed new-format factory file from silently applying stale INI
settings.

## JSON Shape

Use `res/factory/camera_factory_config.template.json` as the canonical template.
The file has two sections:

```json
{
  "version": 1,
  "factory": {
    "BOOT": {
      "PType": 1,
      "PModel": "SCT000"
    },
    "Functions": {
      "Photo_DS_EN": 1,
      "Timer_Range_MAX": 3
    }
  },
  "properties": {
    "Camera_Setting": {
      "CAM_Mode": 0,
      "Video_Size": "1080P/30FPS"
    }
  }
}
```

Field names must match the customer spec exactly. Examples include
`RWakeup _SET`, `CAM_ Ffixed_Shutter`, and `GPS_ Enable`.

## Validation Rules

- Unknown groups or names fail validation.
- Values must match registry type metadata.
- Chapter 2 factory fields map to `DeviceConfig` when a binding exists.
- Chapter 3/4 properties map through the same property projection used by HTTP
  writes.
- Placeholder-only first-pass fields validate as known fields but are not
  applied to runtime storage until a concrete binding is added.

## Atomicity And Restart

The importer validates the whole JSON document before applying any values. A
successful import records `restart_required=true`; runtime code should exit or
restart before normal operation continues so `DeviceConfig` and `Settings`
singletons reload from persisted files.

For compatibility INI input, the existing `update_config.ini` move-and-restart
flow remains the behavior. JSON has priority when both files exist.

## First-Pass Boundaries

- No `src/hal/**` changes are required.
- Client UI and cloud protocol updates are out of scope.
- Section 5 hardware status values that are not safely wired yet must remain
  explicit placeholders in `/api/v1/camera/status`.
