// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <stdexcept>
#include <utility>

#include "dndtree.h"
#include "sysexfile.h"
#include "uploaddialog.h"

using namespace lmd;

namespace {

const QString kAppName = QStringLiteral("Sidestick");
const QString kTitle = QStringLiteral("Sidestick — sample manager for LM Drum (unofficial)");

// Progress dialog that can't be dismissed (no close button, Escape ignored): used while a
// restore is writing, so the window stays locked until it finishes.
class LockedProgressDialog : public QProgressDialog {
public:
    using QProgressDialog::QProgressDialog;

protected:
    void closeEvent(QCloseEvent* e) override { e->ignore(); }
    void reject() override {}
};

const QString kPoolMime = QStringLiteral("application/x-lmd-pool-index");
const QString kSlotMime = QStringLiteral("application/x-lmd-bank-slot");

DndTree* makeList(const QStringList& headers) {
    auto* t = new DndTree;
    t->setHeaderLabels(headers);
    t->setRootIsDecorated(false);
    t->setUniformRowHeights(true);
    t->setAlternatingRowColors(true);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->header()->setStretchLastSection(true);
    return t;
}

QWidget* pane(const QString& title, QWidget* content, QLabel** titleOut = nullptr, QWidget* extra = nullptr) {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(4, 4, 4, 4);
    auto* head = new QHBoxLayout;
    auto* label = new QLabel(title);
    QFont f = label->font();
    f.setBold(true);
    label->setFont(f);
    head->addWidget(label, 1);
    if (extra) head->addWidget(extra);
    v->addLayout(head);
    v->addWidget(content);
    if (titleOut) *titleOut = label;
    return w;
}

}  // namespace

