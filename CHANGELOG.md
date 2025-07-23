<!-- Copyright (c) 2023 Golioth, Inc. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [template_v2.7.2] - 2025-06-03

### Changed

- Upgrade to Golioth Firmware SDK at
  [`v0.18.1`](https://github.com/golioth/golioth-firmware-sdk/releases/tag/v0.18.1)
- Removed `CONFIG_GOLIOTH_SAMPLE_SETTINGS` from prj.conf. With Golioth
  Firmware SDK v0.18.1 this symbol is now automatically selected.

## [template_v2.7.1] - 2025-05-12

### Fixed

- Remove whitespace from project name in west.yml to resolve error when
  calling west init.

## [template_v2.7.0] - 2025-05-09

### Changed

- Upgrade to Golioth Firmware SDK at
  [`v0.18.0`](https://github.com/golioth/golioth-firmware-sdk/releases/tag/v0.18.0)
- Kconfig: move symbols common to nRF91 to overlay in new `socs` folder.
- Use CONFIG_SOC_SERIES_NRF91X over CONFIG_SOC_NRF9160 to better support
  nRF91 series (eg: nRF9151).
- Battery monitor is now a separate Zephyr module.
- README: moved from RST to MD.

### Added

- Kconfig symbol for CoAP path length based on SDK guidance.

### Fixed

- Corrected CHANGELOG year for template_v2.6.0 release.
- Gracefully handle devicetree with no `golioth_led` alias.

### Removed

- Remove support for Aludel Mini hardware which is now end-of-life.

## [template_v2.6.0] - 2025-01-24

### Changed

- Upgrade to Golioth Firmware SDK at
  [`v0.17.0`](https://github.com/golioth/golioth-firmware-sdk/releases/tag/v0.17.0)

## [template_v2.5.0] - 2024-11-26

### Changed

- Upgrade to Golioth Firmware SDK at
  [`v0.16.0`](https://github.com/golioth/golioth-firmware-sdk/releases/tag/v0.16.0) which is based
  on NCS v2.8

## [template_v2.4.1] - 2024-09-11

### Changed

- Use `--sysbuild` for all boards.
- Upgrade `golioth/golioth-zephyr-boards` dependency to
  [`v2.0.1`](https://github.com/golioth/golioth-zephyr-boards/releases/tag/v2.0.1).
- Use static partition table.

## [template_v2.4.0] - 2024-09-05

### Changed

- Upgrade to Golioth Firmware SDK at
  [`v0.15.0`](https://github.com/golioth/golioth-firmware-sdk/releases/tag/v0.15.0)
- Add sample `pipeline` to configure routing stream data (See [Data
  Routing](https://docs.golioth.io/data-routing) documentation)
- Upgrade `golioth/golioth-zephyr-boards` dependency to
  [`v2.0.0`](https://github.com/golioth/golioth-zephyr-boards/releases/tag/v2.0.0).
    - Update board names to match this change
- Upgrade `golioth/zephyr-network-info` dependency to
  [`v1.2.0`](https://github.com/golioth/zephyr-network-info/releases/tag/v1.2.0)
- Upgrade `golioth/libostentus` dependency to
  [`v2.0.0`](https://github.com/golioth/libostentus/releases/tag/v2.0.0)
- Use VERSION file to indicate version number of firmware being built. This number is used by
  MCUboot to verify the correct version is running after an OTA firmware update.

## [template_v2.3.0] - 2024-06-24

### Changed

- Upgrade to Golioth Firmware SDK at v0.14.0
- Use CBOR instead of JSON when sending stream data.
- Upgrade `golioth/golioth-zephyr-boards` dependency to
  [`v1.2.0`](https://github.com/golioth/golioth-zephyr-boards/tree/v1.2.0).

## [template_v2.2.1] - 2024-05-31

### Changed

- Upgrade to Golioth Firmware SDK at v0.13.1

## [template_v2.2.0] - 2024-05-28

### Changed

- Upgrade to Golioth Firmware SDK at v0.13.0
- Change `golioth_lightdb_observe_async()` call to include content type as a parameter

## [template_v2.1.0] - 2024-05-06

### Added

- Pipeline example
- Add support for Aludel Elixir

### Changed

- Merge changes from
  [`golioth/reference-design-template@template_v2.4.1`](https://github.com/golioth/reference-design-template/tree/template_v2.4.1).
- Update board names for Zephyr hardware model v2
- Use `VERSION` file instead of `prj.conf` to set firmware version

## [1.3.0] - 2024-05-17

### Changed
- Merge changes from
  [`golioth/reference-design-template@template_v2.1.0`](https://github.com/golioth/reference-design-template/tree/template_v2.1.0).

### Fixed

- Changing "reset_cumulative" to true will now immediatey update cumulative state on the cloud
  instead of waiting for the next sensor reading to do so.
- Sensor readings now update Ostententus faceplate (if one is connected).

## [1.2.0] - 2023-07-18

### Changed
- Implented a driver for the ina260 sensor
- Merged `template_v1.1.0` from the Reference Design Template (NCS 2.4.1 Zephyr v3.3.99-ncs1-1)
- Converted from qcbor to zcbor to match Golioth Zephyr SDK v0.7.x change

### Fixed
- Added missing ina260 node for channel 1 the nRF9160dk overlay file

## [1.1.4] - 2023-07-15

### Added

- Initial release
- Up-to-date with `template_v1.0.1` of the Reference Design Template
