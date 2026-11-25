// AccountStore 实现：本地 accounts.json 的读写与角色码互转。
// 注意：这是离线兜底数据源；在线登录走 ApiClient → 服务端 users 表（SHA256+盐），
// 这里保留明文密码仅服务于断网演示场景，论文中已如实说明并列入改进方向。
#include "accountstore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// 内置默认账号：与 README/部署手册中的演示账号保持一致，
// 任何“文件丢失/解析失败/列表为空”的情况都回退到这套，保证系统永远可登录演示。
QVector<UserAccount> defaultAccounts()
{
    return {
        {QStringLiteral("student001"), QStringLiteral("123456"), QStringLiteral("演示学生甲"), UserRole::Student, QStringLiteral("演示班级")},
        {QStringLiteral("student002"), QStringLiteral("123456"), QStringLiteral("演示学生乙"), UserRole::Student, QStringLiteral("演示班级")},
        {QStringLiteral("teacher001"), QStringLiteral("123456"), QStringLiteral("演示教师甲"), UserRole::Teacher, QStringLiteral("C++ 程序设计")},
        {QStringLiteral("teacher002"), QStringLiteral("123456"), QStringLiteral("演示教师乙"), UserRole::Teacher, QStringLiteral("高等数学 A")},
        {QStringLiteral("admin"), QStringLiteral("admin123"), QStringLiteral("系统管理员"), UserRole::Admin, QStringLiteral("教务管理")}
    };
}

// UserAccount ↔ JSON 互转：字段名即 accounts.json 的持久化格式，改名会破坏旧文件兼容。
QJsonObject toJson(const UserAccount &account)
{
    QJsonObject object;
    object.insert(QStringLiteral("username"), account.username);
    object.insert(QStringLiteral("password"), account.password);
    object.insert(QStringLiteral("displayName"), account.displayName);
    object.insert(QStringLiteral("role"), roleToCode(account.role));
    object.insert(QStringLiteral("note"), account.note);
    object.insert(QStringLiteral("id"), account.id);
    return object;
}

UserAccount fromJson(const QJsonObject &object)
{
    UserAccount account;
    account.username = object.value(QStringLiteral("username")).toString();
    account.password = object.value(QStringLiteral("password")).toString();
    account.displayName = object.value(QStringLiteral("displayName")).toString();
    account.role = roleFromCode(object.value(QStringLiteral("role")).toString());
    account.note = object.value(QStringLiteral("note")).toString();
    account.id = object.value(QStringLiteral("id")).toInt();
    return account;
}

} // namespace

// 角色 → 界面文本。switch 已穷举枚举，结尾 return 仅为编译器静态分析兜底。
QString roleToText(UserRole role)
{
    switch (role) {
    case UserRole::Student:
        return QStringLiteral("学生");
    case UserRole::Teacher:
        return QStringLiteral("教师");
    case UserRole::Admin:
        return QStringLiteral("管理员");
    }
    return QStringLiteral("学生");
}

// 角色 → 协议码。该码同时用于客户端-服务端协议与数据库 users.role 列，三处必须一致。
QString roleToCode(UserRole role)
{
    switch (role) {
    case UserRole::Student:
        return QStringLiteral("student");
    case UserRole::Teacher:
        return QStringLiteral("teacher");
    case UserRole::Admin:
        return QStringLiteral("admin");
    }
    return QStringLiteral("student");
}

// 协议码 → 角色：宽松解析（去空白、不区分大小写），未知值按最低权限（学生）处理，
// 避免脏数据意外获得教师/管理员权限。
UserRole roleFromCode(const QString &code)
{
    const QString normalized = code.trimmed().toLower();
    if (normalized == QStringLiteral("teacher")) {
        return UserRole::Teacher;
    }
    if (normalized == QStringLiteral("admin")) {
        return UserRole::Admin;
    }
    return UserRole::Student;
}

