// SPDX-License-Identifier: GPL-3.0-or-later
//
// Decoder tests. Temperature/error cases use documented values from
// Protocol.md §6. Block parsing is exercised by synthesising a valid 45-register
// block notification (no full status capture is published in Protocol.md, so we
// build one to the §3.2 format and assert round-trip decode).
#include <map>

#include "phnix/crc.h"
#include "phnix/decode.h"
#include "test_framework.h"

using namespace phnix;

// Build a valid block notification: 63 10 BH BL 00 2D 5A [90 data bytes] CRC.
static std::vector<uint8_t> make_block(uint16_t block_addr,
                                       const std::map<size_t, uint16_t> &regs) {
  std::vector<uint8_t> f = {0x63,
                            0x10,
                            static_cast<uint8_t>(block_addr >> 8),
                            static_cast<uint8_t>(block_addr & 0xFF),
                            0x00,
                            0x2D,
                            0x5A};
  std::vector<uint16_t> payload(45, 0);
  for (const auto &kv : regs) payload[kv.first] = kv.second;
  for (uint16_t v : payload) {
    f.push_back(static_cast<uint8_t>(v >> 8));
    f.push_back(static_cast<uint8_t>(v & 0xFF));
  }
  append_crc(f);
  return f;
}

// --- Temperature -----------------------------------------------------------

TEST(temp1_positive_and_negative) {
  CHECK(temp1_to_celsius(300).value() == 30.0f);
  CHECK(temp1_to_celsius(0xFFE2).value() == -3.0f);  // -30 -> -3.0
  CHECK(temp1_to_celsius(0).value() == 0.0f);
}

TEST(temp1_fault_sentinel) {
  CHECK(is_temp_fault(0x7FFD));
  CHECK(is_temp_fault(0x7FFF));
  CHECK(is_temp_fault(0x7FF0));
  CHECK(!is_temp_fault(300));
  CHECK(!temp1_to_celsius(0x7FFD).has_value());
}

TEST(ambient_fault_special_value) {
  CHECK(is_ambient_fault(0xFFF6));
  CHECK(is_ambient_fault(0x7FFD));  // also honours the standard sentinel
  CHECK(!is_ambient_fault(250));
  CHECK(!ambient_to_celsius(0xFFF6).has_value());
  CHECK(ambient_to_celsius(250).value() == 25.0f);
}

// --- Error register --------------------------------------------------------

TEST(error_single_and_multiple) {
  CHECK_EQ(error_description(0x0000), std::string(""));
  CHECK_EQ(error_description(0x0004), std::string("E03"));
  CHECK_EQ(error_description(0x0006), std::string("E02, E03"));
  CHECK_EQ(error_description(0x0001), std::string("E01"));
}

TEST(error_unknown_bits_logged) {
  CHECK_EQ(error_description(0x0008), std::string("Unknown(bit3)"));
  auto codes = active_error_codes(0x0005);  // E01 + E03
  CHECK_EQ(codes.size(), static_cast<size_t>(2));
  CHECK_EQ(codes[0], std::string("E01"));
  CHECK_EQ(codes[1], std::string("E03"));
}

// --- Block parsing ---------------------------------------------------------

TEST(parse_block_rejects_bad_crc) {
  auto blk = make_block(kBlockTemps, {{1, 250}});
  blk[10] ^= 0xFF;  // corrupt a data byte
  CHECK(!parse_block(blk).valid);
}

TEST(parse_block_reads_header_and_registers) {
  auto blk = make_block(kBlockTemps, {{1, 250}, {2, 281}});
  auto v = parse_block(blk);
  CHECK(v.valid);
  CHECK_EQ(v.block_addr, kBlockTemps);
  CHECK_EQ(v.reg_count, static_cast<size_t>(45));
  CHECK_EQ(v.reg(1), static_cast<uint16_t>(250));
  CHECK_EQ(v.reg(2), static_cast<uint16_t>(281));
}

TEST(apply_temps_block) {
  // inlet 25.0, outlet 28.1, coil 12.0, ambient fault, exhaust 90.0,
  // suction not present (sentinel).
  auto blk = make_block(kBlockTemps, {{0, 0x7FFD},
                                      {1, 250},
                                      {2, 281},
                                      {3, 120},
                                      {4, 0xFFF6},
                                      {5, 900}});
  HeatPumpState st;
  CHECK(apply_block(st, parse_block(blk)));
  CHECK(!st.t_suction.has_value());
  CHECK(st.t_inlet.value() == 25.0f);
  CHECK(st.t_outlet.value() == 28.1f);
  CHECK(st.t_coil.value() == 12.0f);
  CHECK(!st.t_ambient.has_value());  // P04
  CHECK(st.t_exhaust.value() == 90.0f);
}

TEST(apply_control_block_power_and_mode) {
  // offset of power within control block = 0x03F3-0x03E9 = 10; mode = 11.
  auto blk = make_block(kBlockControl, {{10, 1}, {11, 1}});  // on, Heat
  HeatPumpState st;
  CHECK(apply_block(st, parse_block(blk)));
  CHECK(st.power.value() == true);
  CHECK(st.mode.has_value());
  CHECK(*st.mode == Mode::Heat);
}

TEST(apply_setpoint_block_and_current_target) {
  // heat off 0, cool off 5, auto off 22.
  auto blk = make_block(kBlockSetpoint, {{0, 300}, {5, 150}, {22, 320}});
  HeatPumpState st;
  CHECK(apply_block(st, parse_block(blk)));
  CHECK(st.heat_set.value() == 30.0f);
  CHECK(st.cool_set.value() == 15.0f);
  CHECK(st.auto_set.value() == 32.0f);
  // With mode = Cool, current target should follow cool_set.
  st.mode = Mode::Cool;
  CHECK(st.target_for_current_mode().value() == 15.0f);
}

TEST(apply_status_block_fields) {
  // running=10, master_version=16, software=17, outputs=18, error=33.
  auto blk = make_block(kBlockStatus, {{10, 1},
                                       {16, 12},      // 1.2
                                       {17, 494},
                                       {18, 0x0005},  // compressor + high fan
                                       {33, 0x0004}});  // E03
  HeatPumpState st;
  CHECK(apply_block(st, parse_block(blk)));
  CHECK(st.running.value() == true);
  CHECK(st.master_version.value() == 1.2f);
  CHECK_EQ(st.software_code.value(), static_cast<uint16_t>(494));
  CHECK_EQ(st.outputs.value(), static_cast<uint16_t>(0x0005));
  CHECK_EQ(st.error_reg.value(), static_cast<uint16_t>(0x0004));
  CHECK_EQ(error_description(st.error_reg.value()), std::string("E03"));
}

TEST(apply_block_ignores_unknown_block) {
  auto blk = make_block(0x1234, {{0, 1}});
  HeatPumpState st;
  CHECK(!apply_block(st, parse_block(blk)));
}
