// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
#include "processing.h"

#include <algorithm>
#include <cmath>

namespace lmd {

Samples slice(const Samples& s, size_t start, size_t end) {
    end = std::min(end, s.size());
    start = std::min(start, end);
    return Samples(s.begin() + long(start), s.begin() + long(end));
}

void fadeOut(Samples& s, size_t frames) {
    size_t n = s.size(), fade = std::min(frames, n);
    for (size_t k = 0; k < fade; ++k) s[n - fade + k] *= float(double(fade - k - 1) / double(fade));  // 0 at the end
}

void applyGain(Samples& s, double gain) {
    for (float& v : s) v = float(v * gain);
}

std::pair<size_t, size_t> soundBounds(const Samples& s, double thresholdDbfs, size_t lead, size_t tail) {
    const double thr = fromDb(thresholdDbfs);
    size_t first = s.size(), last = 0;
    for (size_t i = 0; i < s.size(); ++i)
        if (std::fabs(s[i]) > thr) {
            if (first == s.size()) first = i;
            last = i;
        }
    if (first == s.size()) return {0, s.size()};
    return {first > lead ? first - lead : 0, std::min(s.size(), last + 1 + tail)};
}

double peak(const Samples& s, size_t start, size_t end) {
    end = std::min(end, s.size());
    double p = 0;
    for (size_t i = std::min(start, end); i < end; ++i) p = std::max(p, double(std::fabs(s[i])));
    return p;
}

}  // namespace lmd
