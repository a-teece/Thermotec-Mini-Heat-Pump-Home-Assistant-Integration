# CLAUDE.md

Context and working notes for this repository. Read this first.

## What this project is

An ESPHome-based BLE bridge that connects a **Thermotec** pool heat pump
(PHNIX OEM hardware, controlled by the **AquaTemp** mobile app) to **Home
Assistant**, without the manufacturer's cloud or app.

The heat pump speaks **Modbus RTU framed over BLE GATT**. This protocol was
reverse-engineered from BTSnoop captures of the AquaTemp Android app. The
firmware in this repo runs on an ESP32 placed within BLE range of the heat
pump; it polls the heat pump, publishes sensor data to HA, and writes
control commands (power, mode, target temperature) back to the heat pump.

**The protocol itself is documented in [`PROTOCOL.md`](./PROTOCOL.md).**
That file is the source of truth for the BLE transport, Modbus framing,
register map, error encoding, and sample frames. Do not duplicate that
content here — read it when you need protocol detail, and update it when
new protocol facts are discovered.

## Repository layout

- `pool-heatpump-proxy.yaml` — the ESPHome device configuration. This is
  the primary artifact. Everything the device does lives here. It is also
  **consumed by others as a remote ESPHome package** (see "Distribution &
  releases" below), so treat it as a published interface: the
  `substitutions:` block at the top is the documented override surface, and
  renaming/removing a substitution is a breaking change.
- `example-device.yaml` — the minimal device file a *consumer* puts on their
  own ESPHome dashboard. It pulls `pool-heatpump-proxy.yaml` from GitHub via
  `packages:` and overrides substitutions/secrets locally. Keep its commented
  override list in sync with the substitutions block in the main YAML.
- `components/phnix/` — the **PHNIX protocol codec**, a small platform-agnostic
  C++17 library (CRC-16, Modbus frame builders, notification decoder, error
  decoding) packaged as a library-only ESPHome external component. The YAML
  lambdas call into it (`phnix::build_*`, `phnix::parse_block`,
  `phnix::error_text`) instead of hand-rolling Modbus. Consumers fetch it from
  GitHub via `external_components:` (no local files); the ref is the
  `phnix_components_ref` substitution (default `main`, keep in sync with the
  package `ref:`). The codec has **no BLE/ESPHome dependency** — that's what
  makes it host-unit-testable and portable to a future native firmware. Keep
  `Protocol.md` and this codec in sync when protocol facts change.
- `tests/phnix/` — host unit tests (CMake + a dependency-free harness) for the
  codec, compiling the same `components/phnix/*.cpp`. Run with
  `cmake -S tests/phnix -B tests/phnix/build && cmake --build tests/phnix/build
  && ctest --test-dir tests/phnix/build`. A separate CI job runs them. Includes
  golden frames captured live from the reference unit. Kept *outside*
  `components/phnix/` so ESPHome never compiles the test harness into firmware.
- `CHANGELOG.md` — user-facing change log (Keep a Changelog format). Update
  the `[Unreleased]` section as part of any change that affects users.
- `PROTOCOL.md` — comprehensive BLE/Modbus protocol reference.
- `LICENSE` — GNU GPL v3.
- `CLAUDE.md` — this file.

## Hardware

- **Controller:** DFRobot FireBeetle 2 ESP32-C6 (`dfrobot_firebeetle2_esp32c6`),
  ESP-IDF framework.
- **Power:** LiPo cell (3.0–4.2 V). The board has a 1:1 voltage divider on
  GPIO0; battery voltage is read via ADC with a `multiply: 2.0` filter.
  Expected battery life is roughly a week to ten days on the 5-minute deep
  sleep cycle. The board (`esp_board`/`esp_variant`) and battery wiring
  (`battery_adc_pin`/`battery_voltage_multiplier`) are **substitutions** so
  the firmware can target other ESP32 boards without a fork; the defaults are
  the FireBeetle 2 values above.
- **Heat pump:** Thermotec (PHNIX OEM). BLE module is BlueNRG-based,
  advertised as `BLUENRG-XXXXXX`. Firmware on the reference unit: Master
  program version `1.2`, Main control software code `494`.
- **BLE constraint:** only one client may connect at a time. The AquaTemp
  app and the ESP will fight over the connection — disconnect one before
  using the other.

## Architecture & key design decisions

These decisions are deliberate. Preserve them unless there's a specific
reason to revisit, and update this section if they change.

1. **The retained MQTT command topic is the source of truth for desired
   state.** Desired power, mode, and target temperature live in the
   **retained command topics** of the device's own MQTT-discovered controls
   (`command_retain: true`), not on the ESP and (since v2.0.0) no longer in HA
   `input_*` helpers. This is what lets the user change settings while the ESP
   is in deep sleep — the broker re-delivers the retained command on the next
   connect, and the ESP reconciles it to the heat pump on every BLE connect.
   Each control also restores its last value from flash on boot.

2. **Deep sleep is entered explicitly, never via `run_duration`.** The
   `on_boot` handler runs the full wake cycle (wait for MQTT broker → receive
   retained commands → connect BLE → poll → sync → settle) and only then calls
   `deep_sleep.enter`. This guarantees we never sleep mid-BLE-write. A
   `prevent_deep_sleep` switch keeps the device awake for OTA/debugging.

3. **Target temperature is mode-aware.** Each operating mode stores its
   target in a different register (Heat `0x0416`, Cool `0x041B`, Auto
   `0x042C` — see `PROTOCOL.md`). The single HA target slider is routed to
   the correct register based on the current mode, and `current_target_temp`
   mirrors whichever register matches the active mode.

4. **Mode writes replicate the app exactly.** Mode changes write two
   registers (`0x03F4` + `0x03F5`). The second register's meaning is
   unknown, but we send the same values the app does. Do not "simplify"
   this to a single-register write — it's untested and may be rejected.

5. **Duplicate-notification handling.** The BLE module re-sends
   notifications. Sensors dedupe with `delta: 0.05` (temperatures) and
   `delayed_on_off: 1s` (output binary sensors).

6. **Error reporting.** Faults surface in HA two ways: the `0x07F2` error
   register is decoded into `Error Code` (raw int) and `Error Description`
   (human-readable, concatenates all active E-codes), and temperature-sensor
   failures are detected from their sentinel values and exposed as five
   `*_temp_fault` binary sensors (P01/P02/P04/P05/P81). `Has Error` ORs the
   error register with all five fault sensors into a single problem flag.
   Temperature sensors also publish `NAN` on probe failure so they go
   *unavailable* in HA — a useful visual signal on its own.

7. **Writes are verified, not fire-and-forget.** Each setting has a
   verify-and-retry script — `sync_power_state`, `sync_target_temp`,
   `sync_mode` — that writes, re-polls, re-checks the corresponding `current_*`
   sensor, and retries (capped at 3) until the heat pump confirms the requested
   value. They're used from both the immediate helper `on_value`/`on_state`
   paths and the `on_connect` reconcile. Power was the worst-hit symptom (most
   frequently toggled, binary, and previously the last of three back-to-back
   un-spaced writes) but the flaw was shared by all three. Two distinct
   failure modes are addressed:
   - **Dropped write.** A single un-acked BLE write is occasionally lost. The
     loop re-checks after polling and writes again.
   - **No reading to compare against (the reason it often didn't self-heal).**
     The old reconcile guarded on `current_*.has_state()` and silently did
     *nothing* when that sensor had no state. `current_*` is populated only by
     a poll's notify fragments, and a deep-sleep wake polls **once**; if that
     fragment is dropped or arrives after the reconcile's fixed wait, the
     change was never even attempted — and with one read attempt per wake it
     did not reliably recover on its own (which is why prevent-deep-sleep
     mode, polling every 30s, felt reliable). The loop now writes **even with
     no reading** and re-polls up to 3× per wake, so a slow/lost fragment no
     longer aborts the sync.
   - In `on_connect` the three syncs run **serialised via `script.wait`**
     (mode → target → power), so no two writes collide, and mode-first means
     the target lands in the now-current mode's register — carrying the
     setpoint across a mode change.
   - `on_boot` no longer sleeps after a fixed delay — it waits on the
     `reconcile_done` global that `on_connect` sets once all three syncs
     (including retries) finish, so retries get the time they need while the
     device still sleeps promptly to save battery.

8. **`Last Connected` heartbeat is retained across deep sleep.** The timestamp
   is stamped from the `time:` component's `on_time_sync` (fires on every
   boot/reconnect, in *both* sleep and prevent-sleep mode — the prevent-sleep
   branch of `on_boot` never reaches the sleep path, so it would otherwise sit
   at `unknown` the whole debug session). The value lives in the
   `last_connected_str` **restored global** (`char[24]`, not `std::string` —
   only trivially-copyable types restore) — now the *only* template sensor
   backed by a restored global (see decision 9). `on_boot` Phase 0 republishes
   it *synchronously, before the MQTT-connect wait*, and
   `enter_deep_sleep_scheduled` calls `global_preferences->sync()` to force the
   value (and the retained control-entity preferences) to flash before sleeping,
   because ESPHome's periodic preferences flush is far longer than a wake (≈1
   small flash write per wake; NVS wear-levels this — not a practical lifetime
   concern on esp-idf). The cross-sleep persistence is mostly belt-and-braces
   now: the MQTT broker also retains the last published value, so HA shows it
   regardless — but the restored global keeps the device-side sensor populated
   through a bare power-cycle/OTA too.

