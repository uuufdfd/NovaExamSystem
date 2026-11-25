#include "examserver.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QtExamServer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("在线考试与多教师阅卷系统服务器端"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption hostOption(QStringList() << QStringLiteral("H") << QStringLiteral("host"),
                                  QStringLiteral("监听地址，默认 0.0.0.0"),
                                  QStringLiteral("host"),
                                  QStringLiteral("0.0.0.0"));
    QCommandLineOption portOption(QStringList() << QStringLiteral("p") << QStringLiteral("port"),
                                  QStringLiteral("监听端口，默认 9000"),
                                  QStringLiteral("port"),
                                  QStringLiteral("9000"));
    QCommandLineOption dbOption(QStringList() << QStringLiteral("d") << QStringLiteral("db"),
                                QStringLiteral("SQLite 数据库路径，仅 db-driver=sqlite 时使用"),
                                QStringLiteral("path"),
                                QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("exam_server.db")));
    QCommandLineOption dbDriverOption(QStringList() << QStringLiteral("db-driver"),
                                      QStringLiteral("数据库驱动：mysql 或 sqlite，默认 mysql；SQLite 仅作开发测试与回退方案"),
                                      QStringLiteral("driver"),
                                      QStringLiteral("mysql"));
    QCommandLineOption mysqlHostOption(QStringList() << QStringLiteral("mysql-host"),
                                       QStringLiteral("MySQL 主机地址"),
                                       QStringLiteral("host"),
                                       QStringLiteral("127.0.0.1"));
    QCommandLineOption mysqlPortOption(QStringList() << QStringLiteral("mysql-port"),
                                       QStringLiteral("MySQL 端口"),
                                       QStringLiteral("port"),
                                       QStringLiteral("3306"));
    QCommandLineOption mysqlDbOption(QStringList() << QStringLiteral("mysql-db"),
                                     QStringLiteral("MySQL 数据库名"),
                                     QStringLiteral("name"),
                                     QStringLiteral("qt_exam_server"));
    QCommandLineOption mysqlUserOption(QStringList() << QStringLiteral("mysql-user"),
                                       QStringLiteral("MySQL 用户名"),
                                       QStringLiteral("user"),
                                       QStringLiteral("qt_exam"));
    QCommandLineOption mysqlPasswordOption(QStringList() << QStringLiteral("mysql-password"),
                                           QStringLiteral("MySQL 密码；也可用环境变量 QT_EXAM_DB_PASSWORD"),
                                           QStringLiteral("password"),
                                           QString());
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.addOption(dbOption);
    parser.addOption(dbDriverOption);
    parser.addOption(mysqlHostOption);
    parser.addOption(mysqlPortOption);
    parser.addOption(mysqlDbOption);
    parser.addOption(mysqlUserOption);
    parser.addOption(mysqlPasswordOption);
    parser.process(app);

    bool ok = false;
    const quint16 port = parser.value(portOption).toUShort(&ok);
    if (!ok || port == 0) {
        qCritical().noquote() << QStringLiteral("端口不合法。");
        return 1;
    }

    const QHostAddress address(parser.value(hostOption));
    if (address.isNull()) {
        qCritical().noquote() << QStringLiteral("监听地址不合法。");
        return 1;
    }

    bool mysqlPortOk = false;
    const int mysqlPort = parser.value(mysqlPortOption).toInt(&mysqlPortOk);
    if (!mysqlPortOk || mysqlPort <= 0 || mysqlPort > 65535) {
        qCritical().noquote() << QStringLiteral("MySQL 端口不合法。");
        return 1;
    }

    DatabaseConfig databaseConfig;
    databaseConfig.driver = parser.value(dbDriverOption).trimmed().toLower();
    databaseConfig.sqlitePath = parser.value(dbOption);
    databaseConfig.host = parser.value(mysqlHostOption);
    databaseConfig.port = mysqlPort;
    databaseConfig.databaseName = parser.value(mysqlDbOption);
    databaseConfig.username = parser.value(mysqlUserOption);
    databaseConfig.password = parser.value(mysqlPasswordOption);
    if (databaseConfig.password.isEmpty()) {
        databaseConfig.password = QString::fromLocal8Bit(qgetenv("QT_EXAM_DB_PASSWORD"));
    }

    if (databaseConfig.driver != QStringLiteral("sqlite") && databaseConfig.driver != QStringLiteral("mysql")) {
        qCritical().noquote() << QStringLiteral("db-driver 只能是 sqlite 或 mysql。");
        return 1;
    }

    ExamServer server;
    QString error;
    if (!server.start(address, port, databaseConfig, &error)) {
        qCritical().noquote() << QStringLiteral("服务器启动失败：%1").arg(error);
        return 1;
    }

    return app.exec();
}
