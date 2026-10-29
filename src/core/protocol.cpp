// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
#include "protocol.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace lmd {

Bytes makeFrame(uint8_t cmd, const Bytes& payload) {
    Bytes f(std::begin(kHeader), std::end(kHeader));
    f.push_back(cmd);
    f.insert(f.end(), payload.begin(), payload.end());
    f.push_back(0xF7);
    return f;
}

std::optional<Frame> parseFrame(const Bytes& raw) {
    constexpr size_t h = sizeof(kHeader);
    if (raw.size() < h + 2 || !std::equal(std::begin(kHeader), std::end(kHeader), raw.begin()) || raw.back() != 0xF7)
        return std::nullopt;
    return Frame{raw[h], Bytes(raw.begin() + h + 1, raw.end() - 1)};
}

bool isAck(const Bytes& raw) {
    auto f = parseFrame(raw);
    return f && f->cmd == kCmdAck && f->payload == Bytes{0x00, 0x00};
}

Bytes pack7(const Bytes& data) {
    Bytes out;
    out.reserve((data.size() + 6) / 7 * 8);
    for (size_t g = 0; g < data.size(); g += 7) {
        uint8_t grp[7] = {};
        for (size_t i = 0; i < 7 && g + i < data.size(); ++i) grp[i] = data[g + i];
        uint8_t msb = 0;
        for (int i = 0; i < 7; ++i)
            if (grp[i] & 0x80) msb |= uint8_t(1 << i);
        out.push_back(msb);
        for (uint8_t b : grp) out.push_back(b & 0x7F);
    }
    return out;
}

Bytes unpack7(const uint8_t* data, size_t len) {
    Bytes out;
    out.reserve(len / 8 * 7 + 7);
    for (size_t g = 0; g < len; g += 8) {
        uint8_t msb = data[g];
        for (size_t i = 1; i < 8 && g + i < len; ++i)
            out.push_back(uint8_t(data[g + i] | ((msb >> (i - 1)) & 1 ? 0x80 : 0)));
    }
    return out;
}

Bytes dataFrame(uint8_t type, const Bytes& data) {
    Bytes payload{type, 0x00, 0x00};
    Bytes packed = pack7(data);
    payload.insert(payload.end(), packed.begin(), packed.end());
    return makeFrame(kCmdData, payload);
}

Bytes dataContents(const Frame& frame) {
    if (frame.payload.size() < 3) return {};
    return unpack7(frame.payload.data() + 3, frame.payload.size() - 3);
}

std::vector<Bytes> uploadFrames(const std::string& name, const Bytes& pcm) {
    Bytes nm(16, 0);
    for (size_t i = 0; i < nm.size() && i < name.size(); ++i) nm[i] = uint8_t(name[i]);

    // Confirmed on 5 SynthTribe uploads: byte sum of PCM + rate + bits + byte sum of name.
    uint64_t sum = kSampleRate + kBitsPerSample;
    for (uint8_t b : pcm) sum += b;
    for (uint8_t b : nm) sum += b;

    Bytes header(16);
    wr32(header, 0, uint32_t(pcm.size()));
    wr32(header, 4, uint32_t(sum));
    wr32(header, 8, kSampleRate);
    wr32(header, 12, kBitsPerSample);
    header.insert(header.end(), nm.begin(), nm.end());

    std::vector<Bytes> frames{dataFrame(kTypeUploadHeader, header)};
    for (size_t i = 0; i < pcm.size(); i += kChunk) {
        size_t end = std::min(i + kChunk, pcm.size());
        frames.push_back(dataFrame(kTypeUploadData, Bytes(pcm.begin() + i, pcm.begin() + end)));
    }
    return frames;
}

std::string hex(const Bytes& b, size_t max) {
    std::string s;
    char buf[4];
    for (size_t i = 0; i < b.size() && i < max; ++i) {
        std::snprintf(buf, sizeof buf, i ? " %02x" : "%02x", b[i]);
        s += buf;
    }
    if (b.size() > max) s += " ...";
    return s;
}

}  // namespace lmd