MainWindow::MainWindow() {
    setWindowTitle(kTitle);
    resize(1180, 720);

    auto* deviceMenu = menuBar()->addMenu(tr("&Device"));
    auto* refreshAct = deviceMenu->addAction(tr("&Refresh"), this, &MainWindow::refresh);
    refreshAct->setShortcut(QKeySequence::Refresh);
    deviceMenu->addSeparator();
    deviceMenu->addAction(tr("&Backup…"), this, &MainWindow::backupDevice);
    deviceMenu->addAction(tr("Re&store…"), this, &MainWindow::restoreDevice);
    deviceMenu->addSeparator();
    deviceMenu->addAction(tr("&Quit"), this, &QWidget::close)->setShortcut(QKeySequence::Quit);
    auto* samplesMenu = menuBar()->addMenu(tr("&Samples"));
    samplesMenu->addAction(tr("&Upload samples…"), this, [this] { uploadSamples({}); })
        ->setShortcut(QKeySequence::Open);
    auto* bankMenu = menuBar()->addMenu(tr("&Bank"));
    bankMenu->addAction(tr("Re&name…"), this, &MainWindow::renameBank);
    bankMenu->addSeparator();
    bankMenu->addAction(tr("Clear &kit…"), this, [this] { clearBank(true); });
    bankMenu->addAction(tr("&Clear bank…"), this, [this] { clearBank(false); });
    bankMenu->addSeparator();
    bankMenu->addAction(tr("&Export…"), this, &MainWindow::exportBank);
    bankMenu->addAction(tr("&Import…"), this, &MainWindow::importBank);
    menuBar()->addMenu(tr("&Help"))->addAction(tr("&About"), this, &MainWindow::showAbout);

    auto* tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->addWidget(new QLabel(tr(" Bank: ")));
    bankBox_ = new QComboBox;
    bankBox_->setMinimumContentsLength(22);
    for (int b = 0; b < kBanks; ++b) bankBox_->addItem(QString::number(b + 1));
    tb->addWidget(bankBox_);
    connect(bankBox_, &QComboBox::currentIndexChanged, this, &MainWindow::showBank);

    poolView_ = makeList({tr("#"), tr("Sample"), tr("Length")});
    bankView_ = makeList({tr("Slot"), tr("Sample")});
    kitView_ = makeList({tr("Voice"), tr("Sample")});

    // Drag a pool sample onto a bank slot; drag a bank slot onto a kit voice.
    poolView_->setDragType(kPoolMime);
    poolView_->setFileDropHandler([this](const QStringList& files) {
        // Open the dialog after the drop has finished, not inside the drop event.
        QTimer::singleShot(0, this, [this, files] { uploadSamples(files); });
    });
    bankView_->setDragType(kSlotMime);
    bankView_->setDropHandler(kPoolMime, [this](QTreeWidgetItem* row, int poolIndex) {
        assignSlot(row->data(0, kItemValueRole).toInt(), poolIndex);
    });
    kitView_->setDropHandler(kSlotMime, [this](QTreeWidgetItem* row, int slot) {
        assignVoice(row->data(0, kItemValueRole).toInt(), slot);
    });
    kitView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(kitView_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* row = kitView_->itemAt(pos);
        auto it = banks_.find(bank_);
        if (!row || it == banks_.end()) return;
        int voice = row->data(0, kItemValueRole).toInt();
        uint16_t plays = it->second.voiceSlot(voiceGroupLeader(voice));
        if (plays == 0 || plays >= kSlots || it->second.slot(plays) == kSlotEmpty) return;  // already silent
        QMenu menu;
        menu.addAction(tr("Turn off %1").arg(groupLabel(voice)), this, [this, voice] { turnOffVoice(voice); });
        menu.exec(kitView_->viewport()->mapToGlobal(pos));
    });
    bankView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(bankView_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* row = bankView_->itemAt(pos);
        if (!row) return;
        int slot = row->data(0, kItemValueRole).toInt();
        auto it = banks_.find(bank_);
        if (slot == 0 || it == banks_.end() || it->second.slot(slot) == kSlotEmpty) return;
        QMenu menu;
        menu.addAction(tr("Remove from bank"), this, [this, slot] { removeSlot(slot); });
        menu.exec(bankView_->viewport()->mapToGlobal(pos));
    });

    auto* split = new QSplitter;
    showFactory_ = new QCheckBox(tr("Show factory samples"));
    showFactory_->setChecked(true);
    connect(showFactory_, &QCheckBox::toggled, this, &MainWindow::fillPool);
    split->addWidget(pane(tr("Sample pool"), poolView_, nullptr, showFactory_));
    split->addWidget(pane(tr("Bank slots"), bankView_, &bankTitle_));
    split->addWidget(pane(tr("Kit"), kitView_));
    split->setSizes({380, 360, 440});
    setCentralWidget(split);

    deviceLabel_ = new QLabel(tr("Not connected"));
    memoryLabel_ = new QLabel(tr("User memory: —"));
    memoryBar_ = new QProgressBar;
    memoryBar_->setFixedWidth(160);
    memoryBar_->setMaximumHeight(14);
    memoryBar_->setTextVisible(false);
    memoryBar_->setValue(0);
    statusBar()->addWidget(deviceLabel_, 1);
    statusBar()->addPermanentWidget(memoryLabel_);
    statusBar()->addPermanentWidget(memoryBar_);

    refresh();
}

void MainWindow::refresh() {
    lastRefreshMs_ = QDateTime::currentMSecsSinceEpoch();
    deviceLabel_->setText(tr("Reading device…"));
    runner_.run(
        [this](Device& d) {
            auto fw = d.firmwareVersion();
            auto pool = d.pool();
            auto banks = d.readAllBanks();
            DeviceRunner::toGui([this, fw, pool, banks] { applySnapshot(fw, pool, banks); });
        },
        [this](const QString& msg) {
            deviceLabel_->setText(tr("Not connected: %1").arg(msg));
            if (onFirstRefresh_) std::exchange(onFirstRefresh_, nullptr)();
        });
}

void MainWindow::applySnapshot(std::string firmware, std::vector<PoolEntry> pool, std::map<int, Bank> banks) {
    firmware_ = std::move(firmware);
    pool_ = std::move(pool);
    banks_ = std::move(banks);

    showConnected();
    uint64_t used = 0;
    for (size_t i = kFactorySamples; i < pool_.size(); ++i) used += sampleStorageBytes(pool_[i].frames);
    memoryBar_->setValue(int(100.0 * used / kUserMemoryBytes + 0.5));
    memoryLabel_->setText(tr("User memory: %1 / %2 MiB (%3%)")
                              .arg(used / 1048576.0, 0, 'f', 2)
                              .arg(kUserMemoryBytes / 1048576.0, 0, 'f', 2)
                              .arg(100.0 * used / kUserMemoryBytes, 0, 'f', 0));

    for (auto& [b, bank] : banks_)
        bankBox_->setItemText(b, QString("%1  %2").arg(b + 1).arg(QString::fromStdString(bank.name())));

    fillPool();
    fillBank();
    if (onFirstRefresh_) std::exchange(onFirstRefresh_, nullptr)();
}

