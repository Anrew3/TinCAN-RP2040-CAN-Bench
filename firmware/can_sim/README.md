# TinCAN Firmware (can_sim)

Template-based vehicle CAN simulator for RP2040 + MCP2515.

## Building

Arduino IDE (or arduino-cli) with:

- **Board core**: [arduino-pico](https://github.com/earlephilhower/arduino-pico) (Raspberry Pi Pico / RP2040)
- **Libraries**: `mcp_can` (coryjfowler), `ArduinoJson`
- **Flash size**: pick a partition with a LittleFS filesystem (e.g. `2MB (Sketch: 1MB, FS: 1MB)`) — template and settings storage lives there

Export the compiled UF2 and place it at `firmware/uf2/can_sim.uf2` so the web
flasher's "Official Firmware" option serves it.

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

Upload flow: send the command, wait for `TEMPLATE_UPLOAD_READY`, send the JSON,
then `TEMPLATE_UPLOAD_END`. On success: `TEMPLATE_UPLOAD_OK`. On CRC failure:
`TEMPLATE_UPLOAD_ERROR: CRC mismatch (...)` and nothing is imported.

### Vehicle state

`RPM:<v>`, `SPEED:<v>`, any gauge name defined by the template, `TEMP:COOLANT:<F>`,
`TEMP:OIL:<F>`, `TPMS:<tire>:<psi>`, `BLINKER:LEFT|RIGHT|BOTH|OFF`, `VIN:<17 chars>`,
button names (`UP`/`DOWN`/`LEFT`/`RIGHT`/`OK`/`SETTINGS`).

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
