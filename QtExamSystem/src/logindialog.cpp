// LoginDialog 实现：登录界面装配、麒麟点击兜底（eventFilter）、登录流程编排。
// 设计要点：服务器地址可改、端口固定 9000；本机回环地址登录时自动拉起本地服务端，
// 方便无虚拟机环境下的单机演示；正式环境固定连 Ubuntu 虚拟机。
#include "logindialog.h"

#include "apiclient.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>

namespace {

// 信号连接统一使用 Qt6 类型安全的成员函数指针写法（&Class::signal），不再用 helper。

// 判断是否本机回环地址（127.0.0.1/localhost/::1）：只有这类地址才尝试自动拉起本地服务端，
// 远程 IP 连不上时直接报错，绝不在本机偷偷起服务造成“连错库”的假象。
bool isLoopbackHost(const QString &host)
{
    const QString normalized = host.trimmed().toLower();
    return normalized == QStringLiteral("127.0.0.1")
        || normalized == QStringLiteral("localhost")
        || normalized == QStringLiteral("::1");
}

// 本机调试便利：ping 不通时按约定路径（../../QtExamServer/build）查找并 startDetached
// 启动本地服务端（SQLite 模式），再轮询 ping 最多 3 秒等它就绪。
// 失败路径全部带可操作的中文提示（缺可执行文件 / 启动失败 / 启动后仍连不上）。
bool ensureLocalServerRunning(QString *error)
{
    QString pingError;
    if (ApiClient::instance().ping(&pingError)) {
        return true;  // 服务端已在运行，无需重复启动
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString serverBuildDir = QDir::cleanPath(appDir.absoluteFilePath(QStringLiteral("../../QtExamServer/build")));
    const QString serverExe = QDir(serverBuildDir).absoluteFilePath(QStringLiteral("QtExamServer.exe"));
    const QString serverDb = QDir(serverBuildDir).absoluteFilePath(QStringLiteral("exam_server.db"));

    if (!QFileInfo::exists(serverExe)) {
        if (error) {
            *error = QStringLiteral("本机服务器未启动，且没有找到服务器程序：\n%1\n\n请先构建 QtExamServer，或把服务器地址改为正在运行的虚拟机 IP。")
                         .arg(serverExe);
        }
        return false;
    }

    const QStringList args = {
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(ApiClient::instance().port()),
        QStringLiteral("--db"), serverDb
    };

    if (!QProcess::startDetached(serverExe, args, serverBuildDir)) {
        if (error) {
            *error = QStringLiteral("尝试启动本机服务器失败：\n%1").arg(serverExe);
        }
        return false;
    }

    // 子进程冷启动需要时间：每 300ms ping 一次，最多 10 次（约 3 秒）。
    // processEvents 排除用户输入，避免等待期间界面假死又防止用户连点登录。
    for (int i = 0; i < 10; ++i) {
        QThread::msleep(300);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (ApiClient::instance().ping(&pingError)) {
            return true;
        }
    }

    if (error) {
        *error = QStringLiteral("已尝试启动本机服务器，但仍无法连接 %1。\n%2")
                     .arg(ApiClient::instance().endpointText(), pingError);
    }
    return false;
}

} // namespace

// 构造即完成全部装配：标题区 → 表单（身份/服务器/账号/密码）→ 提示区 → 按钮区 → 内联 QSS。
// 登录页刻意不依赖全局 app.qss：它先于主题加载出现，必须自包含才能保证首屏观感。
LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("新星考试管理系统登录"));
    setWindowIcon(QIcon(QStringLiteral(":/branding/app_icon.png")));
    setObjectName(QStringLiteral("loginDialog"));
    setModal(true);
    setMinimumWidth(580);
    setMinimumHeight(600);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(34, 34, 34, 34);
    rootLayout->setSpacing(0);

    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("loginCard"));
    rootLayout->addWidget(card);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(34, 32, 34, 30);
    layout->setSpacing(22);

    auto *hero = new QHBoxLayout;
    hero->setSpacing(18);
    auto *logo = new QLabel;
    logo->setObjectName(QStringLiteral("loginLogo"));
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(64, 64);
    const QPixmap logoPixmap(QStringLiteral(":/branding/app_icon.png"));
    if (!logoPixmap.isNull()) {
        logo->setPixmap(logoPixmap.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    auto *title = new QLabel(QStringLiteral("<h2>新星考试管理系统</h2><p>题库、考试、阅卷与成绩管理一体化平台</p>"));
    title->setTextFormat(Qt::RichText);
    title->setObjectName(QStringLiteral("loginTitle"));
    hero->addWidget(logo);
    hero->addWidget(title, 1);
    layout->addLayout(hero);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignTop);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(16);

    roleCombo = new QComboBox;
    roleCombo->addItem(QStringLiteral("教师端"), roleToCode(UserRole::Teacher));
    roleCombo->addItem(QStringLiteral("学生端"), roleToCode(UserRole::Student));
    roleCombo->addItem(QStringLiteral("管理员端"), roleToCode(UserRole::Admin));
    roleCombo->setCurrentIndex(1);

    usernameEdit = new QLineEdit;
    usernameEdit->setPlaceholderText(QStringLiteral("请输入账号"));

    passwordEdit = new QLineEdit;
    passwordEdit->setPlaceholderText(QStringLiteral("请输入密码"));
    passwordEdit->setEchoMode(QLineEdit::Password);

    form->addRow(QStringLiteral("登录身份"), roleCombo);

    hostEdit = new QLineEdit(ApiClient::instance().host());
    hostEdit->setPlaceholderText(QStringLiteral("服务器地址（默认已填写）"));
    hostEdit->setToolTip(QStringLiteral("连接 Ubuntu 虚拟机时填写虚拟机 IP；本机调试时填写 127.0.0.1"));
    portSpin = new QSpinBox;
    portSpin->setRange(1, 65535);
    portSpin->setValue(ApiClient::instance().port());
    portSpin->setReadOnly(true);
    portSpin->setToolTip(QStringLiteral("系统端口固定为 9000，一般不需要修改。"));
    portSpin->setFixedWidth(118);
    auto *serverLine = new QHBoxLayout;
    serverLine->setSpacing(10);
    serverLine->addWidget(hostEdit, 1);
    serverLine->addWidget(portSpin);
    form->addRow(QStringLiteral("服务器"), serverLine);

    form->addRow(QStringLiteral("账号"), usernameEdit);
    form->addRow(QStringLiteral("密码"), passwordEdit);
    layout->addLayout(form);

    hintLabel = new QLabel;
    hintLabel->setObjectName(QStringLiteral("loginHint"));
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(14);
    cancelButton = new QPushButton(QStringLiteral("取消"));
    loginButton = new QPushButton(QStringLiteral("登录系统"));
    loginButton->setObjectName(QStringLiteral("primaryButton"));
    loginButton->setDefault(true);
    cancelButton->setObjectName(QStringLiteral("secondaryButton"));
    cancelButton->setMinimumWidth(124);
    loginButton->setMinimumWidth(124);
    // 麒麟桌面 clicked 信号不稳定的兜底：给两个按钮装事件过滤器，直接以“左键在按钮内释放”
    // 触发动作（见 eventFilter）；输入框的过滤器则让回车键即登录。clicked 连接保留作冗余通路。
    loginButton->installEventFilter(this);
    cancelButton->installEventFilter(this);
    usernameEdit->installEventFilter(this);
    passwordEdit->installEventFilter(this);
    buttons->addStretch();
    buttons->addWidget(cancelButton);
    buttons->addWidget(loginButton);
    layout->addLayout(buttons);

    connect(roleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LoginDialog::updateHint);
    connect(loginButton, &QAbstractButton::clicked, this, &LoginDialog::tryLogin);
    connect(cancelButton, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(passwordEdit, &QLineEdit::returnPressed, this, &LoginDialog::tryLogin);
    connect(usernameEdit, &QLineEdit::returnPressed, this, &LoginDialog::tryLogin);

    updateHint();

    // 登录页内联主题：与全局 defense_theme 同一套蓝白语言，但自包含（先于全局 QSS 出现）。
    // 极简留白：浅蓝渐变背景 + 居中白卡 + 大标题 + 统一圆角输入/按钮。
    setStyleSheet(QStringLiteral(R"(
        QDialog#loginDialog {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 #EAF2FF, stop:0.55 #F6F9FE, stop:1 #E7EEF9);
            font-family: "Microsoft YaHei UI", "Microsoft YaHei", "Noto Sans CJK SC",
                         "WenQuanYi Micro Hei", "Segoe UI", "Sans Serif";
            font-size: 13px;
            color: #1F2937;
        }
        QFrame#loginCard {
            border: 1px solid #E1E8F2;
            border-radius: 14px;
            background: #FFFFFF;
        }
        QLabel#loginLogo {
            border-radius: 12px;
            background: #F4F8FF;
            border: 1px solid #BFD6FB;
        }
        #loginTitle {
            color: #0F172A;
            background: transparent;
        }
        #loginTitle h2 {
            margin: 0;
            font-size: 26px;
            font-weight: 800;
        }
        #loginTitle p, #loginHint {
            color: #64748B;
            background: transparent;
            font-size: 13px;
        }
        QFormLayout QLabel {
            color: #334155;
            background: transparent;
            font-weight: 700;
        }
        QLineEdit, QComboBox, QSpinBox {
            min-height: 42px;
            border: 1px solid #CBD5E1;
            border-radius: 9px;
            padding: 6px 12px;
            background: #FFFFFF;
            font-size: 14px;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus {
            border: 1px solid #2563EB;
            background: #FBFDFF;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            width: 0;
            height: 0;
            border: none;
        }
        QComboBox::drop-down {
            width: 26px;
            border: none;
            background: transparent;
        }
        QComboBox QAbstractItemView {
            border: 1px solid #E5EAF2;
            border-radius: 9px;
            padding: 4px;
            background: #FFFFFF;
            selection-background-color: #EAF1FE;
            selection-color: #1D4ED8;
        }
        QPushButton {
            min-width: 96px;
            min-height: 42px;
            border: 1px solid #CBD5E1;
            border-radius: 9px;
            padding: 6px 18px;
            background: #FFFFFF;
            color: #334155;
            font-size: 14px;
            font-weight: 700;
        }
        QPushButton:hover {
            border-color: #93B5E8;
            background: #F4F8FE;
        }
        QPushButton#primaryButton {
            color: #FFFFFF;
            border-color: #2563EB;
            background: #2563EB;
        }
        QPushButton#primaryButton:hover {
            background: #1D4ED8;
        }
        QPushButton#secondaryButton {
            color: #334155;
            background: #FFFFFF;
        }
        QPushButton#secondaryButton:hover {
            border-color: #93B5E8;
            background: #F4F8FE;
        }
    )"));
}

