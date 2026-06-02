# Recorder Protocol

The wire contract between the `esp32-clklinkrec` Eurorack module and
the `seeed-recorder` Mac menu-bar app.

This document is the source of truth for both sides. When the protocol
changes, edit here first and update both the firmware
([`src/esp32-clklinkrec/`](../src/esp32-clklinkrec/)) and the Mac app
([`~/GitHub/seeed-recorder`](https://github.com/Eight4aWish/seeed-recorder))
to match.

**Protocol version: `1.0`**

## Discovery

The Mac app advertises itself on the local network via mDNS (Bonjour).

- **Service type**: `_recorder._tcp.local.`
- **Service name** (default): `seeed-recorder`
- **Port**: `8765`
- **TXT records**:
  - `version=1.0` — protocol version
  - `app=seeed-recorder` — app identifier (for disambiguating from any
    other `_recorder._tcp.local.` services that might appear)

The firmware queries mDNS at startup and on every WiFi reconnect. If
no `_recorder._tcp.local.` service is found, the firmware logs the
failure to serial and the Capture button does nothing (red LED flashes
briefly to indicate "no Mac available"). The mDNS resolver result is
cached for the lifetime of the WiFi association.

The firmware does **not** support hardcoded IPs in `secrets.h`. If
mDNS is unavailable on your network, the recorder won't be reachable.

## Endpoints

All endpoints accept and return JSON unless noted. The base URL is
`http://<resolved-host>:<resolved-port>`.

### `GET /healthz`

Liveness probe. The firmware calls this once after mDNS resolves to
verify the Mac app is reachable and the protocol version is
compatible.

**Response 200**:
```json
{
  "ok": true,
  "version": "1.0",
  "app": "seeed-recorder",
  "buffer_seconds": 60,
  "audio_source": "BlackHole 2ch"
}
```

- `version` — protocol version the app implements. If the firmware
  sees a major version mismatch, it logs a warning but proceeds.
- `buffer_seconds` — how many seconds of audio the app currently has
  buffered and could potentially save.
- `audio_source` — display string for whichever audio device the app
  is recording from. Informational only.

**Response 503**:
```json
{
  "ok": false,
  "reason": "no_audio_source",
  "detail": "No input device configured."
}
```

Returned when the app is running but cannot record (no input
configured, permissions denied, etc.).

### `POST /capture`

The Capture endpoint. Tells the app to save the contents of its
ring buffer to disk.

**Request body** (optional):
```json
{
  "label": "rack-session-01"
}
```

- `label` — optional string used as a hint for the saved filename.
  The app is free to sanitise or ignore it; if absent, the app picks
  its own name based on timestamp.

The firmware currently always sends an empty body. The `label` field
is reserved for a future variant where the module knows something
about session context.

**Response 200** — capture succeeded:
```json
{
  "ok": true,
  "path": "/Users/me/Music/Recorder/2026-06-02-1843-rack-session-01.wav",
  "duration_seconds": 60,
  "size_bytes": 11289600
}
```

**Response 503** — app couldn't capture:
```json
{
  "ok": false,
  "reason": "buffer_empty",
  "detail": "No audio has been captured yet."
}
```

Other plausible `reason` values: `disk_full`, `permission_denied`,
`no_audio_source`. The firmware treats any 5xx as "error, keep red
LED solid"; the `reason` is logged to serial for debugging.

## Firmware behaviour

| Event | Firmware action | Red LED |
|---|---|---|
| Capture button pressed | Spawn FreeRTOS task; task POSTs `/capture` | turns on |
| `200 OK` received | Log path/duration to serial | turns off |
| `5xx` received | Log reason to serial | stays on |
| Network timeout (1.5 s) | Log timeout to serial | stays on |
| mDNS resolution failure | Skip the POST entirely; log to serial | brief flash, then off |
| Another press while in flight | Ignored; current request continues | unchanged |

There is no automatic timeout that turns the red LED off after an
error. The user clears the error indication by pressing Capture
again on a subsequent successful round-trip. Persistent errors stay
visible until the underlying issue is resolved.

## Mac app behaviour

The app should:

1. **Maintain a rolling audio buffer** of `buffer_seconds` worth of
   incoming audio. Default 60 seconds; configurable in the app's
   preferences.
2. **Advertise the service via Bonjour** on whichever interface is
   serving the local network, on TCP/8765.
3. **Bind the HTTP server to the loopback or LAN interface**, never
   to public interfaces. The protocol has no authentication; security
   relies on the LAN being trusted.
4. **Respond to `GET /healthz` even if there is no audio source** —
   that's how the firmware detects partial-failure states.
5. **Persist captured files** to a user-configurable directory.
   Default: `~/Music/Recorder/`. Filename convention:
   `YYYY-MM-DD-HHMM[-label].wav`. WAV format, 16-bit, stereo, sample
   rate matches the audio source.

## Authentication & security

**None** in `1.0`. The Mac binds only to LAN interfaces; the firmware
assumes the local network is trusted. If the LAN cannot be trusted,
the user should put the recorder on a dedicated subnet or VLAN.

A future `2.0` could add a shared secret in the mDNS TXT record (or
a `?token=` query parameter) for environments where this matters.

## Versioning

- Increment the **minor** version (`1.0` → `1.1`) for additive,
  backwards-compatible changes (new endpoints, new optional fields).
- Increment the **major** version (`1.0` → `2.0`) for breaking
  changes (renamed fields, removed endpoints, semantic shifts).

The firmware logs a warning on major-version mismatch but does not
refuse to operate; the assumption is that breaking changes are rare
enough to fix on both sides in one go.

## Open questions

- **Multiple recorder targets**: should the firmware support more
  than one `_recorder._tcp.local.` service on the LAN simultaneously,
  fanning the Capture event to all of them? Currently it picks the
  first one it resolves and sticks with it.
- **Status push from Mac → module**: useful for showing buffer-full
  warnings on the front panel, but adds a long-lived TCP connection
  or WebSocket. Currently not in scope.
- **Cross-platform**: the Mac app could in principle be ported to
  Windows or Linux. The protocol is OS-agnostic; only the recording
  half is Mac-specific.

## Changelog

- **1.0** (2026-06-02) — initial draft. Defines `GET /healthz` and
  `POST /capture`, mDNS discovery on `_recorder._tcp.local.`, error
  reporting via 5xx + `reason` codes.
