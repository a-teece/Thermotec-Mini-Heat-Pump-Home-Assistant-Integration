// SPDX-License-Identifier: GPL-3.0-or-later
//
// Protocol constants for PHNIX pool heat pumps (the hardware behind the
// AquaTemp app and Thermotec-branded units). Values mirror Protocol.md; that
// document is the source of truth. No platform dependencies.
#pragma once

#include <cstdint>

namespace phnix {

// Modbus framing (Protocol.md §2). The slave address is per-unit and the
// safest source is the byte captured in a live response frame; 0x63 is the
// reference unit's default.
constexpr uint8_t kDefaultSlaveAddress = 0x63;
constexpr uint8_t kFnReadHolding = 0x03;    // Read Holding Registers
constexpr uint8_t kFnWriteMultiple = 0x10;  // Write Multiple Registers

// The "magic" read-all poll: start 0x0007, quantity 0x002D (Protocol.md §3.1).
constexpr uint16_t kReadAllStart = 0x0007;
constexpr uint16_t kReadAllQuantity = 0x002D;

// Control registers (Protocol.md §4.1, §5).
constexpr uint16_t kRegPower = 0x03F3;          // 0=Off, 1=On
constexpr uint16_t kRegMode = 0x03F4;           // 0=Cool, 1=Heat, 2=Auto
constexpr uint16_t kRegModeCompanion = 0x03F5;  // app-set value, see Mode*

// Mode-dependent target registers (Protocol.md §4.2, §5.1.2).
constexpr uint16_t kRegHeatTarget = 0x0416;  // R01
constexpr uint16_t kRegCoolTarget = 0x041B;  // R06
constexpr uint16_t kRegAutoTarget = 0x042C;  // R11

// The four read blocks (Protocol.md §3.3).
constexpr uint16_t kBlockControl = 0x03E9;   // control & engineering settings
constexpr uint16_t kBlockSetpoint = 0x0416;  // setpoints & defrost
constexpr uint16_t kBlockStatus = 0x07D1;    // operational status & errors
constexpr uint16_t kBlockTemps = 0x07FE;     // live temperatures

// Status-block register addresses (Protocol.md §4.3).
constexpr uint16_t kRegRunningState = 0x07DB;  // actual run state (vs intent)
constexpr uint16_t kRegMasterVersion = 0x07E1;  // DIGI5 (x10): 12 -> 1.2
constexpr uint16_t kRegSoftwareCode = 0x07E2;    // DIGI1 raw: e.g. 494
constexpr uint16_t kRegOutputs = 0x07E3;         // output relay bitmask
constexpr uint16_t kRegErrorCode = 0x07F2;       // E-code bitmask

// Error bitmask bits (Protocol.md §4.3.4 / §6.1).
constexpr uint16_t kErrE01 = 0x0001;  // high pressure (INFERRED — do not test)
constexpr uint16_t kErrE02 = 0x0002;  // low pressure
constexpr uint16_t kErrE03 = 0x0004;  // no water flow / flow switch

// Output relay bitmask bits (Protocol.md §4.3.3).
constexpr uint16_t kOutCompressor = 0x0001;
constexpr uint16_t kOutCircPump = 0x0002;
constexpr uint16_t kOutHighFan = 0x0004;
constexpr uint16_t kOut4WayValve = 0x0008;

// Temperature sensor-failure encoding (Protocol.md §6.2).
// Standard sentinel: (raw & 0xFFF0) == 0x7FF0. Ambient is the exception.
constexpr uint16_t kTempFaultMaskAnd = 0xFFF0;
constexpr uint16_t kTempFaultMaskValue = 0x7FF0;
constexpr uint16_t kAmbientFaultValue = 0xFFF6;  // signed -10 = -1.0 C

// Operating mode. The enum value equals the byte written to register 0x03F4.
enum class Mode : uint8_t { Cool = 0, Heat = 1, Auto = 2 };

}  // namespace phnix
