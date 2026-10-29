// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
#include "uploaddialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QButtonGroup>
#include <QPushButton>
#include <QRadioButton>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <exception>
#include <tuple>

#include "audio.h"
#include "icons.h"
#include "model.h"
#include "processing.h"
#include "waveformview.h"

using namespace lmd;

namespace {

enum Column { kPlay, kName, kLength };

constexpr double kSilenceDbfs = -60.0;               // Trim silence threshold
constexpr size_t kLeadFrames = kSampleRate * 2 / 1000;  // keep 2 ms before the first sound
constexpr size_t kTailFrames = kSampleRate * 10 / 1000; // and 10 ms after the last
constexpr size_t kFadeFrames = kSampleRate * 5 / 1000;  // 5 ms fade-out at a trimmed end
constexpr double kTargetDbfs = -0.1;                    // Normalize peak target

QString seconds(size_t frames) { return QString("%1 s").arg(double(frames) / kSampleRate, 0, 'f', 3); }

}  // namespace

void UploadDialog::trimSilence(Entry& e) {
    std::tie(e.start, e.end) = soundBounds(e.audio, kSilenceDbfs, kLeadFrames, kTailFrames);
}

double UploadDialog::gain(int row) const {
    const Entry& e = entries_[size_t(row)];
    if (!e.normalize) return 1.0;
    double p = peak(e.audio, e.start, e.end);
    if (group_->isChecked())  // one gain for every normalized file: the loudest reaches the target
        for (int r = 0; r < int(entries_.size()); ++r)
            if (usable(r) && entries_[size_t(r)].normalize)
                p = std::max(p, peak(entries_[size_t(r)].audio, entries_[size_t(r)].start, entries_[size_t(r)].end));
    return p > 0 ? fromDb(kTargetDbfs) / p : 1.0;
}

Bytes UploadDialog::processed(int row) const {
    const Entry& e = entries_[size_t(row)];
    Samples out = slice(e.audio, e.start, e.end);
    applyGain(out, gain(row));
    if (e.fadeOut && e.end < e.audio.size()) fadeOut(out, kFadeFrames);
    return quantize16(out);
}

bool UploadDialog::changed(int row) const {
    const Entry& e = entries_[size_t(row)];
    return e.start > 0 || e.end < e.audio.size() || e.normalize;
}

void UploadDialog::refreshWave() {
    if (!usable(current_)) {
        wave_->setPcm(nullptr);
        return;
    }
    const Entry& e = entries_[size_t(current_)];
    Samples shown = e.audio;
    applyGain(shown, gain(current_));
    waveData_ = quantize16(shown);
    wave_->setPcm(&waveData_);
    wave_->setSelection(e.start, e.end);
}

void UploadDialog::levelsChanged() {
    player_.stop();
    showPlaying(-1);
    for (int r = 0; r < int(entries_.size()); ++r) updateRow(r);
    refreshWave();
    updateInfo();
}

