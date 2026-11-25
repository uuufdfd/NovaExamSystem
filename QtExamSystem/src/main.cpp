// 客户端程序入口：装配全局外观（字体/图标/QSS）后进入“登录 → 主窗口”循环。
// 角色差异不在这里处理——登录成功后把账号交给 MainWindow，由其按角色构建界面。
#include "logindialog.h"
#include "mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFont>
#include <QIcon>

namespace {

// 从资源加载全局 QSS（:/styles/app.qss）。失败时静默跳过：
// 没有样式表界面仍可用（Qt 默认风格），不能让外观问题阻塞启动。
void loadStyleSheet(QApplication *app)
{
    QFile file(QStringLiteral(":/styles/app.qss"));
    if (!app || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    app->setStyleSheet(QString::fromUtf8(file.readAll()));
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 启动构建标记：纯 ASCII，`strings` 可直接搜到，且启动时打印到终端日志。
    // 用途：确认“当前真正运行的二进制”是哪次编译，排查麒麟旧二进制/多版本混用。
    // 注意：Qt 的 QStringLiteral（如 [NAV]/已切换到/获取题库）以 UTF-16 存储，
    // 普通 strings 搜不到中文，必须用 `strings -el`；而本行是普通 char 字面量，
    // 普通 strings 即可命中 QTEXAM-BUILD。
    qInfo("[QTEXAM-BUILD] markers=NAV+statusbar+winqbank built=%s %s", __DATE__, __TIME__);

    // 首选微软雅黑（Windows 端观感），麒麟上该字体不存在时由 StyleHint 自动
    // 回退到系统无衬线字体（通常为文泉驿/Noto），两端无需分支代码。
    QFont appFont(QStringLiteral("Microsoft YaHei UI"));
    appFont.setPointSize(10);
    appFont.setStyleHint(QFont::SansSerif);
    app.setFont(appFont);
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/app_icon.png")));
    loadStyleSheet(&app);

    // 登录-主界面循环：点击“退出登录/切换账号”时，MainWindow 关闭并请求重新登录，
    // 回到这里再次弹出登录框，可切换其它账号；直接关闭窗口（[X]）或取消登录则退出程序。
    bool keepRunning = true;
    while (keepRunning) {
        LoginDialog loginDialog;
        if (loginDialog.exec() != QDialog::Accepted) {
            break;  // 取消登录 -> 退出程序
        }

        MainWindow window(loginDialog.account());
        window.show();
        app.exec();

        keepRunning = window.isLogoutRequested();  // 仅“退出登录”才回到登录页
    }

    return 0;
}