void MainWindow::showConnected() {
    size_t users = pool_.size() > size_t(kFactorySamples) ? pool_.size() - kFactorySamples : 0;
    QString fw = QString::fromStdString(firmware_);
    if (!firmware_.empty() && !firmwareTested())
        fw += allowUntested_ ? tr(" (untested; editing allowed)") : tr(" (untested; editing disabled)");
    deviceLabel_->setText(tr("LM Drum connected · firmware %1 · %2 samples (%3 user)")
                              .arg(fw)
                              .arg(pool_.size())
                              .arg(users));
}

QString MainWindow::sampleName(uint16_t poolIndex) const {
    if (poolIndex == kSlotOff) return tr("OFF");
    if (poolIndex == kSlotEmpty) return QString();
    if (poolIndex < pool_.size()) return QString::fromStdString(pool_[poolIndex].name);
    return tr("(missing sample %1)").arg(poolIndex);
}

void MainWindow::fillPool() {
    poolView_->clear();
    for (size_t i = showFactory_->isChecked() ? 0 : kFactorySamples; i < pool_.size(); ++i) {
        auto* it = new QTreeWidgetItem(poolView_);
        it->setData(0, kItemValueRole, int(i));
        it->setText(0, QString::number(i));
        it->setText(1, QString::fromStdString(pool_[i].name));
        it->setText(2, QString("%1 s").arg(pool_[i].frames / double(kSampleRate), 0, 'f', 3));
        it->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        it->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        if (i < size_t(kFactorySamples)) {
            for (int c = 0; c < 3; ++c) it->setForeground(c, poolView_->palette().placeholderText());
            it->setToolTip(1, tr("Factory sample (ROM, cannot be deleted)"));
        }
    }
    for (int c = 0; c < 3; ++c) poolView_->resizeColumnToContents(c);
}

void MainWindow::showBank(int index) {
    if (bankBox_->currentIndex() != index) {
        bankBox_->setCurrentIndex(index);  // re-enters via currentIndexChanged
        return;
    }
    bank_ = index;
    fillBank();
}

void MainWindow::fillBank() {
    bankView_->clear();
    kitView_->clear();
    auto found = banks_.find(bank_);
    if (found == banks_.end()) return;
    const Bank& bank = found->second;
    bankTitle_->setText(tr("Bank %1: %2").arg(bank_ + 1).arg(QString::fromStdString(bank.name())));

    for (int s = 0; s < kSlots; ++s) {
        auto* it = new QTreeWidgetItem(bankView_);
        it->setData(0, kItemValueRole, s);
        it->setText(0, QString("%1").arg(s, 3, 10, QChar('0')));
        uint16_t v = bank.slot(s);
        it->setText(1, v == kSlotEmpty ? QStringLiteral("—") : sampleName(v));
        if (v == kSlotEmpty || v == kSlotOff)
            for (int c = 0; c < 2; ++c) it->setForeground(c, bankView_->palette().placeholderText());
    }

    for (int v = 0; v < kVoices; ++v) {
        int leader = voiceGroupLeader(v);
        int slot = bank.voiceSlot(leader);  // what the firmware actually plays
        auto* it = new QTreeWidgetItem(kitView_);
        it->setData(0, kItemValueRole, v);
        it->setText(0, QString::fromLatin1(kVoiceNames[v]));
        // OFF (slot 000) and empty slots both mean "nothing assigned".
        uint16_t pool = slot > 0 && slot < kSlots ? bank.slot(slot) : kSlotOff;
        it->setText(1, pool == kSlotOff || pool == kSlotEmpty ? QStringLiteral("—") : sampleName(pool));
        if (leader != v) {
            QFont f = it->font(1);
            f.setItalic(true);
            for (int c = 0; c < 2; ++c) it->setFont(c, f);
            it->setToolTip(1, tr("Follows %1: the device plays one sample for this group").arg(kVoiceNames[leader]));
        }
    }
    for (QTreeWidget* t : {bankView_, kitView_})
        for (int c = 0; c < t->columnCount(); ++c) t->resizeColumnToContents(c);
}

