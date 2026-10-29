// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// "Prepare upload" window: file list on the left, an editor for the selected file on the right
// (waveform with trim handles, name, preview), batch actions, memory summary, and Upload at the
// bottom. Files are converted to the device format when the window opens; settings are per file,
// and preview, lengths, and the upload all use the processed (trimmed, normalized) audio, kept at
// full precision and rounded to 16-bit once at the end.
#pragma once

#include <QDialog>
#include <QSet>
#include <QStringList>

#include <string>
#include <vector>

#include "previewplayer.h"
#include "processing.h"
#include "protocol.h"

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTimer;
class QToolButton;
class QTreeWidget;
class WaveformView;

class UploadDialog : public QDialog {
public:
    struct Item {
        std::string name;  // device name (upper-case, up to 16 characters)
        lmd::Bytes pcm;    // 24 kHz, 16-bit mono, processed
    };

    // poolNames: names already on the device (the device refuses duplicates).
    UploadDialog(const QStringList& files, uint64_t freeBytes, const QSet<QString>& poolNames, QWidget* parent);
    std::vector<Item> items() const;

private:
    struct Entry {
        QString file;
        QString name;
        lmd::Samples audio;  // converted, unprocessed, full precision
        QString error;
        size_t start = 0, end = 0;  // trim, in frames
        bool fadeOut = true;        // 5 ms fade when the end is trimmed
        bool normalize = false;     // peak-normalize to kTargetDbfs
    };

    static void trimSilence(Entry& e);
    double gain(int row) const;           // linear gain for this row (1 if not normalized)
    lmd::Bytes processed(int row) const;  // trimmed, gained, faded, 16-bit
    bool changed(int row) const;
    bool usable(int row) const { return row >= 0 && entries_[size_t(row)].error.isEmpty(); }
    QString conflict(int row) const;  // why this row's name can't be uploaded, or empty
    void makeNamesUnique();

    void select(int row);
    void showTrim();       // editor fields + waveform from entries_[current_]
    void trimChanged();    // after any trim edit of the current entry
    void updateRow(int row);
    void updateInfo();
    void updateSummary();
    void levelsChanged();  // gains may have changed on any row (group normalize)
    void refreshWave();
    void togglePreview(int row);
    void showPlaying(int row);

    std::vector<Entry> entries_;
    uint64_t freeBytes_;
    QSet<QString> poolNames_;
    int current_ = -1;
    int playing_ = -1;
    bool syncing_ = false;  // suppress feedback while updating widgets

    QTreeWidget* list_;
    std::vector<QToolButton*> playButtons_;
    QLabel* title_;
    WaveformView* wave_;
    QLabel* info_;
    QDoubleSpinBox* startBox_;
    QDoubleSpinBox* endBox_;
    QPushButton* trimSilence_;
    QPushButton* resetTrim_;
    QCheckBox* fadeOut_;
    QRadioButton* asIs_;
    QRadioButton* normalize_;
    QCheckBox* group_;
    lmd::Bytes waveData_;  // what the waveform shows: the current file with its gain applied
    QLineEdit* name_;
    QPushButton* preview_;
    QCheckBox* twelveBit_;
    QLabel* summary_;
    QPushButton* makeUnique_;
    QPushButton* uploadButton_;
    QTimer* playheadTimer_;
    PreviewPlayer player_{this};
};
