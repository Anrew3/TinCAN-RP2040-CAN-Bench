# Ford S650 / SYNC CAN Bus Messages

Consolidated reference combining:
1. **Software** — CAN IDs transmitted by the TinCAN-RP2040-CAN-Bench firmware and the
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
| `0x81`  | Steering-wheel / cluster buttons | `00 00 00 00 00 00 00 00` (idle) | Software + cluster firmware |
| `0x109` | Heartbeat / misc status (gear?) | `00 03 01 00 00 00 00 24` | Software + Notes |
| `0x156` | Engine temperatures | `00 00 00 00 03 00 00 00` | Software + Notes |
| `0x171` | Gear lever P/R/N/D/S (+ M/S gear number) | `04 60 00 00 00 00 00 00` (Drive) | Notes + cluster firmware |
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
- Key bits, read from the S550 12.3" cluster MCU firmware (0x81 reader `FUN_0008bd06`):

  | key | byte | mask | key | byte | mask |
  |-----|------|------|-----|------|------|
  | UP | B0 | `0x08` | SETTINGS | B1 | `0x01` |
  | DOWN | B0 | `0x01` | HUD | B1 | `0x20` |
  | OK | B0 | `0x10` | MEDIA | B1 | `0x08` |
  | BACK | B1 | `0x40` | NAVI | B1 | `0x04` |
  | VIEW | B2 | `0x80` | | | |

- The old `LEFT=0x02` / `RIGHT=0x04` (B0) have **no button in the cluster MCU**, so they never
  reach the display. The S550 template no longer sends them; the F-150 template keeps them.
- The old `SETTINGS = 46 01` also set LEFT, RIGHT and an unmapped B0 bit `0x40`; SETTINGS is
  B1 `0x01` alone.
- Holding: the cluster counts hold levels itself while a key bit stays set, so a long press
  is simply a longer hold of the same frame.

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
- **`0x3B2` B0 high nibble = ignition / cluster power mode** (S550 cluster MCU reader `0x89716` →
  power-mode writer `0x70cd0`): `1` Off, `2` Accessory, `4` **Run (gauges live)**, `8` Start family; any
  other value counts as Off. The lighting base `40 …` already says Run. The S550 template's `IGNITION`
  signal (`SIGNAL:IGNITION:OFF|ACC|RUN|START`) sets just that nibble (mask `F0`). The cluster's `0x3B3`
  reader only uses B4 and B6 bit 0, so ignition must come from `0x3B2`.

### `0x40A` — VIN broadcast (3 consecutive frames)
- Rate: 200 ms (each of 3 parts repeats)
- Byte 0 = `0xC1`; Byte 1 = frame index `i ∈ {0,1,2}`; Bytes 2–7 = next 6 ASCII VIN chars; final frame pads Byte 7 to `0xFF` if needed

### `0x109` — Misc status / heartbeat
- Rate: 10 ms; payload `00 03 01 00 00 00 00 28` (fixed in firmware)

### `0x416` — ABS / traction status
- Rate: 10 ms; payload `50 00 FE 00 01 00 00 00` (fixed in firmware)

**Serial control commands:** `RPM:<value>`, `SPEED:<mph>`, `TIRE:<name>:<psi>`,
`VIN:<17chars>`, `TEMP:<COOLANT|OIL>:<tempF>`, `BLINKER:<LEFT|RIGHT|BOTH|OFF>`,
`HAZARDS`, `GEAR:<state>`, and any button the template defines (S550: `UP`, `DOWN`, `OK`,
`BACK`, `SETTINGS`, `VIEW`, `HUD`, `MEDIA`, `NAVI`).

---

## Handwritten notes (transcribed)

### `0x416` — ABS / Traction  `(50 00 FE 00 01 00 00 00)`
- **Byte 6 (ABS light):** `00` = Off · `40` = Solid · `80` = Flash Slow · `D0` = Flash Fast
- **Byte 5 (Traction light):** `00` = Off · `02` = Solid · `0F` = Flashing

### `0x171` — Reverse (Sync 4)  `(36 32 00 00 00 00 00 00)`
- **Byte 0:** `36` = Reverse — **must match Byte 1**
- **Byte 1:** `32` = Reverse — **must match Byte 0**

