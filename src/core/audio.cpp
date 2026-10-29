// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.h"

#include <samplerate.h>
#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace lmd {

Bytes convertToDevicePcm(const std::string& path) { return quantize16(convertToDeviceFloat(path)); }

Bytes quantize16(const std::vector<float>& samples) {
    Bytes pcm(samples.size() * 2);
    for (size_t i = 0; i < samples.size(); ++i) {
        long v = std::lround(double(samples[i]) * 32768.0);
        wr16(pcm, 2 * i, uint16_t(int16_t(std::clamp(v, -32768L, 32767L))));
    }
    return pcm;
}

std::vector<float> convertToDeviceFloat(const std::string& path) {
    SF_INFO info{};
    SNDFILE* f = sf_open(path.c_str(), SFM_READ, &info);
    if (!f) throw std::runtime_error(path + ": " + sf_strerror(nullptr));
    std::vector<float> interleaved(size_t(info.frames) * size_t(info.channels));
    sf_count_t got = sf_readf_float(f, interleaved.data(), info.frames);
    sf_close(f);

    std::vector<float> mono(static_cast<size_t>(got));
    for (size_t i = 0; i < mono.size(); ++i) {
        double s = 0;
        for (int c = 0; c < info.channels; ++c) s += interleaved[i * size_t(info.channels) + size_t(c)];
        mono[i] = float(s / info.channels);
    }

    if (info.samplerate != kSampleRate && !mono.empty()) {
        SRC_DATA d{};
        d.src_ratio = double(kSampleRate) / info.samplerate;
        std::vector<float> out(size_t(std::ceil(double(mono.size()) * d.src_ratio)) + 64);
        d.data_in = mono.data();
        d.input_frames = long(mono.size());
        d.data_out = out.data();
        d.output_frames = long(out.size());
        if (int err = src_simple(&d, SRC_SINC_BEST_QUALITY, 1))
            throw std::runtime_error(std::string("resample: ") + src_strerror(err));
        out.resize(size_t(d.output_frames_gen));
        mono = std::move(out);
    }

    return mono;
}

void writeDeviceWav(const std::string& path, const Bytes& pcm) {
    SF_INFO info{};
    info.samplerate = kSampleRate;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* f = sf_open(path.c_str(), SFM_WRITE, &info);
    if (!f) throw std::runtime_error(path + ": " + sf_strerror(nullptr));
    std::vector<short> samples(pcm.size() / 2);
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = short(int16_t(rd16(pcm, 2 * i)));
    sf_writef_short(f, samples.data(), sf_count_t(samples.size()));
    sf_close(f);
}

}  // namespace lmd
