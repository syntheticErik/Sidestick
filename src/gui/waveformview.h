// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// Waveform display for device-format PCM (24 kHz, 16-bit mono), with draggable start/end trim
// handles (the trimmed-off parts are dimmed) and an optional playhead.
#pragma once

#include <QWidget>

#include <functional>

#include "protocol.h"

class WaveformView : public QWidget {
public:
    explicit WaveformView(QWidget* parent = nullptr);

    void setPcm(const lmd::Bytes* pcm);  // not owned; nullptr clears
    void setSelection(size_t start, size_t end);  // frames, [start, end)
    void setPlayhead(long frame);  // -1 hides it
    // Called while a handle is dragged.
    void setOnSelectionChanged(std::function<void(size_t, size_t)> f) { onChanged_ = std::move(f); }

    QSize sizeHint() const override { return {520, 160}; }
    QSize minimumSizeHint() const override { return {200, 80}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    size_t frames() const { return pcm_ ? pcm_->size() / 2 : 0; }
    double xOf(size_t frame) const;
    size_t frameAt(double x) const;
    int handleNear(double x) const;  // 0 = start, 1 = end, -1 = none

    const lmd::Bytes* pcm_ = nullptr;
    size_t start_ = 0, end_ = 0;
    long playhead_ = -1;
    int dragging_ = -1;
    std::function<void(size_t, size_t)> onChanged_;
};