UploadDialog::UploadDialog(const QStringList& files, uint64_t freeBytes, const QSet<QString>& poolNames,
                           QWidget* parent)
    : QDialog(parent), freeBytes_(freeBytes), poolNames_(poolNames) {
    setWindowTitle(tr("Prepare upload"));
    resize(960, 540);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const QString& f : files) {
        Entry e;
        e.file = f;
        e.name = QString::fromStdString(deviceName(QFileInfo(f).completeBaseName().toStdString()));
        try {
            e.audio = convertToDeviceFloat(f.toStdString());
            if (e.audio.empty()) e.error = tr("no audio");
        } catch (const std::exception& ex) {
            e.error = QString::fromStdString(ex.what());
        }
        e.end = e.audio.size();
        entries_.push_back(std::move(e));
    }
    QApplication::restoreOverrideCursor();

    // Left: file list.
    list_ = new QTreeWidget;
    list_->setHeaderLabels({QString(), tr("Sample"), tr("Length")});
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->header()->setSectionResizeMode(kName, QHeaderView::Stretch);
    list_->header()->setStretchLastSection(false);
    for (int r = 0; r < int(entries_.size()); ++r) {
        auto* it = new QTreeWidgetItem(list_);
        auto* play = new QToolButton;
        play->setIcon(mediaIcon(palette(), false));
        play->setToolTip(tr("Preview"));
        play->setAutoRaise(true);
        play->setEnabled(usable(r));
        connect(play, &QToolButton::clicked, this, [this, r] { togglePreview(r); });
        playButtons_.push_back(play);
        list_->setItemWidget(it, kPlay, play);
        it->setTextAlignment(kLength, Qt::AlignRight | Qt::AlignVCenter);
        updateRow(r);
    }
    list_->resizeColumnToContents(kPlay);
    list_->resizeColumnToContents(kLength);
    connect(list_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* it) { select(it ? list_->indexOfTopLevelItem(it) : -1); });

    // Right: editor for the selected file.
    auto* editor = new QWidget;
    auto* ev = new QVBoxLayout(editor);
    ev->setContentsMargins(8, 0, 0, 0);
    title_ = new QLabel;
    QFont tf = title_->font();
    tf.setBold(true);
    title_->setFont(tf);
    wave_ = new WaveformView;
    wave_->setToolTip(tr("Drag the handles to trim the start and end"));
    wave_->setOnSelectionChanged([this](size_t s, size_t e) {
        if (current_ < 0) return;
        entries_[size_t(current_)].start = s;
        entries_[size_t(current_)].end = e;
        trimChanged();
    });
    info_ = new QLabel;

    auto makeTime = [this] {
        auto* b = new QDoubleSpinBox;
        b->setDecimals(3);
        b->setSuffix(tr(" s"));
        b->setSingleStep(0.01);
        b->setKeyboardTracking(false);
        return b;
    };
    startBox_ = makeTime();
    endBox_ = makeTime();
    connect(startBox_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (syncing_ || current_ < 0) return;
        Entry& e = entries_[size_t(current_)];
        e.start = std::min(size_t(std::lround(v * kSampleRate)), e.end > 0 ? e.end - 1 : 0);
        showTrim();
        trimChanged();
    });
    connect(endBox_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (syncing_ || current_ < 0) return;
        Entry& e = entries_[size_t(current_)];
        e.end = std::clamp(size_t(std::lround(v * kSampleRate)), e.start + 1, e.audio.size());
        showTrim();
        trimChanged();
    });
    trimSilence_ = new QPushButton(tr("Trim silence"));
    trimSilence_->setToolTip(tr("Move the handles in to where the sound starts and ends (%1 dBFS)")
                                 .arg(kSilenceDbfs, 0, 'f', 0));
    connect(trimSilence_, &QPushButton::clicked, this, [this] {
        if (current_ < 0) return;
        trimSilence(entries_[size_t(current_)]);
        showTrim();
        trimChanged();
    });
    resetTrim_ = new QPushButton(tr("Reset"));
    connect(resetTrim_, &QPushButton::clicked, this, [this] {
        if (current_ < 0) return;
        Entry& e = entries_[size_t(current_)];
        e.start = 0;
        e.end = e.audio.size();
        showTrim();
        trimChanged();
    });
    fadeOut_ = new QCheckBox(tr("Fade out a trimmed end (5 ms, avoids clicks)"));
    connect(fadeOut_, &QCheckBox::toggled, this, [this](bool on) {
        if (syncing_ || current_ < 0) return;
        entries_[size_t(current_)].fadeOut = on;
        trimChanged();
    });

    asIs_ = new QRadioButton(tr("As is"));
    normalize_ = new QRadioButton(tr("Normalize peak (%1 dBFS)").arg(kTargetDbfs, 0, 'f', 1));
    asIs_->setChecked(true);
    auto* levelGroup = new QButtonGroup(this);
    levelGroup->addButton(asIs_);
    levelGroup->addButton(normalize_);
    connect(normalize_, &QRadioButton::toggled, this, [this](bool on) {
        if (syncing_ || current_ < 0) return;
        entries_[size_t(current_)].normalize = on;
        levelsChanged();
    });

    name_ = new QLineEdit;
    name_->setMaxLength(16);
    connect(name_, &QLineEdit::textEdited, this, [this](const QString& t) {
        if (current_ < 0) return;
        QString fixed = QString::fromStdString(deviceName(t.toStdString()));
        if (fixed != t) {
            int pos = name_->cursorPosition();
            name_->setText(fixed);
            name_->setCursorPosition(pos);
        }
        entries_[size_t(current_)].name = fixed;
        for (int r = 0; r < int(entries_.size()); ++r) updateRow(r);  // conflicts can change on other rows
        updateInfo();
        updateSummary();
    });
    preview_ = new QPushButton(mediaIcon(palette(), false), tr("Preview"));
    connect(preview_, &QPushButton::clicked, this, [this] {
        if (current_ >= 0) togglePreview(current_);
    });
    twelveBit_ = new QCheckBox(tr("12-bit, as the device plays it"));
    twelveBit_->setChecked(true);

    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Start")), 0, 0);
    grid->addWidget(startBox_, 0, 1);
    grid->addWidget(new QLabel(tr("End")), 0, 2);
    grid->addWidget(endBox_, 0, 3);
    grid->addWidget(trimSilence_, 0, 4);
    grid->addWidget(resetTrim_, 0, 5);
    grid->setColumnStretch(6, 1);
    grid->addWidget(fadeOut_, 1, 0, 1, 7);
    grid->addWidget(new QLabel(tr("Level")), 2, 0);
    auto* levelRow = new QHBoxLayout;
    levelRow->addWidget(asIs_);
    levelRow->addWidget(normalize_);
    levelRow->addStretch(1);
    grid->addLayout(levelRow, 2, 1, 1, 6);
    grid->addWidget(new QLabel(tr("Name")), 3, 0);
    grid->addWidget(name_, 3, 1, 1, 6);
    auto* previewRow = new QHBoxLayout;
    previewRow->addWidget(preview_);
    previewRow->addWidget(twelveBit_);
    previewRow->addStretch(1);
    ev->addWidget(title_);
    ev->addWidget(wave_, 1);
    ev->addWidget(info_);
    ev->addLayout(grid);
    ev->addLayout(previewRow);

    auto* split = new QSplitter;
    split->addWidget(list_);
    split->addWidget(editor);
    split->setStretchFactor(1, 1);
    split->setSizes({300, 660});

    // Bottom: batch actions, summary, buttons.
    auto* trimAll = new QPushButton(tr("Trim silence on all"));
    connect(trimAll, &QPushButton::clicked, this, [this] {
        for (int r = 0; r < int(entries_.size()); ++r)
            if (usable(r)) {
                trimSilence(entries_[size_t(r)]);
                updateRow(r);
            }
        if (current_ >= 0) showTrim();
        updateSummary();
        levelsChanged();  // trims change peaks, so gains too
    });
    group_ = new QCheckBox(tr("Normalize as a group"));
    group_->setToolTip(tr("Give every normalized file the same gain, so they keep their levels relative to each "
                          "other (the loudest reaches %1 dBFS)").arg(kTargetDbfs, 0, 'f', 1));
    connect(group_, &QCheckBox::toggled, this, [this] { levelsChanged(); });
    makeUnique_ = new QPushButton(tr("Make names unique"));
    makeUnique_->setToolTip(tr("Add a number to names that are already on the device or used twice"));
    connect(makeUnique_, &QPushButton::clicked, this, &UploadDialog::makeNamesUnique);
    summary_ = new QLabel;
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    uploadButton_ = buttons->addButton(tr("Upload"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(trimAll);
    bottom->addWidget(makeUnique_);
    bottom->addWidget(group_);
    bottom->addSpacing(12);
    bottom->addWidget(summary_, 1);
    bottom->addWidget(buttons);

    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(tr("Samples are converted to 24 kHz, 16-bit mono and added to the pool only "
                               "(not to any bank).")));
    v->addWidget(split, 1);
    v->addLayout(bottom);

    playheadTimer_ = new QTimer(this);
    playheadTimer_->setInterval(30);
    connect(playheadTimer_, &QTimer::timeout, this, [this] {
        double pos = player_.position();
        if (playing_ < 0 || playing_ != current_ || pos < 0) {
            wave_->setPlayhead(-1);
            return;
        }
        const Entry& e = entries_[size_t(playing_)];
        wave_->setPlayhead(long(std::min(e.end, e.start + size_t(pos * kSampleRate))));
    });

    updateSummary();
    if (!entries_.empty()) list_->setCurrentItem(list_->topLevelItem(0));
}

