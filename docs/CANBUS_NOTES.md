# Ford S650 / SYNC CAN Bus Messages

Consolidated reference combining:
1. **Software** — CAN IDs transmitted by the TinCAN-CANBUS-Simulator firmware and the
   related RP2040/ESP32 CAN projects (`can_sim`, `CanBus.ino`, `WFCanBus.ino`,
   `CANBUSWControl.ino`, `RP2040CAN*`, etc.), as documented in
   `~/Documents/TinCAN/can_sim/canbus_codes.txt`.
2. **Handwritten notes** — "S650 CAN Messages", "S650 CAN Bus", and "3B2/3 Continued"
   (3 photos, transcribed below).

> **Screen brightness / dimming:** the closest control is **`0x3B2` / `0x3B3` Byte 3
> (Backlight)** for cluster/panel brightness, and **Byte 5 (Day/Night mode)** which drives
> the display's day↔night dimming. There is no dedicated "SYNC display brightness" frame;
> SYNC illumination behavior is otherwise set via the APIM **7D0** as-built config.

---

## Master message list

| CAN ID | Function | Example payload | Source |
|--------|----------|-----------------|--------|
| `0x81`  | Steering-wheel / cluster buttons | `00 00 00 00 00 00 00 00` (idle) | Software |
| `0x109` | Heartbeat / misc status (gear?) | `00 03 01 00 00 00 00 24` | Software + Notes |
| `0x156` | Engine temperatures | `00 00 00 00 03 00 00 00` | Software + Notes |
| `0x171` | Reverse gear (Sync 4) | `36 32 00 00 00 00 00 00` | Notes only |
| `0x202` | Vehicle speed | `04 00 00 00 60 00 25 44` | Software + Notes |
| `0x204` | Engine RPM | `00 00 00 09 C4 00 00 00` | Software + Notes |
| `0x3B2` / `0x3B3` | Lighting, turn signals, doors, backlight, day/night | `40 48 00 10 10 00 00 02` | Software + Notes |
| `0x3B5` | Tire pressures (TPMS) | `00 CE 00 CE 00 CE 00 CE` | Software + Notes |
| `0x416` | ABS / traction status | `50 00 FE 00 01 00 00 00` | Software + Notes |
| `0x40A` | VIN broadcast (3 frames) | `C1 00 ...ASCII...` | Software only |

---

## Software (firmware) definitions

From `canbus_codes.txt` / `can_sim` firmware. IDs used by model traffic:
`0x81, 0x202, 0x204, 0x156, 0x3B2, 0x3B3, 0x3B5, 0x109, 0x416, 0x40A`.

### `0x81` — Steering-wheel / cluster buttons
- Rate: 10 ms. Pressed frame held ~100 ms, then returns to all-zeros.
- Byte 0 = button code: `UP=0x08`, `DOWN=0x01`, `LEFT=0x02`, `RIGHT=0x04`, `OK=0x10`, `SETTINGS=0x46`
- Byte 1 = `0x01` only for SETTINGS, else `0x00`; Bytes 2–7 = `0x00`

### `0x202` — Vehicle speed (mph)
- Rate: 100 ms
- Bytes 6–7 = `speed_mph * 159`, big-endian; Byte 4 = `0x60`; others `0x00`
- *(Note: handwritten notes say `× 175` — see Discrepancies.)*

### `0x204` — Engine RPM
- Rate: 100 ms
- Bytes 3–4 = `RPM / 2`, big-endian (e.g. 2000 RPM → `0x03E8` → Byte3=`0x03`, Byte4=`0xE8`)

### `0x156` — Engine temperatures
- Rate: 100 ms
- Byte 0 = Coolant temp as `(°C + 60)`; Byte 1 = Oil temp as `(°C + 60)`; Byte 4 = `0x03`

### `0x3B5` — Tire pressures (TPMS)
- Rate: 200 ms. Values in kPa, LSB only.
- Byte 1 = Driver Front, Byte 3 = Passenger Front, Byte 5 = Passenger Rear, Byte 7 = Driver Rear
- Scaling when set via serial: `psi * 6.895 → kPa`

### `0x3B2` / `0x3B3` — Exterior lighting / turn signals
- Rate: 10 ms (both IDs carry the same 8-byte payload)
- Base: Byte 4 = `0x10`, Byte 6 = `0x00`
- Right turn: Byte 4 `|= 0x08` when active + blink ON; Left turn: Byte 6 `|= 0x40` when active + blink ON
- *(Handwritten notes decode this ID in much more detail — see below.)*

### `0x40A` — VIN broadcast (3 consecutive frames)
- Rate: 200 ms (each of 3 parts repeats)
- Byte 0 = `0xC1`; Byte 1 = frame index `i ∈ {0,1,2}`; Bytes 2–7 = next 6 ASCII VIN chars; final frame pads Byte 7 to `0xFF` if needed

### `0x109` — Misc status / heartbeat
- Rate: 10 ms; payload `00 03 01 00 00 00 00 28` (fixed in firmware)

### `0x416` — ABS / traction status
- Rate: 10 ms; payload `50 00 FE 00 01 00 00 00` (fixed in firmware)

**Serial control commands:** `RPM:<value>`, `SPEED:<mph>`, `TIRE:<name>:<psi>`,
`VIN:<17chars>`, `TEMP:<COOLANT|OIL>:<tempF>`, `BLINKER:<LEFT|RIGHT|BOTH|OFF>`,
`HAZARDS`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `OK`, `SETTINGS`.

