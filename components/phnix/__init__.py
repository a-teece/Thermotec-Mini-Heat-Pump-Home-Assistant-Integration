# SPDX-License-Identifier: GPL-3.0-or-later
"""phnix — PHNIX heat pump protocol codec, packaged as an ESPHome component.

This is a *library-only* component: it defines no entities and takes no
configuration. Adding an (empty) ``phnix:`` block to a device config makes
ESPHome compile the protocol codec (crc / frame / decode) into the firmware and
exposes its headers to the device's lambdas via the includes added below.

Delivery: consumers pull this from GitHub via ``external_components:`` — no
local files required (see the repo README and example-device.yaml). The exact
same ``.h``/``.cpp`` files are host-unit-tested under ``tests/phnix/`` with
CMake, independently of ESPHome.
"""

import esphome.codegen as cg
import esphome.config_validation as cv

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    # Expose the codec to lambdas in the device YAML. The component's source
    # files are compiled automatically once the component is used.
    cg.add_global(cg.RawExpression('#include "esphome/components/phnix/frame.h"'))
    cg.add_global(cg.RawExpression('#include "esphome/components/phnix/decode.h"'))
