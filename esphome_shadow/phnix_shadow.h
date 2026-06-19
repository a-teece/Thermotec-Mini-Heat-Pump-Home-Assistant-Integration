// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shadow-mode glue between the ESPHome firmware and the platform-agnostic
// `phnix` protocol library. This is the ONE file that depends on both ESPHome
// (for logging) and `phnix` — the library core stays pure.
//
// Purpose: while the existing hand-written lambdas remain the source of truth
// for what the firmware actually sends/parses, these helpers run the library
// over the SAME live data and log any disagreement. Nothing here drives a BLE
// write or publishes a sensor — it is observation only. Once the logs are
// clean against real hardware over many cycles, the lambdas can be cut over to
// call the library directly and this file deleted.
//
// All logging uses the "phnix_shadow" tag, so verbosity can be tuned
// independently, e.g. in the device YAML:
//
//   logger:
//     logs:
//       phnix_shadow: DEBUG   # matches are DEBUG, mismatches are WARN
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "esphome/core/log.h"
#include "phnix/decode.h"
#include "phnix/frame.h"

namespace phnix_shadow {

static const char *const TAG = "phnix_shadow";

inline std::string hex(const std::vector<uint8_t> &v) {
  std::string s;
  char b[4];
  for (auto x : v) {
    snprintf(b, sizeof b, "%02X ", x);
    s += b;
  }
  return s;
}

inline std::string fopt(const std::optional<float> &o) {
  if (!o) return std::string("fault/--");
  char b[16];
  snprintf(b, sizeof b, "%.1f", *o);
  return std::string(b);
}

// Compare a frame the firmware is about to send against what the library
// builds for the same logical command. Logs match (DEBUG) or mismatch (WARN
// with both byte strings). Returns true on match. `actual` is the firmware's
// frame; `expected` is the library's.
inline bool check_frame(const char *what, const std::vector<uint8_t> &actual,
                        const std::vector<uint8_t> &expected) {
  if (actual == expected) {
    ESP_LOGD(TAG, "frame[%s] MATCH (%u bytes)", what,
             static_cast<unsigned>(actual.size()));
    return true;
  }
  ESP_LOGW(TAG, "frame[%s] MISMATCH", what);
  ESP_LOGW(TAG, "  firmware: %s", hex(actual).c_str());
  ESP_LOGW(TAG, "  library:  %s", hex(expected).c_str());
  return false;
}

// Run the library parser over a raw notification. Confirms the library accepts
// real BLE fragments (CRC/length validation) and logs the decoded view so it
// can be eyeballed against the firmware's parser and the HA entities. Also logs
// the raw hex of each valid block so it can be lifted straight into a golden
// unit-test vector. Observation only.
inline void check_notification(const std::vector<uint8_t> &raw) {
  phnix::BlockView b = phnix::parse_block(raw);
  if (!b.valid) {
    // Most non-block frames here are ACKs / exceptions / short frames, which
    // the library is not meant to parse — stay quiet unless it looks like a
    // block-shaped frame that failed CRC (a real red flag worth surfacing).
    if (raw.size() >= 9 && raw[1] == 0x10) {
      ESP_LOGW(TAG, "block-shaped frame REJECTED by lib (CRC/len): %s",
               hex(raw).c_str());
    }
    return;
  }

  phnix::HeatPumpState st;
  phnix::apply_block(st, b);
  ESP_LOGI(TAG, "block 0x%04X raw: %s", b.block_addr, hex(raw).c_str());

  switch (b.block_addr) {
    case phnix::kBlockTemps:
      ESP_LOGI(TAG, "  inlet=%s outlet=%s coil=%s ambient=%s exhaust=%s",
               fopt(st.t_inlet).c_str(), fopt(st.t_outlet).c_str(),
               fopt(st.t_coil).c_str(), fopt(st.t_ambient).c_str(),
               fopt(st.t_exhaust).c_str());
      break;
    case phnix::kBlockSetpoint:
      ESP_LOGI(TAG, "  heat_set=%s cool_set=%s auto_set=%s",
               fopt(st.heat_set).c_str(), fopt(st.cool_set).c_str(),
               fopt(st.auto_set).c_str());
      break;
    case phnix::kBlockControl:
      ESP_LOGI(TAG, "  power=%d mode=%d", st.power ? *st.power : -1,
               st.mode ? static_cast<int>(*st.mode) : -1);
      break;
    case phnix::kBlockStatus:
      ESP_LOGI(TAG, "  running=%d outputs=0x%04X error=0x%04X (%s)",
               st.running ? *st.running : -1, st.outputs ? *st.outputs : 0,
               st.error_reg ? *st.error_reg : 0,
               phnix::error_description(st.error_reg ? *st.error_reg : 0)
                   .c_str());
      break;
    default:
      break;
  }
}

}  // namespace phnix_shadow
