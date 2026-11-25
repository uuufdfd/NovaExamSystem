// ApiClient 实现：TCP 短连接 + 行分隔 JSON 协议的客户端侧。
// 与 QtExamServer/src/examserver.cpp 的 onReadyRead（按 '\n' 切帧）严格对应：
// 请求必须以 '\n' 结尾，响应同样以 '\n' 结尾，一问一答后连接即弃。
#include "apiclient.h"

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QSettings>
#include <QTcpSocket>

namespace {

// 把登录响应中的 user JSON 对象转成本地 UserAccount 结构（字段名与服务端协议一致）。
UserAccount userFromJson(const QJsonObject &object)
{
    UserAccount account;
    account.id = object.value(QStringLiteral("id")).toInt();
    account.username = object.value(QStringLiteral("username")).toString();
    account.displayName = object.value(QStringLiteral("displayName")).toString();
    account.role = roleFromCode(object.value(QStringLiteral("role")).toString());
    account.note = object.value(QStringLiteral("note")).toString();
    return account;
}

// 配置文件固定放在可执行文件同目录：随部署包走，麒麟/Windows 双端行为一致，
// 也方便毕设演示时直接查看或手改 ini。
QString settingsPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/client_config.ini");
}

} // namespace

// Meyers 单例：函数内 static 由 C++11 保证线程安全的一次性构造。
ApiClient &ApiClient::instance()
{
    static ApiClient client;
    return client;
}

ApiClient::ApiClient()
{
    loadSettings();
}

// 读取 client_config.ini 的 server/host、server/port；
// 空值/非法值一律回退到内置默认，保证首次运行（无配置文件）也能直接连默认服务器。
void ApiClient::loadSettings()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    serverHost = settings.value(QStringLiteral("server/host"), serverHost).toString().trimmed();
    if (serverHost.isEmpty()) {
        serverHost = QStringLiteral("127.0.0.1");
    }
    serverPort = static_cast<quint16>(settings.value(QStringLiteral("server/port"), 9000).toUInt());
    if (serverPort == 0) {
        serverPort = 9000;
    }
}

// 写回当前服务器地址。只在登录成功后调用（见 login），
// 避免把一次失败尝试里输错的地址固化进配置。
void ApiClient::saveSettings() const
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("server/host"), serverHost);
    settings.setValue(QStringLiteral("server/port"), serverPort);
}

void ApiClient::setEndpoint(const QString &host, quint16 port)
{
    serverHost = host.trimmed();
    serverPort = port == 0 ? 9000 : port;  // 0 视为未填写，回退默认端口
}

QString ApiClient::host() const
{
    return serverHost;
}

quint16 ApiClient::port() const
{
    return serverPort;
}

QString ApiClient::endpointText() const
{
    return QStringLiteral("%1:%2").arg(serverHost).arg(serverPort);
}

QString ApiClient::token() const
{
    return sessionToken;
}

UserAccount ApiClient::currentUser() const
{
    return sessionUser;
}

// ping 复用通用 request 通道：服务端对 type=ping 立即回 pong，
// 因此它同时验证了 TCP 可达性与协议栈完整性（不只是端口开放）。
bool ApiClient::ping(QString *error)
{
    QJsonObject response;
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("ping"));
    return request(payload, &response, error);
}

bool ApiClient::login(const QString &username,
                      const QString &password,
                      UserRole role,
                      UserAccount *account,
                      QString *error)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("login"));
    payload.insert(QStringLiteral("username"), username.trimmed());
    payload.insert(QStringLiteral("password"), password);
    payload.insert(QStringLiteral("role"), roleToCode(role));

    // 登录请求不能带旧 token（服务端会按 token 鉴权而忽略账号密码），
    // 故先清空；失败时恢复，保证“换号失败仍可用原会话继续操作”。
    const QString previousToken = sessionToken;
    sessionToken.clear();

    QJsonObject response;
    if (!request(payload, &response, error)) {
        sessionToken = previousToken;
        return false;
    }

    sessionToken = response.value(QStringLiteral("token")).toString();
    sessionUser = userFromJson(response.value(QStringLiteral("user")).toObject());
    saveSettings();  // 地址被验证可用，此刻才写入配置文件
    if (account) {
        *account = sessionUser;
    }
    return true;
}

