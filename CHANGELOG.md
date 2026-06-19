# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Release model

- **`main`** is always the latest **stable** firmware. Most users should track
  it (`ref: main` in their device package).
- **Tags** (`vX.Y.Z`) are immutable snapshots of `main`, for users who want to
  pin an exact version. They appear on the repo's
  [Releases](https://github.com/a-teece/thermotec-mini-heat-pump-home-assistant-integration/releases)
  page.
- **Feature branches** carry **in-development** work and may be unstable. Point
  a package at a branch name to test a change before it's released.

Version numbers follow Semantic Versioning:
- **MAJOR** — a change that requires the user to act (e.g. a renamed
  substitution, a new required secret or HA helper).
- **MINOR** — new functionality that is backwards-compatible.
- **PATCH** — backwards-compatible bug fixes only.

## [Unreleased]

### Added

- **`lib/phnix` — a platform-agnostic, host-unit-tested protocol library.**
  First step of extracting the heat-pump Modbus/BLE logic out of the ESPHome
  YAML lambdas into real C++ that can be tested on a dev machine (TDD) and
  reused by a future native firmware. Pure codec (no BLE/ESPHome/Arduino
  dependency): CRC-16, command-frame builders (power, mode, target, read-all,
  generic write), and decoders (TEMP1 + fault sentinels, error bitmask, block
  parsing, aggregated `HeatPumpState`). 31 unit tests assert byte-for-byte
  against frames captured from the reference unit — including six **golden
  block notifications captured live via shadow mode** — added to CI as a
  separate fast job. No change to the shipped firmware behaviour yet.
- **Shadow-mode verification harness** (dev branch only). Compiles `lib/phnix`
  into the firmware and cross-checks it against the existing parser/frame-builder
  lambdas on live hardware, logging any disagreement under the `phnix_shadow`
  tag. Observation only — no runtime behaviour change. See
  [`SHADOW.md`](./SHADOW.md). Removed at cutover, before any release to `main`.
  **Validated on the reference unit:** every power/mode/target frame matched the
  library byte-for-byte, all four block types decoded correctly (incl. an E03
  fault and firmware identity 1.2/494), zero mismatches.

### Fixed
- **`Protocol.md`**: corrected the Heat/Cool labels on the mode-change sample
  frames in Appendix A. The byte sequences and CRCs were always correct; only
  the comment labels were swapped. They now match the authoritative mapping
  used elsewhere in the document and in the firmware (Cool=0, Heat=1, Auto=2).

## [v1.0.0] - 2026-06-08

First public release. An ESPHome firmware for an ESP32 that bridges a PHNIX /
AquaTemp (Thermotec) pool heat pump to Home Assistant over Bluetooth — no
cloud account, no manufacturer app, and no internet connection required. The
heat pump's Modbus-over-BLE protocol was reverse-engineered from the AquaTemp
app and is documented in [`PROTOCOL.md`](./PROTOCOL.md).

### Added

**Monitoring**
- Water, air and refrigerant temperatures: inlet, outlet, ambient, coil and
  exhaust.
- Current target temperature, power state, and operating mode (Heat / Cool /
  Auto) as Home Assistant entities.
- Diagnostics: compressor, circulate-pump, high-fan and 4-way-valve output
  states; defrost type, heat-pump mode type and circulating-pump mode;
  firmware version; and the three engineering set-points (heat/cool/auto).
- Battery monitoring (voltage and a derived charge-level percentage).
- `Last Connected` heartbeat showing when the device last woke and synced.

**Control** (via Home Assistant helpers, so settings can be changed while the
device is asleep and are applied on the next wake)
- Power on/off, operating mode, and target temperature.
- Mode-aware target temperature — the single target slider is routed to the
  correct per-mode register (Heat / Cool / Auto) and mirrored back.

**Error reporting**
- The `0x07F2` error register decoded into a raw `Error Code` and a
  human-readable `Error Description` (concatenates all active E-codes).
- Five temperature-sensor fault flags (P01/P02/P04/P05/P81), and a single
  `Has Error` problem flag that ORs the error register with all fault sensors.

**Power & battery life**
- Deep-sleep cycle with a configurable day/night schedule (separate day and
  night durations, plus a configurable day-window start/end hour).
- A `Prevent Deep Sleep` helper that keeps the device awake for OTA updates
  and debugging.

**Distribution & tooling**
- **Remote-package installation** (#6) — the firmware can be consumed as an
  [ESPHome package](https://esphome.io/components/packages.html) straight from
  GitHub, so users get fixes by recompiling instead of copy/pasting the YAML.
  A new [`example-device.yaml`](./example-device.yaml) shows the minimal
  device file, and a tag-based release model (`main` = stable, feature
  branches = dev, `vX.Y.Z` = pinnable snapshots) is documented.
- **Board and battery configuration via substitutions** (#6) — `esp_board`,
  `esp_variant`, `battery_adc_pin` and `battery_voltage_multiplier` (defaulting
  to the DFRobot FireBeetle 2 ESP32-C6) let the firmware target other ESP32
  boards without editing the upstream file.
- **Continuous integration** (#5) — every change is validated with a full
  `esphome compile` of the device config, type-checking the inline C++ lambdas.
- End-user setup guide (README) and protocol reference (`PROTOCOL.md`).

### Changed
- Per-installation parameters (Wi-Fi, OTA/API credentials, device MAC) moved
  into `secrets.yaml`, and tunables (entity IDs, sleep schedule, board) into
  the `substitutions:` block, so a consumer configures one place without
  touching the firmware body.

### Fixed
- **Reliable setting writes** (#1) — power, mode and target-temperature
  changes are now written, verified against a re-poll, and retried (up to 3×),
  fixing changes that occasionally didn't apply (power was worst-hit). The
  reconcile on connect runs mode → target → power in order so writes don't
  collide, and re-syncs the target into the new mode's register on a mode
  change.
- **`Last Connected` no longer flaps to `unknown`** (#2) — the timestamp is
  retained across deep sleep and republished the instant Home Assistant
  reconnects, removing the per-wake `unavailable → unknown` churn from the
  logbook (and fixing it being stuck at `unknown` in prevent-deep-sleep mode).
- **BLE sensors no longer read `unknown` after every wake** (#3, #4) — all
  BLE-derived sensors (temperatures, power, mode, outputs, fault flags,
  set-points) snapshot their last value before sleep and restore it on boot,
  so Home Assistant shows the last-known reading immediately on reconnect
  rather than `unknown` for the ~30 s until the first poll.
- **Heat/Cool mode inversion** corrected.
