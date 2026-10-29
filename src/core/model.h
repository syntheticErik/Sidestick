// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// LM Drum data model: sample pool records, banks (kit + slot table + name), and backups.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "protocol.h"

namespace lmd {

inline constexpr int kFactorySamples = 109;  // pool indices 0-108 are ROM
inline constexpr int kBanks = 16;
inline constexpr int kVoices = 16;
inline constexpr int kSlots = 128;
inline constexpr uint16_t kSlotOff = 0x07FF;    // slot 0
inline constexpr uint16_t kSlotEmpty = 0x07FE;

// User sample memory per the manual: "14.17 MB" (MiB), about 310 s at 24 kHz, 16-bit.
inline constexpr uint64_t kUserMemoryBytes = 14858322;
// Storage for one user sample: 32-byte header + 16-bit PCM, rounded up to 4 bytes.
// Approximate: the device occasionally pads to a 2048-byte boundary between samples.
inline constexpr uint64_t sampleStorageBytes(uint32_t frames) { return (32 + 2ull * frames + 3) & ~3ull; }

// Firmware versions (MCU1) that writes have been tested against.
inline constexpr const char* kTestedFirmware[] = {"1.2.2"};

// Canonical voice order, as used by the kit records, the MIDI note map, and SynthTribe.
inline constexpr const char* kVoiceNames[kVoices] = {
    "BASS", "SNARE", "SIDESTICK", "CLOSED HAT", "OPEN HAT", "HIGH TOM", "MID TOM", "LOW TOM",
    "RIDE", "CRASH", "CABASA", "TAMBOURINE", "HIGH CONGA", "LOW CONGA", "COWBELL", "CLAPS"};

// The firmware plays the group leader's pointer for every voice in a locked group:
// HIGH TOM for the three toms, HIGH CONGA for both congas (confirmed on the device).
// Returns the voice whose pointer decides what `voice` actually plays.
inline constexpr int voiceGroupLeader(int voice) {
    if (voice >= 5 && voice <= 7) return 5;
    if (voice == 12 || voice == 13) return 12;
    return voice;
}

// Slot each voice points at in a bank the device has never had edited. Only used to reproduce
// that untouched state (Bank::empty()); Sidestick itself never assigns voices by default.
inline constexpr uint16_t kDefaultVoiceSlot[16] = {1, 2, 3, 4, 5, 6, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13};

struct PoolEntry {
    std::string name;
    uint32_t bits = 0;
    uint32_t frames = 0;  // length in 24 kHz sample frames
};

// Pool listing: 32-byte SMPH records, 64 per 0x30 frame.
std::vector<PoolEntry> parsePool(const std::vector<Frame>& frames);

// Upper-cases and restricts to printable ASCII, the way the device stores names.
std::string deviceName(const std::string& name, size_t maxLen = 16);

class Bank {
public:
    static constexpr size_t kSize = 497;

    static std::optional<Bank> fromBytes(const Bytes& data);  // unpacked bank contents
    const Bytes& bytes() const { return raw_; }

    uint16_t slot(int slot) const;
    void setSlot(int slot, uint16_t poolIndex);
    uint16_t voiceSlot(int voice) const;  // bank slot the voice plays
    void setVoiceSlot(int voice, uint16_t slot);
    std::string name() const;
    void setName(const std::string& name);

    // Filling the bank and building the kit are separate steps (as in SynthTribe):
    // a voice plays only a slot it was explicitly assigned, otherwise it is OFF (slot 000).

    // Puts a pool sample in a slot. If the slot was empty, voices still pointing at it (e.g. the
    // device's empty-bank defaults) are turned OFF, so nothing starts playing unasked.
    void putSample(int slot, uint16_t poolIndex);
    // Points a voice (and its locked group) at OFF.
    void turnOffVoice(int voice);

    // A bank exactly as the device holds an untouched one: default kit, all slots empty, name "---".
    static Bank empty();
    // Every voice OFF and per-voice settings back to fresh-bank defaults; slots kept.
    void clearKit();
    // All slots empty, every voice OFF, name "---". Samples stay in the pool.
    void clear();

    bool operator==(const Bank&) const = default;

private:
    Bytes raw_;  // kept whole so fields we don't understand yet round-trip unchanged
};

struct UserSample {
    std::string name;
    Bytes pcm;  // 16-bit signed LE mono, 24 kHz
};

struct Backup {
    std::vector<UserSample> samples;
    std::map<int, Bank> banks;  // bank index 0-15
};

// Parses device backup frames (SynthTribe .ars layout: 0x31 sample data, then 0x4b banks).
Backup parseBackup(const std::vector<Bytes>& rawFrames);

// Reads a single-bank export (.syx: one 78 4b frame). Returns nullopt and sets *error if it isn't one.
std::optional<Bank> bankFromExport(const std::vector<Bytes>& rawFrames, std::string* error);
// Slots of `bank` that refer to pool indices the device doesn't have (exports store indices, not audio).
std::vector<int> missingSampleSlots(const Bank& bank, size_t poolSize);

// Checks that backup frames are safe to restore: only sample data followed by all 16 banks,
// bank slots refer only to samples that will exist afterwards, and the samples fit in memory.
// Returns a list of problems (empty if OK).
std::vector<std::string> restoreProblems(const std::vector<Bytes>& rawFrames);

}  // namespace lmd
