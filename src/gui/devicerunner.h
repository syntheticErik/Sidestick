// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// Runs device jobs on a background thread so the window never blocks on MIDI traffic.
#pragma once

#include <QObject>
#include <QString>
#include <QThread>

#include <functional>
#include <memory>

#include "device.h"

class DeviceRunner {
public:
    DeviceRunner();
    ~DeviceRunner();

    // Queues `job` on the worker thread with a connected Device (connecting first if needed).
    // Jobs run one at a time, in order. If a job throws, the connection is dropped and
    // `onError` is called on the GUI thread.
    void run(std::function<void(lmd::Device&)> job, std::function<void(const QString&)> onError);

    // Posts `f` to the GUI thread (for jobs to deliver results).
    static void toGui(std::function<void()> f);

private:
    QThread thread_;
    QObject* worker_;
    std::unique_ptr<lmd::Device> device_;  // only touched on the worker thread
};