// 界面文本 → 角色：同样未知值回退学生（最低权限原则）。
UserRole roleFromText(const QString &text)
{
    const QString normalized = text.trimmed();
    if (normalized == QStringLiteral("教师")) {
        return UserRole::Teacher;
    }
    if (normalized == QStringLiteral("管理员")) {
        return UserRole::Admin;
    }
    return UserRole::Student;
}

// 读取本地账号文件。三层兜底：打不开 / 不是 JSON 数组 / 解析后为空，全部回退默认账号，
// 因此调用方永远拿到非空列表，无需再做判空。
QVector<UserAccount> AccountStore::loadAccounts()
{
    ensureDataFile();

    QFile file(dataFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return defaultAccounts();
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isArray()) {
        return defaultAccounts();
    }

    QVector<UserAccount> accounts;
    const QJsonArray array = document.array();
    for (const QJsonValue &value : array) {
        const UserAccount account = fromJson(value.toObject());
        if (!account.username.isEmpty()) {  // 跳过缺用户名的脏记录
            accounts.append(account);
        }
    }
    return accounts.isEmpty() ? defaultAccounts() : accounts;
}

// 本地登录校验：先按 用户名+角色 精确定位，找到后才比对密码——
// 这样能区分“账号不存在”和“密码错误”两种提示，便于用户自查。
bool AccountStore::verifyLogin(const QString &username,
                               const QString &password,
                               UserRole role,
                               UserAccount *account,
                               QString *error)
{
    const QVector<UserAccount> accounts = loadAccounts();
    for (const UserAccount &candidate : accounts) {
        if (candidate.username == username.trimmed() && candidate.role == role) {
            if (candidate.password == password) {
                if (account) {
                    *account = candidate;
                }
                return true;
            }

            if (error) {
                *error = QStringLiteral("密码不正确。");
            }
            return false;  // 用户名+角色已命中，密码不对就不再继续找
        }
    }

    if (error) {
        *error = QStringLiteral("该角色下没有找到这个账号。");
    }
    return false;
}

// 管理员界面可见的账号 = 学生 + 教师；管理员账号之间不互相管理（防止误删唯一管理员）。
QVector<UserAccount> AccountStore::manageableAccounts()
{
    QVector<UserAccount> result;
    for (const UserAccount &account : loadAccounts()) {
        if (account.role == UserRole::Student || account.role == UserRole::Teacher) {
            result.append(account);
        }
    }
    return result;
}

// 修改本地密码：先做与服务端一致的最短长度校验，命中后整文件写回。
bool AccountStore::updatePassword(const QString &username, UserRole role, const QString &newPassword, QString *error)
{
    if (newPassword.size() < 6) {
        if (error) {
            *error = QStringLiteral("新密码至少需要 6 位。");
        }
        return false;
    }

    QVector<UserAccount> accounts = loadAccounts();
    for (UserAccount &account : accounts) {
        if (account.username == username && account.role == role) {
            account.password = newPassword;
            return saveAccounts(accounts, error);  // 改一条也整体重写，文件小、实现简单可靠
        }
    }

    if (error) {
        *error = QStringLiteral("没有找到要修改的用户。");
    }
    return false;
}

// 账号文件与可执行文件同目录：随部署包走，麒麟/Windows 行为一致，演示时易于查看。
QString AccountStore::dataFilePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("accounts.json"));
}

// 首次运行没有 accounts.json 时落一份默认账号，保证开箱即可登录演示。
void AccountStore::ensureDataFile()
{
    QFile file(dataFilePath());
    if (file.exists()) {
        return;
    }

    QString error;
    saveAccounts(defaultAccounts(), &error);
}

// 整体序列化写回（Indented 便于人工查看/手改）；只在打不开文件时报错。
bool AccountStore::saveAccounts(const QVector<UserAccount> &accounts, QString *error)
{
    QJsonArray array;
    for (const UserAccount &account : accounts) {
        array.append(toJson(account));
    }

    QFile file(dataFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("无法保存账号文件：%1").arg(dataFilePath());
        }
        return false;
    }

    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    return true;
}