9. **BLE-derived sensor values persist across deep sleep via MQTT's retained
   state topics — not a snapshot-to-globals mechanism.** ESPHome publishes each
   sensor's MQTT state topic with `retain` (the default), and the `mqtt:` block
   sets `discovery_retain: true`, so the broker holds the last value for every
   entity and re-delivers it to Home Assistant on (re)subscribe. While the
   device sleeps, HA therefore shows the last reported reading; combined with
   decision 10 (availability disabled), entities never flip to `unavailable` or
   `unknown` mid-cycle. The first poll ~30 s after each wake overwrites the
   values, or the `delta: 0.05` / `delayed_on_off: 1s` filters suppress it as a
   no-op if unchanged.
   - **Historical note (do not re-add):** v1.x and the early MQTT firmware
     carried every BLE-derived reading in a matching `restore_value: yes`
     global (`g_*`/`snapshot_valid`/`current_mode_str`/…), snapshotted in
     `enter_deep_sleep_scheduled` and republished in `on_boot` Phase 0, to dodge
     the `unknown` the **native API** recorded in the gap between connect and
     the first poll. MQTT retain makes that entire machinery redundant, so it
     was removed (see CHANGELOG v2.0.2). Don't reintroduce it: the one
     underlying ESPHome fact worth keeping is that `template`
     sensor/binary_sensor/text_sensor platforms do **not** support
     `restore_value` (only `globals` and a few others do) — which is why
     `last_connected_str` (decision 8) still needs a restored global, but the
     bulk sensor state does not now that the broker retains it.

