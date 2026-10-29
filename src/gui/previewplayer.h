// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// Plays device-format PCM (24 kHz, 16-bit mono) on the computer's default audio output.
#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QObject>

#include <functional>
#include <memory>

#include "protocol.h"

class QAudioSink;

class PreviewPlayer {
public:
    explicit PreviewPlayer(QObject* owner);
    ~PreviewPlayer();

    // Plays `pcm`; with twelveBit, reduced to 12-bit first, the way the device plays samples.
    // Stops anything already playing. onFinished is called (on the GUI thread) when playback ends.
    void play(const lmd::Bytes& pcm, bool twelveBit, std::function<void()> onFinished = nullptr);
    void stop();
    // Seconds of audio played so far, or -1 when idle (for a playhead).
    double position() const;

private:
    QObject* owner_;
    std::unique_ptr<QAudioSink> sink_;
    QByteArray data_;
    QBuffer buffer_;
    std::function<void()> onFinished_;
};