QString UploadDialog::conflict(int row) const {
    if (!usable(row)) return {};
    QString n = entries_[size_t(row)].name.trimmed();
    if (n.isEmpty()) return tr("needs a name");
    if (poolNames_.contains(n)) return tr("a sample with this name is already on the device");
    for (int r = 0; r < int(entries_.size()); ++r)
        if (r != row && usable(r) && entries_[size_t(r)].name.trimmed() == n) return tr("name used twice in this upload");
    return {};
}

void UploadDialog::makeNamesUnique() {
    QSet<QString> taken = poolNames_;
    for (int r = 0; r < int(entries_.size()); ++r) {
        if (!usable(r)) continue;
        Entry& e = entries_[size_t(r)];
        QString base = e.name.trimmed();
        QString n = base;
        for (int k = 2; n.isEmpty() || taken.contains(n); ++k) {
            QString suffix = QString(" %1").arg(k);
            n = (base.isEmpty() ? QStringLiteral("SAMPLE") : base).left(16 - int(suffix.size())).trimmed() + suffix;
        }
        e.name = n;
        taken.insert(n);
    }
    for (int r = 0; r < int(entries_.size()); ++r) updateRow(r);
    if (current_ >= 0 && usable(current_)) name_->setText(entries_[size_t(current_)].name);
    updateInfo();
    updateSummary();
}

