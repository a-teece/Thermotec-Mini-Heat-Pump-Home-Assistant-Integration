// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frame-builder tests assert byte-for-byte equality against frames captured
// from the reference unit (Protocol.md §5 / Appendix A). These are the
// hard-won, verified-on-hardware sequences — the builders must reproduce them
// exactly, CRC included.
#include "phnix/crc.h"
#include "phnix/frame.h"
#include "test_framework.h"

using namespace phnix;

TEST(build_read_all_matches_capture) {
  CHECK_BYTES(build_read_all(),
              (std::vector<uint8_t>{0x63, 0x03, 0x00, 0x07, 0x00, 0x2D, 0x3C, 0x54}));
}

TEST(build_power_matches_captures) {
  CHECK_BYTES(build_power(false),
              (std::vector<uint8_t>{0x63, 0x10, 0x03, 0xF3, 0x00, 0x01, 0x02, 0x00, 0x00, 0x30, 0xF1}));
  CHECK_BYTES(build_power(true),
              (std::vector<uint8_t>{0x63, 0x10, 0x03, 0xF3, 0x00, 0x01, 0x02, 0x00, 0x01, 0xF1, 0x31}));
}

TEST(build_mode_matches_captures) {
  // NOTE: the authoritative mapping is Cool=0, Heat=1, Auto=2 with companion
  // values 0x0000/0x0088/0x008C (Protocol.md §5.2 and the live firmware).
  // Protocol.md Appendix A mislabels the Heat/Cool frames; these assertions
  // use the correct mapping.
  CHECK_BYTES(build_mode(Mode::Cool),
              (std::vector<uint8_t>{0x63, 0x10, 0x03, 0xF4, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x51}));
  CHECK_BYTES(build_mode(Mode::Heat),
              (std::vector<uint8_t>{0x63, 0x10, 0x03, 0xF4, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x88, 0x4D, 0xF7}));
  CHECK_BYTES(build_mode(Mode::Auto),
              (std::vector<uint8_t>{0x63, 0x10, 0x03, 0xF4, 0x00, 0x02, 0x04, 0x00, 0x02, 0x00, 0x8C, 0xBC, 0x34}));
}

TEST(build_set_target_routes_by_mode) {
  // Cool 15.0 C and Auto 32.0 C have captured frames with known CRCs.
  CHECK_BYTES(build_set_target(Mode::Cool, 15.0f),
              (std::vector<uint8_t>{0x63, 0x10, 0x04, 0x1B, 0x00, 0x01, 0x02, 0x00, 0x96, 0xD1, 0x77}));
  CHECK_BYTES(build_set_target(Mode::Auto, 32.0f),
              (std::vector<uint8_t>{0x63, 0x10, 0x04, 0x2C, 0x00, 0x01, 0x02, 0x01, 0x40, 0x55, 0x3E}));
  // Heat target routes to 0x0416; 30.0 C -> 300 = 0x012C (Protocol.md §5.1.2).
  auto heat = build_set_target(Mode::Heat, 30.0f);
  CHECK_EQ(heat[2], 0x04);
  CHECK_EQ(heat[3], 0x16);
  CHECK_EQ(heat[7], 0x01);
  CHECK_EQ(heat[8], 0x2C);
  CHECK(check_crc(heat));
}

TEST(build_set_target_rounds_to_0p1) {
  // 30.55 -> 305.5 -> rounds to 306 = 0x0132.
  auto f = build_set_target(Mode::Heat, 30.55f);
  CHECK_EQ(f[7], 0x01);
  CHECK_EQ(f[8], 0x32);
}

TEST(target_register_mapping) {
  CHECK_EQ(target_register_for(Mode::Heat), kRegHeatTarget);
  CHECK_EQ(target_register_for(Mode::Cool), kRegCoolTarget);
  CHECK_EQ(target_register_for(Mode::Auto), kRegAutoTarget);
}

TEST(build_write_registers_generic_matches_capture) {
  // Defrost type = Eco (single register 0x0425 = 0x0001).
  CHECK_BYTES(build_write_registers(0x0425, {0x0001}),
              (std::vector<uint8_t>{0x63, 0x10, 0x04, 0x25, 0x00, 0x01, 0x02, 0x00, 0x01, 0x94, 0x07}));
}

TEST(build_write_registers_honours_slave_override) {
  auto f = build_write_registers(kRegPower, {1}, 0x10);
  CHECK_EQ(f[0], 0x10);
  CHECK(check_crc(f));
}
