// Optional screenshot-test entry point. Copy ONLY into a temporary build source
// tree as src/main.cpp; never replace the application's real entry point.
// Captures the actual Qt widgets without editing their pixels or business logic.
// SPDX-License-Identifier: MIT
#include "logindialog.h"
#include "mainwindow.h"
#include "apiclient.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString output = qEnvironmentVariable("NOVA_SCREENSHOT_DIR");
    if (output.isEmpty()) return 1;
    QDir().mkpath(output);
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPointSize(10);
    font.setStyleHint(QFont::SansSerif);
    app.setFont(font);
    // Offscreen platforms may not discover system fonts automatically. Load only
    // a caller-supplied installed font; no font files are redistributed.
    const auto fontPath = qEnvironmentVariable("NOVA_CAPTURE_FONT");
    if (!fontPath.isEmpty()) {
        const int id = QFontDatabase::addApplicationFont(fontPath);
        if (id < 0) return 7;
        const auto families = QFontDatabase::applicationFontFamilies(id);
        if (families.isEmpty()) return 8;
        font.setFamily(families.first());
        app.setFont(font);
    }
    QFile qss(QStringLiteral(":/styles/app.qss"));
    if (qss.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    app.setQuitOnLastWindowClosed(false);
    LoginDialog login;
    login.show();
    MainWindow *window = nullptr;
    int result = 0;
    QTimer::singleShot(500, [&] {
        if (!login.grab().save(output + "/login.png")) { app.exit(2); return; }
        login.hide();
        bool ok = false;
        const auto port = qEnvironmentVariableIntValue("NOVA_TEST_PORT", &ok);
        if (!ok || port < 1 || port > 65535) { app.exit(3); return; }
        ApiClient::instance().setEndpoint(QStringLiteral("127.0.0.1"), static_cast<quint16>(port));
        UserAccount account;
        QString error;
        if (!ApiClient::instance().login(QStringLiteral("teacher001"), QStringLiteral("123456"),
                                         UserRole::Teacher, &account, &error)) { app.exit(4); return; }
        window = new MainWindow(account);
        window->resize(1440, 960);
        window->show();
        QTimer::singleShot(500, [&] {
            bool clicked = false;
            for (auto *button : window->findChildren<QPushButton *>()) {
                if (button->text() == QStringLiteral("题目管理")) {
                    button->click(); clicked = true; break;
                }
            }
            if (!clicked) { app.exit(5); return; }
            QTimer::singleShot(500, [&] {
                result = window->grab().save(output + "/question-bank.png") ? 0 : 6;
                app.exit(result);
            });
        });
    });
    result = app.exec();
    delete window;
    return result;
}
