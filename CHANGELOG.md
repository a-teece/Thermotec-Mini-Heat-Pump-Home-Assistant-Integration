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
- **Remote-package installation.** The firmware can now be consumed as an
  [ESPHome package](https://esphome.io/components/packages.html) straight from
  GitHub — no more copy/pasting the YAML. A new [`example-device.yaml`](./example-device.yaml)
  shows the minimal device file users put on their own ESPHome dashboard.
- **Board and battery configuration via substitutions.** `esp_board`,
  `esp_variant`, `battery_adc_pin`, and `battery_voltage_multiplier` are now
  substitutions (defaulting to the DFRobot FireBeetle 2 ESP32-C6), so the
  firmware can target other ESP32 boards without editing the upstream file.

### Changed
- Documentation reworked around the remote-package workflow (README install
  section, "Using a different ESP32 board", and version/update guidance).
