<!-- Copyright (c) 2023 Golioth, Inc. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
