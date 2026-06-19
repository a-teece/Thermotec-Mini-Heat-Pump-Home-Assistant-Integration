// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame.h"

#include <cmath>

#include "crc.h"

namespace phnix {

std::vector<uint8_t> build_write_registers(uint16_t start_addr,
                                           const std::vector<uint16_t> &values,
                                           uint8_t slave) {
  std::vector<uint8_t> f;
  const uint16_t qty = static_cast<uint16_t>(values.size());
  f.reserve(7 + values.size() * 2 + 2);
  f.push_back(slave);
  f.push_back(kFnWriteMultiple);
  f.push_back(static_cast<uint8_t>(start_addr >> 8));
  f.push_back(static_cast<uint8_t>(start_addr & 0xFF));
  f.push_back(static_cast<uint8_t>(qty >> 8));
  f.push_back(static_cast<uint8_t>(qty & 0xFF));
  f.push_back(static_cast<uint8_t>(qty * 2));  // byte count
  for (uint16_t v : values) {
    f.push_back(static_cast<uint8_t>(v >> 8));
    f.push_back(static_cast<uint8_t>(v & 0xFF));
  }
  append_crc(f);
  return f;
}

std::vector<uint8_t> build_read_all(uint8_t slave) {
  std::vector<uint8_t> f = {
      slave,
      kFnReadHolding,
      static_cast<uint8_t>(kReadAllStart >> 8),
      static_cast<uint8_t>(kReadAllStart & 0xFF),
      static_cast<uint8_t>(kReadAllQuantity >> 8),
      static_cast<uint8_t>(kReadAllQuantity & 0xFF),
  };
  append_crc(f);
  return f;
}

std::vector<uint8_t> build_power(bool on, uint8_t slave) {
  return build_write_registers(kRegPower, {static_cast<uint16_t>(on ? 1 : 0)},
                               slave);
}

uint8_t mode_byte(Mode mode) { return static_cast<uint8_t>(mode); }

uint16_t mode_companion(Mode mode) {
  switch (mode) {
    case Mode::Cool: return 0x0000;
    case Mode::Heat: return 0x0088;
    case Mode::Auto: return 0x008C;
  }
  return 0x0000;
}

std::vector<uint8_t> build_mode(Mode mode, uint8_t slave) {
  return build_write_registers(
      kRegMode, {static_cast<uint16_t>(mode_byte(mode)), mode_companion(mode)},
      slave);
}

uint16_t target_register_for(Mode mode) {
  switch (mode) {
    case Mode::Cool: return kRegCoolTarget;
    case Mode::Heat: return kRegHeatTarget;
    case Mode::Auto: return kRegAutoTarget;
  }
  return kRegHeatTarget;
}

std::vector<uint8_t> build_set_target(Mode mode, float celsius, uint8_t slave) {
  const long x10 = std::lround(celsius * 10.0f);
  return build_write_registers(target_register_for(mode),
                               {static_cast<uint16_t>(x10)}, slave);
}

}  // namespace phnix