bool MainWindow::firmwareTested() const {
    for (const char* t : kTestedFirmware)
        if (firmware_ == t) return true;
    return false;
}

bool MainWindow::canWrite() {
    if (firmwareTested() || (allowUntested_ && !firmware_.empty())) return true;
    QMessageBox::warning(this, tr("Not supported"),
                         firmware_.empty() ? tr("The device is not connected.")
                                           : tr("Firmware %1 has not been tested with Sidestick, so changes are "
                                                "disabled.\n\nYou can override this at your own risk in Help > About.")
                                                 .arg(QString::fromStdString(firmware_)));
    return false;
}

void MainWindow::editBank(int b, std::function<void(Bank&)> edit, const QString& what,
                          std::function<void(Device&)> precheck) {
    if (!canWrite()) return;
    deviceLabel_->setText(tr("%1…").arg(what));
    runner_.run(
        [this, b, edit = std::move(edit), precheck = std::move(precheck)](Device& d) {
            if (precheck) precheck(d);
            Bank bank = d.readBank(b);  // fresh copy: the device may have been edited by hand
            edit(bank);
            d.writeBank(b, bank);
            if (!(d.readBank(b) == bank)) throw std::runtime_error("the bank did not read back as written");
            DeviceRunner::toGui([this, b, bank] {
                banks_[b] = bank;
                bankBox_->setItemText(b, QString("%1  %2").arg(b + 1).arg(QString::fromStdString(bank.name())));
                if (b == bank_) fillBank();
                showConnected();
            });
        },
        [this, what](const QString& msg) {
            showConnected();
            QMessageBox::warning(this, what, msg);
            refresh();  // resync with the device after any failed edit
        });
}

void MainWindow::assignSlot(int slot, int poolIndex) {
    if (slot == 0) return;  // slot 0 is the fixed OFF slot
    // Pool numbers shift when a sample is deleted on the device, so check the number still names
    // the sample that was dragged before writing it.
    std::string expected = size_t(poolIndex) < pool_.size() ? pool_[size_t(poolIndex)].name : std::string();
    editBank(bank_, [slot, poolIndex](Bank& b) { b.putSample(slot, uint16_t(poolIndex)); },
             tr("Putting %1 in slot %2").arg(sampleName(uint16_t(poolIndex))).arg(slot),
             [poolIndex, expected](Device& d) {
                 auto pool = d.pool();
                 if (size_t(poolIndex) >= pool.size() || pool[size_t(poolIndex)].name != expected)
                     throw std::runtime_error(
                         "The sample list changed on the device (probably a sample was deleted there). "
                         "The list has been refreshed; please drag the sample again.");
             });
}

void MainWindow::assignVoice(int voice, int slot) {
    int leader = voiceGroupLeader(voice);
    editBank(bank_,
             [leader, slot](Bank& b) {
                 // Locked groups (toms, congas) get every pointer written, so the data matches what plays.
                 for (int v = 0; v < kVoices; ++v)
                     if (voiceGroupLeader(v) == leader) b.setVoiceSlot(v, uint16_t(slot));
             },
             tr("Assigning slot %1 to %2").arg(slot).arg(kVoiceNames[leader]));
}

// Voices whose sound comes from `slot` (locked groups follow their leader's pointer).
std::vector<int> MainWindow::voicesPlaying(const Bank& b, int slot) {
    std::vector<int> out;
    for (int v = 0; v < kVoices; ++v)
        if (b.voiceSlot(voiceGroupLeader(v)) == slot) out.push_back(v);
    return out;
}

QString MainWindow::groupLabel(int voice) {
    QStringList names;
    for (int v = 0; v < kVoices; ++v)
        if (voiceGroupLeader(v) == voiceGroupLeader(voice)) names << QString::fromLatin1(kVoiceNames[v]);
    return names.join(" / ");
}

