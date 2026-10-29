// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// Play/stop icons drawn in the widget palette's text colour, so they stay visible on light and
// dark themes alike (some styles' stock media icons are always black).
#pragma once

#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPolygonF>

inline QIcon mediaIcon(const QPalette& pal, bool stop) {
    QIcon icon;
    for (int size : {16, 24, 32, 48}) {
        QPixmap px(size, size);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(pal.color(QPalette::ButtonText));
        const double s = size;
        if (stop) p.drawRect(QRectF(s * 0.25, s * 0.25, s * 0.5, s * 0.5));
        else p.drawPolygon(QPolygonF({{s * 0.28, s * 0.2}, {s * 0.28, s * 0.8}, {s * 0.8, s * 0.5}}));
        icon.addPixmap(px);
    }
    return icon;
}
