# PHNIX / AquaTemp Pool Heat Pump — ESPHome Home Assistant Integration

An ESPHome firmware for an ESP32 that bridges a PHNIX pool heat pump to Home Assistant over Bluetooth, with no cloud account, no manufacturer app, and no internet connection required.

The ESP polls the heat pump on a configurable sleep schedule (default: every 5 minutes during the day, every 15 minutes at night), or continuously while awake, and publishes water temperatures, operating mode, target temperature, error codes, and component state to Home Assistant. Power, mode, and target temperature can be controlled from HA even while the ESP is in deep sleep.

---

## Compatibility

This integration works with pool heat pumps controlled by the **AquaTemp** or **AquaTemp Smart** app via **Bluetooth**. PHNIX manufactures this hardware under many brand names; if your app is AquaTemp and you pair via Bluetooth (not WiFi), this project is very likely compatible.

Known compatible brands include: **Thermotec**, **PHNIX**, **AquaTemp** (direct), and various regional re-sellers. See [`PROTOCOL.md`](./PROTOCOL.md) for the full list.

> **WiFi / cloud control is not supported.** Some PHNIX units include an optional WiFi gateway module for cloud control. This project does not support that path. If you use the AquaTemp app over WiFi rather than Bluetooth, see [aquatemp](https://github.com/radical-squared/aquatemp) instead.

---

## What you get

```
                    BLE                    WiFi
Pool Heat Pump ◄──────────► ESP32-C6 ◄──────────► Home Assistant
(PHNIX / AquaTemp)          (FireBeetle 2)
```

> **The heat pump only allows one BLE client at a time.** If the AquaTemp app is connected to the heat pump, the ESP cannot connect, and vice versa. Disconnect the app before expecting the ESP to operate normally.

Once running, the following entities appear in Home Assistant:

| Entity | Type | Description |
|--------|------|-------------|
| Inlet Water Temperature | Sensor | Water temperature entering the heat pump |
| Outlet Water Temperature | Sensor | Water temperature leaving the heat pump |
| Ambient Temperature | Sensor | Outside air temperature at the unit |
| Coil Temperature | Sensor | Refrigerant coil temperature |
| Exhaust Temperature | Sensor | Exhaust gas temperature |
| Current Target Temperature | Sensor | Target temperature currently set on the heat pump |
| Power State | Binary sensor | Whether the heat pump is powered on |
| Current Mode | Text sensor | Operating mode: Heat / Cool / Auto |
| Has Error | Binary sensor | True if any fault is active |
| Error Description | Text sensor | Human-readable description of active faults |
| Heat Pump BLE Connection | Binary sensor | Whether the ESP is connected to the heat pump |

A set of diagnostic entities is also available (in the collapsed Diagnostics section of the HA device page): compressor, circulate pump, high fan, and 4-way valve output states; individual temperature sensor fault indicators (P01/P02/P04/P05/P81); raw error code; firmware version; battery voltage and charge level; last-connected timestamp; and engineering set-points for heating, cooling, and auto modes.

<!-- TODO: Add screenshot of Home Assistant device page here -->

---

## Hardware

### Recommended: DFRobot FireBeetle 2 ESP32-C6

The YAML is written and tested for the [DFRobot FireBeetle 2 ESP32-C6](https://www.dfrobot.com/product-2771.html). This board is recommended because:

- The ESP32-C6 has native BLE 5 support and runs well on the ESP-IDF framework
- The onboard LiPo charger and 1:1 voltage divider on GPIO0 make battery-powered deployment straightforward
- The small form factor fits in a weatherproof enclosure near the heat pump

For battery-powered deployments, a 3.7 V LiPo cell (1000–2000 mAh is a reasonable choice) gives roughly one to two weeks of runtime on the default day/night sleep schedule.

### Using a different ESP32 board

Any ESP32 board with BLE support and ESP-IDF framework compatibility should work. Three things need changing in `pool-heatpump-proxy.yaml`:

1. **`board:`** in the `esp32:` block — change to your board's ESPHome identifier (e.g. `esp32dev`, `esp32-s3-devkitc-1`)
2. **`variant:`** in the `esp32:` block — change to match your chip variant (e.g. `ESP32`, `ESP32S3`)
3. **`pin: GPIO0`** on the `battery_voltage` ADC sensor — change to a valid ADC pin for your board, or remove the `battery_voltage` and `battery_level` sensors entirely if you are not monitoring battery voltage

> The `multiply: 2.0` filter on the battery sensor assumes a 1:1 voltage divider, as found on the FireBeetle 2. If your board does not have this divider, the reading will be approximately half the actual cell voltage. Adjust the multiplier to match your board's divider ratio, or remove the sensor.

---

## Installation

### 1. Find your heat pump's BLE MAC address

Open the AquaTemp app, navigate to your device's settings or info screen, and look for the device MAC address or Bluetooth ID. It will be in the format `AA:BB:CC:DD:EE:FF`. Note this down — you will need it in the next step.

If you cannot find the MAC address in the app, you can retrieve it from the ESPHome logs after flashing (see the note in step 4 below).

### 2. Add the YAML to ESPHome

Copy `pool-heatpump-proxy.yaml` into your ESPHome configuration directory, or paste its contents directly into the ESPHome dashboard editor.

### 3. Configure `secrets.yaml`

Add the following entries to your ESPHome `secrets.yaml` file (accessible from the ESPHome dashboard via the top-right menu):

```yaml
wifi_ssid: "your WiFi network name"
wifi_password: "your WiFi network password"
ap_fallback_password: "a strong password for the fallback hotspot"
ota_password: "a strong password for OTA updates"
pool_heatpump_proxy_api_key: "a random 32-character hex string"
pool_heatpump_proxy_device_mac_address: "AA:BB:CC:DD:EE:FF"
```

To generate a random API key, run `openssl rand -hex 16` in a terminal, or use any random hex generator online.

### 4. Flash the firmware

> **Read the [Sleep mode and OTA](#sleep-mode-and-ota-updates) section before your second flash.** The device enters deep sleep shortly after boot, which will interrupt an OTA update if you do not prepare first.

Connect the FireBeetle 2 via USB and flash from the ESPHome dashboard or with `esphome run pool-heatpump-proxy.yaml`. The first flash must be over USB; subsequent flashes can be OTA.

After flashing, watch the device logs for the line:

```
[heatpump] Heat Pump power=… mode=…
```

This confirms the BLE poll completed successfully and entities will start populating in HA.

> **If you could not find the MAC address in step 1:** Leave `pool_heatpump_proxy_device_mac_address` as a placeholder and flash. The ESPHome BLE tracker will log nearby Bluetooth devices — look for an entry named `BLUENRG-XXXXXX`. The MAC address shown alongside it is your heat pump's. Update `secrets.yaml` and reflash.

### 5. Create Home Assistant helpers

The device reads its desired power state, mode, and target temperature from four HA input helpers. Create these under **Settings → Devices & Services → Helpers → Create helper**:

| Type | Suggested name | Entity ID | Notes |
|------|---------------|-----------|-------|
| Toggle | Pool Heater Prevent Deep Sleep | `input_boolean.pool_heater_prevent_deep_sleep` | Used for OTA and debugging — see [Sleep mode and OTA](#sleep-mode-and-ota-updates) |
| Toggle | Pool Heater Power | `input_boolean.pool_heater_power` | Desired on/off state |
| Number | Pool Target Temperature | `input_number.pool_target_temperature` | Min: 15, Max: 40, Step: 0.5, Unit: °C |
| Dropdown | Pool Heater Mode | `input_select.pool_heater_mode` | Options: Heat, Cool, Auto |

If you use different entity IDs, update the matching `substitutions:` entries at the top of `pool-heatpump-proxy.yaml` to match.

### 6. Create Home Assistant template wrapper entities

The helpers above store the desired state, but to make them controllable from the heat pump's device page in HA, create template entities that wrap each helper and write back to it.

Add the following to your `configuration.yaml` (or a file you include from it), then restart Home Assistant:

```yaml
template:
  - switch:
      - name: "Pool Heater Power"
        unique_id: pool_heater_power_template
        state: "{{ is_state('input_boolean.pool_heater_power', 'on') }}"
        turn_on:
          action: input_boolean.turn_on
          target:
            entity_id: input_boolean.pool_heater_power
        turn_off:
          action: input_boolean.turn_off
          target:
            entity_id: input_boolean.pool_heater_power

  - number:
      - name: "Pool Target Temperature"
        unique_id: pool_target_temperature_template
        state: "{{ states('input_number.pool_target_temperature') | float(26) }}"
        min: 15
        max: 40
        step: 0.5
        unit_of_measurement: "°C"
        set_value:
          action: input_number.set_value
          target:
            entity_id: input_number.pool_target_temperature
          data:
            value: "{{ value }}"

  - select:
      - name: "Pool Heater Mode"
        unique_id: pool_heater_mode_template
        state: "{{ states('input_select.pool_heater_mode') }}"
        options:
          - Heat
          - Cool
          - Auto
        select_option:
          action: input_select.select_option
          target:
            entity_id: input_select.pool_heater_mode
          data:
            option: "{{ option }}"
```

> **Important:** The `option: "{{ option }}"` line in the select's `select_option` action uses a template variable that HA injects when the user picks an option. If you create this entity via the HA visual editor rather than `configuration.yaml`, you must switch to **YAML mode** for the `select_option` action — the visual editor cannot express template variables in action data fields.

After restarting HA, navigate to the heat pump's device page and use **Add to device** to associate the new template entities with the ESPHome device.

---

## Physical setup

Place the ESP within BLE range of the heat pump — typically **2–5 metres** with a clear line of sight. BLE signal degrades quickly through thick walls or metal enclosures; if the connection is unreliable, move the ESP closer before troubleshooting further.

> **Only one BLE client can connect at a time.** If the AquaTemp app is open and connected to the heat pump, the ESP cannot establish a BLE link until the app disconnects. In a deep sleep deployment, the ESP will simply retry on the next wake. While the device is awake, it will reconnect automatically once the app releases the connection.

---

## Power options

### USB power (always-on)

If you have USB power near your heat pump, you can run the ESP permanently from USB:

- Keep the **Pool Heater Prevent Deep Sleep** helper toggled **on** at all times. This disables the sleep cycle; the ESP polls the heat pump every 30 seconds instead.
- The battery voltage and battery level sensors will report meaningless values (~5 V USB supply, not a LiPo cell). You can hide or disable these entities in HA, or remove the `battery_voltage` and `battery_level` sensors from the YAML.
- OTA updates work at any time — the ESP is always awake and reachable.

### LiPo battery with deep sleep

Connect a 3.7 V LiPo cell to the FireBeetle 2's battery connector:

- The ESP wakes on a day/night schedule, connects to the heat pump, syncs any pending helper changes, polls sensor data, pushes everything to HA, then returns to deep sleep.
- Changes made to HA helpers (power, mode, temperature) while the ESP is asleep are queued in HA and applied on the next wake.
- Battery voltage and charge percentage are reported as diagnostic entities.

The sleep durations and day window are all configurable in the `substitutions:` block at the top of `pool-heatpump-proxy.yaml` — see [Sleep schedule](#sleep-schedule) below.

---

## Sleep mode and OTA updates

### Sleep schedule

The sleep duration varies by time of day to balance responsiveness against battery life:

| Window | Default hours | Default duration | Substitution keys |
|--------|--------------|-----------------|-------------------|
| Day | 09:00 – 17:59 | 5 minutes | `deep_sleep_duration_day`, `day_start_hour`, `day_end_hour` |
| Night | 18:00 – 08:59 | 15 minutes | `deep_sleep_duration_night` |

All four values are set in the `substitutions:` block at the top of `pool-heatpump-proxy.yaml`. Hours use 24-hour whole-hour values (`"9"`, `"18"`, etc.). Durations accept ESPHome time strings (`5min`, `30s`, `1h`).

If HA time has not yet synced when the device is ready to sleep, it falls back to the night duration.

The schedule has no effect when **Pool Heater Prevent Deep Sleep** is on — the device stays awake and polls every 30 seconds regardless.

### How sleep works

In battery mode, the ESP runs a brief wake cycle each time it wakes from deep sleep:

1. Connect to WiFi and the HA API
2. Read the current state of all HA helpers
3. Connect to the heat pump via BLE
4. Poll sensor data and sync any pending helper changes to the heat pump
5. Push sensor readings to HA
6. Enter deep sleep

The device is awake for roughly 20–30 seconds per cycle. If you need to interact with it — for an OTA update, live log monitoring, or fast feedback while configuring — you need to keep it awake first.

### Keeping the device awake

Toggle the **Pool Heater Prevent Deep Sleep** helper **on** in HA before the device's next wake. On that wake, the device will detect the toggle and stay awake indefinitely, polling every 30 seconds. Toggle it **off** to return to the normal sleep cycle; the device will enter deep sleep within a few seconds of detecting the change.

> **Always turn Prevent Deep Sleep on before attempting an OTA update.** If the helper is off when the flash begins, the device will go to sleep mid-transfer and the update will fail.

---

## Troubleshooting

**Entities read `unknown` after first flash**
Entities stay `unknown` until the first successful BLE poll completes. Watch the ESPHome logs for `[heatpump] Heat Pump power=… mode=…` — if that line appears, polling is working and entities will populate shortly. If it never appears, check that the BLE MAC address in `secrets.yaml` is correct and that the AquaTemp app is not holding the BLE connection.

**The ESP never connects to the heat pump**
Two common causes: the MAC address in `secrets.yaml` is wrong (check it matches the address from the AquaTemp app exactly, including colons), or the AquaTemp app currently has an active BLE connection to the heat pump. Close the app completely and wait for the ESP's next connection attempt.

**OTA flash hangs or the device is unreachable for OTA**
The device enters deep sleep roughly 20–30 seconds after booting. Turn the **Pool Heater Prevent Deep Sleep** helper **on** in HA, wait for the next wake cycle to pick it up, then attempt the OTA flash. See [Sleep mode and OTA](#sleep-mode-and-ota-updates).

**Target temperature or mode does not sync to the heat pump**
Check that the four HA helpers were created with the exact entity IDs listed in step 5, or that the `substitutions:` block at the top of the YAML has been updated to match your actual entity IDs. The device logs will show `Syncing target:` or `Syncing mode:` when a sync write occurs — if these lines never appear, the helpers are not being read correctly.

**A sensor appears in the Diagnostics section or shows as disabled**
HA hides diagnostic-category entities in a collapsed section on the device page, and some are disabled by default. Click **Show N disabled entities** at the bottom of the device page to reveal and enable them individually.

**Battery voltage reads ~5 V or an unexpected value**
The battery sensor is calibrated for a LiPo cell via the FireBeetle 2's onboard 1:1 voltage divider. If the board is powered via USB, the sensor reflects the USB supply voltage (~5 V) rather than a battery charge level. See [Power options](#power-options) for how to handle USB-powered deployments.

---

## Licence

GNU General Public Licence v3.0 — see [`LICENSE`](./LICENSE).