void MainWindow::turnOffVoice(int voice) {
    int leader = voiceGroupLeader(voice);
    editBank(bank_,
             [leader](Bank& b) { b.turnOffVoice(leader); },
             tr("Turning off %1").arg(groupLabel(voice)));
}

void MainWindow::removeSlot(int slot) {
    auto it = banks_.find(bank_);
    if (it == banks_.end()) return;
    auto users = voicesPlaying(it->second, slot);
    bool turnOff = false;
    if (!users.empty()) {
        QStringList names;
        for (int v : users) names << QString::fromLatin1(kVoiceNames[v]);
        QMessageBox box(QMessageBox::Question, tr("Remove from bank"),
                        tr("Slot %1 is played by %2.").arg(slot, 3, 10, QChar('0')).arg(names.join(", ")),
                        QMessageBox::Cancel, this);
        box.setInformativeText(tr("Remove the sample and turn those voices off?"));
        auto* go = box.addButton(tr("Turn off and remove"), QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() != go) return;
        turnOff = true;
    }
    editBank(bank_,
             [this, slot, turnOff](Bank& b) {
                 auto users = voicesPlaying(b, slot);
                 if (!users.empty() && !turnOff)
                     throw std::runtime_error("a kit voice now plays this slot; refresh and try again");
                 b.setSlot(slot, kSlotEmpty);
                 for (int v : users)
                     if (voiceGroupLeader(v) == v) b.turnOffVoice(v);
             },
             tr("Removing slot %1").arg(slot));
}

void MainWindow::clearBank(bool kitOnly) {
    if (banks_.find(bank_) == banks_.end() || !canWrite()) return;
    QString what = kitOnly ? tr("Clear the kit of bank %1?").arg(bank_ + 1) : tr("Clear bank %1?").arg(bank_ + 1);
    QString detail = kitOnly ? tr("Every voice is silenced and voice settings (level, tune, …) go back to their "
                                  "defaults. The bank's slots and name are kept.")
                             : tr("All slots are emptied, the kit is reset, and the name becomes \"---\". "
                                  "The samples stay in the pool.");
    QMessageBox box(QMessageBox::Warning, kitOnly ? tr("Clear kit") : tr("Clear bank"), what,
                    QMessageBox::Cancel, this);
    box.setInformativeText(bank_ < 7 ? detail + "\n\n" +
                                           tr("This is a factory bank. Only a backup can bring its current "
                                              "contents back.")
                                     : detail);
    auto* go = box.addButton(kitOnly ? tr("Clear kit") : tr("Clear bank"), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != go) return;
    editBank(bank_, [kitOnly](Bank& b) { kitOnly ? b.clearKit() : b.clear(); },
             kitOnly ? tr("Clearing kit") : tr("Clearing bank"));
}

void MainWindow::renameBank() {
    auto it = banks_.find(bank_);
    if (it == banks_.end() || !canWrite()) return;
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Rename bank"), tr("Name for bank %1 (up to 16 characters):").arg(bank_ + 1),
                                         QLineEdit::Normal, QString::fromStdString(it->second.name()), &ok);
    if (!ok) return;
    std::string n = name.toStdString();
    editBank(bank_, [n](Bank& b) { b.setName(n); }, tr("Renaming bank %1").arg(bank_ + 1));
}

// ---- Files: backup / restore / export / import ----

void MainWindow::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);
    // Coming back to the window, e.g. after editing on the device: re-read it (at most every 3 s).
    if (e->type() == QEvent::ActivationChange && isActiveWindow() && !busy_ &&
        QDateTime::currentMSecsSinceEpoch() - lastRefreshMs_ > 3000)
        refresh();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (busy_) {
        QMessageBox::warning(this, tr("Device busy"),
                             tr("A restore or upload is writing to the device. Quitting now could leave its sample "
                                "memory half-written. Please wait for it to finish."));
        e->ignore();
        return;
    }
    e->accept();
}

