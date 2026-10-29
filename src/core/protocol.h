// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// LM Drum SysEx protocol: framing, 8-to-7 packing, and upload encoding.
// The format was decoded by observing the device's USB-MIDI traffic; the constants and comments
// below document it.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lmd {

using Bytes = std::vector<uint8_t>;

// F0, Behringer manufacturer ID 00 20 32, device ID 00, model 01 28, then 00.
inline constexpr uint8_t kHeader[] = {0xF0, 0x00, 0x20, 0x32, 0x00, 0x01, 0x28, 0x00};

inline constexpr uint8_t kCmdAck = 0x01;
inline constexpr uint8_t kCmdVersionRequest = 0x08;
inline constexpr uint8_t kCmdVersion = 0x09;
inline constexpr uint8_t kCmdRead = 0x77;   // host -> device read request
inline constexpr uint8_t kCmdData = 0x78;   // bulk data, both directions

// Data types: first byte after 0x77 / 0x78.
inline constexpr uint8_t kTypeUploadHeader = 0x10;
inline constexpr uint8_t kTypeUploadData = 0x11;
inline constexpr uint8_t kTypePool = 0x30;
inline constexpr uint8_t kTypeSampleData = 0x31;
inline constexpr uint8_t kTypeAllBanks = 0x32;
inline constexpr uint8_t kTypeBank = 0x40;  // + bank index 0-15

inline constexpr int kSampleRate = 24000;
inline constexpr int kBitsPerSample = 16;
inline constexpr size_t kChunk = 2048;      // data bytes per bulk frame

struct Frame {
    uint8_t cmd;
    Bytes payload;  // everything between the command byte and F7
};

Bytes makeFrame(uint8_t cmd, const Bytes& payload);
std::optional<Frame> parseFrame(const Bytes& raw);
bool isAck(const Bytes& raw);

// 8-to-7 packing, LSB-first: bit i of each group's MSB byte is the high bit of data byte i.
Bytes pack7(const Bytes& data);
Bytes unpack7(const uint8_t* data, size_t len);

// 0x78 bulk frame: <type> 00 00 <packed data>.
Bytes dataFrame(uint8_t type, const Bytes& data);
// Unpacked contents of a 0x78 frame (still includes the packing's trailing padding).
Bytes dataContents(const Frame& frame);

// Single-sample upload: one 0x10 header frame, then 0x11 frames of up to 2048 PCM bytes.
// pcm is 16-bit signed little-endian mono at 24 kHz; name is stored as up to 16 bytes.
std::vector<Bytes> uploadFrames(const std::string& name, const Bytes& pcm);

inline uint16_t rd16(const Bytes& b, size_t i) { return uint16_t(b[i] | b[i + 1] << 8); }
inline uint32_t rd32(const Bytes& b, size_t i) {
    return uint32_t(b[i]) | uint32_t(b[i + 1]) << 8 | uint32_t(b[i + 2]) << 16 | uint32_t(b[i + 3]) << 24;
}
inline void wr16(Bytes& b, size_t i, uint16_t v) { b[i] = uint8_t(v); b[i + 1] = uint8_t(v >> 8); }
inline void wr32(Bytes& b, size_t i, uint32_t v) {
    for (int k = 0; k < 4; ++k) b[i + k] = uint8_t(v >> (8 * k));
}

std::string hex(const Bytes& b, size_t max = 32);

}  // namespace lmd
