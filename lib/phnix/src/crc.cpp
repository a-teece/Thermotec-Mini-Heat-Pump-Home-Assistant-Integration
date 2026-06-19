// SPDX-License-Identifier: GPL-3.0-or-later
#include "phnix/crc.h"

namespace phnix {

uint16_t modbus_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001) : (crc >> 1);
    }
  }
  return crc;
}

void append_crc(std::vector<uint8_t> &frame) {
  uint16_t crc = modbus_crc16(frame.data(), frame.size());
  frame.push_back(static_cast<uint8_t>(crc & 0xFF));
  frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
}

bool check_crc(const uint8_t *data, size_t len) {
  if (len < 3) return false;
  uint16_t crc = modbus_crc16(data, len - 2);
  uint16_t got = static_cast<uint16_t>(data[len - 2]) |
                 static_cast<uint16_t>(data[len - 1] << 8);
  return crc == got;
}

}  // namespace phnix
