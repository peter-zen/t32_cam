# CPS-CS-SET1 Camera Parameter HTTP API

This document defines the first-pass HTTP contract for CPS-CS-SET1 camera
parameters. It preserves customer field names exactly, including spaces and
spelling from the spec. Use URL query parameters or JSON bodies for exact raw
names; path parameters are compatibility-only.

## Response Envelope

All endpoints return the existing API envelope:

```json
{
  "code": 0,
  "message": "success",
  "data": {}
}
```

Validation failures use `code` values in the existing style and include field
details in `data` when available.

## Grouped Property Read

```http
GET /api/v1/camera/properties?group=all&include=schema,value
GET /api/v1/camera/properties?group=Camera_Setting&include=value
```

`group` defaults to `all`. `include` accepts `schema`, `value`, or
`schema,value`.

Successful responses return grouped CPS properties:

```json
{
  "code": 0,
  "data": {
    "group": "Camera_Setting",
    "properties": [
      {
        "group": "Camera_Setting",
        "items": [
          {
            "id": "property.Camera_Setting.CAM_Mode",
            "name": "CAM_Mode",
            "raw_name": "CAM_Mode",
            "group": "Camera_Setting",
            "chapter": "3/4",
            "type": "number",
            "permission": "read_write",
            "value": 0,
            "default": 0,
            "enabled": true,
            "readonly": false,
            "source": "persisted"
          }
        ]
      }
    ]
  }
}
```

For `group=all`, `properties[]` follows the customer spec order:
`Camera_Setting`, `Audio_Setting`, `PIR_Setting`, `Timer_Setting`,
`Network_Setting`, `Server_Setting`, `System_Setting`, `AI_Setting`.
The API intentionally uses an array instead of an object map for display order;
clients must not rely on JSON object member order.

Disabled properties include `enabled:false` and `disabled_reason`.
Unimplemented first-pass bindings use `source:"placeholder"` and return their
spec default until hardware or protocol binding is added.

## Exact Raw-Name Read

```http
GET /api/v1/camera/properties/item?name=CAM_%20Ffixed_Shutter
GET /api/v1/camera/properties/item?name=GPS_%20Enable&include=schema,value
```

Use this endpoint for raw names containing spaces, typos, or other characters
that are not safe in a path segment.

## Single Property Write

```http
POST /api/v1/camera/properties/set
Content-Type: application/json

{
  "name": "CAM_Mode",
  "value": 1
}
```

The request updates exactly one property. Disabled properties, readonly fields,
unknown names, invalid values, and placeholder-only storage bindings are
rejected with a deterministic error. Successful responses include the canonical
raw name and the applied value:

```json
{
  "code": 0,
  "data": {
    "name": "CAM_Mode",
    "raw_name": "CAM_Mode",
    "value": 1,
    "updated": true
  }
}
```

## Factory Reset Properties

```http
POST /api/v1/camera/properties/factory-reset
Content-Type: application/json

{
  "names": ["CAM_Mode", "CAM_ Ffixed_Shutter"]
}
```

Request body is optional. Without `names` or `properties`, the API attempts to
reset every registry property to its `default_value` in the spec group order.
Use `group` to reset a single property group, or `names` to reset explicit raw
names/internal ids/legacy aliases:

```json
{
  "group": "Camera_Setting"
}
```

The operation is best-effort per field. Concrete persisted bindings are
restored to their registry defaults. Placeholder/computed/command-only fields
are reported as `skipped` instead of failing the whole reset.

```json
{
  "code": 0,
  "data": {
    "group": "all",
    "total": 2,
    "applied": 1,
    "skipped": 1,
    "failed": 0,
    "results": [
      {
        "name": "CAM_Mode",
        "raw_name": "CAM_Mode",
        "group": "Camera_Setting",
        "default_value": 0,
        "status": "applied",
        "applied_value": 0
      },
      {
        "name": "CAM_ Ffixed_Shutter",
        "raw_name": "CAM_ Ffixed_Shutter",
        "group": "Camera_Setting",
        "status": "skipped",
        "reason": "storage_not_implemented"
      }
    ]
  }
}
```

## Section 5 Status Read

```http
GET /api/v1/camera/status?group=all
GET /api/v1/camera/status?group=device
```

Status fields are read-only display data from section 5. They are separate from
writable chapter 3/4 properties.

`group=all` returns `device`, `signal`, and `sensor` sections. Not-yet-backed
values are explicit:

```json
{
  "name": "Battery1",
  "raw_name": "Battery1",
  "available": false,
  "source": "placeholder",
  "reason": "not_implemented",
  "value": 0
}
```

## Compatibility Endpoints

The following existing endpoints remain for legacy scripts:

