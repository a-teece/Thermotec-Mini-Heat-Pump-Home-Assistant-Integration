// SPDX-License-Identifier: GPL-3.0-or-later
#include "decode.h"

#include <cstdio>

#include "crc.h"

namespace phnix {

// --- Temperature -----------------------------------------------------------

bool is_temp_fault(uint16_t raw) {
  return (raw & kTempFaultMaskAnd) == kTempFaultMaskValue;
}

bool is_ambient_fault(uint16_t raw) {
  return raw == kAmbientFaultValue || is_temp_fault(raw);
}

std::optional<float> temp1_to_celsius(uint16_t raw) {
  if (is_temp_fault(raw)) return std::nullopt;
  return static_cast<int16_t>(raw) / 10.0f;
}

std::optional<float> ambient_to_celsius(uint16_t raw) {
  if (is_ambient_fault(raw)) return std::nullopt;
  return static_cast<int16_t>(raw) / 10.0f;
}

// --- Error register --------------------------------------------------------

std::vector<std::string> active_error_codes(uint16_t error_reg) {
  std::vector<std::string> codes;
  for (int bit = 0; bit < 16; bit++) {
    if (!(error_reg & (1u << bit))) continue;
    switch (bit) {
      case 0: codes.push_back("E01"); break;
      case 1: codes.push_back("E02"); break;
      case 2: codes.push_back("E03"); break;
      default: codes.push_back("Unknown(bit" + std::to_string(bit) + ")");
    }
  }
  return codes;
}

std::string error_description(uint16_t error_reg) {
  std::string out;
  for (const auto &c : active_error_codes(error_reg)) {
    if (!out.empty()) out += ", ";
    out += c;
  }
  return out;
}

std::string error_text(uint16_t error_reg) {
  std::string out;
  auto append = [&](const std::string &s) {
    if (!out.empty()) out += "; ";
    out += s;
  };
  if (error_reg & kErrE01) append("E01 High pressure protection");
  if (error_reg & kErrE02) append("E02 Low pressure protection");
  if (error_reg & kErrE03) append("E03 No water flow");
  uint16_t unknown =
      error_reg & ~static_cast<uint16_t>(kErrE01 | kErrE02 | kErrE03);
  if (unknown) {
    char buf[40];
    snprintf(buf, sizeof buf, "E-unknown bits 0x%04X", unknown);
    append(buf);
  }
  return out;
}

// --- Block parsing ---------------------------------------------------------

static constexpr size_t kHeaderLen = 7;  // slave,fn,addrHi,addrLo,qHi,qLo,bc

uint16_t BlockView::reg(size_t i) const {
  if (!data || i >= reg_count) return 0;
  return static_cast<uint16_t>(data[i * 2] << 8) | data[i * 2 + 1];
}

BlockView parse_block(const uint8_t *buf, size_t len) {
  BlockView v;
  if (len < kHeaderLen + 2) return v;       // header + CRC minimum
  if (buf[1] != kFnWriteMultiple) return v;  // notifications use 0x10
  const uint8_t byte_count = buf[6];
  if (len != kHeaderLen + byte_count + 2) return v;
  if (!check_crc(buf, len)) return v;
  v.valid = true;
  v.block_addr = static_cast<uint16_t>(buf[2] << 8) | buf[3];
  v.data = buf + kHeaderLen;
  v.reg_count = byte_count / 2;
  return v;
}

BlockView parse_block(const std::vector<uint8_t> &buf) {
  return parse_block(buf.data(), buf.size());
}

// --- Aggregated state ------------------------------------------------------

std::optional<float> HeatPumpState::target_for_current_mode() const {
  if (!mode) return std::nullopt;
  switch (*mode) {
    case Mode::Heat: return heat_set;
    case Mode::Cool: return cool_set;
    case Mode::Auto: return auto_set;
  }
  return std::nullopt;
}

// Register offset within a block = absolute address - block start.
static size_t off(uint16_t block, uint16_t addr) { return addr - block; }

bool apply_block(HeatPumpState &st, const BlockView &blk) {
  if (!blk.valid) return false;
  switch (blk.block_addr) {
    case kBlockControl: {
      st.power = blk.reg(off(kBlockControl, kRegPower)) != 0;
      uint16_t m = blk.reg(off(kBlockControl, kRegMode));
      if (m <= 2) st.mode = static_cast<Mode>(m);
      return true;
    }
    case kBlockSetpoint: {
      st.heat_set = temp1_to_celsius(blk.reg(off(kBlockSetpoint, kRegHeatTarget)));
      st.cool_set = temp1_to_celsius(blk.reg(off(kBlockSetpoint, kRegCoolTarget)));
      st.auto_set = temp1_to_celsius(blk.reg(off(kBlockSetpoint, kRegAutoTarget)));
      return true;
    }
    case kBlockStatus: {
      st.running = blk.reg(off(kBlockStatus, kRegRunningState)) != 0;
      st.master_version = blk.reg(off(kBlockStatus, kRegMasterVersion)) / 10.0f;
      st.software_code = blk.reg(off(kBlockStatus, kRegSoftwareCode));
      st.outputs = blk.reg(off(kBlockStatus, kRegOutputs));
      st.error_reg = blk.reg(off(kBlockStatus, kRegErrorCode));
      return true;
    }
    case kBlockTemps: {
      st.t_suction = temp1_to_celsius(blk.reg(0));
      st.t_inlet = temp1_to_celsius(blk.reg(1));
      st.t_outlet = temp1_to_celsius(blk.reg(2));
      st.t_coil = temp1_to_celsius(blk.reg(3));
      st.t_ambient = ambient_to_celsius(blk.reg(4));
      st.t_exhaust = temp1_to_celsius(blk.reg(5));
      return true;
    }
    default:
      return false;
  }
}

}  // namespace phnix