UserAccount LoginDialog::account() const
{
    return loggedInAccount;
}

// 麒麟点击兜底：拦“左键在按钮矩形内释放”直接执行动作并吃掉事件（return true），
// 防止 clicked 在麒麟上偶发触发造成双重执行。这是按钮动作注册表方案在登录页的前身，
// 主窗口侧的同思路实现见 MainWindow::eventFilter + dispatchRegisteredButtonAction。
bool LoginDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == loginButton || watched == cancelButton) {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            auto *button = qobject_cast<QPushButton *>(watched);
            if (button && button->isEnabled()
                && mouseEvent->button() == Qt::LeftButton
                && button->rect().contains(mouseEvent->pos())) {
                if (watched == loginButton) {
                    tryLogin();
                } else {
                    reject();
                }
                return true;
            }
        }
    }

    // 账号/密码框内按回车直接登录（含小键盘 Enter），比 returnPressed 信号更可靠地覆盖两种键码。
    if ((watched == usernameEdit || watched == passwordEdit) && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            tryLogin();
            return true;
        }
    }

    return QDialog::eventFilter(watched, event);
}

// 按当前角色回填演示账号与提示语。毕设演示定位：评审/答辩时免去手输账号；
// 真实部署可去掉回填只留提示。
void LoginDialog::updateHint()
{
    const UserRole role = roleFromCode(roleCombo->currentData().toString());
    if (role == UserRole::Teacher) {
        usernameEdit->setText(QStringLiteral("teacher001"));
        passwordEdit->setText(QStringLiteral("123456"));
        hintLabel->setText(QStringLiteral("教师端示例账号：teacher001 / 123456。登录后可管理题库、发布考试并完成阅卷。"));
        return;
    }

    if (role == UserRole::Student) {
        usernameEdit->setText(QStringLiteral("student001"));
        passwordEdit->setText(QStringLiteral("123456"));
        hintLabel->setText(QStringLiteral("学生端示例账号：student001 / 123456。登录后可查看考试、在线答题并提交试卷。"));
        return;
    }

    usernameEdit->setText(QStringLiteral("admin"));
    passwordEdit->setText(QStringLiteral("admin123"));
    hintLabel->setText(QStringLiteral("管理员端示例账号：admin / admin123。登录后可维护教师和学生账号。"));
}

