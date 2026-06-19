# `phnix` — PHNIX heat pump protocol library

A small, **platform-agnostic** C++17 library that encodes and decodes the
Modbus-RTU-over-BLE protocol spoken by PHNIX pool heat pumps (the hardware
behind the AquaTemp app and Thermotec-branded units).

The protocol itself is documented in [`../../Protocol.md`](../../Protocol.md),
which is the source of truth. This library is an implementation of it.

## Why it exists / scope

This is the first step of extracting the heat-pump logic out of the ESPHome
YAML lambdas into real, **host-unit-testable** code (TDD), so it can be:

1. consumed by the current ESPHome firmware (as an external component), and
2. reused unchanged by a future native firmware,

without rewriting the reverse-engineered protocol twice.

### It is *not* a Bluetooth library

There is **no BLE, ESPHome, or Arduino dependency in here** — by design. The
library is a pure **codec**: bytes in, typed state out; a command in, frame
bytes out. The BLE/GATT transport (connect, subscribe to the notify
characteristic, write to the write characteristic, reconnection, the
one-client-at-a-time constraint) lives in the firmware and feeds this library
through a thin adapter. That separation is exactly what makes the protocol
logic testable on a dev machine and portable across firmwares.

```
  ┌─────────────┐   bytes    ┌──────────────┐  command   ┌─────────────┐
  │ BLE notify  │ ─────────▶ │   phnix lib  │ ◀───────── │  app logic  │
  │  (adapter)  │            │ (this code)  │            │ (firmware)  │
  │ BLE write   │ ◀───────── │   codec      │ ─────────▶ │             │
  └─────────────┘   frame    └──────────────┘   state    └─────────────┘
```

## What's implemented

| Area | Header | Notes |
|------|--------|-------|
| Modbus CRC-16 | `crc.h` | compute / append / validate (Protocol.md §2.3) |
| Protocol constants | `protocol.h` | registers, blocks, masks, `Mode` enum |
| Command frames | `frame.h` | power, mode, target, generic write, read-all |
| Decoders | `decode.h` | TEMP1 + fault sentinels, error bitmask, block parse, `HeatPumpState` |

Not yet ported: the verify-and-retry **sync state machine**
(`sync_power`/`sync_mode`/`sync_target`) — the next increment.

## Build & test

Needs only a C++17 compiler and CMake:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests assert byte-for-byte against frames captured from the reference unit
(see `tests/`), so a green run means the builders reproduce the exact bytes the
AquaTemp app sends. The test harness (`tests/test_framework.h`) is
intentionally dependency-free; swap in GoogleTest/Catch2 later if wanted.

## License

GPL-3.0-or-later, like the rest of the project.
