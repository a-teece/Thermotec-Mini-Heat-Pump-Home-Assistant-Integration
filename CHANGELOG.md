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

- Internal tidy-up (no user impact): retire the snapshot-to-globals machinery
  now that MQTT's retained state topics keep Home Assistant populated across
  deep sleep, and rework the deep-sleep enable so a fresh install boots awake.

## [v2.0.0] - 2026-06-22

**Breaking change.** Home Assistant integration is now over **MQTT** instead of
the ESPHome native API. You must have an MQTT broker (e.g. the Home Assistant
Mosquitto add-on), and the four manually-created HA helpers + template entities
are no longer used — the device publishes its own entities via MQTT discovery.
See **"Upgrading from v1.x"** in the README for the (short) migration.

### Changed

- **MQTT replaces the native API as the sole Home Assistant transport.** The
  device connects to an MQTT broker with Home Assistant discovery, tuned for
  battery/deep-sleep operation: `reboot_timeout: 0s` (a transient broker outage
  never resets the device) and `discovery_retain` (HA re-discovers entities
  after a restart without waiting for a wake). `time:` switched from the API to
  `sntp`. Connection details live in `secrets.yaml` (`mqtt_broker`,
  `mqtt_username`, `mqtt_password`) so deployment needs no device-file edits;
  `mqtt_port` is an optional substitution (default `1883`).
- **The control surface is now device-native.** A **Power** switch, **Mode**
  select, **Target Temperature** number and **Prevent Deep Sleep** switch are
  published via MQTT discovery and appear in HA automatically — no `input_*`
  helpers or `configuration.yaml` template entities to create. Their command
  topics are **retained** (`command_retain`), so a change made while the device
  sleeps is delivered on its next connect (the retained command is the
  desired-state store that the helpers used to be); each also restores its last
  value from flash on boot. The verified `sync_*` reconcile scripts are
  unchanged.
- **Target Temperature** renders as a slider **and** an input box (`mode: auto`)
  on the HA device page.

### Added

- **`enable_deep_sleep` substitution (default `"true"`)** — set to `"false"` to
  permanently disable deep sleep at build time for mains/USB-powered installs.
  The device then stays awake and polls every 30 s, with no dependence on the
  runtime *Prevent Deep Sleep* switch (the build-time setting also overrides
  it, so it can't force a sleep). The default preserves battery deep-sleep
  behaviour.

### Fixed

- **MQTT connection no longer drops/reconnects in a loop** ("cleared Warning
  flag" in the log) — WiFi power-save is disabled (`power_save_mode: none`),
  which the broker keep-alive needs.
- **Simultaneous control changes no longer collide.** Power, mode and target
  writes are serialised and applied **power → mode → target**, so two changes
  made together can't both grab the single BLE write characteristic and drop a
  write. Power-first ensures the pump is on before mode/target are pushed.
- **Power toggle could write the stale (pre-toggle) state** — the optimistic
  switch fired its action before committing the new value; a brief settle now
  lets the new state land first.
- **Longer confirm settle when verifying a power change**, to accommodate the
  heat pump's slow readback of the power register.

### Removed

- **The `pool_heatpump_proxy_api_key` secret and the manual HA helper/template
  setup are no longer needed.** (The API key was for native-API encryption;
  the helpers and `configuration.yaml` template entities are replaced by the
  MQTT-discovered controls.)
- The four `home_assistant_*_name` substitutions (which named the old HA
  helpers) have been removed — the device owns these entities now, so there's
  nothing to point at.

## [v1.1.0] - 2026-06-21

### Added

- **`phnix` protocol codec, extracted into a host-unit-tested ESPHome external
  component** (`components/phnix/`). The heat-pump Modbus/BLE protocol logic
  (CRC-16, command-frame builders, notification decoder, error decoding) now
  lives in real, testable C++ — 32 unit tests run on CI, asserting byte-for-byte
  against frames captured from the reference unit (including six golden block
  notifications captured live from hardware). The same files are portable to a
  future native firmware. Pulled into the firmware from GitHub via
  `external_components:` — no local files for consumers.

### Changed

- **The firmware now uses the `phnix` codec instead of inline lambdas** for the
  Modbus work: the notification parser, the power/mode/target/poll frame
  builders, and the error-code text are all routed through the library. Behaviour
  is unchanged (validated on the reference unit via a shadow-comparison pass:
  every frame matched byte-for-byte and every block decoded correctly before the
  switch), with one robustness improvement — incoming notifications are now
  **CRC-validated** before parsing, so a corrupt frame is dropped rather than
  mis-decoded.
- New `phnix_components_ref` substitution (default `main`) selects the git ref
  the component is fetched from; point it at a feature branch to test
  in-development protocol code.

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
