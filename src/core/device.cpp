// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device.h"

#include <RtMidi.h>

#include <chrono>
#include <stdexcept>
#include <thread>

namespace lmd {

namespace {

using Clock = std::chrono::steady_clock;

Clock::time_point after(double seconds) {
    return Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
}

bool isDataType(const Frame& f, uint8_t type) { return f.cmd == kCmdData && !f.payload.empty() && f.payload[0] == type; }
bool isBankFrame(const Frame& f) {
    return f.cmd == kCmdData && !f.payload.empty() && (f.payload[0] & 0xF0) == kTypeBank;
}

template <class Midi>
unsigned findPort(Midi& m, const std::string& match) {
    for (unsigned i = 0; i < m.getPortCount(); ++i)
        if (m.getPortName(i).find(match) != std::string::npos) return i;
    throw std::runtime_error("no MIDI port matching \"" + match + "\"");
}

template <class Midi>
std::vector<std::string> portNames() {
    Midi m;
    std::vector<std::string> out;
    for (unsigned i = 0; i < m.getPortCount(); ++i) out.push_back(m.getPortName(i));
    return out;
}

}  // namespace

Device::Device(const std::string& portMatch) {
    // A full backup arrives as ~1500 frames in a burst, so the input queue must be deep.
    in_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "Sidestick", 8192);
    out_ = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "Sidestick");
    in_->ignoreTypes(false, true, true);  // keep SysEx; drop timing and active sensing
    in_->openPort(findPort(*in_, portMatch), "Sidestick in");
    out_->openPort(findPort(*out_, portMatch), "Sidestick out");
}

Device::~Device() = default;

std::vector<std::string> Device::inputPorts() { return portNames<RtMidiIn>(); }
std::vector<std::string> Device::outputPorts() { return portNames<RtMidiOut>(); }

void Device::send(const Bytes& frame) { out_->sendMessage(&frame); }

void Device::drain() {
    Bytes m;
    do in_->getMessage(&m);
    while (!m.empty());
}

