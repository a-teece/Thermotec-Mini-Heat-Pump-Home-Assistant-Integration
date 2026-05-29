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
  the primary artifact. Everything the device does lives here.
- `PROTOCOL.md` — comprehensive BLE/Modbus protocol reference.
- `LICENSE` — GNU GPL v3.
- `CLAUDE.md` — this file.

## Hardware

- **Controller:** DFRobot FireBeetle 2 ESP32-C6 (`dfrobot_firebeetle2_esp32c6`),
  ESP-IDF framework.
- **Power:** LiPo cell (3.0–4.2 V). The board has a 1:1 voltage divider on
  GPIO0; battery voltage is read via ADC with a `multiply: 2.0` filter.
  Expected battery life is roughly a week to ten days on the 5-minute deep
  sleep cycle.
- **Heat pump:** Thermotec (PHNIX OEM). BLE module is BlueNRG-based,
  advertised as `BLUENRG-XXXXXX`. Firmware on the reference unit: Master
  program version `1.2`, Main control software code `494`.
- **BLE constraint:** only one client may connect at a time. The AquaTemp
  app and the ESP will fight over the connection — disconnect one before
  using the other.

## Architecture & key design decisions

These decisions are deliberate. Preserve them unless there's a specific
reason to revisit, and update this section if they change.

1. **Home Assistant helpers are the source of truth.** The desired power,
   mode, and target temperature live in HA `input_*` helpers, not on the
   ESP. This is what lets the user change settings while the ESP is in
   deep sleep — the new value is read on the next wake and synced to the
   heat pump. The ESP mirrors each helper and reconciles on every BLE
   connect.

2. **Deep sleep is entered explicitly, never via `run_duration`.** The
   `on_boot` handler runs the full wake cycle (wait for API → read helpers →
   connect BLE → poll → sync → settle) and only then calls
   `deep_sleep.enter`. This guarantees we never sleep mid-BLE-write. A
   `prevent_deep_sleep` helper keeps the device awake for OTA/debugging.

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

## Home Assistant setup

The device depends on four HA helpers (created under Settings → Devices &
Services → Helpers). Entity IDs are configurable via `substitutions:` at
the top of the YAML.

| Helper | Type | Default entity ID |
|--------|------|-------------------|
| Prevent deep sleep | Toggle (`input_boolean`) | `input_boolean.pool_heater_prevent_deep_sleep` |
| Power | Toggle (`input_boolean`) | `input_boolean.pool_heater_power` |
| Target temperature | Number (`input_number`, 15–40, step 0.5) | `input_number.pool_target_temperature` |
| Mode | Dropdown (`input_select`, Heat/Cool/Auto) | `input_select.pool_heater_mode` |

To make these controllable from the device page in HA, each is wrapped in
a **template entity** (switch / number / select) that mirrors the helper
state and writes back to it. The template select's `select_option` action
must pass `option: "{{ option }}"` — the visual editor can't express this,
so that action needs to be edited in YAML mode. Device association is
available for UI-created template helpers but not for YAML-defined ones.

## Build / flash workflow

- Edit `pool-heatpump-proxy.yaml`, then build/flash via the ESPHome
  dashboard (or `esphome run`). First flash is over USB; subsequent ones
  can be OTA.
- A `secrets.yaml` is required. Needed keys (see the comment block at the
  top of the YAML for the full list with examples):
  `wifi_ssid`, `wifi_password`, `ap_fallback_password`, `ota_password`,
  `pool_heatpump_proxy_api_key`, `pool_heatpump_proxy_device_mac_address`.
- After flashing, confirm a clean compile and watch the device logs for the
  `[heatpump] Heat Pump power=… mode=…` line — that confirms the poll
  parser is running and entities will populate. Entities read `unknown`
  until the first successful poll.
- **Gotcha:** a sensor that has a `name:` but doesn't appear in HA usually
  means the firmware didn't actually flash, or the entity is in the
  collapsed Diagnostic section / disabled. Check the build succeeded first.

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

- [ ] **Mode-change target re-sync** — after a mode change, re-poll then
  push the desired target into the new mode's register so the target
  carries across modes instead of leaving the slider out of sync.
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
