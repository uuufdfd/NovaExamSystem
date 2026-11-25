#pragma once

#include "accountstore.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

// 客户端与考试服务器的唯一通信入口（单例）。
// 协议：TCP 短连接 + 换行分隔 JSON——每次 request() 新建连接、写一行请求、读一行响应后即断开。
// 不维护长连接，因此不需要心跳/自动重连机制，网络异常完全由每次调用的三段超时兜底
// （连接 2.5s / 发送 3s / 等响应 5s），这是与服务端 examserver 约定的设计决策。
// 登录成功后保存会话 token，后续所有请求自动携带，服务端据此鉴权。
class ApiClient
{
public:
    // 全局唯一实例（Meyers 单例：首次调用时构造，构造内自动加载本地配置）。
    static ApiClient &instance();

    // 服务器地址持久化在程序目录 client_config.ini（QSettings INI 格式），
    // 启动时读取、登录成功后写回，实现“配置一次、下次启动直接可用”。
    void loadSettings();
    void saveSettings() const;
    // 设置服务器地址（登录界面修改地址时调用）；port 传 0 时回退默认 9000。
    void setEndpoint(const QString &host, quint16 port);
    QString host() const;
    quint16 port() const;
    // “host:port” 形式的展示文本，用于连接失败提示与登录页显示。
    QString endpointText() const;
    // 登录成功后服务端签发的会话令牌；未登录时为空串。
    QString token() const;
    // 当前登录用户（登录响应里的 user 对象解析结果）。
    UserAccount currentUser() const;

    // 连通性探测：发送 type=ping，登录界面用它在登录前确认服务器可达。
    bool ping(QString *error = nullptr);
    // 登录：成功后保存 token / 当前用户，并把可用的服务器地址写入配置文件。
    bool login(const QString &username,
               const QString &password,
               UserRole role,
               UserAccount *account,
               QString *error);
    // 通用请求入口：payload 自动补 token → 连接 → 发送 → 等待一行响应 → 解析 JSON。
    // 返回 false 时 *error 为中文原因；服务端 success=false 的业务失败同样返回 false。
    bool request(QJsonObject payload, QJsonObject *response, QString *error);

private:
    ApiClient();  // 私有构造：仅 instance() 可创建，构造时即 loadSettings()

    QString serverHost = QStringLiteral("127.0.0.1");  // 默认指向 Ubuntu 虚拟机服务端
    quint16 serverPort = 9000;
    QString sessionToken;     // 登录后非空；request() 发现 payload 缺 token 时自动附加
    UserAccount sessionUser;  // 当前会话用户，退出登录由上层重建窗口时自然废弃
};
