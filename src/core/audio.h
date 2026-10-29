// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// Audio conversion to and from the LM Drum's sample format (24 kHz, 16-bit, mono).
#pragma once

#include <string>
#include <vector>

#include "protocol.h"

namespace lmd {

// Reads any audio file libsndfile supports and converts it the way SynthTribe does:
// average the channels to mono, band-limited resample to 24 kHz, round to 16-bit.
// No normalization or other processing. Unlike SynthTribe, keeps the full length.
Bytes convertToDevicePcm(const std::string& path);

// The same conversion at full precision (mono, 24 kHz, float, nominally -1..1, not clipped), for
// processing (trim, normalize) before rounding to 16-bit once with quantize16().
std::vector<float> convertToDeviceFloat(const std::string& path);
// Rounds to 16-bit signed LE PCM, clipping to the 16-bit range.
Bytes quantize16(const std::vector<float>& samples);

// Writes device PCM (16-bit LE mono, 24 kHz) as a WAV file.
void writeDeviceWav(const std::string& path, const Bytes& pcm);

}  // namespace lmd