| Endpoint | Status | Notes |
| --- | --- | --- |
| `GET /api/v1/camera/properties/{name}` | Compatibility | Supports URL-safe legacy aliases/internal IDs. Not canonical for raw names with spaces. |
| `POST /api/v1/camera/properties/{name}` | Compatibility | Writes one legacy alias or URL-safe registry name. |
| `POST /api/v1/camera/properties` | Compatibility | Existing batch update behavior is retained. |
| `POST /api/v1/camera/properties/reset` | Compatibility | Legacy reset behavior is retained for legacy scripts. New CPS clients should use `/properties/factory-reset`. |

New CPS-CS-SET1 clients should use `/properties`, `/properties/item`,
`/properties/set`, `/properties/factory-reset`, and `/status`.

## Current Property Catalog

The runtime registry is the source of truth. Clients should use
`GET /api/v1/camera/properties?group=all&include=schema,value` to fetch the
authoritative type, options, ranges, defaults, current values, enabled state,
and storage source. This table is the current first-pass raw-name adaptation
index.

| Group | Raw names |
| --- | --- |
| `Camera_Setting` | `CAM_Mode`, `CAM_ImageSize`, `CAM_ImageQuality`, `CAM_Shooting_P`, `CAM_Shooting_INT`, `CAM_ Ffixed_Shutter`, `CAM_ Min_Shutter`, `Video_Size`, `Video_Encoded`, `Video_Bitrate_Type`, `Video_Bitrate_Value`, `Video_Length` |
| `Audio_Setting` | `Audio_SPK_Volume` |
| `PIR_Setting` | `PIR_Mode`, `PIR_Sensitivity`, `PIR_Interval`, `PIR_MaxShooting` |
| `Timer_Setting` | `Timer_Enable`, `Timer_PIR_Enable`, `Timer_Interval_Time`, `Timer_1Start`, `Timer_1End`, `Timer_2Start`, `Timer_2End`, `Timer_3Start`, `Timer_3End`, `Timer_4Start`, `Timer_4End`, `Timer_5Start`, `Timer_5End`, `Timer_Repeats` |
| `Network_Setting` | `CSSID`, `CPWR`, `UPID`, `UPWR`, `DHCP_ON`, `LOCAL_IP`, `NETMASK`, `GATEWAY`, `DNS1`, `DNS2` |
| `Server_Setting` | `M_Server`, `NTP_Server`, `NTP_Timezone`, `BS_Server`, `AI_Server` |
| `System_Setting` | `Device_Name`, `GPS_ Enable`, `GPS_Value`, `Stamp`, `Stamp_List`, `Cycle`, `Upload_Protocol`, `Upload_Order`, `Upload_Mode`, `Upload_NUFQ`, `Upload_Delete`, `Data_Suicide`, `HeartRate`, `VTS_Sensitivity`, `Remote_Wakeup` |
| `AI_Setting` | `Target_List`, `Focus_Target_List`, `Recognition_Rate`, `Target_Quantity_Enable`, `Increment_Enable`, `AI_Enable`, `AI_Alarm_Enable` |

Storage status is per-field in the runtime schema:

| `source` | Client meaning |
| --- | --- |
| `persisted` | Backed by `Settings` or `DeviceConfig`; read/write can be supported when permission and dependency state allow it. |
| `computed` | Read-only runtime value. |
| `placeholder` | First-pass known field without a concrete binding; it is returned for schema compatibility and rejected on writes. |

Dependencies are also returned per-field. For example, photo details can be
disabled by `Photo_DS_EN`, video details by `Video_DS_EN`, and timer ranges 4/5
by `Timer_Range_MAX`.

## Current Status Catalog

`GET /api/v1/camera/status?group=all` returns section 5 status fields. These
are read-only and are not accepted by `/properties/set`. Storage capacity/status
is intentionally not duplicated here; clients should use `/api/v1/storage/info`.

| Group | Raw names |
| --- | --- |
| `device` | `PID`, `DUID`, `Device_Name`, `PCompany`, `PModel`, `PName`, `FW_Version`, `MCU_Version`, `Location_LON`, `Location_LAT`, `Location_ELE`, `Battery_Type`, `Battery1`, `Battery2`, `EPower`, `SPower`, `Device_MAC`, `Device_IP`, `Device_IMEI`, `Device_NO`, `Event_Total`, `Event_NUFQ` |
| `signal` | `Signal_Type`, `Signal_CF`, `Signal_TP`, `Signal_BW`, `Signal_RSSI`, `Signal_RSRP`, `Signal_RSRQ`, `Signal_RL`, `Signal_SNR`, `Signal_TD` |
| `sensor` | `Sensor_CDS`, `Sensor_TEMPS`, `Sensor_RHS`, `Sensor_APS`, `Sensor_AL`, `Sensor_UVL`, `Sensor_NOISE`, `Sensor_CO`, `Sensor_CO2`, `Sensor_O2` |

Fields with no safe first-pass backing are returned with
`available:false`, `source:"placeholder"`, and `reason:"not_implemented"`.
