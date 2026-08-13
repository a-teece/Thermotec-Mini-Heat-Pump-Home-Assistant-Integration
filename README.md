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
| Pool Water Temperature | Sensor | *(Optional)* Reading from an external DS18B20 probe on a customizable GPIO pin — see [Optional: external water temperature probe](#optional-external-water-temperature-probe) |
| Ambient Temperature | Sensor | Outside air temperature at the unit |
| Coil Temperature | Sensor | Refrigerant coil temperature |
| Exhaust Temperature | Sensor | Exhaust gas temperature |
| Current Target Temperature | Sensor | Target temperature currently set on the heat pump |
| Power State | Binary sensor | Whether the heat pump is powered on |
| Current Mode | Text sensor | Operating mode: Heat / Cool / Auto |
| Has Error | Binary sensor | True if any fault is active |
| Error Description | Text sensor | Human-readable description of active faults |
| Heat Pump BLE Connection | Binary sensor | Whether the ESP is connected to the heat pump |

The following **controls** also appear (published via MQTT discovery — no setup
required):

| Entity | Type | Description |
|--------|------|-------------|
| Power | Switch | Turn the heat pump on / off |
| Mode | Select | Heat / Cool / Auto |
| Target Temperature | Number (slider) | Desired water temperature (15–40 °C) |
| Prevent Deep Sleep | Switch | Keep the device awake for OTA / debugging |

A set of diagnostic entities is also available (in the collapsed Diagnostics section of the HA device page): compressor, circulate pump, high fan, and 4-way valve output states; individual temperature sensor fault indicators (P01/P02/P04/P05/P81); raw error code; the heat pump's controller firmware version (Master Program Version); the bridge's own firmware version (which release of this package the ESP is running); battery voltage and charge level; last-connected timestamp; and engineering set-points for heating, cooling, and auto modes.

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

Any ESP32 board with BLE support and ESP-IDF framework compatibility should work. The board and battery-monitoring specifics are exposed as **substitutions**, so you can retarget the firmware without editing the main YAML — just override them in your own device file (see [Installation](#installation)):

| Substitution | Default (FireBeetle 2) | What it sets |
|--------------|------------------------|--------------|
| `esp_board` | `dfrobot_firebeetle2_esp32c6` | The `board:` identifier in the `esp32:` block (e.g. `esp32dev`, `esp32-s3-devkitc-1`) |
| `esp_variant` | `ESP32C6` | The `variant:` in the `esp32:` block (e.g. `ESP32`, `ESP32S3`) |
| `battery_adc_pin` | `GPIO0` | The ADC pin wired to the battery sense line |
| `battery_voltage_multiplier` | `"2.0"` | Compensates for the board's voltage divider |
| `battery_voltage_empty` | `"3.0"` | Voltage that maps to 0% on the Battery Level sensor |
| `battery_voltage_full` | `"4.2"` | Voltage that maps to 100% on the Battery Level sensor |
| `water_temp_sensor_pin` | `GPIO2` | The GPIO pin wired to an optional external DS18B20 water temperature probe — see [Optional: external water temperature probe](#optional-external-water-temperature-probe) |

```yaml
# In your device YAML's substitutions: block
substitutions:
  esp_board: esp32-s3-devkitc-1
  esp_variant: ESP32S3
  battery_adc_pin: GPIO1
  battery_voltage_multiplier: "1.0"
```

> The `battery_voltage_multiplier` default of `2.0` assumes a 1:1 voltage divider, as found on the FireBeetle 2. If your board does not have this divider, set it to `1.0`; for any other divider ratio, set it to match. If your board is not battery-powered at all, you can simply ignore the battery voltage and battery level entities (hide or disable them in Home Assistant).

### Battery calibration

`Battery Level` is a linear map of `Battery Voltage` between `battery_voltage_empty` (0%) and `battery_voltage_full` (100%). LiPo cells don't discharge linearly, though — voltage stays fairly flat for most of the discharge, then sags hard under WiFi/BLE load once the cell nears empty (the "cliff"). A real capture from the reference unit: voltage declined gently from 3.76 V to 3.00 V over about two days, then collapsed to 2.61 V in around 12 minutes, and the device stayed unreachable for almost 15 hours before recovering.

If your device dies while `Battery Level` still reads well above 0%, your cell/board is collapsing at a higher voltage than the default assumes. Pull your device's `*_battery_voltage` entity history in Home Assistant (Developer Tools → History, or a longer-range query if you have a history/logbook integration) around the time it went dark, find the voltage where it collapsed, and set `battery_voltage_empty` just above that:

```yaml
substitutions:
  battery_voltage_empty: "3.3"   # example — use your own observed collapse voltage
```

This won't add runtime, but it makes 0% mean "about to die" instead of "already collapsing." For advance warning before the cliff — rather than waiting for 0% — build a Home Assistant automation that alerts on `Battery Voltage` or `Battery Level` crossing a threshold of your choosing, the same way you'd alert on a stale `Last Connected` (see the tip further down).

### Optional: external water temperature probe

The heat pump already reports **Inlet** and **Outlet Water Temperature** over BLE — but those are read at the heat pump's own plumbing, not the pool/spa itself. If you'd rather measure the actual pool water temperature (e.g. a probe dropped in the pool, or clipped to a return jet), wire a **DS18B20** (or other 1-Wire compatible) probe to a free GPIO pin and it will appear in HA as **Pool Water Temperature**.

This is entirely optional hardware — the firmware always includes the sensor, but if you don't wire a probe, the entity simply stays unavailable and nothing else depends on it.

Wiring (standard 1-Wire):

- DS18B20 **data** pin → your chosen GPIO (`water_temp_sensor_pin`, default `GPIO2`)
- A **~4.7 kΩ pull-up resistor** between the data line and 3.3 V
- DS18B20 **VDD** → 3.3 V, **GND** → GND

Override the pin in your device file if `GPIO2` conflicts with something else on your board (check your board's pinout first, and avoid any pin already used by another substitution, e.g. `battery_adc_pin`):

```yaml
substitutions:
  water_temp_sensor_pin: GPIO4
```

If you don't want the entity at all (no probe, ever), you can just ignore/hide it in HA — same as the battery sensors on a non-battery board.

---

## Installation

### 1. Find your heat pump's BLE MAC address

Open the AquaTemp app, navigate to your device's settings or info screen, and look for the device MAC address or Bluetooth ID. It will be in the format `AA:BB:CC:DD:EE:FF`. Note this down — you will need it in the next step.

If you cannot find the MAC address in the app, you can retrieve it from the ESPHome logs after flashing (see the note in step 4 below).

### 2. Add the firmware to ESPHome

There are two ways to do this. **The remote-package method is recommended** — it means you never copy/paste the firmware, and you receive fixes and new features just by recompiling.

#### Option A — Remote package (recommended)

Instead of copying the ~1,800-line YAML, you create a tiny device file that pulls the firmware straight from this repo as an [ESPHome package](https://esphome.io/components/packages.html). Copy [`example-device.yaml`](./example-device.yaml) into your ESPHome config directory (or paste it into the dashboard's **New device** editor) and rename it, e.g. `pool-heatpump.yaml`:

```yaml
substitutions:
  device_name: pool-heatpump-proxy
  friendly_name: Pool Heatpump Proxy

  # Required — see step 3 below. The upstream package can't use `!secret`
  # itself (some remote/CI build servers compile it standalone, with no
  # secrets.yaml of their own), so it only has placeholder defaults. This is
  # what wires in your real values.
  wifi_ssid: !secret wifi_ssid
  wifi_password: !secret wifi_password
  ap_fallback_password: !secret ap_fallback_password
  ota_password: !secret ota_password
  mqtt_broker: !secret mqtt_broker
  mqtt_username: !secret mqtt_username
  mqtt_password: !secret mqtt_password
  pool_heatpump_proxy_device_mac_address: !secret pool_heatpump_proxy_device_mac_address

  # Override any other substitutions here (HA entity IDs, board, sleep
  # schedule…). Anything you don't set uses the upstream default.

packages:
  thermotec_heatpump:
    url: https://github.com/a-teece/thermotec-mini-heat-pump-home-assistant-integration
    ref: main                       # latest stable — see "Versions" below
    files: [pool-heatpump-proxy.yaml]
    refresh: 1d
```

Your `secrets.yaml` (next step) is read from your own ESPHome install — secrets are **never** pulled from GitHub. The `!secret` lookups above live in *this* file, not the upstream package, precisely so that's true. See [`example-device.yaml`](./example-device.yaml) for the fully-commented version with every override listed.

> **If you already have a device file from before this substitutions block existed:** add the eight `wifi_ssid` … `pool_heatpump_proxy_device_mac_address` lines shown above to its `substitutions:` block and recompile. Without them, the upstream package silently falls back to its own placeholder values (e.g. a WiFi SSID of `CHANGE_ME`) and the device will fail to come back online.

> **Versions.** The `ref:` field selects which version of the firmware you compile against:
> - **`main`** — the latest **stable** release. Recommended for most users.
> - **`vX.Y.Z`** — a specific stable release, pinned so it never changes under you. Browse the repo's [Releases](https://github.com/a-teece/thermotec-mini-heat-pump-home-assistant-integration/releases) page and copy a tag.
> - **a feature branch name** — the latest **in-development** code (for testing a fix before it's released). Expect rough edges.
>
> ESPHome caches the fetched package and only re-checks GitHub every `refresh:` interval (`1d` above). To force an immediate re-pull after changing `ref:` or to grab the newest commit, run `esphome clean <your-file>.yaml` (or **Clean Build Files** in the dashboard) before recompiling.

#### Option B — Copy the YAML manually

If you prefer to vendor the firmware yourself (e.g. to make local edits), copy `pool-heatpump-proxy.yaml` into your ESPHome configuration directory, or paste its contents directly into the ESPHome dashboard editor. You will need to re-copy it to pick up upstream fixes. Since your copy becomes your own top-level config file, you can freely replace its placeholder `wifi_ssid: "CHANGE_ME"`-style substitution defaults with `!secret` lookups directly, instead of adding the separate substitutions block Option A uses.

### 3. Configure `secrets.yaml`

Add the following entries to your ESPHome `secrets.yaml` file (accessible from the ESPHome dashboard via the top-right menu). If you're using Option A (remote package), these are the values the `!secret` lookups in your device file's `substitutions:` block (step 2) resolve to:

```yaml
wifi_ssid: "your WiFi network name"
wifi_password: "your WiFi network password"
ap_fallback_password: "a strong password for the fallback hotspot"
ota_password: "a strong password for OTA updates"
pool_heatpump_proxy_device_mac_address: "AA:BB:CC:DD:EE:FF"
mqtt_broker: "homeassistant.local"        # or your broker's IP, e.g. 192.168.1.x
mqtt_username: "your MQTT broker username"
mqtt_password: "your MQTT broker password"
```

> **MQTT:** the device talks to Home Assistant over MQTT (e.g. the Home
> Assistant Mosquitto add-on — see [MQTT setup](#5-set-up-mqtt) below). All the
> connection details — `mqtt_broker`, `mqtt_username`, `mqtt_password` — live in
> `secrets.yaml`, so there's nothing to set in your device file. Override only
> the port via the `mqtt_port` substitution if your broker isn't on 1883.

### 4. Flash the firmware

> **Read the [Sleep mode and OTA](#sleep-mode-and-ota-updates) section before your second flash.** The device enters deep sleep shortly after boot, which will interrupt an OTA update if you do not prepare first.

Connect the FireBeetle 2 via USB and flash from the ESPHome dashboard or with `esphome run pool-heatpump-proxy.yaml`. The first flash must be over USB; subsequent flashes can be OTA.

After flashing, watch the device logs for the line:

```
[heatpump] Heat Pump power=… mode=…
```

This confirms the BLE poll completed successfully and entities will start populating in HA.

> **If you could not find the MAC address in step 1:** Leave `pool_heatpump_proxy_device_mac_address` as a placeholder and flash. The ESPHome BLE tracker will log nearby Bluetooth devices — look for an entry named `BLUENRG-XXXXXX`. The MAC address shown alongside it is your heat pump's. Update `secrets.yaml` and reflash.

### 5. Set up MQTT

The device talks to Home Assistant over **MQTT** — there are **no helpers or
template entities to create**. The control entities (Power, Mode, Target
Temperature, Prevent Deep Sleep) and all the sensors are published
automatically via MQTT discovery and appear on the device's page in HA.

You need:

1. **An MQTT broker.** The easiest option is the
   [Mosquitto broker add-on](https://github.com/home-assistant/addons/blob/master/mosquitto/DOCS.md)
   in Home Assistant. Install it, and create (or note) a username/password for
   the device to log in with.
2. **The MQTT integration enabled in Home Assistant**
   (**Settings → Devices & Services → Add Integration → MQTT**), pointed at the
   same broker. This is what receives the discovery messages.
3. **The three MQTT secrets** in your `secrets.yaml` (`mqtt_broker`,
   `mqtt_username`, `mqtt_password`) — see step 3 above.

After flashing, the heat pump appears as a new MQTT device in HA with all its
entities. Power, mode and target temperature are controllable directly, and —
because their command topics are retained — changes you make while the device
is asleep are applied on its next wake.

> Override the broker port only if it isn't the default 1883, via the
> `mqtt_port` substitution in your device file.

> ### Upgrading from v1.x (the native-API version)
>
> v2.0.0 switched from the ESPHome native API to MQTT. If you ran an earlier
> version:
>
> 1. Add the MQTT broker + the three `mqtt_*` secrets (above) and reflash.
> 2. The old `pool_heatpump_proxy_api_key` secret and the four `input_*`
>    helpers + `configuration.yaml` template entities are no longer used — you
>    can delete them.
> 3. In HA, **delete the old ESPHome (native API) integration entry** for this
>    device (**Settings → Devices & Services → ESPHome →** the device **→ ⋮ →
>    Delete**). The device's old entities are owned by that integration and
>    can't be removed individually while it exists; deleting the entry clears
>    them and removes the duplicates left behind by the new MQTT entities. (A
>    small amount of recent history for those entities is lost — expected, and
>    not worth preserving.)

---

## Physical setup

Place the ESP within BLE range of the heat pump — typically **2–5 metres** with a clear line of sight. BLE signal degrades quickly through thick walls or metal enclosures; if the connection is unreliable, move the ESP closer before troubleshooting further.

> **Only one BLE client can connect at a time.** If the AquaTemp app is open and connected to the heat pump, the ESP cannot establish a BLE link until the app disconnects. In a deep sleep deployment, the ESP will simply retry on the next wake. While the device is awake, it will reconnect automatically once the app releases the connection.

---

## Power options

### USB power (always-on)

If you have USB power near your heat pump, you can run the ESP permanently from USB:

- **Recommended:** set `enable_deep_sleep: "false"` in your device file's substitutions. This permanently disables the sleep cycle at build time — the ESP stays awake and polls every 30 seconds — so you don't depend on the runtime switch being on. (Alternatively, keep the **Prevent Deep Sleep** switch toggled **on** at all times for the same effect without recompiling.)
- The battery voltage and battery level sensors will report meaningless values (~5 V USB supply, not a LiPo cell). You can hide or disable these entities in HA, or remove the `battery_voltage` and `battery_level` sensors from the YAML.
- OTA updates work at any time — the ESP is always awake and reachable.

### LiPo battery with deep sleep

Connect a 3.7 V LiPo cell to the FireBeetle 2's battery connector:

- The ESP wakes on a day/night schedule, connects to the heat pump, syncs any pending control changes, polls sensor data, pushes everything to HA, then returns to deep sleep.
- Changes made to the controls (power, mode, temperature) while the ESP is asleep are retained by MQTT and applied on the next wake.
- While the ESP sleeps, its entities keep showing their **last reported value** rather than going `unavailable` — the values are retained on the MQTT broker. Use the `Last Connected` heartbeat (and `Battery Level` / `Heat Pump BLE Connection`) to tell whether the bridge is actually alive; a stale `Last Connected` is the signal that it has stopped waking. (You can build an HA automation that alerts when `Last Connected` is older than ~20 minutes.)
- Battery voltage and charge percentage are reported as diagnostic entities. See [Battery calibration](#battery-calibration) if the device dies before Battery Level reaches 0%.

The sleep durations and day window are all configurable in the `substitutions:` block at the top of `pool-heatpump-proxy.yaml` — see [Sleep schedule](#sleep-schedule) below.

---

## Sleep mode and OTA updates

### Sleep schedule

The sleep duration varies by time of day to balance responsiveness against battery life:

| Window | Default hours | Default duration | Substitution keys |
|--------|--------------|-----------------|-------------------|
| Day | 09:00 – 17:59 | 5 minutes | `deep_sleep_duration_day`, `day_start_hour`, `day_end_hour` |
| Night | 18:00 – 08:59 | 15 minutes | `deep_sleep_duration_night` |

All four values are `substitutions:` — set them in your own device file (remote-package method) or at the top of `pool-heatpump-proxy.yaml` (manual copy). Hours use 24-hour whole-hour values (`"9"`, `"18"`, etc.). Durations accept ESPHome time strings (`5min`, `30s`, `1h`).

If HA time has not yet synced when the device is ready to sleep, it falls back to the night duration.

The schedule has no effect when the **Prevent Deep Sleep** switch is on — the device stays awake and polls every 30 seconds regardless.

### How sleep works

In battery mode, the ESP runs a brief wake cycle each time it wakes from deep sleep:

1. Connect to WiFi and the MQTT broker
2. Receive the latest desired control state (the retained MQTT commands for
   power, mode and target temperature)
3. Connect to the heat pump via BLE
4. Poll sensor data and sync any pending control changes to the heat pump
5. Publish sensor readings to HA over MQTT
6. Enter deep sleep

The device is awake for roughly 20–30 seconds per cycle. If you need to interact with it — for an OTA update, live log monitoring, or fast feedback while configuring — you need to keep it awake first.

### Keeping the device awake

Toggle the **Prevent Deep Sleep** switch **on** in HA before the device's next wake. On that wake, the device will detect the toggle and stay awake indefinitely, polling every 30 seconds. Toggle it **off** to return to the normal sleep cycle; the device will enter deep sleep within a few seconds of detecting the change.

> **Always turn Prevent Deep Sleep on before attempting an OTA update.** If it's off when the flash begins, the device will go to sleep mid-transfer and the update will fail.

### Recovering a stuck device

A battery device is only awake briefly, so if it ever gets into a bad state the firmware provides two ways to get it back **without a USB/serial re-flash**:

- **Automatic (recovery mode).** If the device wakes but repeatedly fails to reach the heat pump over BLE — `safe_mode_failure_threshold` consecutive wakes (default **3**) — it stops sleeping and stays awake, so it remains reachable for OTA and diagnosis instead of disappearing into deep sleep. The **Recovery Mode** binary sensor turns on and the **Consecutive Wake Failures** diagnostic sensor shows the count. It resumes normal operation after a reset/power-cycle once the underlying cause (e.g. heat pump powered off, or the AquaTemp app holding the BLE link) is resolved.
- **Manual (double-reset).** Press the board's **reset button twice** while it's awake to force always-on mode — useful when the device can't reach WiFi/MQTT at all, so the HA **Prevent Deep Sleep** switch can't reach it. A normal single reset / power-cycle returns it to the normal sleep cycle.

Both are tunable via `substitutions:` in your device file:

| Substitution | Default | What it does |
|--------------|---------|--------------|
| `safe_mode_failure_threshold` | `"3"` | Consecutive failed wakes before the device stays awake in recovery mode. Raise to make it less eager; set very high to effectively disable. |
| `enable_double_reset_wake` | `"true"` | Enable double-tap-reset to force always-on. Set `"false"` to disable it. |

> **Tip:** alert on the **Recovery Mode** / **Consecutive Wake Failures** sensors (and a stale **Last Connected**) with an HA automation so you're notified when the bridge needs attention.

---

## Troubleshooting

**Entities read `unknown` after first flash**
Entities stay `unknown` until the first successful BLE poll completes. Watch the ESPHome logs for `[heatpump] Heat Pump power=… mode=…` — if that line appears, polling is working and entities will populate shortly. If it never appears, check that the BLE MAC address in `secrets.yaml` is correct and that the AquaTemp app is not holding the BLE connection.

**The ESP never connects to the heat pump**
Two common causes: the MAC address in `secrets.yaml` is wrong (check it matches the address from the AquaTemp app exactly, including colons), or the AquaTemp app currently has an active BLE connection to the heat pump. Close the app completely and wait for the ESP's next connection attempt.

**OTA flash hangs or the device is unreachable for OTA**
The device enters deep sleep roughly 20–30 seconds after booting. Turn the **Prevent Deep Sleep** switch **on** in HA, wait for the next wake cycle to pick it up, then attempt the OTA flash. See [Sleep mode and OTA](#sleep-mode-and-ota-updates).

**The device stopped waking / won't respond to the Prevent Deep Sleep switch**
If it's still on WiFi it should pick up the retained switch command on its next wake. If it can't reach WiFi/MQTT at all, the switch can't reach it either — press the board's **reset button twice** to force always-on mode, then flash or investigate over serial. Repeated failures to reach the heat pump also trip **Recovery Mode** automatically. See [Recovering a stuck device](#recovering-a-stuck-device).

**Target temperature or mode does not sync to the heat pump**
Changes are written only while the ESP is connected to the heat pump over BLE.
The device logs show `Mode set to …` / `Target set to …` when you change a
control, then a `Syncing …` line when the write occurs. If a control change in
HA produces nothing in the log, check the device is connected to MQTT (the
*Heat Pump BLE Connection* sensor and the broker logs) and that the control
entities appeared via MQTT discovery.

**A sensor appears in the Diagnostics section or shows as disabled**
HA hides diagnostic-category entities in a collapsed section on the device page, and some are disabled by default. Click **Show N disabled entities** at the bottom of the device page to reveal and enable them individually.

**Battery voltage reads ~5 V or an unexpected value**
The battery sensor is calibrated for a LiPo cell via the FireBeetle 2's onboard 1:1 voltage divider. If the board is powered via USB, the sensor reflects the USB supply voltage (~5 V) rather than a battery charge level. See [Power options](#power-options) for how to handle USB-powered deployments.

---

## Licence

GNU General Public Licence v3.0 — see [`LICENSE`](./LICENSE).
