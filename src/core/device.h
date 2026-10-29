// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// Live connection to an LM Drum over USB-MIDI (RtMidi: ALSA / CoreMIDI / Windows MM).
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "model.h"
#include "protocol.h"

class RtMidiIn;
class RtMidiOut;

namespace lmd {

// The device didn't store an uploaded sample (it ACKs every frame but silently refuses, e.g. a
// name that is already in the pool). Nothing on the device changed.
struct UploadRejected : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Device {
public:
    // Opens the first input and output port whose name contains `portMatch`.
    explicit Device(const std::string& portMatch = "LM DRUM");
    ~Device();

    static std::vector<std::string> inputPorts();
    static std::vector<std::string> outputPorts();

    std::string firmwareVersion();  // e.g. "1.2.2" (MCU1)
    std::vector<PoolEntry> pool(double firstTimeout = 2.0);
    Bank readBank(int index);
    std::map<int, Bank> readAllBanks();
    void writeBank(int index, const Bank& bank);

    // Same requests as SynthTribe's Backup; the returned frames are the .ars file contents.
    std::vector<Bytes> backupFrames();

    struct Placement {
        int bank;
        int slot;
    };
    struct UploadResult {
        int poolIndex;
        std::optional<Placement> devicePlacement;  // where the device auto-filed it (undone)
    };
    // Uploads a sample, then undoes the device's automatic placement into its selected bank,
    // so the sample is in the pool only. Refuses to write if a bank changed in any other way.
    // Throws UploadRejected (before sending anything) if the name is already in the pool, or if
    // the device refuses the sample.
    UploadResult uploadUnassigned(const std::string& name, const Bytes& pcm);

    // Sends frames one at a time, waiting for the device's ACK after each.
    void sendAcked(const std::vector<Bytes>& frames, double timeout = 2.0);

    // Replays backup frames (as SynthTribe's Restore does), replacing all user samples and banks.
    // Call restoreProblems() first. The device ACKs the last sample-data frame only after committing
    // everything to flash (about 32 s for 2.4 MB of samples), so that frame gets a long timeout.
    // progress(sent, total) is called after each ACKed frame.
    void restore(const std::vector<Bytes>& frames, const std::function<void(size_t, size_t)>& progress = nullptr);

private:
    using Want = std::function<bool(const Frame&)>;
    std::vector<Bytes> request(const Bytes& frame, const Want& want, size_t maxFrames, double firstTimeout,
                               double gapTimeout, const std::function<bool(const Frame&)>& isLast = nullptr);
    std::optional<Bytes> receive(double timeout);
    void drain();
    void send(const Bytes& frame);
    void sendOneAcked(const Bytes& frame, size_t n, double timeout);

    std::unique_ptr<RtMidiIn> in_;
    std::unique_ptr<RtMidiOut> out_;
};

}  // namespace lmd