void MainWindow::backupDevice() {
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save backup"),
        QDir::home().filePath(QString("LM Drum backup %1.ars").arg(QDate::currentDate().toString(Qt::ISODate))),
        tr("LM Drum backups (*.ars);;All files (*)"));
    if (path.isEmpty()) return;
    auto* dlg = new QProgressDialog(tr("Reading all user samples and banks from the device…"), QString(), 0, 0, this);
    dlg->setWindowTitle(tr("Backup"));
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setMinimumDuration(0);
    dlg->show();
    QPointer<QProgressDialog> guard(dlg);
    runner_.run(
        [this, path, guard](Device& d) {
            auto frames = d.backupFrames();
            writeSysexFile(path.toStdString(), frames);
            Backup bk = parseBackup(frames);
            DeviceRunner::toGui([this, path, guard, n = bk.samples.size(), banks = bk.banks.size()] {
                if (guard) guard->deleteLater();
                QMessageBox::information(this, tr("Backup"),
                                         tr("Saved %1 user samples and %2 banks to\n%3").arg(n).arg(banks).arg(path));
            });
        },
        [this, guard](const QString& msg) {
            if (guard) guard->deleteLater();
            QMessageBox::warning(this, tr("Backup failed"), msg);
        });
}

void MainWindow::restoreDevice() {
    if (!canWrite()) return;
    QString path = QFileDialog::getOpenFileName(this, tr("Restore backup"), QDir::homePath(),
                                                tr("LM Drum backups (*.ars);;All files (*)"));
    if (path.isEmpty()) return;
    std::vector<Bytes> frames;
    try {
        frames = readSysexFile(path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Restore"), QString::fromStdString(e.what()));
        return;
    }
    auto problems = restoreProblems(frames);
    if (!problems.empty()) {
        QStringList list;
        for (auto& p : problems) list << QString::fromStdString(p);
        QMessageBox::warning(this, tr("Restore"), tr("This file can't be restored:\n\n%1").arg(list.join("\n")));
        return;
    }
    Backup bk = parseBackup(frames);
    size_t deviceUsers = pool_.size() > size_t(kFactorySamples) ? pool_.size() - kFactorySamples : 0;
    QMessageBox box(QMessageBox::Warning, tr("Restore"),
                    tr("Replace ALL user samples and all 16 banks on the device?"), QMessageBox::Cancel, this);
    box.setInformativeText(tr("On the device now: %1 user samples.\nIn %2: %3 user samples, %4 banks.\n\n"
                              "Every bank is overwritten, including factory banks 1-7. Samples that are not in the "
                              "file are removed. Factory samples, patterns, songs, and settings are not affected.\n\n"
                              "This takes about a minute. Don't touch or switch off the device until it finishes.")
                               .arg(deviceUsers)
                               .arg(QFileInfo(path).fileName())
                               .arg(bk.samples.size())
                               .arg(bk.banks.size()));
    auto* go = box.addButton(tr("Restore"), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != go) return;

    size_t sampleFrames = frames.size() - bk.banks.size();
    auto* dlg = new LockedProgressDialog(tr("Sending sample data…"), QString(), 0, int(frames.size()), this);
    dlg->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);  // no close button
    dlg->setWindowTitle(tr("Restore"));
    dlg->setWindowModality(Qt::ApplicationModal);
    dlg->setMinimumDuration(0);
    dlg->setAutoClose(false);
    dlg->setAutoReset(false);
    dlg->show();
    QPointer<QProgressDialog> guard(dlg);
    busy_ = true;
    runner_.run(
        [this, frames, sampleFrames, guard](Device& d) {
            d.restore(frames, [guard, sampleFrames, total = frames.size()](size_t n, size_t) {
                if (n % 10 && n + 1 != sampleFrames && n != total) return;
                DeviceRunner::toGui([guard, n, sampleFrames, total] {
                    if (!guard) return;
                    guard->setValue(int(n));
                    if (n + 1 == sampleFrames)
                        guard->setLabelText(tr("Saving samples to the device's flash memory…\n"
                                               "(this can take a minute; the device is busy, not stuck)"));
                    else if (n >= sampleFrames)
                        guard->setLabelText(tr("Sending banks…"));
                });
            });
            DeviceRunner::toGui([guard] {
                if (guard) guard->setLabelText(tr("Verifying: reading everything back from the device…"));
            });
            bool ok = d.backupFrames() == frames;
            DeviceRunner::toGui([this, guard, ok] {
                busy_ = false;
                if (guard) guard->deleteLater();
                if (ok)
                    QMessageBox::information(this, tr("Restore"),
                                             tr("Restore complete and verified: the device now matches the backup."));
                else
                    QMessageBox::critical(this, tr("Restore"),
                                          tr("The restore finished, but the device's contents do not match the "
                                             "file. Try restoring again."));
                refresh();
            });
        },
        [this, guard](const QString& msg) {
            busy_ = false;
            if (guard) guard->deleteLater();
            QMessageBox::critical(this, tr("Restore failed"),
                                  tr("%1\n\nThe device's user sample memory may be incomplete. Restore a backup "
                                     "again to fix it.")
                                      .arg(msg));
            refresh();
        });
}