// 登录主流程：输入校验 → 写入服务器地址 → 锁界面给进度反馈 →
// （回环地址）确保本地服务端在跑 → ApiClient 登录 → accept() 交还 main 循环。
// 任何失败路径都先恢复界面再弹错误框，保证可以立即重试。
void LoginDialog::tryLogin()
{
    QString error;
    const UserRole role = roleFromCode(roleCombo->currentData().toString());
    if (hostEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("连接地址为空"), QStringLiteral("请填写服务器地址。"));
        hostEdit->setFocus();
        return;
    }
    if (usernameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("账号为空"), QStringLiteral("请输入登录账号。"));
        usernameEdit->setFocus();
        return;
    }
    if (passwordEdit->text().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("密码为空"), QStringLiteral("请输入登录密码。"));
        passwordEdit->setFocus();
        return;
    }
    // 先把端点写入 ApiClient 再做任何网络操作；地址要等登录成功才持久化（见 ApiClient::login）。
    ApiClient::instance().setEndpoint(hostEdit->text(), static_cast<quint16>(portSpin->value()));

    setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (hintLabel) {
        hintLabel->setText(QStringLiteral("正在连接服务器 %1:%2，请稍候...")
                               .arg(hostEdit->text().trimmed())
                               .arg(portSpin->value()));
    }
    // 网络调用是同步阻塞的，先强制刷一帧让“正在连接...”提示真正画出来。
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    const auto restoreLoginUi = [this] {
        QApplication::restoreOverrideCursor();
        setEnabled(true);
        updateHint();
    };

    // 仅回环地址尝试自动拉起本地服务端；远程地址连不上直接由登录失败分支报错。
    if (isLoopbackHost(hostEdit->text())) {
        QString connectError;
        if (!ensureLocalServerRunning(&connectError)) {
            restoreLoginUi();
            QMessageBox::warning(this, QStringLiteral("服务器未就绪"), connectError);
            return;
        }
    }
    if (!ApiClient::instance().login(usernameEdit->text(), passwordEdit->text(), role, &loggedInAccount, &error)) {
        restoreLoginUi();
        QMessageBox::warning(this, QStringLiteral("登录失败"), error);
        return;
    }

    QApplication::restoreOverrideCursor();
    if (hintLabel) {
        hintLabel->setText(QStringLiteral("登录成功，正在进入系统..."));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    accept();
}