10. **MQTT birth/will (availability) messages are disabled.** ESPHome's default
    behaviour publishes `online`/`offline` to `<prefix>/status`, and MQTT
    discovery wires *every* entity to that availability topic — so each deep
    sleep makes the broker mark the device `offline` and HA flips the whole
    device to `unavailable`, despite the retained state values (decision 9)
    still being present. A battery deep-sleep device is asleep ~85% of each
    cycle, so this made the dashboard read `unavailable` almost whenever the
    user looked (see CHANGELOG v2.0.1). Setting `birth_message:` and
    `will_message:` empty in the `mqtt:` block removes the availability topic,
    so HA shows the last retained value across sleep. **`shutdown_message:`
    must be emptied as well** — it's a third, independent message that also
    defaults to a *retained* `offline` on `<prefix>/status` and is **not**
    disabled by disabling birth/will; deep-sleep entry is a clean shutdown, so
    leaving it enabled re-published a retained `offline` on every sleep, which
    permanently stranded any entity still availability-wired by pre-v2.0.1
    discovery — with birth disabled, nothing publishes `online` again (see
    CHANGELOG Unreleased). **Trade-off:** HA can no
    longer auto-detect a genuinely dead bridge — liveness is observed instead
    via `Last Connected` / `Battery Level` / `Heat Pump BLE Connection` (alert
    on a stale `Last Connected`). Do not re-enable birth/will without restoring
    that distinction another way.

## Home Assistant setup

**No manual HA helpers or template entities.** Since the MQTT transition
(v2.0.0) the device owns its own control entities and publishes them via
**MQTT discovery**, so they appear under the device automatically — there is
nothing to create in Settings → Helpers and no `configuration.yaml` editing.

| Control | Entity type (MQTT-discovered) | Notes |
|---------|-------------------------------|-------|
| Prevent deep sleep | `switch` | Keeps the device awake for OTA / debugging |
| Power | `switch` | On/off |
| Target temperature | `number` (15–40, step 0.5) | Renders as slider **and** input box (`mode: auto`) |
| Mode | `select` (Heat/Cool/Auto) | Mode-aware target routing — see decision 3 |

The desired state used to live in HA `input_*` helpers; it now lives in the
**retained MQTT command topics** (`command_retain: true` on each control), so a
change made while the device sleeps is re-delivered on its next connect and
reconciled to the heat pump (decision 1). Each control also restores its last
value from flash on boot. The only HA prerequisite is an **MQTT broker** (e.g.
the Mosquitto add-on) — the `mqtt_broker` / `mqtt_username` / `mqtt_password`
secrets point the device at it.

## Build / flash workflow

- Edit `pool-heatpump-proxy.yaml`, then build/flash via the ESPHome
  dashboard (or `esphome run`). First flash is over USB; subsequent ones
  can be OTA.
- A `secrets.yaml` is required. Needed keys (see the comment block at the
  top of the YAML for the full list with examples):
  `wifi_ssid`, `wifi_password`, `ap_fallback_password`, `ota_password`,
  `mqtt_broker`, `mqtt_username`, `mqtt_password`,
  `pool_heatpump_proxy_device_mac_address`. (The old
  `pool_heatpump_proxy_api_key` was for native-API encryption and was removed
  in v2.0.0 — the transport is MQTT now.)