void MainWindow::uploadSamples(QStringList files) {
    if (!canWrite()) return;
    if (files.isEmpty())
        files = QFileDialog::getOpenFileNames(this, tr("Upload samples"), QDir::homePath(),
                                              tr("Audio files (*.wav *.aif *.aiff *.flac *.ogg);;All files (*)"));
    if (files.isEmpty()) return;
    uint64_t used = 0;
    for (size_t i = kFactorySamples; i < pool_.size(); ++i) used += sampleStorageBytes(pool_[i].frames);
    QSet<QString> names;
    for (const PoolEntry& p : pool_) names.insert(QString::fromStdString(p.name));
    UploadDialog dlg(files, kUserMemoryBytes - std::min(used, kUserMemoryBytes), names, this);
    if (dlg.exec() != QDialog::Accepted) return;
    auto items = dlg.items();
    if (items.empty()) return;

    auto* progress = new LockedProgressDialog(QString(), QString(), 0, int(items.size()), this);
    progress->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    progress->setWindowTitle(tr("Upload"));
    progress->setWindowModality(Qt::ApplicationModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->show();
    QPointer<QProgressDialog> guard(progress);
    busy_ = true;
    auto finish = [this, guard](const QString& text, bool error) {
        busy_ = false;
        if (guard) guard->deleteLater();
        if (error) QMessageBox::warning(this, tr("Upload"), text);
        else statusBar()->showMessage(text, 8000);
        refresh();
    };
    runner_.run(
        [items, guard, finish](Device& d) {
            QStringList skipped;
            for (size_t i = 0; i < items.size(); ++i) {
                DeviceRunner::toGui([guard, i, n = items.size(), name = items[i].name] {
                    if (!guard) return;
                    guard->setValue(int(i));
                    guard->setLabelText(tr("Uploading %1 of %2: %3").arg(i + 1).arg(n).arg(QString::fromStdString(name)));
                });
                try {
                    d.uploadUnassigned(items[i].name, items[i].pcm);
                } catch (const UploadRejected& e) {  // nothing changed on the device: skip it, carry on
                    skipped << QString::fromStdString(e.what());
                }
            }
            DeviceRunner::toGui([finish, n = items.size(), skipped] {
                if (skipped.isEmpty()) finish(tr("Uploaded %1 sample(s) to the pool").arg(n), false);
                else finish(tr("Uploaded %1 of %2 sample(s). Skipped:\n\n%3")
                                .arg(n - size_t(skipped.size())).arg(n).arg(skipped.join("\n")), true);
            });
        },
        [finish](const QString& msg) {
            finish(tr("Upload stopped: %1\n\nSamples uploaded before the error are in the pool.").arg(msg), true);
        });
}

void MainWindow::exportBank() {
    auto it = banks_.find(bank_);
    if (it == banks_.end()) return;
    QString name = QString::fromStdString(it->second.name()).trimmed();
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export bank"), QDir::home().filePath(QString("Bank %1 %2.syx").arg(bank_ + 1).arg(name)),
        tr("Bank exports (*.syx);;All files (*)"));
    if (path.isEmpty()) return;
    int b = bank_;
    runner_.run(
        [this, b, path](Device& d) {
            Bank bank = d.readBank(b);  // fresh copy
            writeSysexFile(path.toStdString(), {dataFrame(uint8_t(kTypeBank + b), bank.bytes())});
            DeviceRunner::toGui([this, b, path] {
                statusBar()->showMessage(tr("Exported bank %1 to %2").arg(b + 1).arg(path), 6000);
            });
        },
        [this](const QString& msg) { QMessageBox::warning(this, tr("Export failed"), msg); });
}

