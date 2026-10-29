// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
#include "model.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace lmd {

namespace {

constexpr size_t kRecord = 32;
constexpr size_t kVoiceRecord = 14;
constexpr size_t kVoiceSlotOffset = 4;
constexpr size_t kSlotTable = 0xE0;
constexpr size_t kNameOffset = 0x1E0;
constexpr size_t kNameLen = 16;

bool isSmph(const Bytes& b, size_t i) { return i + kRecord <= b.size() && std::memcmp(&b[i], "SMPH", 4) == 0; }

std::string recordName(const Bytes& b, size_t i) {
    std::string s;
    for (size_t k = 8; k < 28 && b[i + k]; ++k) s += char(b[i + k]);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}  // namespace

std::vector<PoolEntry> parsePool(const std::vector<Frame>& frames) {
    std::vector<PoolEntry> out;
    for (const Frame& f : frames) {
        Bytes data = dataContents(f);
        data.resize(data.size() / kRecord * kRecord);  // drop packing padding
        for (size_t i = 0; isSmph(data, i); i += kRecord)
            out.push_back({recordName(data, i), rd32(data, i + 4), rd32(data, i + 28)});
    }
    return out;
}

std::string deviceName(const std::string& name, size_t maxLen) {
    std::string s;
    for (char c : name) {
        if (s.size() == maxLen) break;
        unsigned char u = static_cast<unsigned char>(c);
        s += (u >= 0x20 && u < 0x7F) ? char(std::toupper(u)) : '_';
    }
    return s;
}

std::optional<Bank> Bank::fromBytes(const Bytes& data) {
    if (data.size() < kSize) return std::nullopt;
    Bank b;
    b.raw_.assign(data.begin(), data.begin() + kSize);
    return b;
}

uint16_t Bank::slot(int s) const { return rd16(raw_, kSlotTable + 2 * size_t(s)); }
void Bank::setSlot(int s, uint16_t poolIndex) { wr16(raw_, kSlotTable + 2 * size_t(s), poolIndex); }
uint16_t Bank::voiceSlot(int v) const { return rd16(raw_, size_t(v) * kVoiceRecord + kVoiceSlotOffset); }
void Bank::setVoiceSlot(int v, uint16_t s) { wr16(raw_, size_t(v) * kVoiceRecord + kVoiceSlotOffset, s); }

std::string Bank::name() const {
    std::string s(raw_.begin() + kNameOffset, raw_.begin() + kNameOffset + kNameLen);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

void Bank::setName(const std::string& name) {
    std::string s = deviceName(name, kNameLen);
    s.resize(kNameLen, ' ');
    std::memcpy(&raw_[kNameOffset], s.data(), kNameLen);
}

void Bank::turnOffVoice(int voice) {
    int leader = voiceGroupLeader(voice);
    for (int v = 0; v < kVoices; ++v)
        if (voiceGroupLeader(v) == leader) setVoiceSlot(v, 0);
}

void Bank::putSample(int s, uint16_t poolIndex) {
    if (slot(s) == kSlotEmpty)
        for (int v = 0; v < kVoices; ++v)
            if (voiceGroupLeader(v) == v && voiceSlot(v) == s) turnOffVoice(v);
    setSlot(s, poolIndex);
}

void Bank::clear() {
    *this = empty();
    for (int v = 0; v < kVoices; ++v) setVoiceSlot(v, 0);
}

Bank Bank::empty() {
    // Matches the device's untouched banks byte for byte (checked against bank 14 in the tests).
    Bank b;
    b.raw_.assign(kSize, 0);
    for (int v = 0; v < kVoices; ++v) {
        size_t r = size_t(v) * kVoiceRecord;
        b.raw_[r] = 0xFF;
        b.raw_[r + 2] = 0x7F;  // level 127
        b.raw_[r + 3] = 0x0C;  // 12-bit
        wr16(b.raw_, r + 4, kDefaultVoiceSlot[v]);
        wr16(b.raw_, r + 8, 12000);
        uint16_t tune = (v == 5 || v == 12) ? 3200 : (v == 7 || v == 13) ? 1600 : 2400;  // cents + 2400
        wr16(b.raw_, r + 12, tune);
    }
    b.setSlot(0, kSlotOff);
    for (int s = 1; s < kSlots; ++s) b.setSlot(s, kSlotEmpty);
    b.setName("---");
    return b;
}

void Bank::clearKit() {
    Bank e = empty();
    std::copy(e.raw_.begin(), e.raw_.begin() + kSlotTable, raw_.begin());
    for (int v = 0; v < kVoices; ++v) setVoiceSlot(v, 0);
}

Backup parseBackup(const std::vector<Bytes>& rawFrames) {
    Backup out;
    Bytes blob;
    for (const Bytes& raw : rawFrames) {
        auto f = parseFrame(raw);
        if (!f || f->cmd != kCmdData || f->payload.empty()) continue;
        uint8_t type = f->payload[0];
        Bytes data = dataContents(*f);
        if (type == kTypeSampleData) {
            data.resize(std::min(data.size(), kChunk));
            blob.insert(blob.end(), data.begin(), data.end());
        } else if ((type & 0xF0) == kTypeBank) {
            if (auto b = Bank::fromBytes(data)) out.banks[type & 0x0F] = *b;
        }
    }
    size_t pos = 0;
    while (pos < blob.size()) {
        // Records are 4-byte aligned, but the stream sometimes jumps to the next frame boundary.
        if (!isSmph(blob, pos)) {
            pos = (pos + kChunk - 1) / kChunk * kChunk;
            if (!isSmph(blob, pos)) break;
        }
        uint32_t frames = rd32(blob, pos + 28);
        size_t start = pos + kRecord, end = start + size_t(frames) * 2;
        if (end > blob.size()) throw std::runtime_error("backup: sample data truncated");
        out.samples.push_back({recordName(blob, pos), Bytes(blob.begin() + start, blob.begin() + end)});
        pos = (end + 3) & ~size_t(3);
    }
    return out;
}

std::optional<Bank> bankFromExport(const std::vector<Bytes>& rawFrames, std::string* error) {
    auto f = rawFrames.size() == 1 ? parseFrame(rawFrames[0]) : std::nullopt;
    if (!f || f->cmd != kCmdData || f->payload.empty() || (f->payload[0] & 0xF0) != kTypeBank) {
        if (error) *error = "not a single-bank export";
        return std::nullopt;
    }
    auto b = Bank::fromBytes(dataContents(*f));
    if (!b && error) *error = "short bank data";
    return b;
}

std::vector<int> missingSampleSlots(const Bank& bank, size_t poolSize) {
    std::vector<int> out;
    for (int s = 1; s < kSlots; ++s)
        if (bank.slot(s) != kSlotEmpty && bank.slot(s) >= poolSize) out.push_back(s);
    return out;
}

std::vector<std::string> restoreProblems(const std::vector<Bytes>& rawFrames) {
    std::vector<std::string> problems;
    bool seenBank = false;
    for (size_t i = 0; i < rawFrames.size(); ++i) {
        auto f = parseFrame(rawFrames[i]);
        uint8_t type = f && f->cmd == kCmdData && !f->payload.empty() ? f->payload[0] : 0;
        if (type == kTypeSampleData && !seenBank) continue;
        if ((type & 0xF0) == kTypeBank) {
            seenBank = true;
            continue;
        }
        problems.push_back("frame " + std::to_string(i) + " is not sample data or a bank (or is out of order)");
        break;
    }
    Backup bk;
    try {
        bk = parseBackup(rawFrames);
    } catch (const std::exception& e) {
        problems.push_back(e.what());
        return problems;
    }
    if (bk.banks.size() != kBanks) problems.push_back("expected 16 banks, found " + std::to_string(bk.banks.size()));
    size_t poolSize = kFactorySamples + bk.samples.size();
    for (auto& [b, bank] : bk.banks)
        for (int s = 1; s < kSlots; ++s)
            if (bank.slot(s) != kSlotEmpty && bank.slot(s) >= poolSize)
                problems.push_back("bank " + std::to_string(b + 1) + " slot " + std::to_string(s) + " refers to pool index " +
                                   std::to_string(bank.slot(s)) + ", beyond the restored pool");
    uint64_t used = 0;
    for (auto& s : bk.samples) used += sampleStorageBytes(uint32_t(s.pcm.size() / 2));
    if (used > kUserMemoryBytes) problems.push_back("samples need more than the device's user memory");
    return problems;
}

}  // namespace lmd
