// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
#include "waveformview.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

using namespace lmd;

namespace {

constexpr int kGrab = 6;                    // px: how close the mouse must be to grab a handle
constexpr size_t kMinFrames = kSampleRate / 1000;  // 1 ms minimum selection

}  // namespace

WaveformView::WaveformView(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void WaveformView::setPcm(const Bytes* pcm) {
    pcm_ = pcm;
    start_ = 0;
    end_ = frames();
    playhead_ = -1;
    update();
}

void WaveformView::setSelection(size_t start, size_t end) {
    start_ = std::min(start, frames());
    end_ = std::clamp(end, start_, frames());
    update();
}

void WaveformView::setPlayhead(long frame) {
    if (frame == playhead_) return;
    playhead_ = frame;
    update();
}

double WaveformView::xOf(size_t frame) const {
    return frames() ? 1.0 + double(frame) / double(frames()) * (width() - 3) : 1.0;
}

size_t WaveformView::frameAt(double x) const {
    double t = std::clamp((x - 1.0) / std::max(1, width() - 3), 0.0, 1.0);
    return size_t(std::lround(t * double(frames())));
}

int WaveformView::handleNear(double x) const {
    if (!frames()) return -1;
    double ds = std::abs(x - xOf(start_)), de = std::abs(x - xOf(end_));
    if (std::min(ds, de) > kGrab) return -1;
    return ds <= de ? 0 : 1;
}

void WaveformView::mousePressEvent(QMouseEvent* e) {
    dragging_ = handleNear(e->position().x());
}

void WaveformView::mouseMoveEvent(QMouseEvent* e) {
    double x = e->position().x();
    if (dragging_ < 0) {
        setCursor(handleNear(x) >= 0 ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }
    size_t f = frameAt(x);
    if (dragging_ == 0) start_ = std::min(f, end_ > kMinFrames ? end_ - kMinFrames : 0);
    else end_ = std::max(f, std::min(start_ + kMinFrames, frames()));
    update();
    if (onChanged_) onChanged_(start_, end_);
}

void WaveformView::mouseReleaseEvent(QMouseEvent*) { dragging_ = -1; }

void WaveformView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QPalette& pal = palette();
    QRect r = rect().adjusted(0, 0, -1, -1);
    p.fillRect(r, pal.base());
    p.setPen(pal.mid().color());
    p.drawRect(r);
    const size_t n = frames();
    if (!n) return;

    const int w = std::max(1, r.width() - 1), mid = r.center().y();
    const double half = (r.height() - 4) / 2.0;
    QColor wave = pal.text().color();
    QColor dim = pal.placeholderText().color();
    for (int x = 0; x < w; ++x) {
        size_t a = size_t(double(x) * n / w), b = std::max(a + 1, size_t(double(x + 1) * n / w));
        int lo = 0, hi = 0;
        for (size_t i = a; i < b && i < n; ++i) {
            int v = int16_t(rd16(*pcm_, 2 * i));
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        p.setPen(a >= start_ && a < end_ ? wave : dim);
        p.drawLine(r.left() + 1 + x, mid - int(hi / 32768.0 * half), r.left() + 1 + x, mid - int(lo / 32768.0 * half));
    }
    p.setPen(pal.mid().color());
    p.drawLine(r.left() + 1, mid, r.right() - 1, mid);

    // Trimmed-off regions, then the handles.
    QColor shade = pal.window().color();
    shade.setAlpha(150);
    int xs = int(xOf(start_)), xe = int(xOf(end_));
    if (xs > r.left() + 1) p.fillRect(QRect(QPoint(r.left() + 1, r.top() + 1), QPoint(xs, r.bottom() - 1)), shade);
    if (xe < r.right() - 1) p.fillRect(QRect(QPoint(xe, r.top() + 1), QPoint(r.right() - 1, r.bottom() - 1)), shade);
    QPen handle(pal.highlight().color(), 2);
    p.setPen(handle);
    for (int x : {xs, xe}) {
        p.drawLine(x, r.top() + 1, x, r.bottom() - 1);
        p.drawRect(x - 3, r.top() + 1, 6, 6);
    }

    if (playhead_ >= 0) {
        p.setPen(QPen(QColor(230, 60, 60), 2));  // fixed red: visible on light and dark themes
        int x = int(xOf(size_t(playhead_)));
        p.drawLine(x, r.top() + 1, x, r.bottom() - 1);
    }
}
