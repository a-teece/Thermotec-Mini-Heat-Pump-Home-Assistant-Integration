# Shadow-mode testing (`phnix` library vs the firmware lambdas)

This is a **temporary, dev-branch-only** harness for validating the
[`lib/phnix`](./lib/phnix) protocol library against real hardware, *before*
the firmware is cut over to use it. It changes **no runtime behaviour** — the
existing hand-written lambdas still send every frame and publish every sensor.
The library runs alongside them over the same live data and logs any
disagreement.

> ⚠️ **Do not merge this to `main` as-is.** The `esphome: includes:` block and
> the `phnix_shadow::` calls reference files by path relative to the repo, which
> remote-package consumers don't have. This branch is for testers who have the
> repo checked out. Cutover (replacing the lambdas with library calls) and
> removing this scaffolding is a later, separate change.

## What it does

- **Frame builders** — at each point the firmware builds a write frame (power,
  mode, target), it also calls `phnix::build_*` and logs `MATCH` / `MISMATCH`
  with both byte strings. The firmware's bytes are still what gets sent.
- **Notification parser** — every BLE notification is also fed to
  `phnix::parse_block` / `apply_block`; the decoded view and the **raw hex** of
  each block are logged. A block-shaped frame the library *rejects* is logged
  as a WARN (a real red flag).

All logging uses the `phnix_shadow` tag.

## How to run it

1. **Point your device at this branch.** In your `example-device.yaml`:

   ```yaml
   packages:
     pool_heatpump_proxy: github://a-teece/thermotec-mini-heat-pump-home-assistant-integration/pool-heatpump-proxy.yaml@claude/mqtt-vs-home-assistant-bcfshv
   ```

   (Because shadow mode uses `includes:` with repo-relative paths, building from
   a **local checkout** of this branch is the reliable path — a pure remote
   package won't fetch the `lib/`/`esphome_shadow/` files.)

2. **No logger changes are needed.** All shadow output (`MATCH`, `MISMATCH`,
   the decoded view, and the raw block hex) is logged at INFO/WARN under the
   `phnix_shadow` tag, so it shows at the firmware's default `level: INFO`.

   > Note: don't set `logs: { phnix_shadow: DEBUG }` — ESPHome rejects a per-tag
   > level *more* verbose than the global `level:` (which is INFO here), and
   > there's nothing logged below INFO anyway.

3. **Keep the device awake** so it polls continuously: turn on the
   `Prevent Deep Sleep` helper (`input_boolean.pool_heater_prevent_deep_sleep`).
   Disconnect the AquaTemp app first — only one BLE client may connect at a time.

4. **Flash** (USB for the first flash on this branch, OTA after) and watch the
   logs (`esphome logs ...` or the dashboard).

## What to look for

- `frame[power] MATCH`, `frame[mode] MATCH`, `frame[target] MATCH` after you
  toggle power / change mode / move the target slider in HA. Exercise each at
  least once, in each mode for the target.
- `block 0x07FE raw: 63 10 07 FE ...` etc. — one line per block, four blocks
  per poll. The decoded summary line should agree with the HA entities.
- **Any `MISMATCH` or `REJECTED` line** is a bug in the library (or a protocol
  case the captures missed) — copy it into an issue.

## Capturing a golden test vector

The decode unit tests currently use *synthesised* block frames. To harden them
with a real capture:

1. From the logs, copy a full `block 0x____ raw: …` line for each of the four
   blocks (ideally one showing a fault and one showing normal temps).
2. Add them to `lib/phnix/tests/test_decode.cpp` as `make_block`-style raw
   byte arrays and assert the decoded `HeatPumpState`. (Paste the raw bytes
   directly rather than rebuilding them, so the test pins the real wire format
   including the real slave address and CRC.)

## When shadow mode is clean

Once `MATCH` holds across many poll cycles and a power/mode/target change each,
and a real golden frame is committed, the next PR cuts the lambdas over to call
`phnix::build_*` / `phnix::apply_block` directly and deletes this file, the
`esphome_shadow/` glue, and the `includes:` block.
