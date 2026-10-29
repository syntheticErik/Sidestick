// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// Main window: sample pool | bank slots | kit, filled from the connected device.
#pragma once

#include <QMainWindow>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "devicerunner.h"
#include "model.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class DndTree;

class MainWindow : public QMainWindow {
public:
    MainWindow();

    void refresh();
    void showBank(int index);
    // Called once after the first successful refresh (used by --screenshot).
    void setOnFirstRefresh(std::function<void()> f) { onFirstRefresh_ = std::move(f); }

private:
    void applySnapshot(std::string firmware, std::vector<lmd::PoolEntry> pool, std::map<int, lmd::Bank> banks);
    void fillPool();
    void fillBank();
    QString sampleName(uint16_t poolIndex) const;
    void showConnected();
    void showAbout();

    // Editing: each is a verified read-modify-write of the current bank on the worker thread.
    bool canWrite();
    // `precheck` (optional) runs on the device first and throws to abort without writing.
    void editBank(int bank, std::function<void(lmd::Bank&)> edit, const QString& what,
                  std::function<void(lmd::Device&)> precheck = nullptr);
    void assignSlot(int slot, int poolIndex);
    void assignVoice(int voice, int slot);
    void removeSlot(int slot);
    void turnOffVoice(int voice);
    static std::vector<int> voicesPlaying(const lmd::Bank& bank, int slot);
    static QString groupLabel(int voice);
    void renameBank();
    void clearBank(bool kitOnly);

    void uploadSamples(QStringList files);
    void backupDevice();
    void restoreDevice();
    void exportBank();
    void importBank();

protected:
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;

private:

    DeviceRunner runner_;
    std::string firmware_;
    std::vector<lmd::PoolEntry> pool_;
    std::map<int, lmd::Bank> banks_;
    int bank_ = 0;
    bool busy_ = false;  // a restore or upload is running
    bool allowUntested_ = false;  // Help > About override, this session only
    bool firmwareTested() const;
    qint64 lastRefreshMs_ = 0;
    std::function<void()> onFirstRefresh_;

    QComboBox* bankBox_;
    QLabel* bankTitle_;
    DndTree* poolView_;
    DndTree* bankView_;
    DndTree* kitView_;
    QCheckBox* showFactory_;
    QLabel* deviceLabel_;
    QLabel* memoryLabel_;
    QProgressBar* memoryBar_;
};
