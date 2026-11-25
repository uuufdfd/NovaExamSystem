#pragma once

#include "accountstore.h"

#include <QDialog>

class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QObject;
class QPushButton;
class QSpinBox;

// 登录对话框：三角色选择 + 服务器地址配置 + 账号密码登录。
// main.cpp 的“登录→主窗口”循环每轮新建一个本对话框；Accepted 表示登录成功，
// 成功账号经 account() 交给 MainWindow 按角色构建界面。
// 自带独立 QSS（不依赖全局样式），保证登录页在任何环境下观感一致。
class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);

    // 登录成功后的账号信息（含服务端返回的 id/displayName/role）。
    UserAccount account() const;

private:
    // 麒麟桌面 QPushButton::clicked 不稳定的兜底：直接拦鼠标释放触发登录/取消，
    // 与 MainWindow 的按钮动作注册表同思路（本对话框是该方案最早的落地点）。
    bool eventFilter(QObject *watched, QEvent *event) override;

    // 切换角色时回填该角色的演示账号并刷新提示文案。
    void updateHint();
    // 校验输入 → 写入服务器地址 → （本机回环时自动拉起本地服务端）→ 调 ApiClient 登录。
    void tryLogin();

    QComboBox *roleCombo = nullptr;
    QLineEdit *hostEdit = nullptr;
    QSpinBox *portSpin = nullptr;      // 端口固定 9000，只读展示
    QLineEdit *usernameEdit = nullptr;
    QLineEdit *passwordEdit = nullptr;
    QPushButton *loginButton = nullptr;
    QPushButton *cancelButton = nullptr;
    QLabel *hintLabel = nullptr;       // 角色提示 / 连接进度 / 登录结果的统一文案位
    UserAccount loggedInAccount;       // tryLogin 成功后填充，account() 对外只读
};
