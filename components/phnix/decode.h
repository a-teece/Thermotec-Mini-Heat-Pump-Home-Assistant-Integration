// SPDX-License-Identifier: GPL-3.0-or-later
//
// Decoders: turn raw notification bytes into typed values and aggregated
// state. Pure functions over byte buffers; no I/O.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "protocol.h"

namespace phnix {

// --- Temperature (TEMP1: signed 16-bit, x10) ------------------------------

// True if `raw` is the standard sensor-failure sentinel ((raw&0xFFF0)==0x7FF0).
bool is_temp_fault(uint16_t raw);

// True if the ambient sensor (T05) reads a fault: the 0xFFF6 special value or
// the standard sentinel. NOTE: a genuine -1.0 C reading collides with 0xFFF6
// (Protocol.md §6.2.2) — callers needing subzero operation must add heuristics.
bool is_ambient_fault(uint16_t raw);

// Decode a TEMP1 value to degrees C, or nullopt on sensor fault.
std::optional<float> temp1_to_celsius(uint16_t raw);

// As above but using the ambient sensor's fault rule.
std::optional<float> ambient_to_celsius(uint16_t raw);

// --- Error register (0x07F2) ----------------------------------------------

// Human display codes for the set bits, in bit order. Known bits map to
// "E01".."E03"; unknown set bits map to "Unknown(bitN)".
std::vector<std::string> active_error_codes(uint16_t error_reg);

// The active codes joined with ", ", or "" when no bits are set.
std::string error_description(uint16_t error_reg);

// Verbose, human-readable description of the active E-code bits, joined with
// "; " (e.g. "E02 Low pressure protection; E03 No water flow"). Unknown set
// bits are summarised as "E-unknown bits 0x....". Empty string if none set.
// (P-code sensor faults are not in this register — see Protocol.md §6.2.)
std::string error_text(uint16_t error_reg);

// --- Block notification parsing -------------------------------------------

// A validated view over one block notification (Protocol.md §3.2).
struct BlockView {
  bool valid = false;            // header well-formed and CRC correct
  uint16_t block_addr = 0;       // e.g. 0x07FE
  const uint8_t *data = nullptr; // -> first payload byte (offset 7)
  size_t reg_count = 0;          // number of 16-bit registers in payload

  // Big-endian register at zero-based offset `i`; 0 if out of range.
  uint16_t reg(size_t i) const;
};

BlockView parse_block(const uint8_t *buf, size_t len);
BlockView parse_block(const std::vector<uint8_t> &buf);

// --- Aggregated state ------------------------------------------------------

// nullopt fields are "not yet seen" / "fault / not present". Floats are used
// for temperatures and setpoints so an absent value maps cleanly to HA's
// "unavailable".
struct HeatPumpState {
  std::optional<bool> power;             // 0x03F3 (user intent)
  std::optional<Mode> mode;              // 0x03F4
  std::optional<bool> running;           // 0x07DB (actual state)
  std::optional<uint16_t> error_reg;     // 0x07F2
  std::optional<uint16_t> outputs;       // 0x07E3
  std::optional<float> master_version;   // 0x07E1 (x10)
  std::optional<uint16_t> software_code; // 0x07E2

  std::optional<float> heat_set;  // R01 0x0416
  std::optional<float> cool_set;  // R06 0x041B
  std::optional<float> auto_set;  // R11 0x042C

  std::optional<float> t_suction;  // T01 (often absent on single-system)
  std::optional<float> t_inlet;    // T02 -> P01
  std::optional<float> t_outlet;   // T03 -> P02
  std::optional<float> t_coil;     // T04 -> P05
  std::optional<float> t_ambient;  // T05 -> P04
  std::optional<float> t_exhaust;  // T06 -> P81

  // The setpoint the heat pump actually displays, given the current mode.
  std::optional<float> target_for_current_mode() const;
};

// Fold one parsed block into `st`. Returns true if the block was recognised.
bool apply_block(HeatPumpState &st, const BlockView &blk);

}  // namespace phnix
