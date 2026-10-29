// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// Pre-upload processing on full-precision device-rate audio (mono, 24 kHz, float -1..1).
// Round to 16-bit once at the end with quantize16() (audio.h).
#pragma once

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace lmd {

using Samples = std::vector<float>;

// Frames [start, end).
Samples slice(const Samples& s, size_t start, size_t end);

// Linear fade to silence over the last `frames` frames.
void fadeOut(Samples& s, size_t frames);

// Multiplies every sample by `gain`.
void applyGain(Samples& s, double gain);

// Trim points around the sound: from `lead` frames before the first sample above thresholdDbfs
// to `tail` frames after the last. Returns {0, size} if nothing exceeds the threshold.
std::pair<size_t, size_t> soundBounds(const Samples& s, double thresholdDbfs, size_t lead, size_t tail);

// Largest absolute sample in frames [start, end) (0 for silence).
double peak(const Samples& s, size_t start = 0, size_t end = SIZE_MAX);

inline double toDb(double linear) { return 20.0 * std::log10(linear); }
inline double fromDb(double db) { return std::pow(10.0, db / 20.0); }

}  // namespace lmd