void UploadDialog::updateRow(int row) {
    const Entry& e = entries_[size_t(row)];
    QTreeWidgetItem* it = list_->topLevelItem(row);
    bool trimmed = e.error.isEmpty() && changed(row);
    it->setText(kName, e.error.isEmpty() ? e.name + (trimmed ? " *" : "") : QFileInfo(e.file).fileName());
    it->setText(kLength, e.error.isEmpty() ? seconds(e.end - e.start) : tr("skipped"));
    it->setToolTip(kName, e.error.isEmpty() ? e.file + (trimmed ? tr("\n* trimmed or normalized") : QString())
                                            : tr("%1\nSkipped: %2").arg(e.file, e.error));
    if (!e.error.isEmpty())
        for (int c = 0; c < 3; ++c) it->setForeground(c, list_->palette().placeholderText());
    QString why = conflict(row);
    it->setIcon(kName, why.isEmpty() ? QIcon() : style()->standardIcon(QStyle::SP_MessageBoxWarning));
    if (!why.isEmpty()) it->setToolTip(kName, it->toolTip(kName) + "\n" + why);
}

void UploadDialog::select(int row) {
    player_.stop();
    showPlaying(-1);
    current_ = row;
    bool ok = usable(row);
    const Entry* e = row >= 0 ? &entries_[size_t(row)] : nullptr;
    title_->setText(e ? QFileInfo(e->file).fileName() : QString());
    refreshWave();
    for (QWidget* w : std::initializer_list<QWidget*>{startBox_, endBox_, trimSilence_, resetTrim_, fadeOut_, asIs_,
                                                     normalize_, name_, preview_})
        w->setEnabled(ok);
    name_->setText(ok ? e->name : QString());
    if (ok) showTrim();
    updateInfo();
}

void UploadDialog::showTrim() {
    const Entry& e = entries_[size_t(current_)];
    double len = double(e.audio.size()) / kSampleRate;
    syncing_ = true;
    startBox_->setRange(0.0, len);
    endBox_->setRange(0.0, len);
    startBox_->setValue(double(e.start) / kSampleRate);
    endBox_->setValue(double(e.end) / kSampleRate);
    fadeOut_->setChecked(e.fadeOut);
    (e.normalize ? normalize_ : asIs_)->setChecked(true);
    syncing_ = false;
    wave_->setSelection(e.start, e.end);
}

