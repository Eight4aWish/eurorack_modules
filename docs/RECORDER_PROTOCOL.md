# Recorder Protocol

The wire contract between the `esp32-clklinkrec` Eurorack module and
the `seeed-recorder` Mac menu-bar app *over the HTTP/WiFi trigger
path*. The same Mac app also accepts triggers from the `seeed-recorder`
RP2040 module over USB-MIDI; that contract lives in
[`~/GitHub/seeed-recorder/docs/DESIGN.md`](https://github.com/Eight4aWish/seeed-recorder/blob/main/docs/DESIGN.md).
Both transports are first-class. The capture engine, buffer, and file
output are shared.

This document is the source of truth for the HTTP/WiFi side. When the
protocol changes, edit here first and update both the firmware
([`src/esp32-clklinkrec/`](../src/esp32-clklinkrec/)) and the Mac app
([`~/GitHub/seeed-recorder/mac-app/`](https://github.com/Eight4aWish/seeed-recorder/tree/main/mac-app))
to match.

**Protocol version: `2.0`**

## Discovery

The Mac app advertises itself on the local network via mDNS (Bonjour).

- **Service type**: `_recorder._tcp.local.`
- **Service name** (default): `seeed-recorder`
- **Port**: `8765`
- **TXT records**:
  - `version=2.0` — protocol version
  - `app=seeed-recorder` — app identifier
  - `link_peer=true|false` — whether the Mac is currently participating
    in an Ableton Link session as a follower (informational)

The firmware queries mDNS at startup and on every WiFi reconnect.

### mDNS fallback

If mDNS resolution fails (some networks — guest VLANs, IoT-segregated
WiFi, certain corporate environments — block multicast DNS), the
firmware checks `secrets.h` for an optional `RECORDER_HOST` define:

```c
// secrets.h
#define WIFI_SSID "studio"
#define WIFI_PASS "..."
// Optional. If defined AND mDNS doesn't resolve within 2 s, the
// firmware uses this as the recorder address. Format is hostname:port
// or ip:port. Port defaults to 8765 if omitted.
#define RECORDER_HOST "macbook.local"
```

If both mDNS and the fallback fail, Capture button presses log to
serial and briefly flash the red LED (200 ms), then the firmware
returns to idle.

The resolved address is cached for the lifetime of the WiFi
association.

## Endpoints

All endpoints accept and return JSON unless noted. The base URL is
`http://<resolved-host>:<resolved-port>`.

### `GET /healthz`

Liveness probe. The firmware calls this once after the address
resolves to verify the Mac app is reachable and the protocol version
is compatible.

**Response 200**:
```json
{
  "ok": true,
  "version": "2.0",
  "app": "seeed-recorder",
  "buffer_seconds": 300,
  "audio_source": "Focusrite Scarlett 16i6",
  "channels_active": 8,
  "link_peer": true,
  "link_tempo": 120.0
}
```

- `version` — protocol version the app implements. If the firmware
  sees a major version mismatch it logs a warning but proceeds.
- `buffer_seconds` — how many seconds of audio the app currently has
  buffered.
- `audio_source` — display string for whichever audio device the app
  is recording from. Informational.
- `channels_active` — count of channels currently configured for
  capture (mono channels + stereo pairs counted as 2). Informational.
- `link_peer` — whether the Mac app is currently joined to an
  Ableton Link session. If `true`, captures will include tempo
  information; if `false`, the BPM-in-filename and WAV-metadata
  fields are omitted.
- `link_tempo` — current tempo as a double, only present when
  `link_peer` is `true`.

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

The Capture endpoint. Tells the app to save the last
`buffer_seconds` of audio from its ring buffer to disk.

**Request body**: empty (`Content-Length: 0`). The firmware doesn't
need to send anything — the Mac knows its own buffer state, tempo,
and channel config.

**Response 200** — capture succeeded:
```json
{
  "ok": true,
  "files": [
    "/Users/me/Music/Recorder/2026-06-05_18-43-12_120bpm_ch01-02.wav",
    "/Users/me/Music/Recorder/2026-06-05_18-43-12_120bpm_ch03.wav",
    "/Users/me/Music/Recorder/2026-06-05_18-43-12_120bpm_ch04.wav"
  ],
  "duration_seconds": 300,
  "bpm": 120.0,
  "link_playing": true
}
```

- `files` — list of files written (one per mono channel, one per
  stereo pair). Order matches channel configuration in the app.
- `bpm` and `link_playing` — only present when the Mac app was
  joined to a Link session at capture time.

**Response 503** — app couldn't capture:
```json
{
  "ok": false,
  "reason": "buffer_empty",
  "detail": "No audio has been captured yet."
}
```

Other plausible `reason` values: `disk_full`, `permission_denied`,
`no_audio_source`, `capture_in_flight`. The firmware treats any 5xx
as "error, keep red LED solid"; the `reason` is logged to serial
for debugging.

### Concurrent presses

If a `POST /capture` arrives while the previous capture is still
being extracted, the Mac returns **503 with `reason: "capture_in_flight"`**
immediately. The firmware ignores the second press (red LED stays
in its current state, no new request fires). Extraction usually
completes within 2 s, so the contention window is small.

## File output

The HTTP capture path produces **identical output** to the USB-MIDI
capture path. The two transports differ only in how the trigger event
reaches the Mac app's capture engine.

### Naming

```
YYYY-MM-DD_HH-MM-SS[_<bpm>bpm]_ch<NN>[-<NN>].wav
```

- Timestamp = wall-clock at the moment the Mac receives the trigger
  (not the start of the captured window — that's `now - buffer_seconds`).
- `<bpm>` segment included **only** when `link_peer` is true at
  capture time. BPM rounded to nearest integer. When omitted, the
  filename collapses to `YYYY-MM-DD_HH-MM-SS_ch<NN>.wav` — no
  placeholder, no `nobpm_` token.
- `ch<NN>` for mono channels. `ch<NN>-<MM>` for stereo pairs
  (e.g. `ch01-02`).

### Format

- WAV, 32-bit float, sample rate matches the audio source.
- Mono channels emit one WAV with one channel.
- Stereo pairs emit one interleaved stereo WAV (Ableton-compatible —
  no multichannel WAVs).
- Per-channel configuration (mono / stereo L / stereo R / off) lives
  in the Mac app's settings UI.

### Metadata

The Mac app embeds capture context into the WAV file itself using
the LIST-INFO chunk family:

- `ICMT` (comment) → `BPM=<n>; Link=<playing|stopped>; Source=<recorder_v2>`
  (omitting the `BPM=` and `Link=` fields when `link_peer` is false)
- `ICRD` (creation date) → ISO-8601 timestamp at trigger time
- `ISFT` (software) → `seeed-recorder/<app_version>`

DAWs like Ableton, Reaper, and Logic read these fields. Means
tempo survives a file rename.

## Firmware behaviour

| Event | Firmware action | Red LED |
|---|---|---|
| Capture button pressed (idle state) | POST `/capture` on a FreeRTOS task; LED solid for duration | turns on |
| `200 OK` received | Log files + duration + bpm to serial | turns off |
| `503 capture_in_flight` | Treat as no-op (Mac already busy); log to serial | turns off |
| Other `5xx` | Log reason to serial | stays on (error sticky) |
| Network timeout (1.5 s) | Log timeout to serial | stays on (error sticky) |
| Address resolution failure | Skip the POST; log to serial | brief 200 ms flash |
| Capture button pressed while LED already on (in-flight) | Press is ignored | unchanged |
| Capture button pressed while LED is error-sticky | Fires a new request; on `200 OK` the error sticky clears | depends on outcome |

The "error sticky" LED state is cleared only by a subsequent
successful capture round-trip. The user clears the error
indication by pressing Capture again once the underlying issue is
resolved.

## Mac app behaviour

The app should:

1. **Maintain a rolling audio buffer** of `buffer_seconds` worth of
   incoming audio per channel. Default 5 minutes; configurable in the
   app's preferences (range 30 s – 30 min).
2. **Advertise the service via Bonjour** on whichever interface is
   serving the local network, on TCP/8765. Include the `link_peer`
   TXT record.
3. **Bind the HTTP server to LAN interfaces only**, never to public
   interfaces. The protocol has no authentication; security relies
   on the LAN being trusted.
4. **Respond to `GET /healthz` even if there is no audio source** —
   that's how the firmware detects partial-failure states.
5. **Persist captured files** to a user-configurable directory.
   Default: `~/Music/Recorder/`. Filename and format as documented
   above.
6. **Join the Ableton Link network as a follower** whenever the app
   is running. The app must never propose its own tempo — it reads
   `link_tempo` and `link_playing` purely to enrich captures. If no
   other Link peer is on the LAN, `link_peer` resolves to `false`
   and BPM information is omitted from filenames + metadata.
7. **Expose a manual capture trigger** in the menu bar UI. Clicking
   it has identical semantics to a `POST /capture` from the firmware,
   so users can trigger captures without either module reachable
   (e.g. for testing or when the rack is disconnected).

## Authentication & security

**None** in `2.0`. The Mac binds only to LAN interfaces; the firmware
assumes the local network is trusted. If the LAN cannot be trusted,
the user should put the recorder on a dedicated subnet or VLAN.

## Versioning

- Increment the **minor** version (`2.0` → `2.1`) for additive,
  backwards-compatible changes (new endpoints, new optional fields).
- Increment the **major** version (`2.0` → `3.0`) for breaking
  changes.

The firmware logs a warning on major-version mismatch but does not
refuse to operate.

## Open questions

- **Multiple recorder targets**: the firmware currently picks the
  first `_recorder._tcp.local.` service it resolves. If you run two
  Mac apps on the same LAN (e.g. desktop + laptop), the choice is
  non-deterministic. Could add a TXT-record-based preference
  ("primary recorder") in a future version.
- **Status push from Mac → module**: useful for showing buffer-full
  warnings on the front panel, but adds a long-lived TCP connection
  or WebSocket. Out of scope for `2.0`.

## Changelog

- **2.0** (2026-06-05) — file format aligned with seeed-recorder Mac
  app: per-channel mono / stereo-pair 32-bit float WAVs (was
  "16-bit stereo"). Filename includes BPM segment when the Mac app
  is joined to a Link session. `RECORDER_HOST` fallback added when
  mDNS is unavailable. Mac app joins Link as follower-only. Mac
  menu bar exposes a manual capture trigger with identical
  semantics. `503 capture_in_flight` introduced for concurrent
  presses. Label segment dropped from filenames.
- **1.0** (2026-06-02) — initial draft. Defines `GET /healthz` and
  `POST /capture`, mDNS discovery on `_recorder._tcp.local.`, error
  reporting via 5xx + `reason` codes.
