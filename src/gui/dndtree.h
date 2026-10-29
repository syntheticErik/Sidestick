// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// QTreeWidget with simple typed drag and drop: rows carry an integer (pool index, slot, or voice)
// under a custom MIME type, and drops are handed to a callback with the target row.
#pragma once

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTreeWidget>
#include <QUrl>

#include <functional>

inline constexpr int kItemValueRole = Qt::UserRole;

class DndTree : public QTreeWidget {
public:
    // Rows of this tree can be dragged as `type` carrying their kItemValueRole value.
    void setDragType(const QString& type) {
        dragType_ = type;
        setDragEnabled(true);
        setDragDropMode(acceptType_.isEmpty() ? DragOnly : DragDrop);
    }
    // Drops of `type` onto a row call onDrop(row, value).
    void setDropHandler(const QString& type, std::function<void(QTreeWidgetItem*, int)> onDrop) {
        acceptType_ = type;
        onDrop_ = std::move(onDrop);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(dragType_.isEmpty() ? DropOnly : DragDrop);
    }

    // Files dropped from outside the app (e.g. a file manager) call onFiles(local paths).
    void setFileDropHandler(std::function<void(const QStringList&)> onFiles) {
        onFiles_ = std::move(onFiles);
        setAcceptDrops(true);
        setDragDropMode(dragType_.isEmpty() ? DropOnly : DragDrop);
    }

protected:
    QStringList mimeTypes() const override { return {dragType_}; }

    bool isFileDrop(const QMimeData* m) const { return onFiles_ && m->hasUrls(); }

    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
        if (items.isEmpty() || dragType_.isEmpty()) return nullptr;
        auto* m = new QMimeData;
        m->setData(dragType_, QByteArray::number(items.first()->data(0, kItemValueRole).toInt()));
        return m;
    }

    void dragEnterEvent(QDragEnterEvent* e) override {
        if (isFileDrop(e->mimeData()) || (!acceptType_.isEmpty() && e->mimeData()->hasFormat(acceptType_)))
            e->acceptProposedAction();
        else e->ignore();
    }

    void dragMoveEvent(QDragMoveEvent* e) override {
        if (isFileDrop(e->mimeData())) {
            e->acceptProposedAction();
            return;
        }
        QTreeWidgetItem* target = itemAt(e->position().toPoint());
        if (target && e->mimeData()->hasFormat(acceptType_)) {
            setCurrentItem(target);
            e->acceptProposedAction();
        } else {
            e->ignore();
        }
    }

    void dropEvent(QDropEvent* e) override {
        if (isFileDrop(e->mimeData())) {
            QStringList files;
            for (const QUrl& u : e->mimeData()->urls())
                if (u.isLocalFile()) files << u.toLocalFile();
            e->acceptProposedAction();
            if (!files.isEmpty()) onFiles_(files);
            return;
        }
        QTreeWidgetItem* target = itemAt(e->position().toPoint());
        if (!target || !e->mimeData()->hasFormat(acceptType_)) {
            e->ignore();
            return;
        }
        e->acceptProposedAction();
        onDrop_(target, e->mimeData()->data(acceptType_).toInt());
    }

private:
    QString dragType_;
    QString acceptType_;
    std::function<void(QTreeWidgetItem*, int)> onDrop_;
    std::function<void(const QStringList&)> onFiles_;
};