void MainWindow::importBank() {
    if (banks_.find(bank_) == banks_.end() || !canWrite()) return;
    QString path = QFileDialog::getOpenFileName(this, tr("Import bank"), QDir::homePath(),
                                                tr("Bank exports (*.syx);;All files (*)"));
    if (path.isEmpty()) return;
    std::string err;
    std::optional<Bank> bank;
    try {
        bank = bankFromExport(readSysexFile(path.toStdString()), &err);
    } catch (const std::exception& e) {
        err = e.what();
    }
    if (!bank) {
        QMessageBox::warning(this, tr("Import"), tr("%1: %2").arg(path, QString::fromStdString(err)));
        return;
    }
    auto missing = missingSampleSlots(*bank, pool_.size());
    if (!missing.empty()) {
        QMessageBox::warning(this, tr("Import"),
                             tr("This bank uses samples the device doesn't have (slot %1 refers to sample %2). "
                                "Bank exports store sample numbers, not audio, so they only work with the same "
                                "sample pool.")
                                 .arg(missing[0])
                                 .arg(bank->slot(missing[0])));
        return;
    }
    QMessageBox box(QMessageBox::Warning, tr("Import"),
                    tr("Replace bank %1 with \"%2\"?").arg(bank_ + 1).arg(QString::fromStdString(bank->name())),
                    QMessageBox::Cancel, this);
    box.setInformativeText(bank_ < 7 ? tr("This is a factory bank. Only a backup can bring its current contents back.")
                                     : tr("The bank's current slots, kit, and name are replaced."));
    auto* go = box.addButton(tr("Import"), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != go) return;
    Bank b = *bank;
    editBank(bank_, [b](Bank& target) { target = b; }, tr("Importing into bank %1").arg(bank_ + 1));
}

void MainWindow::showAbout() {
    QStringList tested;
    for (const char* t : kTestedFirmware) tested << QString::fromLatin1(t);
    QDialog dlg(this);
    dlg.setWindowTitle(tr("About"));
    auto* text = new QLabel(
        tr("<b>%1</b> %2<p>An unofficial sample manager for the Behringer LM Drum: organize the sample pool, "
           "banks, and kits, upload samples, and back up the device.</p>"
           "<p><b>Unofficial.</b> Not affiliated with, endorsed by, or supported by Behringer "
           "or Music Tribe. LM Drum is a trademark of its owner.</p>"
           "<p>Tested with LM Drum firmware %3.</p>")
            .arg(kAppName, QApplication::applicationVersion(), tested.join(", ")));
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    auto* allow = new QCheckBox(tr("Allow editing with untested firmware (this session only)"));
    allow->setChecked(allowUntested_);
    connect(allow, &QCheckBox::toggled, this, [this, allow, tested](bool on) {
        if (on) {
            QMessageBox box(QMessageBox::Warning, tr("Untested firmware"),
                            tr("Sidestick has only been tested with LM Drum firmware %1.").arg(tested.join(", ")),
                            QMessageBox::Cancel, this);
            box.setInformativeText(
                tr("Other firmware versions may work differently. Uploads, bank edits, imports, and restores "
                   "could fail, or damage the user samples and banks on your device.\n\n"
                   "Make a backup (Device > Backup) before editing. You use this at your own risk.\n\n"
                   "Reading, backups, and exports are always allowed. This setting resets when Sidestick restarts."));
            auto* go = box.addButton(tr("Allow editing"), QMessageBox::DestructiveRole);
            box.setDefaultButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() != go) {
                QSignalBlocker block(allow);
                allow->setChecked(false);
                return;
            }
        }
        allowUntested_ = on;
        if (!pool_.empty()) showConnected();
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(text);
    v->addWidget(allow);
    v->addWidget(buttons);
    dlg.exec();
}
