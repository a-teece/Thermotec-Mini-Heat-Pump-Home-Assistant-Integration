// SPDX-License-Identifier: GPL-3.0-or-later
//
// Modbus RTU CRC-16 as used by PHNIX heat pumps.
// Polynomial 0xA001 (reflected 0x8005), init 0xFFFF, transmitted low byte
// first. See Protocol.md §2.3.
//
// This header is part of the platform-agnostic `phnix` protocol library: it
// has no BLE, ESPHome or Arduino dependencies and is host-unit-testable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace phnix {

// Compute the Modbus RTU CRC-16 over `len` bytes of `data`.
uint16_t modbus_crc16(const uint8_t *data, size_t len);

inline uint16_t modbus_crc16(const std::vector<uint8_t> &v) {
  return modbus_crc16(v.data(), v.size());
}

// Append the CRC to `frame` in Modbus transmit order: low byte then high byte.
void append_crc(std::vector<uint8_t> &frame);

// Validate a complete frame (payload followed by its 2 CRC bytes). Returns
// false for frames shorter than 3 bytes.
bool check_crc(const uint8_t *data, size_t len);

inline bool check_crc(const std::vector<uint8_t> &v) {
  return check_crc(v.data(), v.size());
}

}  // namespace phnix