void UploadDialog::trimChanged() {
    if (playing_ == current_) {  // the audio changed under the preview
        player_.stop();
        showPlaying(-1);
    }
    syncing_ = true;
    const Entry& e = entries_[size_t(current_)];
    startBox_->setValue(double(e.start) / kSampleRate);
    endBox_->setValue(double(e.end) / kSampleRate);
    syncing_ = false;
    updateSummary();
    if (entries_[size_t(current_)].normalize) levelsChanged();  // the trimmed peak sets the gain
    else {
        updateRow(current_);
        updateInfo();
    }
}

void UploadDialog::updateInfo() {
    if (current_ < 0) {
        info_->clear();
        return;
    }
    const Entry& e = entries_[size_t(current_)];
    if (!e.error.isEmpty()) {
        info_->setText(tr("Can't be uploaded: %1").arg(e.error));
        return;
    }
    size_t frames = e.end - e.start;
    QString trimmed = frames < e.audio.size() ? tr(" (of %1)").arg(seconds(e.audio.size())) : QString();
    double g = gain(current_);
    double pk = peak(e.audio, e.start, e.end) * g;
    QString why = conflict(current_);
    info_->setText(tr("%1%2 · peak %3 dBFS%4 · %5 KiB on the device%6")
                       .arg(seconds(frames))
                       .arg(trimmed)
                       .arg(pk > 0 ? toDb(pk) : -INFINITY, 0, 'f', 1)
                       .arg(e.normalize ? tr(" (gain %1%2 dB)").arg(toDb(g) >= 0 ? "+" : "").arg(toDb(g), 0, 'f', 1)
                                        : QString())
                       .arg(sampleStorageBytes(uint32_t(frames)) / 1024.0, 0, 'f', 0)
                       .arg(why.isEmpty() ? QString() : tr(" · ⚠ Name: %1").arg(why)));
}

void UploadDialog::updateSummary() {
    uint64_t need = 0;
    int count = 0, conflicts = 0;
    for (int r = 0; r < int(entries_.size()); ++r) {
        const Entry& e = entries_[size_t(r)];
        if (!e.error.isEmpty()) continue;
        need += sampleStorageBytes(uint32_t(e.end - e.start));
        ++count;
        if (!conflict(r).isEmpty()) ++conflicts;
    }
    bool fits = need <= freeBytes_;
    summary_->setText(tr("%1 sample(s) · %2 MiB needed · %3 MiB free%4")
                          .arg(count)
                          .arg(need / 1048576.0, 0, 'f', 2)
                          .arg(freeBytes_ / 1048576.0, 0, 'f', 2)
                          .arg(!fits ? tr(" · not enough space on the device")
                                     : conflicts ? tr(" · %1 name(s) need changing (see the warnings)").arg(conflicts)
                                                 : QString()));
    makeUnique_->setEnabled(conflicts > 0);
    uploadButton_->setEnabled(count > 0 && fits && conflicts == 0);
}

void UploadDialog::togglePreview(int row) {
    if (playing_ == row) {
        player_.stop();
        showPlaying(-1);
        return;
    }
    player_.play(processed(row), twelveBit_->isChecked(), [this] { showPlaying(-1); });
    showPlaying(row);
}

void UploadDialog::showPlaying(int row) {
    playing_ = row;
    auto icon = [this](bool on) { return mediaIcon(palette(), on); };
    for (int r = 0; r < int(playButtons_.size()); ++r) playButtons_[size_t(r)]->setIcon(icon(r == row));
    bool cur = row >= 0 && row == current_;
    preview_->setIcon(icon(cur));
    preview_->setText(cur ? tr("Stop") : tr("Preview"));
    if (row >= 0) playheadTimer_->start();
    else {
        playheadTimer_->stop();
        wave_->setPlayhead(-1);
    }
}

std::vector<UploadDialog::Item> UploadDialog::items() const {
    std::vector<Item> out;
    for (int r = 0; r < int(entries_.size()); ++r)
        if (usable(r)) out.push_back({entries_[size_t(r)].name.trimmed().toStdString(), processed(r)});
    return out;
}