---

## Handwritten notes (transcribed)

### `0x416` — ABS / Traction  `(50 00 FE 00 01 00 00 00)`
- **Byte 6 (ABS light):** `00` = Off · `40` = Solid · `80` = Flash Slow · `D0` = Flash Fast
- **Byte 5 (Traction light):** `00` = Off · `02` = Solid · `0F` = Flashing

### `0x171` — Reverse (Sync 4)  `(36 32 00 00 00 00 00 00)`
- **Byte 0:** `36` = Reverse — **must match Byte 1**
- **Byte 1:** `32` = Reverse — **must match Byte 0**

### `0x202` — Speed (Sync 4)  `(04 00 00 00 60 00 25 44)`
- **Byte 4:** must be `6x` (`60`) for the speed gauge to work
- **Bytes 6–7:** `Mph × 175 = Speed` value

### `0x3B5` — Tire pressures  `(00 CE 00 CE 00 CE 00 CE)`
- **Byte 1 = FL tire** · **Byte 3 = FR tire** · **Byte 5 = RR tire** · **Byte 7 = RL tire**

### `0x109`  `(00 03 01 00 00 00 00 24)`
- **Byte 2 — controls gear** *(Untested)*

### `0x156` — Temps  `(00 00 00 00 03 00 00 00)`
- **Byte 0 = Coolant temp** · **Byte 1 = Oil temp**

### `0x204` — RPM  `(00 00 00 09 C4 00 00 00)`
- **Bytes 3/4 = RPM / 2**

### `0x3B2` / `0x3B3`  `(40 48 00 10 10 00 00 02)`
- **Byte 0 (headlamp):** `40` = Headlamp Off · `44` = Headlamp On
- **Byte 1:** `48` = DRL · `88` = Night · `4A` = Hazard
- **Byte 3 — Backlight (display/cluster brightness):**
  - `0x00–0x10` (hex) / `0–17` (dec) = brightness level
  - `0A` = MyColor
- **Byte 4 (turn signals):** `10` = Off · `16` = Off · `18` = Right on
- **Byte 5 (day/night):** `00` = Day Mode · `50` = Night Mode
- **Byte 6:** `00` = nothing · `40` = Left on
- **Byte 7 (doors/hood):** `00` = Doors closed · `10` = Pass door open · `20` = Driver door open ·
  `30` = Both open · `02` = Hood closed · `0A` = Hood open

---

## Implementation status (firmware v2.1.0+)

As of v2.1.0 the notes' extra decodes are implemented through the template
**signals** system — named multi-state controls that overlay a byte span onto
an already-transmitted frame, set at runtime with `SIGNAL:<name>:<state>` (or a
raw number for continuous controls like backlight). The Ford templates ship
these signals; any template can define its own.

| Feature | CAN ID / byte | Signal / command | Status |
|---------|---------------|------------------|--------|
| Reverse / gear | `0x171` B0/B1 (`36 32`) | `GEAR:R` (gear selector) | ✅ implemented (0x171 now transmitted) |
| ABS light | `0x416` B6 | `SIGNAL:ABS:OFF\|SOLID\|SLOW\|FAST` | ✅ implemented |
| Traction light | `0x416` B5 | `SIGNAL:TRACTION:OFF\|SOLID\|FLASH` | ✅ implemented |
| Headlamp | `0x3B2/3` B0 | `SIGNAL:HEADLAMP:OFF\|ON` | ✅ implemented |
| DRL / night / hazard | `0x3B2/3` B1 | `SIGNAL:MODE:DRL\|NIGHT\|HAZARD` | ✅ implemented |
| Backlight / brightness | `0x3B2/3` B3 | `SIGNAL:BACKLIGHT:<0-17>` | ✅ implemented (slider in UI) |
| Day / night dimming | `0x3B2/3` B5 | `SIGNAL:DAYNIGHT:DAY\|NIGHT` | ✅ implemented |
| Doors / hood | `0x3B2/3` B7 | `SIGNAL:DOORS:CLOSED\|PASSENGER\|DRIVER\|BOTH\|HOODOPEN` | ✅ implemented |
| Turn signals | `0x3B2/3` B4/B6 | `BLINKER:LEFT\|RIGHT\|BOTH\|OFF` | ✅ (pre-existing) |
| Speed scale | `0x202` B6/7 | gauge `scale` field (editable in template) | ✅ customizable per template |

## Remaining discrepancies (verify on hardware)

| ID | Firmware default | Notes | Note |
|----|------------------|-------|------|
| `0x202` speed scale | `mph × 159` | `mph × 175` | Now a template field — set per vehicle once confirmed on hardware |
| `0x109` last byte | `...00 28` | `...00 24` | Byte 7 left as firmware default (`28`) by request; adjust in the template if `24` proves correct |
| `0x3B5` byte map | B1=DF, B3=PF, B5=PR, B7=DR | B1=FL, B3=FR, B5=RR, B7=RL | Same byte positions, corner-naming differs (PR vs RR, DR vs RL) |

---

*Generated from software analysis + 3 handwritten note photos. The `0x171`
reverse frame, the `0x3B2/3` sub-fields (backlight, doors, day/night, headlamp,
mode), and the `0x416` ABS/traction decode are now implemented as template
signals; only the value discrepancies above remain to confirm on hardware.*
