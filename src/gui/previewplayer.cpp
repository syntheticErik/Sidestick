// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewplayer.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>

#include <algorithm>
#include <cstring>
#include <vector>

using namespace lmd;

PreviewPlayer::PreviewPlayer(QObject* owner) : owner_(owner) {}

PreviewPlayer::~PreviewPlayer() { stop(); }

void PreviewPlayer::stop() {
    if (sink_) {
        sink_->disconnect();
        sink_->stop();
        sink_.reset();
    }
    buffer_.close();
    onFinished_ = nullptr;
}

double PreviewPlayer::position() const { return sink_ ? sink_->processedUSecs() / 1e6 : -1.0; }

void PreviewPlayer::play(const Bytes& pcm, bool twelveBit, std::function<void()> onFinished) {
    stop();
    std::vector<float> samples(pcm.size() / 2);
    for (size_t i = 0; i < samples.size(); ++i) {
        int v = int16_t(rd16(pcm, 2 * i));
        if (twelveBit) v = std::clamp((v + 8) & ~15, -32768, 32752);  // round to 12-bit steps
        samples[i] = float(v) / 32768.0f;
    }

    QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    QAudioFormat fmt;
    fmt.setSampleRate(kSampleRate);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Float);
    if (!dev.isFormatSupported(fmt)) fmt = dev.preferredFormat();

    // Convert to the output format: linear-interpolated rate change (preview only), all channels equal.
    int rate = fmt.sampleRate(), ch = fmt.channelCount();
    size_t outFrames = size_t(double(samples.size()) * rate / kSampleRate);
    data_.resize(qsizetype(outFrames * size_t(ch) * size_t(fmt.bytesPerSample())));
    char* out = data_.data();
    for (size_t i = 0; i < outFrames; ++i) {
        double pos = double(i) * kSampleRate / rate;
        size_t k = size_t(pos);
        double t = pos - double(k);
        float a = k < samples.size() ? samples[k] : 0.0f;
        float b = k + 1 < samples.size() ? samples[k + 1] : a;
        float s = float(a + (b - a) * t);
        for (int c = 0; c < ch; ++c) {
            switch (fmt.sampleFormat()) {
            case QAudioFormat::Float: std::memcpy(out, &s, 4); break;
            case QAudioFormat::Int32: { int32_t v = int32_t(s * 2147483520.0f); std::memcpy(out, &v, 4); break; }
            case QAudioFormat::Int16: { int16_t v = int16_t(s * 32767.0f); std::memcpy(out, &v, 2); break; }
            default: { uint8_t v = uint8_t(s * 127.0f + 128.0f); std::memcpy(out, &v, 1); break; }
            }
            out += fmt.bytesPerSample();
        }
    }

    buffer_.setData(data_);
    buffer_.open(QIODevice::ReadOnly);
    sink_ = std::make_unique<QAudioSink>(dev, fmt);
    onFinished_ = std::move(onFinished);
    QObject::connect(sink_.get(), &QAudioSink::stateChanged, owner_, [this](QAudio::State st) {
        if (st != QAudio::IdleState) return;
        // Deferred: the sink can't be destroyed from inside its own signal.
        QMetaObject::invokeMethod(owner_, [this, sink = sink_.get()] {
            if (sink_.get() != sink) return;  // something else started meanwhile
            auto done = onFinished_;
            stop();
            if (done) done();
        }, Qt::QueuedConnection);
    });
    sink_->start(&buffer_);
}