> **Superseded by the cluster firmware decode.** The S550 12.3" cluster MCU reads 0x171 in
> `FUN_00083a1a` (B0) and `FUN_00089b78` (B1/B2); the lever logic is `FUN_0004cfb0`:
>
> | field | meaning |
> |-------|---------|
> | B0 bits 0–1 | mode: 1 manual (M), 2 SelectShift (S); 3 = invalid |
> | B0 bits 2–3 | **lever valid — must be 1** |
> | B0 bits 4–7 | gear number 1–10 for the M/S display |
> | B1 bits 5–7 | **lever: 0 P, 1 R, 2 N, 3 D, 4 S** |
>
> So P = `04 00`, R = `04 20`, N = `04 40`, D = `04 60`, S = `04 80`, no gear = `00 00`.
> `36 32` works for Reverse only because B0 bits 2–3 = 1 and B1 bits 5–7 = 1; it also sets
> SelectShift gear 3 (B0 high nibble `3`, mode `2`). The old `00 00` for P/N/D is "lever not
> valid", so the cluster showed no gear.

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
| Gear P/R/N/D/S | `0x171` B0 bits 2–3 = 1, B1 bits 5–7 = lever | `GEAR:PARK\|REVERSE\|NEUTRAL\|DRIVE\|SPORT\|NONE` (gear selector) | ✅ firmware-derived encoding (S550 template 1.1.0) — verify on the bench |
| Ignition / power mode | `0x3B2` B0 high nibble (1 Off, 2 Acc, 4 Run, 8 Start) | `SIGNAL:IGNITION:OFF\|ACC\|RUN\|START` (masked `F0`) | ✅ firmware-derived (S550 template 1.2.0) — verify on the bench |
| Fuel | — | — | ❌ not on CAN for the S550: the cluster reads analog fuel senders. `0x207` is not among the IDs the cluster receives, so the S550 template no longer sends it |
| ABS light | `0x416` B6 | `SIGNAL:ABS:OFF\|SOLID\|SLOW\|FAST` | ✅ implemented |
| Traction light | `0x416` B5 | `SIGNAL:TRACTION:OFF\|SOLID\|FLASH` | ✅ implemented |
| Headlights (B0+B1 pair) | `0x3B2/3` B0+B1 | `SIGNAL:HEADLIGHTS:DRL\|ON` | ✅ implemented |
| Backlight / brightness | `0x3B2/3` B3 | `SIGNAL:BACKLIGHT:<0-17>` | ✅ implemented (slider in UI) |
| Day / night dimming | `0x3B2/3` B5 | `SIGNAL:DAYNIGHT:DAY\|NIGHT` | ✅ implemented |
| Doors / hood | `0x3B2/3` B7 | `SIGNAL:DOORS:CLOSED\|PASSENGER\|DRIVER\|BOTH\|HOODOPEN` | ✅ implemented |
| Turn signals | `0x3B2/3` B4/B6 | `BLINKER:LEFT\|RIGHT\|BOTH\|OFF` | ✅ (pre-existing) |
| Speed scale | `0x202` B6/7 | gauge `scale` field (editable in template) | ✅ customizable per template |

## SYNC head-unit wake sequence (from the v1 boot sketch)

Booting a dormant SYNC 3 APIM needs a wake/enable sequence that the cluster
notes above don't cover — it lived only in the original v1 boot sketch. All
three are sent **continuously**:

| CAN ID | Payload | Role |
|--------|---------|------|
| `0x3B3` | `41 00 00 00 4C 00 00 00` | CGEA-1.3 network **wake** (boots the unit) |
| `0x048` | `00 00 00 00 07 00 E0 00` | C1MCA **turn-on** ("turned most units on") |
| `0x109` | `00 03 01 00 00 00 00 28` | accessory-on / park keepalive (also the cluster heartbeat) |

> **`0x3B3` is dual-use.** The wake payload (`41 00 00 00 4C…`) collides with the
> lighting decode (`40 48 C0 10…`) on bytes 0 and 4, so the frame can't be both at
> once. v2.1.0 handles this with a runtime switch: **`BODY:WAKE`** (default — sends
> the wake, boots the unit) vs **`BODY:LIGHT`** (sends the lighting base so the
> headlight/turn-signal/door controls take effect). Templates set it via
> `blinkers.wakeBase` + `wakeDefault`. `0x048` and `0x109` are plain continuous
> keepalives (`background`), no conflict.
>
> **Cluster note:** in `BODY:WAKE` TinCAN sends `0x3B3` only, no `0x3B2`. The S550 cluster takes its
> power mode from `0x3B2` B0, so for a cluster-only bench use `BODY:LIGHT` (or check whether the bench's
> hardwired ignition input keeps it awake).

## S550: candidates not added yet (need pinning or a bench check)

From the cluster-firmware CAN guide. The cluster reads these, but which byte carries which value is not
pinned firmly enough to put in the template:

| ID | what it drives | why not added |
|----|----------------|---------------|
| `0x42D` | MyGauges cylinder-head + inlet-air temp (raw ÷ 10) | the reader's value is a 10-bit field `B1[0..1] B2`, not the B0–B1 / B2–B3 the guide lists; which value is which isn't pinned |
| `0x43E` | air-fuel ratio + cylinder-head temp (÷ 10) | two fields (13-bit `B5 B6[3..7]`, 10-bit `B6[0..1] B7`); not pinned which is AFR |
| `0x46A` | inlet-air temp (÷ 10) + page config | byte not pinned |
| `0x179` | distance-to-empty (km) + trip average speed | DTE field position not pinned (B2–B3 vs B6–B7) |
| `0x42F` | combined speedo/tach/fuel/temp feed | field meanings not pinned |
| `0x167` | warning lamps | bits not tied to named lamps yet |
| `0x3B4` | per-tyre warning colour | fields not pinned |

## Remaining discrepancies (verify on hardware)

| ID | Firmware default | Notes | Note |
|----|------------------|-------|------|
| `0x202` speed scale | `mph × 159` | `mph × 175` | Now a template field. The cluster firmware reads B6–B7 as **0.01 km/h** (a real car sends `mph × 160.934`); its speedo curve reads ~1 % high, which `× 159` happens to cancel |
| `0x109` last byte | `...00 28` | `...00 24` | Byte 7 left as firmware default (`28`) by request; adjust in the template if `24` proves correct |
| `0x3B5` byte map | B1=DF, B3=PF, B5=PR, B7=DR | B1=FL, B3=FR, B5=RR, B7=RL | Same byte positions, corner-naming differs (PR vs RR, DR vs RL) |

---

*Generated from software analysis + 3 handwritten note photos, with the `0x81` key bits and
`0x171` gear fields later corrected from the S550 cluster MCU firmware. The `0x171`
gear frame, the `0x3B2/3` sub-fields (backlight, doors, day/night, headlamp,
mode), and the `0x416` ABS/traction decode are now implemented as template
signals; only the value discrepancies above remain to confirm on hardware.*