// 一次完整的“连接→发送→收一行→解析”短连接事务。
// 三段超时（连接 2.5s / 写 3s / 读 5s）是同步阻塞调用，仅在登录页与各业务点偶发触发，
// 时长以“局域网内虚拟机”场景标定；超时文案直接面向最终用户（毕设演示可读）。
bool ApiClient::request(QJsonObject payload, QJsonObject *response, QString *error)
{
    // 已登录则自动补 token；调用方显式塞过 token 时尊重调用方（如未来的代登录调试）。
    if (!sessionToken.isEmpty() && !payload.contains(QStringLiteral("token"))) {
        payload.insert(QStringLiteral("token"), sessionToken);
    }

    // 重置密码请求只打印字段名与密码长度，绝不打印明文密码，便于核对“前端到底发了哪些字段”。
    if (payload.value(QStringLiteral("type")).toString() == QStringLiteral("reset_password")) {
        qDebug() << "[API_RESET_PASSWORD_PAYLOAD]"
                 << "hasUserId=" << payload.contains(QStringLiteral("userId"))
                 << "hasId=" << payload.contains(QStringLiteral("id"))
                 << "hasUsername=" << payload.contains(QStringLiteral("username"))
                 << "passwordLen=" << payload.value(QStringLiteral("newPassword")).toString().length();
    }

    QTcpSocket socket;
    // 显式禁用系统代理：校园网/实验室环境常配 HTTP 代理，会劫持裸 TCP 导致莫名超时。
    socket.setProxy(QNetworkProxy::NoProxy);
    socket.connectToHost(serverHost, serverPort);
    if (!socket.waitForConnected(2500)) {
        if (error) {
            // 把最常见的两类底层错误翻译成可操作的中文提示，其余保留 Qt 原文。
            QString reason = socket.errorString();
            if (socket.error() == QAbstractSocket::SocketTimeoutError) {
                reason = QStringLiteral("连接超时，请确认服务器已经启动，且 IP 地址和端口填写正确。");
            } else if (socket.error() == QAbstractSocket::ConnectionRefusedError) {
                reason = QStringLiteral("连接被拒绝，通常是服务器程序没有启动。");
            } else if (reason.isEmpty()) {
                reason = QStringLiteral("网络连接异常。");
            }
            *error = QStringLiteral("无法连接服务器 %1。\n%2").arg(endpointText(), reason);
        }
        return false;
    }

    // Compact 序列化 + 末尾 '\n'：换行符是协议帧边界，服务端按行切包。
    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n';
    if (socket.write(data) != data.size() || !socket.waitForBytesWritten(3000)) {
        if (error) {
            *error = QStringLiteral("发送请求失败：%1").arg(socket.errorString());
        }
        return false;
    }

    // 响应可能分多个 TCP 段到达，循环累积直到出现帧结束符 '\n'（分包处理）。
    QByteArray buffer;
    while (!buffer.contains('\n')) {
        if (!socket.waitForReadyRead(5000)) {
            if (error) {
                *error = QStringLiteral("等待服务器响应超时：%1").arg(socket.errorString());
            }
            return false;
        }
        buffer.append(socket.readAll());
    }

    const QByteArray line = buffer.left(buffer.indexOf('\n')).trimmed();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("服务器返回数据异常。");
        }
        return false;
    }

    const QJsonObject object = document.object();
    // 先回传完整响应再判 success：失败时调用方也能拿到服务端附带的上下文字段。
    if (response) {
        *response = object;
    }
    if (!object.value(QStringLiteral("success")).toBool()) {
        if (error) {
            *error = object.value(QStringLiteral("error")).toString(QStringLiteral("请求失败。"));
        }
        return false;
    }
    return true;
}
