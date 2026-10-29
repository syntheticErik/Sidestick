// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
#include "devicerunner.h"

#include <QCoreApplication>

#include <exception>

DeviceRunner::DeviceRunner() : worker_(new QObject) {
    worker_->moveToThread(&thread_);
    thread_.start();
}

DeviceRunner::~DeviceRunner() {
    QMetaObject::invokeMethod(worker_, [this] { device_.reset(); }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
    delete worker_;
}

void DeviceRunner::run(std::function<void(lmd::Device&)> job, std::function<void(const QString&)> onError) {
    QMetaObject::invokeMethod(
        worker_,
        [this, job = std::move(job), onError = std::move(onError)] {
            try {
                if (!device_) device_ = std::make_unique<lmd::Device>();
                job(*device_);
            } catch (const std::exception& e) {
                device_.reset();  // reconnect on the next job
                QString msg = QString::fromStdString(e.what());
                toGui([onError, msg] { onError(msg); });
            }
        },
        Qt::QueuedConnection);
}

void DeviceRunner::toGui(std::function<void()> f) {
    QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(f), Qt::QueuedConnection);
}
