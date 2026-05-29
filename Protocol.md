# PHNIX Pool Heat Pump — BLE Modbus Protocol Reference

This document describes the BLE protocol used by **PHNIX-manufactured**
pool heat pumps for communication with the **AquaTemp** mobile app. PHNIX
is the original equipment manufacturer (OEM); their heat pumps are sold
worldwide under a range of distributor and reseller brand names. The
common identifier is that the brand uses the **AquaTemp** app (sometimes
called AquaTemp Smart) for Bluetooth control.

This reference is the result of reverse-engineering the protocol via
BTSnoop captures of the official Android app combined with cross-referencing
a leaked Modbus protocol document covering related models.

The information here is sufficient to build a third-party controller —
e.g. an ESPHome bridge to Home Assistant — that reads sensor data and
controls the heat pump without using the manufacturer's cloud or app.

## Scope and applicability

### Manufacturer and app

- **OEM:** PHNIX (`phnix.com`, also written PHINX)
- **Mobile app:** **AquaTemp** (Android package `com.phinx.poolheater4`,
  iOS equivalent in the Apple App Store). Sometimes branded "AquaTemp
  Smart".

### Known re-branded distributors

The same PHNIX hardware is sold worldwide under at least the following
brand names (non-exhaustive list — if the app you use is AquaTemp, the
protocol below very likely applies):

- **Thermotec** (the verified reference unit for this document)
- **AquaTemp** (manufacturer-direct branding in some markets)
- **PHNIX** (manufacturer-direct branding)
- Various other regional distributors and re-sellers

If you discover that this protocol works on a heat pump under a different
brand name, please add it to the list above via a pull request.

### Verified hardware

The reference unit used for this reverse-engineering is:

- **Brand:** Thermotec
- **Type:** Heat-only single-system pool heat pump
- **BLE module:** BlueNRG-based, advertised as `BLUENRG-XXXXXX` where
  the suffix is the last 3 bytes of the BLE MAC address.
- **Firmware identity** (read from the unit — see §4.3.2):
  - Master program version number: `1.2`
  - Main control software code: `494`
- A separate WiFi gateway option exists for cloud control but the BLE
  protocol does not depend on it.

### Coverage caveats

- **Single-system heat pumps:** Fully covered. The register layout in §4
  was decoded directly from a single-system unit.
- **Multi-system (dual-compressor) units:** Partially covered. The same
  Modbus transport, function codes, and base layout apply, but
  additional registers exist that this document does not detail. The
  supplementary PHNIX Modbus protocol document (PDF, v1.0 dated
  2023-06-21) describes the multi-system registers including separate
  bitmasks for "System 1" and "System 2" pressure switches, compressors,
  fan motors, etc.
- **Cooling and Auto modes:** Documented, but the reference unit had
  these disabled by default in its engineering menu. They were enabled
  for verification but real-world cool/auto operation has limited
  capture data behind it.

## Document conventions

- All multi-byte integers are **big-endian** unless stated otherwise.
- "Registers" are 16-bit values addressed by 16-bit address. Heat-pump
  protocol convention uses register *addresses* (e.g. `0x0416`) rather
  than coil/holding numbering.