std::optional<Bytes> Device::receive(double timeout) {
    auto deadline = after(timeout);
    Bytes m;
    while (true) {
        in_->getMessage(&m);
        if (!m.empty()) return m;
        if (Clock::now() > deadline) return std::nullopt;
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

std::vector<Bytes> Device::request(const Bytes& frame, const Want& want, size_t maxFrames, double firstTimeout,
                                   double gapTimeout, const std::function<bool(const Frame&)>& isLast) {
    drain();
    send(frame);
    std::vector<Bytes> got;
    auto deadline = after(firstTimeout);
    while (got.size() < maxFrames) {
        double left = std::chrono::duration<double>(deadline - Clock::now()).count();
        if (left <= 0) break;
        auto m = receive(left);
        if (!m) break;
        auto f = parseFrame(*m);
        if (!f || !want(*f)) continue;  // skips stray bytes and unrelated messages
        got.push_back(*m);
        if (isLast && isLast(*f)) break;
        deadline = after(gapTimeout);
    }
    return got;
}

std::string Device::firmwareVersion() {
    auto r = request(makeFrame(kCmdVersionRequest, {0x00}), [](const Frame& f) { return f.cmd == kCmdVersion; }, 1,
                     2.0, 2.0);
    if (r.empty()) throw std::runtime_error("no reply to version request");
    Bytes p = parseFrame(r[0])->payload;
    if (p.size() < 4) throw std::runtime_error("short version reply: " + hex(p));
    return std::to_string(p[1]) + "." + std::to_string(p[2]) + "." + std::to_string(p[3]);
}

std::vector<PoolEntry> Device::pool(double firstTimeout) {
    // Every pool frame but the last holds exactly 64 records, which lets us stop without waiting.
    auto last = [](const Frame& f) { return dataContents(f).size() / 32 < 64; };
    auto raw = request(makeFrame(kCmdRead, {kTypePool}), [](const Frame& f) { return isDataType(f, kTypePool); }, 64,
                       firstTimeout, 0.5, last);
    std::vector<Frame> frames;
    for (const Bytes& r : raw) frames.push_back(*parseFrame(r));
    return parsePool(frames);
}

Bank Device::readBank(int index) {
    uint8_t type = uint8_t(kTypeBank + index);
    auto r = request(makeFrame(kCmdRead, {type}), [&](const Frame& f) { return isDataType(f, type); }, 1, 2.0, 2.0);
    if (r.empty()) throw std::runtime_error("no reply reading bank " + std::to_string(index + 1));
    auto b = Bank::fromBytes(dataContents(*parseFrame(r[0])));
    if (!b) throw std::runtime_error("short bank reply");
    return *b;
}

std::map<int, Bank> Device::readAllBanks() {
    auto raw = request(makeFrame(kCmdRead, {kTypeAllBanks}), isBankFrame, kBanks, 2.0, 0.5);
    std::map<int, Bank> out;
    for (const Bytes& r : raw) {
        Frame f = *parseFrame(r);
        if (auto b = Bank::fromBytes(dataContents(f))) out[f.payload[0] & 0x0F] = *b;
    }
    if (out.size() != kBanks) throw std::runtime_error("expected 16 banks, got " + std::to_string(out.size()));
    return out;
}

void Device::writeBank(int index, const Bank& bank) {
    sendAcked({dataFrame(uint8_t(kTypeBank + index), bank.bytes())});
}

std::vector<Bytes> Device::backupFrames() {
    auto out = request(makeFrame(kCmdRead, {kTypeSampleData}),
                       [](const Frame& f) { return isDataType(f, kTypeSampleData); }, SIZE_MAX, 5.0, 1.0);
    auto banks = request(makeFrame(kCmdRead, {kTypeAllBanks}), isBankFrame, kBanks, 2.0, 0.5);
    if (banks.size() != kBanks) throw std::runtime_error("backup: expected 16 banks, got " + std::to_string(banks.size()));
    out.insert(out.end(), banks.begin(), banks.end());
    return out;
}

void Device::sendOneAcked(const Bytes& frame, size_t n, double timeout) {
    send(frame);
    auto deadline = after(timeout);
    while (true) {
        double left = std::chrono::duration<double>(deadline - Clock::now()).count();
        auto m = left > 0 ? receive(left) : std::nullopt;
        if (!m) throw std::runtime_error("frame " + std::to_string(n) + ": no ACK within timeout");
        if (isAck(*m)) return;
        auto f = parseFrame(*m);
        if (f && f->cmd == kCmdAck) throw std::runtime_error("frame " + std::to_string(n) + ": device replied " + hex(*m));
    }
}

void Device::sendAcked(const std::vector<Bytes>& frames, double timeout) {
    drain();
    for (size_t n = 0; n < frames.size(); ++n) sendOneAcked(frames[n], n, timeout);
}

void Device::restore(const std::vector<Bytes>& frames, const std::function<void(size_t, size_t)>& progress) {
    size_t lastSampleFrame = SIZE_MAX;
    for (size_t n = 0; n < frames.size(); ++n) {
        auto f = parseFrame(frames[n]);
        if (f && f->cmd == kCmdData && !f->payload.empty() && f->payload[0] == kTypeSampleData) lastSampleFrame = n;
    }
    drain();
    for (size_t n = 0; n < frames.size(); ++n) {
        sendOneAcked(frames[n], n, n == lastSampleFrame ? 600.0 : 5.0);
        if (progress) progress(n + 1, frames.size());
    }
}

Device::UploadResult Device::uploadUnassigned(const std::string& name, const Bytes& pcm) {
    auto before = readAllBanks();
    auto existing = pool();
    std::string trimmed = name.substr(0, name.find_last_not_of(' ') + 1);  // pool names have no trailing spaces
    for (const PoolEntry& p : existing)
        if (p.name == trimmed)  // the device silently refuses duplicate names
            throw UploadRejected("a sample named \"" + name + "\" is already in the pool");
    int newIndex = int(existing.size());
    sendAcked(uploadFrames(name, pcm));

    // After an upload's last ACK the device is silent for ~1.1-1.3 s (saving to flash) and may drop
    // requests sent meanwhile, so send one request and wait for it rather than retrying.
    if (int(pool(5.0).size()) != newIndex + 1)
        throw UploadRejected("the device did not store \"" + name + "\" (nothing was changed)");

    auto now = readAllBanks();
    UploadResult result{newIndex, std::nullopt};
    for (int b = 0; b < kBanks; ++b) {
        if (now[b] == before[b]) continue;
        std::vector<int> changed;
        for (int s = 0; s < kSlots; ++s)
            if (now[b].slot(s) != before[b].slot(s)) changed.push_back(s);
        Bank probe = now[b];
        if (changed.size() == 1) probe.setSlot(changed[0], before[b].slot(changed[0]));
        bool expected = changed.size() == 1 && probe == before[b] && before[b].slot(changed[0]) == kSlotEmpty &&
                        now[b].slot(changed[0]) == newIndex && !result.devicePlacement;
        if (!expected)
            throw std::runtime_error("unexpected change in bank " + std::to_string(b + 1) + "; not touching it");
        result.devicePlacement = Placement{b, changed[0]};
    }
    if (auto p = result.devicePlacement) {
        writeBank(p->bank, before[p->bank]);
        if (!(readBank(p->bank) == before[p->bank]))
            throw std::runtime_error("bank " + std::to_string(p->bank + 1) + " did not restore cleanly");
    }
    return result;
}

}  // namespace lmd
