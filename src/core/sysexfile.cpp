// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sysexfile.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace lmd {

std::vector<Bytes> readSysexFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    Bytes raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<Bytes> frames;
    size_t start = 0;
    bool inFrame = false;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == 0xF0) {
            start = i;
            inFrame = true;
        } else if (raw[i] == 0xF7 && inFrame) {
            frames.emplace_back(raw.begin() + start, raw.begin() + i + 1);
            inFrame = false;
        }
    }
    return frames;
}

void writeSysexFile(const std::string& path, const std::vector<Bytes>& frames) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path);
    for (const Bytes& f : frames) out.write(reinterpret_cast<const char*>(f.data()), std::streamsize(f.size()));
    if (!out) throw std::runtime_error("write failed: " + path);
}

}  // namespace lmd