- After flashing, confirm a clean compile and watch the device logs for the
  `[heatpump] Heat Pump power=… mode=…` line — that confirms the poll
  parser is running and entities will populate. Entities read `unknown`
  until the first successful poll.
- **Gotcha:** a sensor that has a `name:` but doesn't appear in HA usually
  means the firmware didn't actually flash, or the entity is in the
  collapsed Diagnostic section / disabled. Check the build succeeded first.

## Distribution & releases

The firmware is published for others to consume as a **remote ESPHome
package** — they reference `pool-heatpump-proxy.yaml` from GitHub via
`packages:` (see `example-device.yaml`) rather than copying it. Consequences
to keep in mind when editing:

- **The substitutions block is a public API.** Renaming or removing a
  substitution breaks every downstream device file. Add new substitutions
  with sensible defaults instead, and mirror them into `example-device.yaml`'s
  commented override list and the README.
- **`!secret` resolves on the consumer's machine**, not from GitHub — never
  commit a `secrets.yaml`, and keep the required-secrets list in the YAML
  header, README, and this file consistent.

**Release model (decided with the maintainer):**

- **`main` is always stable.** It is what most users track (`ref: main`).
  Don't merge half-finished work into it.
- **Feature branches are the dev channel.** Testers point a package at a
  branch name to get in-development code before it lands on `main`.
- **Tags (`vX.Y.Z`) are immutable stable snapshots** of `main`, for users who
  want to pin. Semantic versioning: MAJOR = a user must act (renamed
  substitution, new required secret/helper), MINOR = backwards-compatible
  feature, PATCH = backwards-compatible fix.

**Cutting a release** (maintainer, after a change is merged to `main`):

1. Move the `[Unreleased]` notes in `CHANGELOG.md` under a new `## [vX.Y.Z]`
   heading with the date, and set the `firmware_version` substitution in
   `pool-heatpump-proxy.yaml` to the same `X.Y.Z` (it feeds the `Bridge
   Firmware Version` diagnostic sensor and the `project:` boot-log stamp);
   commit both to `main`.
2. Tag and push: `git tag -a vX.Y.Z -m "vX.Y.Z" && git push origin vX.Y.Z`.
3. Create a GitHub Release from the tag (paste the changelog section) so it
   shows on the Releases page that the README links to.

No tags exist yet; the first one should be `v1.0.0` cut from `main` once this
package work merges.

## Testing faults safely

Error decoding was verified by deliberately triggering faults. The only
safe method used was **disconnecting sensor/switch cables** (dry contacts) —
never anything involving refrigerant pressure. Confirmed mappings live in
`PROTOCOL.md`. The easy repeatable test is disconnecting the flow switch
(triggers E03); the firmware's `has_error` / `error_description` should
react. **Do not** deliberately trigger high-pressure (E01) or exhaust
over-temperature (P82) faults.

## Outstanding work / TODO

In rough priority order:

- [x] **Mode-change target re-sync** — done. `sync_mode` is followed by
  `sync_target_temp` (in both the `desired_mode` `on_value` path and the
  `on_connect` reconcile, which now runs mode-first), so the desired target
  is pushed into the new mode's register instead of leaving the slider out
  of sync.
- [ ] Consider exposing read-only H-tab engineering settings (comp stop
  temp, exhaust limit) as diagnostics if useful.

## Known protocol unknowns

Tracked in full in `PROTOCOL.md` (§ open questions). Summary:

- Register `0x03F5` (the mode "companion" register) — purpose unknown; we
  copy the app's values.
- Error register bits beyond E01/E02/E03 — E06, E08, P82, TP, DF not yet
  captured. The `error_description` lambda logs unknown bits so they can be
  mapped when first observed.
- Pump settings P02/P04 (`0x040E`, `0x0410`) — addresses likely but values
  seen don't match the app display.

When any of these get resolved, update **both** `PROTOCOL.md` and the
relevant lambda in the YAML.

## Conventions

- This is a GPL v3 project; keep it that way and preserve the license
  header expectations for any substantial new files.
- Branding: **PHNIX** is the OEM, **Thermotec** is the verified brand on
  the reference unit, **AquaTemp** is the *app* (used across all rebrands) —
  never call the heat pump "AquaTemp".
- Modbus CRC16 throughout: poly `0xA001`, init `0xFFFF`, low byte first.
- When adding a sensor that parses BLE data, it's published from the single
  `notify_handler` lambda, keyed on the 4 block addresses. Keep new parsing
  in the correct `block == 0x…` branch.
