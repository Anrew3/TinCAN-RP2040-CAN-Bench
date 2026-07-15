# TinCAN Firmware (can_sim)

Template-based vehicle CAN simulator for RP2040 + MCP2515.

## Building

**Board core** — install [arduino-pico](https://github.com/earlephilhower/arduino-pico)
(Raspberry Pi Pico / RP2040) via Boards Manager. `SPI` and `LittleFS` ship with
this core — no separate install.

**Libraries** — install both from the Arduino IDE Library Manager
(*Tools → Manage Libraries…*):

| Library | Author | Version | Notes |
|---------|--------|---------|-------|
| **ArduinoJson** | Benoit Blanchon | **7.x** (the Library Manager default) | Uses the v7 API (`JsonDocument`, `.to<>()`, `.add<>()`). |
| **mcp_can** | coryjfowler | latest | The MCP2515 driver (`mcp_can.h`). |

> `ArduinoJson.h: No such file or directory` just means ArduinoJson isn't
> installed yet — install it as above. `mcp_can.h: No such file...` is the
> same thing for the CAN driver.

**Flash size** — pick a partition with a LittleFS filesystem
(*Tools → Flash Size → e.g. `2MB (Sketch: 1MB, FS: 1MB)`*); templates and
settings persist there.

Export the compiled UF2 (*Sketch → Export Compiled Binary* writes it under
`firmware/can_sim/build/…/can_sim.ino.uf2`) and copy it to
**`firmware/can_sim.uf2`** — that's the file the web flasher's "Official
Firmware" option fetches. Only the `.uf2` is needed; the `.elf`/`.bin`/`.hex`/
`.map` build artifacts can be ignored.

## Serial protocol (9600 baud, newline-terminated)

### System

| Command | Description |
|---------|-------------|
| `VERSION` | Replies `VERSION:x.y.z` |
| `STATUS` | Replies `STATUS:{...}` JSON: version, template, canOk, freeHeap, LittleFS usage, uptime |
| `RESTART` | Watchdog reboot |
| `BOOTLOADER` | Reboots into BOOTSEL mode — the `RPI-RP2` drive appears, ready to flash (used by the web flasher) |
| `FACTORYRESET` | Deletes custom templates + settings, reboots |

### Templates (stored on-device in LittleFS)

| Command | Description |
|---------|-------------|
| `TEMPLATE:LIST` | List built-in + stored custom templates (`*` = active) |
| `TEMPLATE:LOAD:<id>` | Switch template (persists across reboots) |
| `TEMPLATE:SHOW` / `TEMPLATE:EXPORT` | Inspect / dump active template |
| `TEMPLATE:UPLOAD:<size>[:<crc32>]` | Start upload; optional CRC32 (hex, IEEE) of the payload is verified before import |
| `TEMPLATE:DELETE:<id>` | Delete a custom template |
| `BOOTSEQ` | Replay the template's boot message sequence |

Templates define two kinds of standing CAN traffic, both fully customizable
per template (and editable in the web controller's Template Editor):

- **`background`** — keepalives sent continuously at each message's
  `intervalMs` (e.g. cluster heartbeat `0x109`); max 5.
- **`boot`** — a one-shot sequence sent in order at power-up and on template
  load, each frame after its own `delayMs`; max 10. Use for head-unit /
  SYNC init handshakes. Replay manually with `BOOTSEQ`.

```json
"boot": [
  { "canId": "0x109", "data": "00 03 01 00 00 00 00 24", "delayMs": 100 },
  { "canId": "0x3B2", "data": "40 48 00 10 10 00 00 02", "delayMs": 250 }
]
```

Upload flow: send the command, wait for `TEMPLATE_UPLOAD_READY`, send the JSON,
then `TEMPLATE_UPLOAD_END`. On success: `TEMPLATE_UPLOAD_OK`. On CRC failure:
`TEMPLATE_UPLOAD_ERROR: CRC mismatch (...)` and nothing is imported.

### Vehicle state

`RPM:<v>`, `SPEED:<v>`, any gauge name defined by the template, `TEMP:COOLANT:<F>`,
`TEMP:OIL:<F>`, `TPMS:<tire>:<psi>`, `BLINKER:LEFT|RIGHT|BOTH|OFF`, `VIN:<17 chars>`,
button names (`UP`/`DOWN`/`LEFT`/`RIGHT`/`OK`/`SETTINGS`).

Gauge output scaling (e.g. speed `mph × 159`), min/max, and offset are template
fields (`scale`, `offset`, `min`, `max`) and editable in the web Template Editor.

### Signals (indicators, lighting, gear, ...)

A **signal** is a named, multi-state control that overlays a byte span onto a
frame the template already transmits — warning lights, headlamp, day/night,
doors, gear, backlight, etc. Fully template-defined, so new indicators need no
firmware change.

| Command | Description |
|---------|-------------|
| `SIGNAL:<name>:<state>` | Set a signal to a named state (e.g. `SIGNAL:ABS:FLASH`) |
| `SIGNAL:<name>:<number>` | Set a raw byte value for continuous controls (e.g. `SIGNAL:BACKLIGHT:12`) |
| `GEAR:<P\|R\|N\|D>` | Shorthand for the `GEAR` signal (reverse drives `0x171`) |

The Ford templates ship: `GEAR`, `HEADLAMP`, `MODE` (DRL/night/hazard),
`BACKLIGHT`, `DAYNIGHT`, `DOORS`, `ABS`, `TRACTION`. Each signal's `canId` must
be a frame the template transmits (a gauge, the blinker/body frame, or a
background message). Example:

```json
"signals": [
  { "name": "ABS", "canId": "0x416", "startByte": 6,
    "default": "OFF",
    "states": { "OFF": "00", "SOLID": "40", "SLOW": "80", "FAST": "D0" } },
  { "name": "GEAR", "canId": "0x171", "startByte": 0,
    "default": "PARK",
    "states": { "PARK": "00 00", "REVERSE": "36 32", "NEUTRAL": "00 00", "DRIVE": "00 00" } }
]
```

### Simulation

| Command | Description |
|---------|-------------|
| `SWEEP:<gauge>:<from>:<to>:<ms>[:cycles]` | Ramp a gauge over `<ms>` per pass. `cycles=1` one-way (default), `2` there-and-back, `0` ping-pong forever |
| `SWEEP:STOP[:<gauge>]` | Stop all (or one gauge's) sweeps |
| `DEMO:ON` / `DEMO:OFF` | Looping scripted drive cycle: rev, pull away, blinkers, cruise, brake to stop |

### Sniffer

| Command | Description |
|---------|-------------|
| `SNIFF:ON` / `SNIFF:OFF` | Stream received frames as candump-format lines: `(sec.usec) can0 204#0000E803` — paste/save these and import straight into SavvyCAN or can-utils tooling |
| `SNIFF:FILTER:<id>` | Only show one CAN ID (hex) |
| `SNIFF:NOFILTER` | Clear the filter |

### Custom frames

`CAN:<id> <b0> <b1> ...[:count][:interval_ms]`, `SHOWCAN`, `CANCELCAN:<slot>`, `CANSTOP`.

### Logging

`VERBOSE:ON|OFF` and `LOGRATE:<ms>` — both persist across reboots (`/settings.cfg` in LittleFS).

## SavvyCAN (GVRET)

The firmware speaks the GVRET binary serial protocol. In SavvyCAN:

**Connection → Open Connection Window → Add New Device Connection →
Serial Connection (GVRET)** and pick the TinCAN's COM port.

- Received bus traffic streams into SavvyCAN's frame list.
- Frames sent from SavvyCAN are transmitted onto the bus.
- Bus speed changes and listen-only mode set in SavvyCAN are applied to the
  MCP2515; listen-only also pauses the simulator's own TX so the bus stays quiet.
- GVRET mode engages automatically on the first binary byte and disengages when
  the port closes — the plain-text console comes back on the next connect.

Note: the MCP2515 is a classic-CAN controller; SavvyCAN features that need
CAN-FD or multiple buses don't apply. USB CDC bandwidth comfortably handles a
500 kbps bus near full load, but a fully saturated 1 Mbps bus may drop frames.
