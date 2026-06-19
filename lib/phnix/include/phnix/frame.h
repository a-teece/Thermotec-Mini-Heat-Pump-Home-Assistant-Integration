// SPDX-License-Identifier: GPL-3.0-or-later
//
// Command-frame builders. Each returns a complete, CRC-terminated Modbus RTU
// frame ready to hand to a transport (BLE write characteristic). These
// functions perform no I/O — that is the firmware adapter's job.
#pragma once

#include <cstdint>
#include <vector>

#include "phnix/protocol.h"

namespace phnix {

// Generic Write Multiple Registers (function 0x10) frame, CRC appended.
std::vector<uint8_t> build_write_registers(uint16_t start_addr,
                                           const std::vector<uint16_t> &values,
                                           uint8_t slave = kDefaultSlaveAddress);

// The magic "read all status" poll command (elicits four block notifications).
std::vector<uint8_t> build_read_all(uint8_t slave = kDefaultSlaveAddress);

// Power on/off (register 0x03F3).
std::vector<uint8_t> build_power(bool on, uint8_t slave = kDefaultSlaveAddress);

// Mode change: the two-register write (0x03F4 + companion 0x03F5) that mirrors
// the app exactly. Single-register mode writes are untested — see Protocol.md
// §5.2.
std::vector<uint8_t> build_mode(Mode mode, uint8_t slave = kDefaultSlaveAddress);

// Target temperature, routed to the register for `mode`. The value is rounded
// to the protocol's 0.1 C resolution. No range clamping is applied: the
// per-mode min/max limits are read from the device, so clamping belongs to the
// caller that holds that state.
std::vector<uint8_t> build_set_target(Mode mode, float celsius,
                                      uint8_t slave = kDefaultSlaveAddress);

// Helpers (also useful to callers building their own frames).
uint16_t target_register_for(Mode mode);
uint8_t mode_byte(Mode mode);
uint16_t mode_companion(Mode mode);

}  // namespace phnix
