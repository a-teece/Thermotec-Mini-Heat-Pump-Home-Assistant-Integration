# `phnix` — PHNIX heat pump protocol codec (ESPHome external component)

A small, **platform-agnostic** C++17 codec for the Modbus-RTU-over-BLE protocol
spoken by PHNIX pool heat pumps (the hardware behind the AquaTemp app and
Thermotec-branded units), packaged as an ESPHome external component.

The protocol itself is documented in [`../../Protocol.md`](../../Protocol.md),
which is the source of truth. This component is an implementation of it.

## How it's used

The firmware pulls this in from GitHub at compile time — no local files for
consumers:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/a-teece/thermotec-mini-heat-pump-home-assistant-integration
      ref: main
    components: [phnix]
phnix:        # library-only component, no configuration
```

`__init__.py` then exposes the codec headers to the device's lambdas, which call
`phnix::build_*` / `phnix::parse_block` directly (see `pool-heatpump-proxy.yaml`).

### It is *not* a Bluetooth component

There is **no BLE/GATT logic in here** — by design. It is a pure **codec**:
bytes in → typed state out; a command in → frame bytes out. The BLE transport
(connect, subscribe to notify, write, reconnection, the one-client constraint)
stays in the firmware and feeds this codec. That separation is what makes the
protocol logic host-unit-testable and portable to a future native firmware.

```
  ┌─────────────┐   bytes    ┌──────────────┐  command   ┌─────────────┐
  │ BLE notify  │ ─────────▶ │  phnix codec │ ◀───────── │  app logic  │
  │ (firmware)  │            │ (this code)  │            │ (firmware)  │
  │ BLE write   │ ◀───────── │              │ ─────────▶ │             │
  └─────────────┘   frame    └──────────────┘   state    └─────────────┘
```

## What's implemented

| Area | Header | Notes |
|------|--------|-------|
| Modbus CRC-16 | `crc.h` | compute / append / validate (Protocol.md §2.3) |
| Protocol constants | `protocol.h` | registers, blocks, masks, `Mode` enum |
| Command frames | `frame.h` | power, mode, target, generic write, read-all |
| Decoders | `decode.h` | TEMP1 + fault sentinels, error bitmask + verbose text, block parse, `HeatPumpState` |

Not yet ported: the verify-and-retry **sync state machine**
(`sync_power`/`sync_mode`/`sync_target`), and decoding of the
firmware's extra diagnostic registers (limits, defrost type, pump mode) — those
are still read directly in the YAML.

## Build & test (host)

The codec is plain C++17 with no ESP dependency, so it builds and tests on any
dev machine / CI. The tests live outside this component dir (so ESPHome never
compiles them into the firmware) and compile these very same files:

```sh
cmake -S tests/phnix -B tests/phnix/build      # run from the repo root
cmake --build tests/phnix/build
ctest --test-dir tests/phnix/build --output-on-failure
```

Tests assert byte-for-byte against frames captured from the reference unit, so a
green run means the builders reproduce the exact bytes the AquaTemp app sends.
`tests/phnix/test_golden.cpp` goes further: it pins six complete block
notifications captured *live* from the heat pump, so the decoder is verified
against the real wire format, not just synthesised frames. The test harness
(`tests/phnix/test_framework.h`) is intentionally dependency-free.

## License

GPL-3.0-or-later, like the rest of the project.
