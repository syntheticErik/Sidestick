// SPDX-FileCopyrightText: 2026 Erik Evans
// SPDX-License-Identifier: GPL-3.0-or-later
// sidestick: Qt desktop app for managing LM Drum samples, banks, and kits.
//
// Development option: --screenshot <file.png> [--bank N] renders the window after the first
// device refresh, saves it, and exits (use with QT_QPA_PLATFORM=offscreen).
// --prepare-screenshot <file.png> <audio files...> does the same for the Prepare upload window
// (no device needed).
#include <QApplication>
#include <QIcon>
#include <QTimer>

#include "mainwindow.h"
#include "model.h"
#include "uploaddialog.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Sidestick");
    QApplication::setApplicationVersion(SIDESTICK_VERSION);
    QGuiApplication::setDesktopFileName(SIDESTICK_APP_ID);  // matches <app id>.desktop for taskbar/portal
    QIcon icon;
    for (int s : {16, 22, 24, 32, 48, 64, 128, 256, 512}) icon.addFile(QString(":/icons/%1.png").arg(s), QSize(s, s));
    QApplication::setWindowIcon(icon);

    QStringList args0 = app.arguments();
    if (args0.size() >= 4 && args0[1] == "--prepare-screenshot") {
        UploadDialog dlg(args0.mid(3), lmd::kUserMemoryBytes, {QStringLiteral("EXAMPLE SAMPLE")}, nullptr);
        dlg.show();
        QTimer::singleShot(300, &dlg, [&dlg, out = args0[2]] {
            dlg.grab().save(out);
            QApplication::quit();
        });
        return app.exec();
    }

    QString shot;
    int bank = 0;
    QStringList args = app.arguments();
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == "--screenshot") shot = args[i + 1];
        if (args[i] == "--bank") bank = args[i + 1].toInt() - 1;
    }

    MainWindow w;
    w.show();
    if (!shot.isEmpty()) {
        w.setOnFirstRefresh([&w, shot, bank] {
            w.showBank(bank);
            QTimer::singleShot(200, &w, [&w, shot] {
                w.grab().save(shot);
                QApplication::quit();
            });
        });
    }
    return app.exec();
}
