#pragma once

#include <QString>
#include <QVector>

// 系统三种登录角色：决定登录入口、侧边栏导航与可见页面（见 MainWindow 构造）。
enum class UserRole
{
    Student,
    Teacher,
    Admin
};

// 用户账号的客户端内存表示。
// 在线模式下由服务端 login 响应填充（password 留空，鉴权走服务端 SHA256 哈希比对）；
// 仅本地 accounts.json 兜底链路才会用到 password 字段。
struct UserAccount
{
    QString username;
    QString password;
    QString displayName;
    UserRole role = UserRole::Student;
    QString note;     // 备注（班级/课程等），仅展示用
    int id = 0;       // 服务端数据库主键；本地兜底账号为 0
};

// 角色与显示文本（学生/教师/管理员）、协议码（student/teacher/admin）的互转。
// 协议码必须与服务端 users.role 字段取值保持一致，不可随意改动。
QString roleToText(UserRole role);
QString roleToCode(UserRole role);
UserRole roleFromCode(const QString &code);
UserRole roleFromText(const QString &text);

// 本地账号兜底存储：程序目录 accounts.json。
// 定位是“服务器不可达时的离线演示/应急登录”，正式数据源是服务端 MySQL 的 users 表；
// 全部为静态方法，无实例状态，每次调用即读/写文件。
class AccountStore
{
public:
    // 读取全部本地账号；文件缺失/损坏/为空时回退内置默认账号（保证永远可登录）。
    static QVector<UserAccount> loadAccounts();
    // 本地校验登录（仅离线兜底链路使用）：按 用户名+角色 定位，再比对明文密码。
    static bool verifyLogin(const QString &username,
                            const QString &password,
                            UserRole role,
                            UserAccount *account,
                            QString *error);
    // 管理员可管理的账号列表 = 学生 + 教师（管理员账号不在界面中互管）。
    static QVector<UserAccount> manageableAccounts();
    // 修改本地账号密码（≥6 位），改完立即整体写回 accounts.json。
    static bool updatePassword(const QString &username, UserRole role, const QString &newPassword, QString *error);

private:
    static QString dataFilePath();
    // 首次运行时生成带默认账号的 accounts.json，避免“空数据无法演示”。
    static void ensureDataFile();
    static bool saveAccounts(const QVector<UserAccount> &accounts, QString *error);
};