- Temperatures are signed 16-bit, scaled ×10: a register value of `300`
  means `30.0 °C`. A value of `-30` (i.e. `0xFFE2` two's complement)
  means `-3.0 °C`.
- "Confirmed" means observed in the wild via BLE capture on the
  reference unit. "Inferred" means the mapping is strongly suggested
  by the supplementary PHNIX doc or by pattern-matching against other
  confirmed entries but has not been directly verified.

---

## 1. BLE transport layer

The heat pump's BLE module exposes a single proprietary GATT service with
three characteristics. **Only one BLE client may be connected at a time** —
the AquaTemp app and a third-party controller will fight for the connection
if both are running. Disconnect one before using the other.

### 1.1 Service & characteristic UUIDs

| Role | UUID (short) | UUID (full) |
|---|---|---|
| Service | `0xFF01` | `0000ff01-0000-1000-8000-00805f9b34fb` |
| Write characteristic | `0xFF03` | `0000ff03-0000-1000-8000-00805f9b34fb` |
| Notify characteristic | `0xFF02` | `0000ff02-0000-1000-8000-00805f9b34fb` |

### 1.2 Operation

- To send a command, the client writes a complete Modbus RTU frame (see
  §2) to the **write characteristic**.
- The heat pump responds via one or more **notifications** on the notify
  characteristic.
- Read responses larger than the BLE MTU are **fragmented across multiple
  notifications**. A single "read all status" command from the client
  produces **four** notification messages (one per data block — see §3).
  Each notification is itself a complete Modbus frame including header and
  CRC; the BLE module re-frames the response into multiple Modbus frames
  rather than truly fragmenting one large frame.

### 1.3 Connection behaviour notes

- The notify characteristic must be subscribed to (CCCD write `01 00`)
  before notifications will be sent. Most BLE stacks do this automatically
  when you call `setCharacteristicNotification` / equivalent.
- The notify characteristic is **notify-only**. Some BLE stacks attempt
  an initial *read* on every characteristic during service discovery;
  this read returns `status=2` (read not permitted) for `0xFF02`. The
  error is benign and can be safely ignored.
- The BLE module sometimes sends the *same* notification multiple times
  in quick succession (apparent retry behaviour). Consumers should
  deduplicate on the content of the parsed registers, not assume one
  notification per logical update.

---

## 2. Modbus layer

The application protocol is **standard Modbus RTU** framed inside BLE
GATT writes/notifications. Anyone familiar with Modbus RTU will recognise
everything below.

### 2.1 Slave address

The heat pump's Modbus slave address is **`0x63`** (99 decimal) on the
reference unit. The supplementary PHNIX doc states `0x10` for other units.
The slave address may be configurable or vary per model. **Use the byte
captured in any response frame as ground truth.**

### 2.2 Supported function codes

| Code | Name | Direction | Usage on this protocol |
|---|---|---|---|
| `0x03` | Read Holding Registers | Client → Heat pump | Bulk read of status |
| `0x10` | Write Multiple Registers | Client → Heat pump | All writes; also used by the heat pump in response frames |

Function `0x06` (Write Single Register) is **not used** by the official
app. All writes — even of a single register — are framed as Write Multiple
Registers (`0x10`). The heat pump may or may not reject `0x06`; the safe
choice is to mirror the app.

### 2.3 CRC

Standard Modbus RTU CRC-16:
- Polynomial: `0xA001` (reflected form of `0x8005`)
- Initial value: `0xFFFF`
- Transmitted **low byte first** (little-endian)
- Computed over the entire frame from slave address up to (but not
  including) the CRC itself.

Reference C++ implementation:

```c
uint16_t modbus_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
        }
    }
    return crc;
}
// Append: msg.push_back(crc & 0xFF); msg.push_back((crc >> 8) & 0xFF);
```

### 2.4 Data types

These follow the PHNIX protocol doc's conventions.

| Type | Description |
|---|---|
| `TEMP1` | Signed 16-bit, scaled ×10. Range −30 to 97 °C. Value `0x7FFD`–`0x7FFF` indicates **sensor failure** (see §5.2). The ambient sensor is an exception — see §5.2. |
| `DIGI1` | Unsigned 16-bit, unit 1. e.g. `123` displays as `123`. |
| `DIGI5` | Unsigned 16-bit, unit 0.1. e.g. `123` displays as `12.3`. |

---

## 3. Reading data: the four-block model

### 3.1 The "read all" command

The mobile app's primary status-poll command is:

```
63 03 00 07 00 2D 3C 54
│  │  ─┬── ─┬── ─┬──
│  │   │    │    └─ CRC16 (3C 54)
│  │   │    └────── Quantity: 0x002D = 45 registers
│  │   └─────────── Start address: 0x0007
│  └──────────────── Function code: 0x03 (Read Holding)
└─────────────────── Slave address: 0x63
```

Despite the nominal start address `0x0007` and length 45, this single
request elicits **four separate notification responses**, one for each of
four logical blocks in the heat pump's register map. The "start address
of `0x0007`" appears to be a magic value the BLE module recognises rather
than a literal Modbus address; reading any other range with function `0x03`
returns Modbus exception `0x01` (Illegal Function).

The four blocks are read in the order the heat pump sends them, which has
been observed as **0x03E9, 0x0416, 0x07D1, 0x07FE**, though consumers
should not rely on the order. Identify each notification by the block
address embedded in the response header.

### 3.2 Notification response format

Each block notification is 99 bytes:

```
63 10 BH BL 00 2D 5A [90 bytes of register data] CL CH
│  │  ──┬── ──┬── │  ──────────────┬───────────── ──┬──
│  │    │     │   │                │                └─ CRC16 (low byte first)
│  │    │     │   │                └─ 45 registers × 2 bytes = 90 bytes payload
│  │    │     │   └─ Byte count: 0x5A = 90
│  │    │     └─ Register quantity: 0x002D = 45
│  │    └─ Block start address (e.g. 0x07FE)
│  └─ Function code: 0x10
└─ Slave address
```

Within the 90-byte payload, register *N* of the block (zero-indexed) lives
at byte offsets `7 + N*2` and `7 + N*2 + 1` of the **full** notification
buffer (i.e. the data section starts immediately after the 7-byte header).
The high byte comes first.

### 3.3 The four blocks at a glance

| Block address | Decimal | Contents | Section |
|---|---|---|---|
| `0x03E9` | 1001 | Control & engineering settings | §4.1 |
| `0x0416` | 1046 | Setpoints & defrost params | §4.2 |
| `0x07D1` | 2001 | Operational status, outputs, error code | §4.3 |
| `0x07FE` | 2046 | Live temperature readings | §4.4 |

---

## 4. Register map

In each table below:
- **Reg #** is the zero-based offset within the block.
- **Absolute address** is the Modbus register address (block start +
  offset). This is what you'd write to using function `0x10`.
- **Status** is one of:
  - ✅ Confirmed by BLE capture on the reference unit
  - 📐 Inferred from the supplementary PHNIX doc or strong pattern
  - ❓ Unknown — observed but meaning not yet decoded

### 4.1 Block 0x03E9 — control & engineering settings

Registers in this block correspond to the heat pump's user-power, mode,
and "H tab" engineer-menu settings. Register addresses fall in the range
`0x03E9`–`0x0415`.

| Reg # | Addr | Setting | Type | Values | Status |
|---|---|---|---|---|---|
| 10 | `0x03F3` | **Power state** | bool | 0=Off, 1=On | ✅ |
| 11 | `0x03F4` | **Mode** | enum | 0=Cool, 1=Heat, 2=Auto | ✅ |
| 12 | `0x03F5` | Unknown ("mode companion") | — | See §6.1 | ❓ |
| 17 | `0x03FA` | **H01** Power-down memory | bool | 0=No, 1=Yes | 📐 |
| 18 | `0x03FB` | **H02** Temperature unit | enum | 0=°C, 1=°F (inferred) | 📐 |
| 19 | `0x03FC` | **H03** Compressor stop temp | TEMP1 | e.g. `−70` = −7.0 °C | ✅ |
| 20 | `0x03FD` | **H04** Exhaust temp high limit | TEMP1 | e.g. `1050` = 105.0 °C | ✅ |
| 21 | `0x03FE` | **H05** Outlet water temp sensor | bool | 0=Off, 1=On | 📐 |
| 22 | `0x03FF` | **H06** Mode type | enum | 1=Heat pump, 2=Heat only (4-way valve) | ✅ |
| 36 | `0x040D` | **P01** Circulating pump mode | enum | 0=Normal, 1=Stop, 2=Interval | ✅ |
| 37 | `0x040E` | P02 Pump interval (uncertain) | DIGI1 | minutes | ❓ |
| 38 | `0x040F` | **P03** Pump duration | DIGI1 | minutes | 📐 |
| 39 | `0x0410` | P04 Pump advance (uncertain) | DIGI1 | minutes | ❓ |

### 4.2 Block 0x0416 — setpoints & defrost

Register addresses fall in `0x0416`–`0x0442`. This block contains the
"R tab" setpoints and the "D tab" defrost parameters.

| Reg # | Addr | Setting | Type | Range / values | Status |
|---|---|---|---|---|---|
| 0 | `0x0416` | **R01** Heating set | TEMP1 | typ. 15.0–40.0 °C | ✅ |
| 1 | `0x0417` | R02 Temp difference (heat) | TEMP1 | typ. 0.5 °C steps | 📐 |
| 2 | `0x0418` | R03 Power-off temp diff (heat) | TEMP1 | typ. 0.5 °C steps | 📐 |
| 3 | `0x0419` | R04 Minimum heat limit | TEMP1 | e.g. 15.0 °C | ✅ |
| 4 | `0x041A` | R05 Maximum heat limit | TEMP1 | e.g. 40.0 °C | ✅ |
| 5 | `0x041B` | **R06** Cooling set | TEMP1 | typ. 10.0–35.0 °C | ✅ |
| 6 | `0x041C` | R07 Temp difference (cool) | TEMP1 | 0.5 °C steps | 📐 |
| 7 | `0x041D` | R08 Power-off temp diff (cool) | TEMP1 | 0.5 °C steps | 📐 |
| 8 | `0x041E` | R09 Minimum cool limit | TEMP1 | e.g. 10.0 °C | ✅ |
| 9 | `0x041F` | R10 Maximum cool limit | TEMP1 | e.g. 35.0 °C | ✅ |
| 10 | `0x0420` | **D01** Start defrost temp | TEMP1 | e.g. −3.0 °C | ✅ |
| 11 | `0x0421` | **D02** End defrost temp | TEMP1 | e.g. 13.0 °C | ✅ |
| 12 | `0x0422` | **D03** Defrost cycle | DIGI1 | minutes (e.g. 40) | ✅ |
| 13 | `0x0423` | **D04** Defrost max duration | DIGI1 | minutes (e.g. 8) | ✅ |
| 14 | `0x0424` | **D05** Defrost min duration | DIGI1 | minutes (e.g. 3) | ✅ |
| 15 | `0x0425` | **D06** Defrost type | enum | 0=Normal, 1=Eco | ✅ |
| 22 | `0x042C` | **R11** Auto set | TEMP1 | typ. 15.0–40.0 °C | ✅ |

**Important design note:** the heat pump's *displayed* target temperature
follows the **current mode**: it shows R01 when in Heat mode, R06 in Cool
mode, R11 in Auto. A third-party controller that exposes a single "target
temperature" slider should write the slider's value to whichever register
matches the current mode.

### 4.3 Block 0x07D1 — operational status

Register addresses fall in `0x07D1`–`0x07FD`. This block contains live
operational state. It changes frequently as the heat pump runs.

| Reg # | Addr | Setting | Type | Status |
|---|---|---|---|---|
| 10 | `0x07DB` | **Unit running state** | bool | ✅ |
| 16 | `0x07E1` | **Master program version number** (see §4.3.2) | DIGI5 | ✅ |
| 17 | `0x07E2` | **Main control software code** (see §4.3.2) | DIGI1 | ✅ |
| 18 | `0x07E3` | **Output bitmask** (compressor / fan / etc.) | bitmask | ✅ |
| 33 | `0x07F2` | **Error code bitmask** | bitmask | ✅ |
| 39 | `0x07F8` | Compressor frequency (multi-system reg per PHNIX doc) | DIGI1 (Hz) | 📐 |
| 43 | `0x07FC` | Fan motor speed (multi-system reg per PHNIX doc) | DIGI1 (rpm) | 📐 |

#### 4.3.1 Unit running state (`0x07DB`)

This is `1` while the heat pump is "running normally" and `0` when stopped.
Note this differs subtly from the user power command at `0x03F3`:

- **Power state (`0x03F3`)** is the user's on/off *intent*.
- **Unit running state (`0x07DB`)** is the heat pump's actual *state*. It
  may be `0` even when power is on if the heat pump has self-stopped due
  to a fault (e.g. coil or ambient sensor failure).

Useful for detecting silent safety shutdowns.

#### 4.3.2 Firmware identity (`0x07E1`, `0x07E2`)

Two consecutive read-only registers expose the heat pump's firmware
identity. Both are stable across reads and across operating states (i.e.
they only ever change when the unit's firmware is updated). The labels
below match what the official AquaTemp app displays on its "about"
screen.

| Register | Label (app / control panel) | Type | Example |
|---|---|---|---|
| `0x07E1` | **Master program version number** | DIGI5 (×10) | raw `12` → `1.2` |
| `0x07E2` | **Main control software code** | DIGI1 (raw) | `494` |

These represent two distinct concepts:

- `0x07E1` is a *version* in the conventional `major.minor` sense
  (encoded ×10 — i.e. you divide by 10 for display, with one decimal
  place). On the reference unit: `1.2`.
- `0x07E2` is a *build/software code*, a plain integer identifier with
  no scaling. On the reference unit: `494`.

A consumer should expose them as **separate** entities so the labels
match what users see on the heat pump itself.

#### 4.3.3 Output bitmask (`0x07E3`)

A 16-bit bitfield indicating which output relays are currently energised.

| Bit | Mask | Output | Status |
|---|---|---|---|
| 0 | `0x0001` | Compressor | ✅ |
| 1 | `0x0002` | Circulating pump | 📐 |
| 2 | `0x0004` | High fan | ✅ |
| 3 | `0x0008` | 4-way valve | 📐 |

Multiple bits may be set simultaneously. The labelling matches the
parameter pages O01–O04 in the official app.

#### 4.3.4 Error code bitmask (`0x07F2`)

A 16-bit bitfield where each bit corresponds to a different operational
fault. **Multiple bits may be set at once** if multiple faults are active.

| Bit | Mask | Display code | Description | Status |
|---|---|---|---|---|
| 0 | `0x0001` | **E01** | High pressure protection | 📐 |
| 1 | `0x0002` | **E02** | Low pressure protection | ✅ |
| 2 | `0x0004` | **E03** | No water flow / flow switch failure | ✅ |
| 3 | `0x0008` | ? | Unknown | ❓ |
| 4 | `0x0010` | ? | Unknown (possibly E06 inlet/outlet diff) | ❓ |
| 5 | `0x0020` | ? | Unknown (possibly E08 communication) | ❓ |
| 6–15 | | ? | Unknown | ❓ |

⚠️ **E01 is inferred only.** Triggering E01 (high-pressure protection)
deliberately is unsafe; do not attempt to verify it. The pattern of E02
on bit 1 and E03 on bit 2 strongly suggests E01 on bit 0, but this is not
proven on the reference unit.

P-codes (sensor failures) are **NOT** encoded in this register — see §5.2.

### 4.4 Block 0x07FE — live temperatures

Register addresses fall in `0x07FE`–`0x082A`. Only the first six
registers (the actual sensor readings) are decoded here; the remainder
are mostly zero and undocumented.

| Reg # | Addr | Sensor | Notes |
|---|---|---|---|
| 0 | `0x07FE` | T01 Suction temperature | Often `0x7FFD` (sensor not installed on single-system models) |
| 1 | `0x07FF` | **T02 Inlet water temperature** | Failure → P01 |
| 2 | `0x0800` | **T03 Outlet water temperature** | Failure → P02 |
| 3 | `0x0801` | **T04 Coil temperature** | Failure → P05; can swing widely (5–25 °C) during normal compressor cycling |
| 4 | `0x0802` | **T05 Ambient temperature** | Failure → P04 (special encoding — see §5.2) |
| 5 | `0x0803` | **T06 Exhaust temperature** | Failure → P81; runs hot during normal compressor operation |

All values are `TEMP1` (signed 16-bit, scaled ×10). Sensor-failure
detection rules are in §5.2.

---

## 5. Writes (control commands)

All writes use Modbus function `0x10` (Write Multiple Registers), even
when writing a single register. The frame format is:

```
63 10 AH AL QH QL BC [bytes of register data] CL CH
│  │  ─┬── ─┬── │  ──────────┬─────────────── ──┬──
│  │   │    │   │            │                   └─ CRC16 (low byte first)
│  │   │    │   │            └─ Register data: BC bytes (= QH:QL × 2)
│  │   │    │   └─ Byte count BC (= QH:QL × 2)
│  │   │    └─ Quantity QH:QL (number of registers)
│  │   └─ Start address AH:AL
│  └─ Function code: 0x10
└─ Slave address
```

### 5.1 Single-register writes

#### 5.1.1 Power on/off — register `0x03F3`

```
OFF: 63 10 03 F3 00 01 02 00 00 30 F1
ON:  63 10 03 F3 00 01 02 00 01 F1 31
```

The heat pump responds with a standard Modbus ACK on success.

#### 5.1.2 Target temperature — registers `0x0416`, `0x041B`, or `0x042C`

The target temperature is stored in a different register depending on the
heat pump's *current* mode:

| Current mode | Target register |
|---|---|
| Heat | `0x0416` |
| Cool | `0x041B` |
| Auto | `0x042C` |

Example: set heat target to 30.5 °C (value 305 = `0x0131`):

```
63 10 04 16 00 01 02 01 31 [CRC_lo CRC_hi]
```

The two CRC bytes must be computed per §2.3.

#### 5.1.3 Other settings

Single-register writes with the same shape are used for:

| Register | Setting | Sample values |
|---|---|---|
| `0x03FF` | H06 Mode type | `0x0001` Heat pump, `0x0002` Heat only |
| `0x040D` | P01 Pump mode | `0x0000` Normal, `0x0001` Stop, `0x0002` Interval |
| `0x0425` | D06 Defrost type | `0x0000` Normal, `0x0001` Eco |

### 5.2 Mode change — register `0x03F4` (two registers)

Mode changes are written as a **two-register** write covering `0x03F4`
and `0x03F5`:

```
COOL:  63 10 03 F4 00 02 04 00 00 00 00 1C 51
HEAT:  63 10 03 F4 00 02 04 00 01 00 88 4D F7
AUTO:  63 10 03 F4 00 02 04 00 02 00 8C BC 34
```

- `0x03F4` is the mode: `0` Cool, `1` Heat, `2` Auto.
- `0x03F5` ("mode companion") is a value whose semantics are unknown
  but which the app sets to specific values per mode: `0x0000` for Cool,
  `0x0088` for Heat, `0x008C` for Auto.

⚠️ **Recommendation:** mirror the app exactly. Writing only register
`0x03F4` (single-register write) is untested and may be rejected by the
heat pump or cause unexpected state. Writing a different value to
`0x03F5` than the app does is also untested. The safest implementation
hard-codes the three captured frames.

---

## 6. Error encoding (display codes)

The heat pump's front panel can display two categories of error code:

- **E-codes** (`E01`, `E02`, …) — operational faults (pressure
  protection, water flow failure, etc.).
- **P-codes** (`P01`, `P02`, …) — temperature sensor failures.

Plus a few special codes:

- **TP** — Low ambient temperature protection.
- **DF** — Defrost cycle active.

The two error categories are encoded **completely differently** in the
register map.

### 6.1 E-codes — bitmask at register `0x07F2`

See §4.3.3. Each bit represents one fault. To decode:

```c
uint16_t code = read_register(0x07F2);
if (code & 0x0001) /* E01 — DO NOT trigger deliberately */;
if (code & 0x0002) /* E02 */;
if (code & 0x0004) /* E03 */;
// etc.
```

Multiple bits may be set simultaneously. If a future fault sets a bit
not yet documented here, log the raw value and add a mapping once the
display code is observed alongside it.

### 6.2 P-codes — temperature sensor failure sentinels

P-codes are **not** in the error register. They are signalled by the
relevant temperature register reading a magic "sensor failure" value
instead of a real temperature.

#### 6.2.1 Standard sentinel: `0x7FFD`

Five of the six temperature sensors signal failure as `0x7FFD` (or
nominally any value in `0x7FF0`–`0x7FFF`; the official PHNIX doc
specifies `32767` = `0x7FFF` but the reference unit consistently emits
`0x7FFD`). Use the mask `(raw & 0xFFF0) == 0x7FF0` for robust detection.

| Sensor | Register | Sentinel value | Display code |
|---|---|---|---|
| T02 Inlet | `0x07FF` | `0x7FFD` | **P01** |
| T03 Outlet | `0x0800` | `0x7FFD` | **P02** |
| T04 Coil | `0x0801` | `0x7FFD` | **P05** |
| T06 Exhaust | `0x0803` | `0x7FFD` | **P81** |

T01 Suction (`0x07FE`) typically reads `0x7FFD` *permanently* on
single-system units because the sensor isn't installed. Treat it as
"not present" rather than "fault".

#### 6.2.2 Ambient sensor: `0xFFF6` (special case)

The T05 Ambient sensor uses a different sentinel: when disconnected, it
reads **`0xFFF6` (= signed −10 = −1.0 °C)**. This is *not* a sentinel
pattern but appears to be a real value the sensor's input circuit reads
when open-circuited (possibly due to a pull-down on that input).

```c
bool ambient_fault = (raw == (int16_t)0xFFF6) || ((raw & 0xFFF0) == 0x7FF0);
```

⚠️ **Edge case:** a genuine ambient temperature of exactly −1.0 °C
(possible in winter) will incorrectly trigger this detection. If you
need to operate in subzero conditions, consider additional heuristics
(e.g. require the value to persist for several polls, or cross-reference
the unit-running state at `0x07DB`).

### 6.3 Critical vs non-critical faults

Sensor failures fall into two categories based on how the heat pump
handles them:

| Fault | Heat pump self-stop? | Detection |
|---|---|---|
| P01 Inlet | No — keeps running | T02 = sentinel |
| P02 Outlet | No — keeps running | T03 = sentinel |
| **P04 Ambient** | **Yes** — stops itself | T05 = `0xFFF6` |
| **P05 Coil** | **Yes** — stops itself | T04 = sentinel |
| P81 Exhaust | No — keeps running | T06 = sentinel |

When the heat pump self-stops, register `0x07DB` (unit running state)
drops to 0. This can be used as an additional sanity-check signal.

### 6.4 Display codes not yet decoded

The following are listed in the official manual but have not yet been
mapped to register values:

- **E06** — Inlet/outlet temperature difference too large
- **E08** — Communication failure
- **P82** — Exhaust temperature protection (over-temperature)
- **TP** — Low ambient temperature protection
- **DF** — Defrost cycle (informational, not really a fault)

E08 is probably easy to trigger deliberately by disconnecting the cable
between the wired controller and the main board. The others are either
seasonal or risky to trigger.

---

## 7. Implementation notes

### 7.1 Polling strategy

For a low-overhead consumer, a single "read all" command every 30 s is
plenty for normal monitoring. This produces four notification responses
covering every documented register.

### 7.2 Write strategy

For control operations (power, mode, target temp):

1. Send the appropriate write frame.
2. The heat pump replies with an 8-byte ACK: `63 10 [AH] [AL] 00 [QH] [CL] [CH]`.
3. Optionally re-poll a few seconds later to verify the change took
   effect.

### 7.3 Duplicate notifications

As noted in §1.3, the BLE module sometimes emits the same notification
multiple times. Consumers should deduplicate either:

- At the **publication layer** (e.g. only emit changes greater than
  some threshold for floats; only emit on actual transitions for
  binaries), or
- At the **frame layer** (suppress identical consecutive notification
  payloads).

### 7.4 Common Modbus exceptions observed

| Exception | Code | When seen |
|---|---|---|
| Illegal Function | `0x01` | Function `0x03` with an address other than the magic `0x0007` |

Other Modbus exceptions have not been observed but should be handled
gracefully if they occur.

### 7.5 Locating the heat pump's BLE MAC address

The advertised local name follows the format `BLUENRG-XXXXXX` where the
suffix is the last 3 bytes of the MAC. Scan for that prefix on first
discovery; record the full MAC for subsequent re-connection.

---

## 8. Open questions and gaps

The following are known to exist but have not been fully decoded on the
reference unit. Contributions welcome.

1. **Register `0x03F5` semantics.** Written by the app during mode
   changes with mode-dependent values (`0x0000`/`0x0088`/`0x008C` for
   Heat/Cool/Auto). Unknown whether it's required or what it represents.
2. **P-tab pump settings P02 and P04.** Likely at `0x040E` and `0x0410`
   but values seen in the wild don't match the user's display.
3. **Error register bits 3 and above.** E06, E08, P82, TP, DF have not
   been triggered/captured. The PHNIX doc suggests additional bitmask
   registers (e.g. `2078`, `2079`) exist on multi-system units that may
   be relevant.
4. **Modbus exception cases beyond `0x01`** — handling code paths
   not yet exercised.

---

## 9. References

- **Official PHNIX Modbus protocol document, v1.0 (2023-06-21)** — leaked
  PDF covering related multi-system models. Most data type conventions
  and many register addresses cross-reference cleanly.
- **AquaTemp Android app** (`com.phinx.poolheater4`, version 2.1.4
  observed) — original source of all confirmed mappings via BTSnoop HCI
  capture. The app is OEM'd by PHNIX and shipped by all re-branded
  distributors.
- **Thermotec / PHNIX user manual** — defines the display codes
  (E01–E08, P01–P82, TP, DF) and their human-readable meanings. The
  same code set appears across distributor manuals because the underlying
  controller is identical.
- **heatpumps4pools.com** — UK distributor for Thermotec and related
  brands; their service manuals may contain additional implementation
  detail not in the user manual.

---

## Appendix A — Sample full frames

For copy-paste convenience, here are some complete, CRC-valid frames
captured from the reference unit:

```
# Read all status (the magic poll command)
63 03 00 07 00 2D 3C 54

# Power off
63 10 03 F3 00 01 02 00 00 30 F1
# Power on
63 10 03 F3 00 01 02 00 01 F1 31

# Mode = Heat
63 10 03 F4 00 02 04 00 00 00 00 1C 51
# Mode = Cool
63 10 03 F4 00 02 04 00 01 00 88 4D F7
# Mode = Auto
63 10 03 F4 00 02 04 00 02 00 8C BC 34

# Set heat target = 30.0 °C (300 = 0x012C)
63 10 04 16 00 01 02 01 2C [CRC_lo CRC_hi]

# Set cool target = 15.0 °C (150 = 0x0096)
63 10 04 1B 00 01 02 00 96 D1 77

# Set auto target = 32.0 °C (320 = 0x0140)
63 10 04 2C 00 01 02 01 40 55 3E

# Defrost type = Eco
63 10 04 25 00 01 02 00 01 94 07
# Defrost type = Normal
63 10 04 25 00 01 02 00 00 55 C7

# Mode type = Heat only (4-way valve)
63 10 03 FF 00 01 02 00 02 B1 FC
# Mode type = Heat pump
63 10 03 FF 00 01 02 00 01 F1 FD

# Pump mode = Stop
63 10 04 0D 00 01 02 00 01 92 2F
# Pump mode = Interval
63 10 04 0D 00 01 02 00 02 D2 2E
# Pump mode = Normal
63 10 04 0D 00 01 02 00 00 53 EF
```

---

## Appendix B — Glossary

- **BLE** — Bluetooth Low Energy.
- **BTSnoop** — Android's HCI packet capture format. Generated via
  developer options → "Enable Bluetooth HCI snoop log" then extracted
  via `adb bugreport`.
- **GATT** — Generic Attribute Profile, the protocol that defines
  services and characteristics over BLE.
- **CCCD** — Client Characteristic Configuration Descriptor. Writing
  `01 00` to this enables notifications on a GATT characteristic.
- **MTU** — Maximum Transmission Unit. Default for BLE is 23 bytes
  (20 bytes of payload after the 3-byte ATT header).
- **Modbus RTU** — A serial protocol for industrial control. Frames
  consist of slave address, function code, payload, and CRC.
- **Sentinel value** — A "magic" value used in place of a real reading
  to signal a special condition (here, sensor failure).