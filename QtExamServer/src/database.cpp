#include "database.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonValue>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QtGlobal>
#include <algorithm>
#include <QVariant>

namespace {

QString queryError(const QSqlQuery &query)
{
    return query.lastError().text();
}

QVariant fieldValue(const QSqlQuery &query, const char *name)
{
    const int index = query.record().indexOf(QLatin1String(name));
    return index >= 0 ? query.value(index) : QVariant();
}

QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QDateTime parseUtc(const QString &value)
{
    QDateTime dateTime = QDateTime::fromString(value, Qt::ISODate);
    if (!dateTime.isValid()) {
        return {};
    }
    if (dateTime.timeSpec() == Qt::LocalTime) {
        dateTime.setTimeSpec(Qt::UTC);
    }
    return dateTime.toUTC();
}

QString normalizedRole(const QString &role)
{
    return role.trimmed().toLower();
}

bool isManageableRole(const QString &role)
{
    return role == QStringLiteral("teacher") || role == QStringLiteral("student");
}

bool isAccountRole(const QString &role)
{
    return role == QStringLiteral("teacher")
        || role == QStringLiteral("student")
        || role == QStringLiteral("admin");
}

bool isObjectiveQuestionType(const QString &type)
{
    const QString t = type.trimmed();
    return t.contains(QStringLiteral("单选"))
        || t.contains(QStringLiteral("多选"))
        || t.contains(QStringLiteral("选择"))
        || t.contains(QStringLiteral("判断"))
        || t.contains(QStringLiteral("填空"));
}

QString canonicalAnswer(QString text)
{
    text = text.trimmed().toUpper();
    text.replace(QStringLiteral("Ａ"), QStringLiteral("A"));
    text.replace(QStringLiteral("Ｂ"), QStringLiteral("B"));
    text.replace(QStringLiteral("Ｃ"), QStringLiteral("C"));
    text.replace(QStringLiteral("Ｄ"), QStringLiteral("D"));
    text.replace(QStringLiteral("，"), QStringLiteral(","));
    text.replace(QStringLiteral("。"), QStringLiteral("."));
    text.replace(QStringLiteral("、"), QStringLiteral(","));
    text.replace(QStringLiteral("；"), QStringLiteral(";"));

    QString compact;
    for (const QChar &ch : text) {
        if (!ch.isSpace()) {
            compact.append(ch);
        }
    }

    if (compact == QStringLiteral("正确") || compact == QStringLiteral("对") || compact == QStringLiteral("是")
        || compact == QStringLiteral("TRUE") || compact == QStringLiteral("T") || compact == QStringLiteral("YES")
        || compact == QStringLiteral("Y") || compact == QStringLiteral("√")) {
        return QStringLiteral("A");
    }
    if (compact == QStringLiteral("错误") || compact == QStringLiteral("错") || compact == QStringLiteral("否")
        || compact == QStringLiteral("FALSE") || compact == QStringLiteral("F") || compact == QStringLiteral("NO")
        || compact == QStringLiteral("N") || compact == QStringLiteral("×") || compact == QStringLiteral("X")) {
        return QStringLiteral("B");
    }

    QString letters;
    for (const QChar &ch : compact) {
        if (ch >= QLatin1Char('A') && ch <= QLatin1Char('D') && !letters.contains(ch)) {
            letters.append(ch);
        }
    }
    if (!letters.isEmpty()) {
        std::sort(letters.begin(), letters.end());
        return letters;
    }
    return compact;
}

QString riskLevelForScore(int score)
{
    if (score >= 90) {
        return QStringLiteral("严重风险");
    }
    if (score >= 60) {
        return QStringLiteral("高风险");
    }
    if (score >= 30) {
        return QStringLiteral("中风险");
    }
    return QStringLiteral("低风险");
}

QJsonObject questionToJson(const QSqlQuery &query)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), fieldValue(query, "id").toInt());
    object.insert(QStringLiteral("type"), fieldValue(query, "type").toString());
    object.insert(QStringLiteral("course"), fieldValue(query, "course").toString());
    object.insert(QStringLiteral("content"), fieldValue(query, "content").toString());
    object.insert(QStringLiteral("optionA"), fieldValue(query, "option_a").toString());
    object.insert(QStringLiteral("optionB"), fieldValue(query, "option_b").toString());
    object.insert(QStringLiteral("optionC"), fieldValue(query, "option_c").toString());
    object.insert(QStringLiteral("optionD"), fieldValue(query, "option_d").toString());
    object.insert(QStringLiteral("answer"), fieldValue(query, "answer").toString());
    object.insert(QStringLiteral("score"), fieldValue(query, "score").toDouble());
    object.insert(QStringLiteral("difficulty"), fieldValue(query, "difficulty").toString());
    object.insert(QStringLiteral("analysis"), fieldValue(query, "analysis").toString());
    object.insert(QStringLiteral("createdAt"), fieldValue(query, "created_at").toString());
    const int activeIndex = query.record().indexOf(QLatin1String("is_active"));
    if (activeIndex >= 0) {
        object.insert(QStringLiteral("isActive"), query.value(activeIndex).toInt() != 0);
    }
    return object;
}

QJsonObject userToJson(const QSqlQuery &query)
{
    QJsonObject user;
    user.insert(QStringLiteral("id"), fieldValue(query, "id").toInt());
    user.insert(QStringLiteral("username"), fieldValue(query, "username").toString());
    user.insert(QStringLiteral("displayName"), fieldValue(query, "display_name").toString());
    user.insert(QStringLiteral("role"), fieldValue(query, "role").toString());
    user.insert(QStringLiteral("note"), fieldValue(query, "note").toString());
    user.insert(QStringLiteral("createdAt"), fieldValue(query, "created_at").toString());
    const int activeIndex = query.record().indexOf(QLatin1String("is_active"));
    user.insert(QStringLiteral("isActive"), activeIndex < 0 || query.value(activeIndex).toInt() != 0);
    return user;
}

QJsonObject examToJson(const QSqlQuery &query, int studentId)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), fieldValue(query, "id").toInt());
    object.insert(QStringLiteral("name"), fieldValue(query, "name").toString());
    object.insert(QStringLiteral("course"), fieldValue(query, "course").toString());
    object.insert(QStringLiteral("creatorName"), fieldValue(query, "creator_name").toString());
    object.insert(QStringLiteral("startAt"), fieldValue(query, "start_at").toString());
    object.insert(QStringLiteral("endAt"), fieldValue(query, "end_at").toString());
    object.insert(QStringLiteral("durationMinutes"), fieldValue(query, "duration_minutes").toInt());
    object.insert(QStringLiteral("maxSubmissions"), fieldValue(query, "max_submissions").toInt());
    object.insert(QStringLiteral("status"), fieldValue(query, "status").toString());
    object.insert(QStringLiteral("createdAt"), fieldValue(query, "created_at").toString());
    object.insert(QStringLiteral("publishedAt"), fieldValue(query, "published_at").toString());
    QString publishStatus = fieldValue(query, "publish_status").toString();
    if (publishStatus.isEmpty()) {
        publishStatus = object.value(QStringLiteral("status")).toString() == QStringLiteral("published")
                            ? QStringLiteral("published")
                            : QStringLiteral("unpublished");
    }
    object.insert(QStringLiteral("publishStatus"), publishStatus);
    object.insert(QStringLiteral("publishTime"), fieldValue(query, "publish_time").toString());
    object.insert(QStringLiteral("publishTeacherId"), fieldValue(query, "publish_teacher_id").toInt());

    const int finalCount = studentId > 0 ? fieldValue(query, "final_count").toInt() : 0;
    const int draftCount = studentId > 0 ? fieldValue(query, "draft_count").toInt() : 0;
    object.insert(QStringLiteral("mySubmissionCount"), finalCount);
    object.insert(QStringLiteral("hasDraft"), draftCount > 0);

    const QString status = object.value(QStringLiteral("status")).toString();
    const QDateTime startAt = parseUtc(object.value(QStringLiteral("startAt")).toString());
    const QDateTime endAt = parseUtc(object.value(QStringLiteral("endAt")).toString());
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QString state = status;
    bool canSubmit = false;
    if (status == QStringLiteral("published")) {
        if (startAt.isValid() && now < startAt) {
            state = QStringLiteral("not_started");
        } else if (endAt.isValid() && now > endAt) {
            state = QStringLiteral("expired");
        } else {
            state = QStringLiteral("running");
            canSubmit = finalCount < object.value(QStringLiteral("maxSubmissions")).toInt(1);
        }
    }
    object.insert(QStringLiteral("state"), state);
    object.insert(QStringLiteral("canSubmit"), canSubmit);
    return object;
}

double jsonNumber(const QJsonObject &object, const QString &key, double fallback = 0)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return value.toDouble();
    }
    bool ok = false;
    const double number = value.toString().toDouble(&ok);
    return ok ? number : fallback;
}

} // namespace

bool Database::open(const DatabaseConfig &config, QString *error)
{
    activeDriver = config.driver.trimmed().toLower();
    const bool useMySql = (activeDriver == QStringLiteral("mysql") || activeDriver == QStringLiteral("qmysql"));
    if (useMySql) {
        activeDriver = QStringLiteral("mysql");
        db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"));
        db.setHostName(config.host);
        db.setPort(config.port);
        db.setDatabaseName(config.databaseName);
        db.setUserName(config.username);
        db.setPassword(config.password);
        db.setConnectOptions(QStringLiteral("MYSQL_OPT_RECONNECT=1"));
        // 不记录密码，避免日志泄露凭据。
        qInfo().noquote() << QStringLiteral("[DB_CONFIG] driver=mysql host=%1 port=%2 database=%3 user=%4")
                                 .arg(config.host)
                                 .arg(config.port)
                                 .arg(config.databaseName, config.username);
    } else {
        activeDriver = QStringLiteral("sqlite");
        const QFileInfo info(config.sqlitePath);
        if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath())) {
            if (error) {
                *error = QStringLiteral("无法创建数据库目录：%1").arg(info.absolutePath());
            }
            return false;
        }
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
        db.setDatabaseName(config.sqlitePath);
        qInfo().noquote() << QStringLiteral("[DB_CONFIG] driver=sqlite path=%1").arg(config.sqlitePath);
    }

    if (!db.open()) {
        const QString driverList = QSqlDatabase::drivers().join(QStringLiteral(", "));
        const bool mysqlDriverMissing = useMySql && !QSqlDatabase::drivers().contains(QStringLiteral("QMYSQL"));
        qWarning().noquote() << QStringLiteral("[DB_OPEN_ERROR] driver=%1 error=%2 availableDrivers=[%3]%4")
                                    .arg(activeDriver,
                                         db.lastError().text(),
                                         driverList,
                                         mysqlDriverMissing ? QStringLiteral(" QMYSQL_DRIVER_MISSING=1")
                                                            : QString());
        if (error) {
            if (useMySql) {
                // 默认 MySQL 启动失败时明确报错，绝不静默回退 SQLite，避免误以为在跑 MySQL。
                *error = QStringLiteral("MySQL 连接失败：%1\n"
                                        "  driver=mysql host=%2 port=%3 database=%4 user=%5\n"
                                        "  当前 Qt SQL 驱动：%6")
                             .arg(db.lastError().text())
                             .arg(config.host)
                             .arg(config.port)
                             .arg(config.databaseName, config.username, driverList);
                if (mysqlDriverMissing) {
                    *error += QStringLiteral("\n  缺少 QMYSQL 驱动（QMYSQL driver not loaded）。"
                                             "Ubuntu 执行：sudo apt install libqt5sql5-mysql；"
                                             "Windows 需安装 Qt 的 MySQL 插件及 libmysql/libmariadb 客户端库。"
                                             "\n  如仅做本地开发测试，可临时改用 --db-driver sqlite 回退。");
                }
            } else {
                *error = QStringLiteral("SQLite 连接失败：%1。当前 Qt SQL 驱动：%2")
                             .arg(db.lastError().text(), driverList);
            }
        }
        return false;
    }

    qInfo().noquote() << QStringLiteral("[DB_OPEN_OK] driver=%1").arg(activeDriver);

    if (!isMySql()) {
        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    }
    return true;
}

bool Database::initialize(QString *error)
{
    const QString pk = isMySql()
                           ? QStringLiteral("id INTEGER PRIMARY KEY AUTO_INCREMENT")
                           : QStringLiteral("id INTEGER PRIMARY KEY AUTOINCREMENT");
    const QString tableOptions = isMySql()
                                     ? QStringLiteral(" ENGINE=InnoDB DEFAULT CHARSET=utf8mb4")
                                     : QString();

    const QStringList statements = {
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS users (
                %1,
                username VARCHAR(100) NOT NULL UNIQUE,
                password_hash VARCHAR(128) NOT NULL,
                display_name VARCHAR(100) NOT NULL,
                role VARCHAR(20) NOT NULL CHECK(role IN ('student', 'teacher', 'admin')),
                note VARCHAR(255),
                is_active INTEGER NOT NULL DEFAULT 1,
                created_at VARCHAR(40) NOT NULL
            )%2
        )SQL").arg(pk, tableOptions),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS questions (
                %1,
                type VARCHAR(40) NOT NULL,
                course VARCHAR(100),
                content LONGTEXT NOT NULL,
                option_a LONGTEXT,
                option_b LONGTEXT,
                option_c LONGTEXT,
                option_d LONGTEXT,
                answer LONGTEXT,
                score DOUBLE DEFAULT 0,
                difficulty VARCHAR(40),
                analysis LONGTEXT,
                creator_id INTEGER,
                is_active INTEGER NOT NULL DEFAULT 1,
                created_at VARCHAR(40) NOT NULL,
                FOREIGN KEY(creator_id) REFERENCES users(id)
            )%2
        )SQL").arg(pk, tableOptions),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS exams (
                %1,
                name VARCHAR(200) NOT NULL,
                course VARCHAR(100),
                creator_id INTEGER NOT NULL,
                start_at VARCHAR(40) NOT NULL,
                end_at VARCHAR(40) NOT NULL,
                duration_minutes INTEGER NOT NULL DEFAULT 90,
                max_submissions INTEGER NOT NULL DEFAULT 1,
                status VARCHAR(30) NOT NULL DEFAULT 'draft',
                created_at VARCHAR(40) NOT NULL,
                published_at VARCHAR(40),
                closed_at VARCHAR(40),
                FOREIGN KEY(creator_id) REFERENCES users(id)
            )%2
        )SQL").arg(pk, tableOptions),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS exam_publish_records (
                %1,
                exam_id INTEGER NOT NULL,
                teacher_id INTEGER NOT NULL,
                publish_time VARCHAR(40) NOT NULL,
                status VARCHAR(30) NOT NULL DEFAULT 'published',
                FOREIGN KEY(exam_id) REFERENCES exams(id) ON DELETE CASCADE,
                FOREIGN KEY(teacher_id) REFERENCES users(id)
            )%2
        )SQL").arg(pk, tableOptions),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS exam_questions (
                %1,
                exam_id INTEGER NOT NULL,
                question_id INTEGER NOT NULL,
                position INTEGER NOT NULL,
                score DOUBLE DEFAULT 0,
                FOREIGN KEY(exam_id) REFERENCES exams(id) ON DELETE CASCADE,
                FOREIGN KEY(question_id) REFERENCES questions(id)
            )%2
        )SQL").arg(pk, tableOptions),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS submissions (
                %1,
                exam_id INTEGER,
                exam_name VARCHAR(200),
                course VARCHAR(100),
                student_id INTEGER NOT NULL,
                status VARCHAR(30) NOT NULL DEFAULT 'pending',
                grader_id INTEGER,
                locked_at VARCHAR(40),
                graded_at VARCHAR(40),
                submitted_at VARCHAR(40),
                total_score DOUBLE DEFAULT 0,
                risk_score INTEGER DEFAULT 0,
                risk_level VARCHAR(20) DEFAULT '低风险',
                comment LONGTEXT,
                created_at VARCHAR(40) NOT NULL,
                FOREIGN KEY(exam_id) REFERENCES exams(id),
                FOREIGN KEY(student_id) REFERENCES users(id),
                FOREIGN KEY(grader_id) REFERENCES users(id)
            )%2
        )SQL").arg(pk, tableOptions),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS submission_answers (
                %1,
                submission_id INTEGER NOT NULL,
                question_id INTEGER NOT NULL,
                question_index INTEGER NOT NULL,
                answer LONGTEXT,
                score DOUBLE DEFAULT 0,
                comment LONGTEXT,
                FOREIGN KEY(submission_id) REFERENCES submissions(id) ON DELETE CASCADE,
                FOREIGN KEY(question_id) REFERENCES questions(id)
            )%2
        )SQL").arg(pk, tableOptions),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS system_logs (
                %1,
                actor_id INTEGER,
                action VARCHAR(100) NOT NULL,
                detail LONGTEXT,
                created_at VARCHAR(40) NOT NULL
            )%2
        )SQL").arg(pk, tableOptions),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS user_face_profiles (
                %1,
                user_id INTEGER NOT NULL UNIQUE,
                face_template LONGTEXT NOT NULL,
                updated_at VARCHAR(40) NOT NULL,
                FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
            )%2
        )SQL").arg(pk, tableOptions),
        QStringLiteral(R"SQL(
            CREATE TABLE IF NOT EXISTS exam_abnormal_logs (
                %1,
                exam_id INTEGER NOT NULL,
                student_id INTEGER NOT NULL,
                abnormal_type VARCHAR(40) NOT NULL,
                risk_score INTEGER NOT NULL,
                description LONGTEXT,
                create_time VARCHAR(40),
                FOREIGN KEY(exam_id) REFERENCES exams(id),
                FOREIGN KEY(student_id) REFERENCES users(id)
            )%2
        )SQL").arg(pk, tableOptions)
    };

    for (const QString &statement : statements) {
        if (!execSql(statement, error)) {
            return false;
        }
    }

    if (!addColumnIfMissing(QStringLiteral("exams"),
                            QStringLiteral("max_submissions"),
                            QStringLiteral("max_submissions INTEGER NOT NULL DEFAULT 1"),
                            error)) {
        return false;
    }
    if (!addColumnIfMissing(QStringLiteral("exams"),
                            QStringLiteral("status"),
                            QStringLiteral("status VARCHAR(30) NOT NULL DEFAULT 'draft'"),
                            error)) {
        return false;
    }
    if (!addColumnIfMissing(QStringLiteral("exams"),
                            QStringLiteral("published_at"),
                            QStringLiteral("published_at VARCHAR(40)"),
                            error)) {
        return false;
    }
    if (!addColumnIfMissing(QStringLiteral("exams"),
                            QStringLiteral("closed_at"),
                            QStringLiteral("closed_at VARCHAR(40)"),
                            error)) {
        return false;
    }
    if (!addColumnIfMissing(QStringLiteral("submissions"),
                            QStringLiteral("submitted_at"),
                            QStringLiteral("submitted_at VARCHAR(40)"),
                            error)) {
        return false;
    }
    if (!addColumnIfMissing(QStringLiteral("submissions"),
                            QStringLiteral("risk_score"),
                            QStringLiteral("risk_score INTEGER DEFAULT 0"),
                            error)) {
        return false;
    }
    if (!addColumnIfMissing(QStringLiteral("submissions"),
                            QStringLiteral("risk_level"),
                            QStringLiteral("risk_level VARCHAR(20) DEFAULT '低风险'"),
                            error)) {
        return false;
    }
    if (!addColumnIfMissing(QStringLiteral("questions"),
                            QStringLiteral("is_active"),
                            QStringLiteral("is_active INTEGER NOT NULL DEFAULT 1"),
                            error)) {
        return false;
    }
    if (!addColumnIfMissing(QStringLiteral("users"),
                            QStringLiteral("is_active"),
                            QStringLiteral("is_active INTEGER NOT NULL DEFAULT 1"),
                            error)) {
        return false;
    }
    if (!ensurePublishRecordUniqueness(error)) {
        return false;
    }

    // 热点索引属于性能优化，创建失败不应阻断服务启动；失败在 ensureHotIndexes 内部以 qWarning 记录。
    ensureHotIndexes();

    if (!seedDefaultUsers(error)) {
        return false;
    }
    if (!seedSampleQuestions(error)) {
        return false;
    }
    if (!seedSampleExam(error)) {
        return false;
    }
    return seedExistingPublishRecords(error);
}

bool Database::authenticate(const QString &username,
                            const QString &password,
                            const QString &role,
                            ServerUser *user,
                            QString *error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, username, display_name, role, note FROM users "
        "WHERE username = :username AND password_hash = :password_hash AND role = :role "
        "AND is_active = 1"));
    query.bindValue(QStringLiteral(":username"), username.trimmed());
    query.bindValue(QStringLiteral(":password_hash"), passwordHash(password));
    query.bindValue(QStringLiteral(":role"), normalizedRole(role));

    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }

    if (!query.next()) {
        if (error) {
            *error = QStringLiteral("账号、密码或角色不正确。");
        }
        return false;
    }

    if (user) {
        user->id = fieldValue(query, "id").toInt();
        user->username = fieldValue(query, "username").toString();
        user->displayName = fieldValue(query, "display_name").toString();
        user->role = fieldValue(query, "role").toString();
        user->note = fieldValue(query, "note").toString();
    }
    return true;
}

QJsonArray Database::listUsers(const QString &roleFilter, QString *error)
{
    QJsonArray users;
    QSqlQuery query(db);
    const QString filter = normalizedRole(roleFilter);
    if (isAccountRole(filter)) {
        query.prepare(QStringLiteral(
            "SELECT id, username, display_name, role, note, created_at, is_active "
            "FROM users WHERE role = :role AND is_active = 1 ORDER BY role, username"));
        query.bindValue(QStringLiteral(":role"), filter);
    } else {
        query.prepare(QStringLiteral(
            "SELECT id, username, display_name, role, note, created_at, is_active "
            "FROM users WHERE role IN ('admin', 'teacher', 'student') AND is_active = 1 "
            "ORDER BY CASE role WHEN 'admin' THEN 0 WHEN 'teacher' THEN 1 ELSE 2 END, username"));
    }

    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return users;
    }

    while (query.next()) {
        users.append(userToJson(query));
    }
    return users;
}

bool Database::createUser(const QJsonObject &user, QString *error)
{
    const QString role = normalizedRole(user.value(QStringLiteral("role")).toString());
    const QString username = user.value(QStringLiteral("username")).toString().trimmed();
    const QString password = user.value(QStringLiteral("password")).toString();
    QString displayName = user.value(QStringLiteral("displayName")).toString().trimmed();

    if (!isAccountRole(role)) {
        if (error) {
            *error = QStringLiteral("只能创建管理员、教师或学生账号。");
        }
        return false;
    }
    if (username.isEmpty() || password.size() < 6) {
        if (error) {
            *error = QStringLiteral("账号不能为空，密码至少 6 位。");
        }
        return false;
    }
    if (displayName.isEmpty()) {
        displayName = username;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO users (username, password_hash, display_name, role, note, is_active, created_at) "
        "VALUES (:username, :password_hash, :display_name, :role, :note, 1, :created_at)"));
    query.bindValue(QStringLiteral(":username"), username);
    query.bindValue(QStringLiteral(":password_hash"), passwordHash(password));
    query.bindValue(QStringLiteral(":display_name"), displayName);
    query.bindValue(QStringLiteral(":role"), role);
    query.bindValue(QStringLiteral(":note"), user.value(QStringLiteral("note")).toString());
    query.bindValue(QStringLiteral(":created_at"), nowUtc());

    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    return true;
}

bool Database::updateUser(int userId, const QJsonObject &user, QString *error)
{
    const QString role = normalizedRole(user.value(QStringLiteral("role")).toString());
    if (!isAccountRole(role)) {
        if (error) {
            *error = QStringLiteral("只能修改管理员、教师或学生账号。");
        }
        return false;
    }

    QString displayName = user.value(QStringLiteral("displayName")).toString().trimmed();
    if (displayName.isEmpty()) {
        if (error) {
            *error = QStringLiteral("姓名不能为空。");
        }
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE users SET display_name = :display_name, note = :note "
        "WHERE id = :id AND role = :role AND is_active = 1"));
    query.bindValue(QStringLiteral(":display_name"), displayName);
    query.bindValue(QStringLiteral(":role"), role);
    query.bindValue(QStringLiteral(":note"), user.value(QStringLiteral("note")).toString());
    query.bindValue(QStringLiteral(":id"), userId);

    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        if (error) {
            *error = QStringLiteral("没有找到可修改的账号，或账号已停用。");
        }
        return false;
    }
    return true;
}

bool Database::deleteUser(int userId, int actorId, QString *error)
{
    QSqlQuery roleQuery(db);
    roleQuery.prepare(QStringLiteral("SELECT role, is_active FROM users WHERE id = :id"));
    roleQuery.bindValue(QStringLiteral(":id"), userId);
    if (!roleQuery.exec()) {
        if (error) {
            *error = queryError(roleQuery);
        }
        return false;
    }
    if (!roleQuery.next()) {
        if (error) {
            *error = QStringLiteral("没有找到要停用的账号。");
        }
        return false;
    }
    const QString role = fieldValue(roleQuery, "role").toString();
    if (!isAccountRole(role)) {
        if (error) {
            *error = QStringLiteral("只能停用管理员、教师或学生账号。");
        }
        return false;
    }
    if (fieldValue(roleQuery, "is_active").toInt() == 0) {
        if (error) {
            *error = QStringLiteral("该账号已经停用。");
        }
        return false;
    }
    if (userId == actorId) {
        if (error) {
            *error = QStringLiteral("不能删除当前登录账号。");
        }
        return false;
    }

    if (role == QStringLiteral("admin")) {
        QSqlQuery adminCount(db);
        adminCount.prepare(QStringLiteral("SELECT COUNT(*) FROM users WHERE role = 'admin' AND is_active = 1"));
        if (!adminCount.exec()) {
            if (error) {
                *error = queryError(adminCount);
            }
            return false;
        }
        if (adminCount.next() && adminCount.value(0).toInt() <= 1) {
            if (error) {
                *error = QStringLiteral("不能删除最后一个管理员。");
            }
            return false;
        }
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE users SET is_active = 0 WHERE id = :id AND is_active = 1"));
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        if (error) {
            *error = QStringLiteral("账号停用失败，请刷新列表后重试。");
        }
        return false;
    }
    return true;
}

bool Database::changePassword(const QString &targetUsername,
                              const QString &targetRole,
                              const QString &newPassword,
                              QString *error)
{
    const QString role = normalizedRole(targetRole);
    if (!isManageableRole(role)) {
        if (error) {
            *error = QStringLiteral("管理员只能修改教师或学生密码。");
        }
        return false;
    }
    if (newPassword.size() < 6) {
        if (error) {
            *error = QStringLiteral("新密码至少需要 6 位。");
        }
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE users SET password_hash = :password_hash WHERE username = :username AND role = :role"));
    query.bindValue(QStringLiteral(":password_hash"), passwordHash(newPassword));
    query.bindValue(QStringLiteral(":username"), targetUsername.trimmed());
    query.bindValue(QStringLiteral(":role"), role);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        if (error) {
            *error = QStringLiteral("没有找到要修改的用户。");
        }
        return false;
    }
    return true;
}

bool Database::changeOwnPassword(int userId,
                                 const QString &oldPassword,
                                 const QString &newPassword,
                                 QString *error)
{
    if (oldPassword.isEmpty()) {
        if (error) {
            *error = QStringLiteral("请输入旧密码。");
        }
        return false;
    }
    if (newPassword.size() < 6) {
        if (error) {
            *error = QStringLiteral("新密码至少需要 6 位。");
        }
        return false;
    }

    QSqlQuery guard(db);
    guard.prepare(QStringLiteral("SELECT id FROM users WHERE id = :id AND password_hash = :old_hash"));
    guard.bindValue(QStringLiteral(":id"), userId);
    guard.bindValue(QStringLiteral(":old_hash"), passwordHash(oldPassword));
    if (!guard.exec()) {
        if (error) {
            *error = queryError(guard);
        }
        return false;
    }
    if (!guard.next()) {
        if (error) {
            *error = QStringLiteral("旧密码不正确。");
        }
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE users SET password_hash = :password_hash WHERE id = :id"));
    query.bindValue(QStringLiteral(":password_hash"), passwordHash(newPassword));
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        if (error) {
            *error = QStringLiteral("密码未更新，请重新登录后再试。");
        }
        return false;
    }
    return true;
}

bool Database::resetPassword(int userId, const QString &newPassword, QString *error)
{
    if (newPassword.size() < 6) {
        if (error) {
            *error = QStringLiteral("新密码至少需要 6 位。");
        }
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE users SET password_hash = :password_hash WHERE id = :id AND is_active = 1"));
    query.bindValue(QStringLiteral(":password_hash"), passwordHash(newPassword));
    query.bindValue(QStringLiteral(":id"), userId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        if (error) {
            *error = QStringLiteral("没有找到要重置密码的账号，或账号已停用。");
        }
        return false;
    }
    return true;
}

bool Database::saveFaceProfile(int userId, const QString &faceTemplate, QString *error)
{
    const QString normalized = faceTemplate.trimmed();
    // 允许特殊哨兵 "SIMULATION_MODE"（无摄像头演示模式留痕）持久化，
    // 其余真实 SFace 模板仍要求足够长度，防止空/截断数据。
    if (normalized != QStringLiteral("SIMULATION_MODE") && normalized.size() < 200) {
        if (error) {
            *error = QStringLiteral("人脸模板数据为空或过短。");
        }
        return false;
    }

    QSqlQuery userQuery(db);
    userQuery.prepare(QStringLiteral("SELECT role FROM users WHERE id = :id"));
    userQuery.bindValue(QStringLiteral(":id"), userId);
    if (!userQuery.exec()) {
        if (error) {
            *error = queryError(userQuery);
        }
        return false;
    }
    if (!userQuery.next() || fieldValue(userQuery, "role").toString() != QStringLiteral("student")) {
        if (error) {
            *error = QStringLiteral("只有学生账号可以采集人脸信息。");
        }
        return false;
    }

    QSqlQuery existsQuery(db);
    existsQuery.prepare(QStringLiteral("SELECT id FROM user_face_profiles WHERE user_id = :user_id"));
    existsQuery.bindValue(QStringLiteral(":user_id"), userId);
    if (!existsQuery.exec()) {
        if (error) {
            *error = queryError(existsQuery);
        }
        return false;
    }

    QSqlQuery query(db);
    if (existsQuery.next()) {
        query.prepare(QStringLiteral(
            "UPDATE user_face_profiles SET face_template = :face_template, updated_at = :updated_at "
            "WHERE user_id = :user_id"));
    } else {
        query.prepare(QStringLiteral(
            "INSERT INTO user_face_profiles (user_id, face_template, updated_at) "
            "VALUES (:user_id, :face_template, :updated_at)"));
    }
    query.bindValue(QStringLiteral(":user_id"), userId);
    query.bindValue(QStringLiteral(":face_template"), normalized);
    query.bindValue(QStringLiteral(":updated_at"), nowUtc());
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    return true;
}

QJsonObject Database::faceProfile(int userId, QString *error)
{
    QJsonObject profile;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT p.face_template, p.updated_at, u.username, u.display_name "
        "FROM user_face_profiles p JOIN users u ON u.id = p.user_id "
        "WHERE p.user_id = :user_id"));
    query.bindValue(QStringLiteral(":user_id"), userId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return profile;
    }
    if (!query.next()) {
        profile.insert(QStringLiteral("hasProfile"), false);
        return profile;
    }
    profile.insert(QStringLiteral("hasProfile"), true);
    profile.insert(QStringLiteral("faceTemplate"), fieldValue(query, "face_template").toString());
    profile.insert(QStringLiteral("updatedAt"), fieldValue(query, "updated_at").toString());
    profile.insert(QStringLiteral("username"), fieldValue(query, "username").toString());
    profile.insert(QStringLiteral("displayName"), fieldValue(query, "display_name").toString());
    return profile;
}

bool Database::insertQuestions(const QJsonArray &questions, int creatorId, QString *error, QJsonArray *insertedIds)
{
    if (questions.isEmpty()) {
        if (error) {
            *error = QStringLiteral("题目列表为空。");
        }
        return false;
    }
    if (insertedIds) {
        *insertedIds = QJsonArray();
    }

    if (!db.transaction()) {
        if (error) {
            *error = db.lastError().text();
        }
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"SQL(
        INSERT INTO questions (
            type, course, content, option_a, option_b, option_c, option_d,
            answer, score, difficulty, analysis, creator_id, created_at
        ) VALUES (
            :type, :course, :content, :option_a, :option_b, :option_c, :option_d,
            :answer, :score, :difficulty, :analysis, :creator_id, :created_at
        )
    )SQL"));

    for (const QJsonValue &value : questions) {
        const QJsonObject question = value.toObject();
        const QString type = question.value(QStringLiteral("type")).toString().trimmed();
        const QString content = question.value(QStringLiteral("content")).toString().trimmed();
        if (type.isEmpty() || content.isEmpty()) {
            db.rollback();
            if (error) {
                *error = QStringLiteral("题目必须包含题型和题干。");
            }
            return false;
        }

        query.bindValue(QStringLiteral(":type"), type);
        query.bindValue(QStringLiteral(":course"), question.value(QStringLiteral("course")).toString());
        query.bindValue(QStringLiteral(":content"), content);
        query.bindValue(QStringLiteral(":option_a"), question.value(QStringLiteral("optionA")).toString());
        query.bindValue(QStringLiteral(":option_b"), question.value(QStringLiteral("optionB")).toString());
        query.bindValue(QStringLiteral(":option_c"), question.value(QStringLiteral("optionC")).toString());
        query.bindValue(QStringLiteral(":option_d"), question.value(QStringLiteral("optionD")).toString());
        query.bindValue(QStringLiteral(":answer"), question.value(QStringLiteral("answer")).toString());
        query.bindValue(QStringLiteral(":score"), jsonNumber(question, QStringLiteral("score"), 0));
        query.bindValue(QStringLiteral(":difficulty"), question.value(QStringLiteral("difficulty")).toString(QStringLiteral("中等")));
        query.bindValue(QStringLiteral(":analysis"), question.value(QStringLiteral("analysis")).toString());
        query.bindValue(QStringLiteral(":creator_id"), creatorId);
        query.bindValue(QStringLiteral(":created_at"), nowUtc());

        if (!query.exec()) {
            db.rollback();
            if (error) {
                *error = queryError(query);
            }
            return false;
        }
        if (insertedIds) {
            insertedIds->append(query.lastInsertId().toInt());
        }
    }

    if (!db.commit()) {
        if (error) {
            *error = db.lastError().text();
        }
        return false;
    }
    return true;
}

bool Database::updateQuestion(int questionId, const QJsonObject &question, QString *error)
{
    const QString type = question.value(QStringLiteral("type")).toString().trimmed();
    const QString content = question.value(QStringLiteral("content")).toString().trimmed();
    const QString answer = question.value(QStringLiteral("answer")).toString().trimmed();
    if (questionId <= 0 || type.isEmpty() || content.isEmpty()) {
        if (error) {
            *error = QStringLiteral("题型和题干不能为空。");
        }
        return false;
    }
    if (isObjectiveQuestionType(type) && answer.isEmpty()) {
        if (error) {
            *error = QStringLiteral("客观题必须填写参考答案。");
        }
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(R"SQL(
        UPDATE questions SET
            type = :type,
            course = :course,
            content = :content,
            option_a = :option_a,
            option_b = :option_b,
            option_c = :option_c,
            option_d = :option_d,
            answer = :answer,
            score = :score,
            difficulty = :difficulty,
            analysis = :analysis
        WHERE id = :id
    )SQL"));
    query.bindValue(QStringLiteral(":type"), type);
    query.bindValue(QStringLiteral(":course"), question.value(QStringLiteral("course")).toString().trimmed());
    query.bindValue(QStringLiteral(":content"), content);
    query.bindValue(QStringLiteral(":option_a"), question.value(QStringLiteral("optionA")).toString().trimmed());
    query.bindValue(QStringLiteral(":option_b"), question.value(QStringLiteral("optionB")).toString().trimmed());
    query.bindValue(QStringLiteral(":option_c"), question.value(QStringLiteral("optionC")).toString().trimmed());
    query.bindValue(QStringLiteral(":option_d"), question.value(QStringLiteral("optionD")).toString().trimmed());
    query.bindValue(QStringLiteral(":answer"), answer);
    query.bindValue(QStringLiteral(":score"), jsonNumber(question, QStringLiteral("score"), 0));
    query.bindValue(QStringLiteral(":difficulty"), question.value(QStringLiteral("difficulty")).toString().trimmed());
    query.bindValue(QStringLiteral(":analysis"), question.value(QStringLiteral("analysis")).toString().trimmed());
    query.bindValue(QStringLiteral(":id"), questionId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        QSqlQuery exists(db);
        exists.prepare(QStringLiteral("SELECT id FROM questions WHERE id = :id"));
        exists.bindValue(QStringLiteral(":id"), questionId);
        if (exists.exec() && exists.next()) {
            return true;
        }
        if (error) {
            *error = QStringLiteral("没有找到要修改的题目。");
        }
        return false;
    }
    return true;
}

bool Database::deleteQuestion(int questionId, QString *error)
{
    if (questionId <= 0) {
        if (error) {
            *error = QStringLiteral("题目编号无效。");
        }
        return false;
    }

    QSqlQuery refs(db);
    refs.prepare(QStringLiteral(
        "SELECT "
        "(SELECT COUNT(*) FROM exam_questions WHERE question_id = :id) + "
        "(SELECT COUNT(*) FROM submission_answers WHERE question_id = :id) AS refs"));
    refs.bindValue(QStringLiteral(":id"), questionId);
    if (!refs.exec()) {
        if (error) {
            *error = queryError(refs);
        }
        return false;
    }
    if (refs.next() && fieldValue(refs, "refs").toInt() > 0) {
        if (error) {
            *error = QStringLiteral("该题目已被试卷或答卷使用，不能直接删除。");
        }
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM questions WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), questionId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        if (error) {
            *error = QStringLiteral("没有找到要删除的题目。");
        }
        return false;
    }
    return true;
}

bool Database::cleanupDuplicateQuestion(int questionId, QString *action, QString *error)
{
    if (action) {
        action->clear();
    }
    if (questionId <= 0) {
        if (error) {
            *error = QStringLiteral("题目编号无效。");
        }
        return false;
    }

    QSqlQuery exists(db);
    exists.prepare(QStringLiteral("SELECT is_active FROM questions WHERE id = :id"));
    exists.bindValue(QStringLiteral(":id"), questionId);
    if (!exists.exec()) {
        if (error) {
            *error = queryError(exists);
        }
        return false;
    }
    if (!exists.next()) {
        if (error) {
            *error = QStringLiteral("没有找到要清理的题目。");
        }
        return false;
    }
    if (fieldValue(exists, "is_active").toInt() == 0) {
        if (action) {
            *action = QStringLiteral("skipped");
        }
        if (error) {
            error->clear();
        }
        return true;
    }

    QSqlQuery refs(db);
    refs.prepare(QStringLiteral(
        "SELECT "
        "(SELECT COUNT(*) FROM exam_questions WHERE question_id = :id) + "
        "(SELECT COUNT(*) FROM submission_answers WHERE question_id = :id) AS refs"));
    refs.bindValue(QStringLiteral(":id"), questionId);
    if (!refs.exec()) {
        if (error) {
            *error = queryError(refs);
        }
        return false;
    }

    const bool hasRefs = refs.next() && fieldValue(refs, "refs").toInt() > 0;
    if (hasRefs) {
        QSqlQuery disable(db);
        disable.prepare(QStringLiteral("UPDATE questions SET is_active = 0 WHERE id = :id AND is_active <> 0"));
        disable.bindValue(QStringLiteral(":id"), questionId);
        if (!disable.exec()) {
            if (error) {
                *error = queryError(disable);
            }
            return false;
        }
        if (action) {
            *action = QStringLiteral("disabled");
        }
        if (error) {
            error->clear();
        }
        return true;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM questions WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), questionId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        if (error) {
            *error = QStringLiteral("没有找到要清理的题目。");
        }
        return false;
    }
    if (action) {
        *action = QStringLiteral("deleted");
    }
    if (error) {
        error->clear();
    }
    return true;
}

QJsonArray Database::listQuestions(QString *error)
{
    QJsonArray questions;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, type, course, content, option_a, option_b, option_c, option_d, "
        "answer, score, difficulty, analysis, is_active, created_at FROM questions "
        "WHERE is_active = 1 ORDER BY id DESC"));
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return questions;
    }
    while (query.next()) {
        questions.append(questionToJson(query));
    }
    return questions;
}

QJsonArray Database::listQuestionsPaged(int page, int pageSize, int *total, QString *error)
{
    QJsonArray questions;
    if (total) {
        *total = 0;
    }

    const int safePage = qMax(1, page);
    const int safePageSize = qBound(1, pageSize, 100);

    QSqlQuery countQuery(db);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) AS total FROM questions WHERE is_active = 1"))) {
        if (error) {
            *error = queryError(countQuery);
        }
        return questions;
    }
    if (countQuery.next() && total) {
        *total = fieldValue(countQuery, "total").toInt();
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, type, course, content, option_a, option_b, option_c, option_d, "
        "answer, score, difficulty, analysis, is_active, created_at FROM questions "
        "WHERE is_active = 1 ORDER BY id DESC LIMIT ? OFFSET ?"));
    query.addBindValue(safePageSize);
    query.addBindValue((safePage - 1) * safePageSize);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return questions;
    }
    while (query.next()) {
        questions.append(questionToJson(query));
    }
    return questions;
}

int Database::createExam(const QJsonObject &exam, const QJsonArray &questionIds, int creatorId, QString *error)
{
    const QString name = exam.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty() || questionIds.isEmpty()) {
        if (error) {
            *error = QStringLiteral("考试名称和题目不能为空。");
        }
        return 0;
    }

    QDateTime startAt = parseUtc(exam.value(QStringLiteral("startAt")).toString());
    QDateTime endAt = parseUtc(exam.value(QStringLiteral("endAt")).toString());
    if (!startAt.isValid()) {
        startAt = QDateTime::currentDateTimeUtc();
    }
    if (!endAt.isValid()) {
        endAt = startAt.addSecs(90 * 60);
    }
    if (endAt <= startAt) {
        if (error) {
            *error = QStringLiteral("结束时间必须晚于开始时间。");
        }
        return 0;
    }

    if (!db.transaction()) {
        if (error) {
            *error = db.lastError().text();
        }
        return 0;
    }

    QSqlQuery insertExam(db);
    insertExam.prepare(QStringLiteral(
        "INSERT INTO exams (name, course, creator_id, start_at, end_at, duration_minutes, max_submissions, status, created_at, published_at) "
        "VALUES (:name, :course, :creator_id, :start_at, :end_at, :duration_minutes, :max_submissions, 'draft', :created_at, NULL)"));
    insertExam.bindValue(QStringLiteral(":name"), name);
    insertExam.bindValue(QStringLiteral(":course"), exam.value(QStringLiteral("course")).toString());
    insertExam.bindValue(QStringLiteral(":creator_id"), creatorId);
    insertExam.bindValue(QStringLiteral(":start_at"), startAt.toString(Qt::ISODate));
    insertExam.bindValue(QStringLiteral(":end_at"), endAt.toString(Qt::ISODate));
    insertExam.bindValue(QStringLiteral(":duration_minutes"), qMax(1, exam.value(QStringLiteral("durationMinutes")).toInt(90)));
    insertExam.bindValue(QStringLiteral(":max_submissions"), qMax(1, exam.value(QStringLiteral("maxSubmissions")).toInt(1)));
    insertExam.bindValue(QStringLiteral(":created_at"), nowUtc());

    if (!insertExam.exec()) {
        db.rollback();
        if (error) {
            *error = queryError(insertExam);
        }
        return 0;
    }

    const int examId = insertExam.lastInsertId().toInt();
    QSqlQuery scoreQuery(db);
    QSqlQuery insertQuestion(db);
    insertQuestion.prepare(QStringLiteral(
        "INSERT INTO exam_questions (exam_id, question_id, position, score) "
        "VALUES (:exam_id, :question_id, :position, :score)"));

    for (int i = 0; i < questionIds.size(); ++i) {
        int questionId = 0;
        double score = -1;
        if (questionIds.at(i).isObject()) {
            const QJsonObject item = questionIds.at(i).toObject();
            questionId = item.value(QStringLiteral("id")).toInt();
            score = jsonNumber(item, QStringLiteral("score"), -1);
        } else {
            questionId = questionIds.at(i).toInt();
        }

        scoreQuery.prepare(QStringLiteral("SELECT score FROM questions WHERE id = :id AND is_active = 1"));
        scoreQuery.bindValue(QStringLiteral(":id"), questionId);
        if (!scoreQuery.exec() || !scoreQuery.next()) {
            db.rollback();
            if (error) {
                *error = QStringLiteral("题目不存在，无法发布考试。");
            }
            return 0;
        }
        if (score < 0) {
            score = fieldValue(scoreQuery, "score").toDouble();
        }

        insertQuestion.bindValue(QStringLiteral(":exam_id"), examId);
        insertQuestion.bindValue(QStringLiteral(":question_id"), questionId);
        insertQuestion.bindValue(QStringLiteral(":position"), i + 1);
        insertQuestion.bindValue(QStringLiteral(":score"), score);
        if (!insertQuestion.exec()) {
            db.rollback();
            if (error) {
                *error = queryError(insertQuestion);
            }
            return 0;
        }
    }

    if (!db.commit()) {
        if (error) {
            *error = db.lastError().text();
        }
        return 0;
    }
    return examId;
}

bool Database::publishExam(int examId, int actorId, QString *error)
{
    if (examId <= 0 || actorId <= 0) {
        if (error) {
            *error = QStringLiteral("考试或发布人信息不正确。");
        }
        return false;
    }

    if (!db.transaction()) {
        if (error) {
            *error = db.lastError().text();
        }
        return false;
    }

    QSqlQuery examQuery(db);
    examQuery.prepare(QStringLiteral("SELECT id FROM exams WHERE id = :id"));
    examQuery.bindValue(QStringLiteral(":id"), examId);
    if (!examQuery.exec()) {
        db.rollback();
        if (error) {
            *error = queryError(examQuery);
        }
        return false;
    }
    if (!examQuery.next()) {
        db.rollback();
        if (error) {
            *error = QStringLiteral("考试不存在。");
        }
        return false;
    }

    QSqlQuery existing(db);
    existing.prepare(QStringLiteral("SELECT id FROM exam_publish_records WHERE exam_id = :exam_id"));
    existing.bindValue(QStringLiteral(":exam_id"), examId);
    if (!existing.exec()) {
        db.rollback();
        if (error) {
            *error = queryError(existing);
        }
        return false;
    }
    if (existing.next()) {
        db.rollback();
        if (error) {
            *error = QStringLiteral("您已发布此次考试");
        }
        return false;
    }

    const QString publishTime = nowUtc();
    QSqlQuery insertRecord(db);
    insertRecord.prepare(QStringLiteral(
        "INSERT INTO exam_publish_records (exam_id, teacher_id, publish_time, status) "
        "VALUES (:exam_id, :teacher_id, :publish_time, 'published')"));
    insertRecord.bindValue(QStringLiteral(":exam_id"), examId);
    insertRecord.bindValue(QStringLiteral(":teacher_id"), actorId);
    insertRecord.bindValue(QStringLiteral(":publish_time"), publishTime);
    if (!insertRecord.exec()) {
        db.rollback();
        if (error) {
            const QString detail = queryError(insertRecord);
            *error = detail.contains(QStringLiteral("UNIQUE"), Qt::CaseInsensitive)
                         || detail.contains(QStringLiteral("duplicate"), Qt::CaseInsensitive)
                         ? QStringLiteral("您已发布此次考试")
                         : detail;
        }
        return false;
    }

    QSqlQuery updateExam(db);
    updateExam.prepare(QStringLiteral(
        "UPDATE exams SET status = 'published', published_at = :published_at, closed_at = NULL "
        "WHERE id = :id"));
    updateExam.bindValue(QStringLiteral(":published_at"), publishTime);
    updateExam.bindValue(QStringLiteral(":id"), examId);
    if (!updateExam.exec() || updateExam.numRowsAffected() <= 0) {
        db.rollback();
        if (error) {
            *error = updateExam.lastError().isValid() ? queryError(updateExam) : QStringLiteral("考试不存在。");
        }
        return false;
    }

    if (!db.commit()) {
        if (error) {
            *error = db.lastError().text();
        }
        return false;
    }
    return true;
}

bool Database::closeExam(int examId, int actorId, QString *error)
{
    Q_UNUSED(actorId);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE exams SET status = 'closed', closed_at = :closed_at WHERE id = :id"));
    query.bindValue(QStringLiteral(":closed_at"), nowUtc());
    query.bindValue(QStringLiteral(":id"), examId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool Database::deleteExam(int examId, QString *error)
{
    if (examId <= 0) {
        if (error) {
            *error = QStringLiteral("考试编号无效。");
        }
        return false;
    }

    QSqlQuery exists(db);
    exists.prepare(QStringLiteral("SELECT id FROM exams WHERE id = :id"));
    exists.bindValue(QStringLiteral(":id"), examId);
    if (!exists.exec()) {
        if (error) {
            *error = queryError(exists);
        }
        return false;
    }
    if (!exists.next()) {
        if (error) {
            *error = QStringLiteral("考试不存在。");
        }
        return false;
    }

    if (!db.transaction()) {
        if (error) {
            *error = db.lastError().text();
        }
        return false;
    }

    const QStringList statements = {
        QStringLiteral("DELETE FROM submission_answers WHERE submission_id IN (SELECT id FROM submissions WHERE exam_id = :exam_id)"),
        QStringLiteral("DELETE FROM exam_abnormal_logs WHERE exam_id = :exam_id"),
        QStringLiteral("DELETE FROM submissions WHERE exam_id = :exam_id"),
        QStringLiteral("DELETE FROM exam_publish_records WHERE exam_id = :exam_id"),
        QStringLiteral("DELETE FROM exam_questions WHERE exam_id = :exam_id"),
        QStringLiteral("DELETE FROM exams WHERE id = :exam_id")
    };

    for (const QString &statement : statements) {
        QSqlQuery query(db);
        query.prepare(statement);
        query.bindValue(QStringLiteral(":exam_id"), examId);
        if (!query.exec()) {
            db.rollback();
            if (error) {
                *error = queryError(query);
            }
            return false;
        }
    }

    if (!db.commit()) {
        if (error) {
            *error = db.lastError().text();
        }
        return false;
    }
    return true;
}

QJsonArray Database::listExams(const ServerUser &user, QString *error)
{
    QJsonArray exams;
    QSqlQuery query(db);
    if (user.role == QStringLiteral("student")) {
        query.prepare(QStringLiteral(
            "SELECT e.*, u.display_name AS creator_name, pr.status AS publish_status, "
            "pr.publish_time AS publish_time, pr.teacher_id AS publish_teacher_id, "
            "(SELECT COUNT(*) FROM submissions s WHERE s.exam_id = e.id AND s.student_id = :student_id AND s.status <> 'draft') AS final_count, "
            "(SELECT COUNT(*) FROM submissions s WHERE s.exam_id = e.id AND s.student_id = :student_id AND s.status = 'draft') AS draft_count "
            "FROM exams e JOIN users u ON u.id = e.creator_id "
            "LEFT JOIN exam_publish_records pr ON pr.exam_id = e.id "
            "WHERE e.status = 'published' ORDER BY e.start_at DESC, e.id DESC"));
        query.bindValue(QStringLiteral(":student_id"), user.id);
    } else {
        query.prepare(QStringLiteral(
            "SELECT e.*, u.display_name AS creator_name, pr.status AS publish_status, "
            "pr.publish_time AS publish_time, pr.teacher_id AS publish_teacher_id, "
            "0 AS final_count, 0 AS draft_count "
            "FROM exams e JOIN users u ON u.id = e.creator_id "
            "LEFT JOIN exam_publish_records pr ON pr.exam_id = e.id "
            "ORDER BY e.id DESC"));
    }

    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return exams;
    }

    while (query.next()) {
        exams.append(examToJson(query, user.role == QStringLiteral("student") ? user.id : 0));
    }
    return exams;
}

QJsonObject Database::examDetail(int examId, QString *error)
{
    QJsonObject root;
    QSqlQuery examQuery(db);
    examQuery.prepare(QStringLiteral(
        "SELECT e.*, u.display_name AS creator_name, pr.status AS publish_status, "
        "pr.publish_time AS publish_time, pr.teacher_id AS publish_teacher_id, "
        "0 AS final_count, 0 AS draft_count "
        "FROM exams e JOIN users u ON u.id = e.creator_id "
        "LEFT JOIN exam_publish_records pr ON pr.exam_id = e.id "
        "WHERE e.id = :id"));
    examQuery.bindValue(QStringLiteral(":id"), examId);
    if (!examQuery.exec()) {
        if (error) {
            *error = queryError(examQuery);
        }
        return root;
    }
    if (!examQuery.next()) {
        if (error) {
            *error = QStringLiteral("考试不存在。");
        }
        return root;
    }

    root.insert(QStringLiteral("exam"), examToJson(examQuery, 0));

    QJsonArray questions;
    QSqlQuery questionQuery(db);
    questionQuery.prepare(QStringLiteral(
        "SELECT q.id, q.type, q.course, q.content, q.option_a, q.option_b, q.option_c, q.option_d, "
        "q.answer, eq.score, q.difficulty, q.analysis, q.created_at, eq.position "
        "FROM exam_questions eq JOIN questions q ON q.id = eq.question_id "
        "WHERE eq.exam_id = :exam_id ORDER BY eq.position"));
    questionQuery.bindValue(QStringLiteral(":exam_id"), examId);
    if (!questionQuery.exec()) {
        if (error) {
            *error = queryError(questionQuery);
        }
        return {};
    }
    while (questionQuery.next()) {
        QJsonObject question = questionToJson(questionQuery);
        question.insert(QStringLiteral("position"), fieldValue(questionQuery, "position").toInt());
        questions.append(question);
    }
    root.insert(QStringLiteral("questions"), questions);
    return root;
}

int Database::saveStudentAnswers(int examId,
                                 int studentId,
                                 const QJsonArray &answers,
                                 bool finalSubmit,
                                 QString *error,
                                 QJsonObject *gradingSummary)
{
    QJsonObject detail = examDetail(examId, error);
    if (error && !error->isEmpty()) {
        return 0;
    }
    const QJsonObject exam = detail.value(QStringLiteral("exam")).toObject();
    if (exam.value(QStringLiteral("status")).toString() != QStringLiteral("published")) {
        if (error) {
            *error = QStringLiteral("考试未发布或已关闭。");
        }
        return 0;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime startAt = parseUtc(exam.value(QStringLiteral("startAt")).toString());
    const QDateTime endAt = parseUtc(exam.value(QStringLiteral("endAt")).toString());
    if ((startAt.isValid() && now < startAt) || (endAt.isValid() && now > endAt)) {
        if (error) {
            *error = QStringLiteral("当前不在考试允许提交时间内。");
        }
        return 0;
    }

    if (finalSubmit && finalSubmissionCount(examId, studentId, error) >= exam.value(QStringLiteral("maxSubmissions")).toInt(1)) {
        if (error && error->isEmpty()) {
            *error = QStringLiteral("已达到本场考试允许提交次数。");
        }
        return 0;
    }

    QMap<int, QString> answerByQuestionId;
    for (const QJsonValue &value : answers) {
        const QJsonObject answer = value.toObject();
        answerByQuestionId.insert(answer.value(QStringLiteral("questionId")).toInt(),
                                  answer.value(QStringLiteral("answer")).toString());
    }

    const QJsonArray questions = detail.value(QStringLiteral("questions")).toArray();
    QMap<int, double> autoScoreByQuestionId;
    QMap<int, QString> autoCommentByQuestionId;
    double autoScore = 0;
    double paperScore = 0;
    int objectiveCount = 0;
    int manualCount = 0;
    int correctCount = 0;
    int wrongCount = 0;
    int blankCount = 0;

    for (const QJsonValue &value : questions) {
        const QJsonObject question = value.toObject();
        const int questionId = question.value(QStringLiteral("id")).toInt();
        const double maxScore = jsonNumber(question, QStringLiteral("score"), 0);
        const QString type = question.value(QStringLiteral("type")).toString();
        const QString studentAnswer = answerByQuestionId.value(questionId).trimmed();
        const QString referenceAnswer = question.value(QStringLiteral("answer")).toString().trimmed();
        paperScore += maxScore;

        if (!finalSubmit) {
            continue;
        }

        if (!isObjectiveQuestionType(type) || referenceAnswer.isEmpty()) {
            ++manualCount;
            autoScoreByQuestionId.insert(questionId, 0);
            autoCommentByQuestionId.insert(questionId,
                                           referenceAnswer.isEmpty()
                                               ? QStringLiteral("参考答案为空，待教师确认。")
                                               : QStringLiteral("主观题待教师人工批阅。"));
            continue;
        }

        ++objectiveCount;
        if (studentAnswer.isEmpty()) {
            ++blankCount;
            ++wrongCount;
            autoScoreByQuestionId.insert(questionId, 0);
            autoCommentByQuestionId.insert(questionId, QStringLiteral("自动判分：未作答。"));
            continue;
        }

        const bool correct = canonicalAnswer(studentAnswer) == canonicalAnswer(referenceAnswer);
        if (correct) {
            ++correctCount;
            autoScore += maxScore;
            autoScoreByQuestionId.insert(questionId, maxScore);
            autoCommentByQuestionId.insert(questionId, QStringLiteral("自动判分：正确。"));
        } else {
            ++wrongCount;
            autoScoreByQuestionId.insert(questionId, 0);
            autoCommentByQuestionId.insert(questionId, QStringLiteral("自动判分：错误。"));
        }
    }

    const bool autoFinished = finalSubmit && manualCount == 0;
    const QString finalStatus = finalSubmit ? (autoFinished ? QStringLiteral("graded") : QStringLiteral("pending"))
                                            : QStringLiteral("draft");
    const QString finalComment = finalSubmit
        ? (autoFinished
               ? QStringLiteral("客观题已自动判分完成。")
               : QStringLiteral("客观题已自动判分，主观题待教师批阅。"))
        : QString();

    if (!db.transaction()) {
        if (error) {
            *error = db.lastError().text();
        }
        return 0;
    }

    int submissionId = existingDraftSubmission(examId, studentId, error);
    if (error && !error->isEmpty()) {
        db.rollback();
        return 0;
    }

    if (submissionId <= 0) {
        QSqlQuery insertSubmission(db);
        insertSubmission.prepare(QStringLiteral(
            "INSERT INTO submissions (exam_id, exam_name, course, student_id, status, submitted_at, created_at) "
            "VALUES (:exam_id, :exam_name, :course, :student_id, :status, :submitted_at, :created_at)"));
        insertSubmission.bindValue(QStringLiteral(":exam_id"), examId);
        insertSubmission.bindValue(QStringLiteral(":exam_name"), exam.value(QStringLiteral("name")).toString());
        insertSubmission.bindValue(QStringLiteral(":course"), exam.value(QStringLiteral("course")).toString());
        insertSubmission.bindValue(QStringLiteral(":student_id"), studentId);
        insertSubmission.bindValue(QStringLiteral(":status"), finalStatus);
        insertSubmission.bindValue(QStringLiteral(":submitted_at"), finalSubmit ? nowUtc() : QVariant());
        insertSubmission.bindValue(QStringLiteral(":created_at"), nowUtc());
        if (!insertSubmission.exec()) {
            db.rollback();
            if (error) {
                *error = queryError(insertSubmission);
            }
            return 0;
        }
        submissionId = insertSubmission.lastInsertId().toInt();
    } else {
        QSqlQuery updateSubmission(db);
        updateSubmission.prepare(QStringLiteral(
            "UPDATE submissions SET status = :status, submitted_at = :submitted_at, grader_id = NULL, locked_at = NULL "
            "WHERE id = :id AND status = 'draft'"));
        updateSubmission.bindValue(QStringLiteral(":status"), finalStatus);
        updateSubmission.bindValue(QStringLiteral(":submitted_at"), finalSubmit ? nowUtc() : QVariant());
        updateSubmission.bindValue(QStringLiteral(":id"), submissionId);
        if (!updateSubmission.exec()) {
            db.rollback();
            if (error) {
                *error = queryError(updateSubmission);
            }
            return 0;
        }
    }

    QSqlQuery deleteAnswers(db);
    deleteAnswers.prepare(QStringLiteral("DELETE FROM submission_answers WHERE submission_id = :submission_id"));
    deleteAnswers.bindValue(QStringLiteral(":submission_id"), submissionId);
    if (!deleteAnswers.exec()) {
        db.rollback();
        if (error) {
            *error = queryError(deleteAnswers);
        }
        return 0;
    }

    QSqlQuery insertAnswer(db);
    insertAnswer.prepare(QStringLiteral(
        "INSERT INTO submission_answers (submission_id, question_id, question_index, answer, score, comment) "
        "VALUES (:submission_id, :question_id, :question_index, :answer, :score, :comment)"));

    for (int i = 0; i < questions.size(); ++i) {
        const QJsonObject question = questions.at(i).toObject();
        const int questionId = question.value(QStringLiteral("id")).toInt();
        insertAnswer.bindValue(QStringLiteral(":submission_id"), submissionId);
        insertAnswer.bindValue(QStringLiteral(":question_id"), questionId);
        insertAnswer.bindValue(QStringLiteral(":question_index"), i + 1);
        insertAnswer.bindValue(QStringLiteral(":answer"), answerByQuestionId.value(questionId));
        insertAnswer.bindValue(QStringLiteral(":score"), finalSubmit ? autoScoreByQuestionId.value(questionId, 0) : 0);
        insertAnswer.bindValue(QStringLiteral(":comment"), finalSubmit ? autoCommentByQuestionId.value(questionId) : QString());
        if (!insertAnswer.exec()) {
            db.rollback();
            if (error) {
                *error = queryError(insertAnswer);
            }
            return 0;
        }
    }

    QSqlQuery updateTotal(db);
    updateTotal.prepare(QStringLiteral(
        "UPDATE submissions SET total_score = :total_score, comment = :comment, graded_at = :graded_at "
        "WHERE id = :id"));
    updateTotal.bindValue(QStringLiteral(":total_score"), finalSubmit ? autoScore : 0);
    updateTotal.bindValue(QStringLiteral(":comment"), finalComment);
    updateTotal.bindValue(QStringLiteral(":graded_at"), autoFinished ? nowUtc() : QVariant());
    updateTotal.bindValue(QStringLiteral(":id"), submissionId);
    if (!updateTotal.exec()) {
        db.rollback();
        if (error) {
            *error = queryError(updateTotal);
        }
        return 0;
    }

    if (!db.commit()) {
        if (error) {
            *error = db.lastError().text();
        }
        return 0;
    }
    if (gradingSummary) {
        gradingSummary->insert(QStringLiteral("autoScore"), autoScore);
        gradingSummary->insert(QStringLiteral("paperScore"), paperScore);
        gradingSummary->insert(QStringLiteral("objectiveCount"), objectiveCount);
        gradingSummary->insert(QStringLiteral("manualCount"), manualCount);
        gradingSummary->insert(QStringLiteral("correctCount"), correctCount);
        gradingSummary->insert(QStringLiteral("wrongCount"), wrongCount);
        gradingSummary->insert(QStringLiteral("blankCount"), blankCount);
        gradingSummary->insert(QStringLiteral("status"), finalStatus);
    }
    return submissionId;
}

QJsonArray Database::listMySubmissions(int studentId, QString *error)
{
    QJsonArray submissions;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT s.id, s.exam_id, s.exam_name, s.course, s.status, s.locked_at, s.graded_at, s.submitted_at, "
        "s.total_score, s.risk_score, s.risk_level, s.comment, stu.username AS student_username, stu.display_name AS student_name, "
        "tea.username AS grader_username, tea.display_name AS grader_name "
        "FROM submissions s "
        "JOIN users stu ON stu.id = s.student_id "
        "LEFT JOIN users tea ON tea.id = s.grader_id "
        "WHERE s.student_id = :student_id ORDER BY s.id DESC"));
    query.bindValue(QStringLiteral(":student_id"), studentId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return submissions;
    }

    while (query.next()) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), fieldValue(query, "id").toInt());
        object.insert(QStringLiteral("examId"), fieldValue(query, "exam_id").toInt());
        object.insert(QStringLiteral("examName"), fieldValue(query, "exam_name").toString());
        object.insert(QStringLiteral("course"), fieldValue(query, "course").toString());
        object.insert(QStringLiteral("studentUsername"), fieldValue(query, "student_username").toString());
        object.insert(QStringLiteral("studentName"), fieldValue(query, "student_name").toString());
        object.insert(QStringLiteral("status"), fieldValue(query, "status").toString());
        object.insert(QStringLiteral("graderUsername"), fieldValue(query, "grader_username").toString());
        object.insert(QStringLiteral("graderName"), fieldValue(query, "grader_name").toString());
        object.insert(QStringLiteral("lockedAt"), fieldValue(query, "locked_at").toString());
        object.insert(QStringLiteral("gradedAt"), fieldValue(query, "graded_at").toString());
        object.insert(QStringLiteral("submittedAt"), fieldValue(query, "submitted_at").toString());
        object.insert(QStringLiteral("totalScore"), fieldValue(query, "total_score").toDouble());
        object.insert(QStringLiteral("riskScore"), fieldValue(query, "risk_score").toInt());
        object.insert(QStringLiteral("riskLevel"), fieldValue(query, "risk_level").toString());
        object.insert(QStringLiteral("comment"), fieldValue(query, "comment").toString());
        submissions.append(object);
    }
    return submissions;
}

QJsonArray Database::listSubmissions(const QString &statusFilter, QString *error)
{
    QJsonArray submissions;
    QSqlQuery query(db);
    const QString filter = normalizedRole(statusFilter);
    const QString baseSql = QStringLiteral(
        "SELECT s.id, s.exam_id, s.exam_name, s.course, s.status, s.locked_at, s.graded_at, s.submitted_at, "
        "s.total_score, s.risk_score, s.risk_level, s.comment, stu.username AS student_username, stu.display_name AS student_name, "
        "tea.username AS grader_username, tea.display_name AS grader_name "
        "FROM submissions s "
        "JOIN users stu ON stu.id = s.student_id "
        "LEFT JOIN users tea ON tea.id = s.grader_id ");

    if (filter == QStringLiteral("pending") || filter == QStringLiteral("grading") || filter == QStringLiteral("graded")) {
        query.prepare(baseSql + QStringLiteral("WHERE s.status = :status ORDER BY s.id DESC"));
        query.bindValue(QStringLiteral(":status"), filter);
    } else {
        query.prepare(baseSql + QStringLiteral("WHERE s.status IN ('pending', 'grading', 'graded') ORDER BY s.id DESC"));
    }

    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return submissions;
    }

    while (query.next()) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), fieldValue(query, "id").toInt());
        object.insert(QStringLiteral("examId"), fieldValue(query, "exam_id").toInt());
        object.insert(QStringLiteral("examName"), fieldValue(query, "exam_name").toString());
        object.insert(QStringLiteral("course"), fieldValue(query, "course").toString());
        object.insert(QStringLiteral("studentUsername"), fieldValue(query, "student_username").toString());
        object.insert(QStringLiteral("studentName"), fieldValue(query, "student_name").toString());
        object.insert(QStringLiteral("status"), fieldValue(query, "status").toString());
        object.insert(QStringLiteral("graderUsername"), fieldValue(query, "grader_username").toString());
        object.insert(QStringLiteral("graderName"), fieldValue(query, "grader_name").toString());
        object.insert(QStringLiteral("lockedAt"), fieldValue(query, "locked_at").toString());
        object.insert(QStringLiteral("gradedAt"), fieldValue(query, "graded_at").toString());
        object.insert(QStringLiteral("submittedAt"), fieldValue(query, "submitted_at").toString());
        object.insert(QStringLiteral("totalScore"), fieldValue(query, "total_score").toDouble());
        object.insert(QStringLiteral("riskScore"), fieldValue(query, "risk_score").toInt());
        object.insert(QStringLiteral("riskLevel"), fieldValue(query, "risk_level").toString());
        object.insert(QStringLiteral("comment"), fieldValue(query, "comment").toString());
        submissions.append(object);
    }
    return submissions;
}

QJsonObject Database::submissionDetail(int submissionId, const ServerUser &viewer, QString *error)
{
    QJsonObject root;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT s.id, s.exam_id, s.exam_name, s.course, s.student_id, s.status, s.locked_at, s.graded_at, s.submitted_at, "
        "s.total_score, s.risk_score, s.risk_level, s.comment, stu.username AS student_username, stu.display_name AS student_name, "
        "tea.username AS grader_username, tea.display_name AS grader_name, s.grader_id "
        "FROM submissions s "
        "JOIN users stu ON stu.id = s.student_id "
        "LEFT JOIN users tea ON tea.id = s.grader_id "
        "WHERE s.id = :id"));
    query.bindValue(QStringLiteral(":id"), submissionId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return root;
    }
    if (!query.next()) {
        if (error) {
            *error = QStringLiteral("答卷不存在。");
        }
        return root;
    }

    if (viewer.role == QStringLiteral("student") && fieldValue(query, "student_id").toInt() != viewer.id) {
        if (error) {
            *error = QStringLiteral("只能查看自己的答卷。");
        }
        return root;
    }

    QJsonObject submission;
    submission.insert(QStringLiteral("id"), fieldValue(query, "id").toInt());
    submission.insert(QStringLiteral("examId"), fieldValue(query, "exam_id").toInt());
    submission.insert(QStringLiteral("examName"), fieldValue(query, "exam_name").toString());
    submission.insert(QStringLiteral("course"), fieldValue(query, "course").toString());
    submission.insert(QStringLiteral("studentUsername"), fieldValue(query, "student_username").toString());
    submission.insert(QStringLiteral("studentName"), fieldValue(query, "student_name").toString());
    submission.insert(QStringLiteral("studentId"), fieldValue(query, "student_id").toInt());
    submission.insert(QStringLiteral("status"), fieldValue(query, "status").toString());
    submission.insert(QStringLiteral("graderUsername"), fieldValue(query, "grader_username").toString());
    submission.insert(QStringLiteral("graderName"), fieldValue(query, "grader_name").toString());
    submission.insert(QStringLiteral("graderId"), fieldValue(query, "grader_id").toInt());
    submission.insert(QStringLiteral("lockedAt"), fieldValue(query, "locked_at").toString());
    submission.insert(QStringLiteral("gradedAt"), fieldValue(query, "graded_at").toString());
    submission.insert(QStringLiteral("submittedAt"), fieldValue(query, "submitted_at").toString());
    submission.insert(QStringLiteral("totalScore"), fieldValue(query, "total_score").toDouble());
    submission.insert(QStringLiteral("riskScore"), fieldValue(query, "risk_score").toInt());
    submission.insert(QStringLiteral("riskLevel"), fieldValue(query, "risk_level").toString());
    submission.insert(QStringLiteral("comment"), fieldValue(query, "comment").toString());
    root.insert(QStringLiteral("submission"), submission);

    QJsonArray answers;
    QSqlQuery answerQuery(db);
    answerQuery.prepare(QStringLiteral(
        "SELECT a.id AS answer_id, a.question_id, a.question_index, a.answer AS student_answer, "
        "a.score AS awarded_score, a.comment AS answer_comment, "
        "q.type, q.content, q.option_a, q.option_b, q.option_c, q.option_d, q.answer AS reference_answer, "
        "COALESCE(eq.score, q.score) AS max_score "
        "FROM submission_answers a "
        "JOIN submissions s ON s.id = a.submission_id "
        "JOIN questions q ON q.id = a.question_id "
        "LEFT JOIN exam_questions eq ON eq.exam_id = s.exam_id AND eq.question_id = a.question_id "
        "WHERE a.submission_id = :submission_id ORDER BY a.question_index"));
    answerQuery.bindValue(QStringLiteral(":submission_id"), submissionId);
    if (!answerQuery.exec()) {
        if (error) {
            *error = queryError(answerQuery);
        }
        return {};
    }
    while (answerQuery.next()) {
        QJsonObject answer;
        answer.insert(QStringLiteral("answerId"), fieldValue(answerQuery, "answer_id").toInt());
        answer.insert(QStringLiteral("questionId"), fieldValue(answerQuery, "question_id").toInt());
        answer.insert(QStringLiteral("questionIndex"), fieldValue(answerQuery, "question_index").toInt());
        answer.insert(QStringLiteral("type"), fieldValue(answerQuery, "type").toString());
        answer.insert(QStringLiteral("content"), fieldValue(answerQuery, "content").toString());
        answer.insert(QStringLiteral("optionA"), fieldValue(answerQuery, "option_a").toString());
        answer.insert(QStringLiteral("optionB"), fieldValue(answerQuery, "option_b").toString());
        answer.insert(QStringLiteral("optionC"), fieldValue(answerQuery, "option_c").toString());
        answer.insert(QStringLiteral("optionD"), fieldValue(answerQuery, "option_d").toString());
        answer.insert(QStringLiteral("referenceAnswer"), fieldValue(answerQuery, "reference_answer").toString());
        answer.insert(QStringLiteral("studentAnswer"), fieldValue(answerQuery, "student_answer").toString());
        answer.insert(QStringLiteral("maxScore"), fieldValue(answerQuery, "max_score").toDouble());
        answer.insert(QStringLiteral("score"), fieldValue(answerQuery, "awarded_score").toDouble());
        answer.insert(QStringLiteral("comment"), fieldValue(answerQuery, "answer_comment").toString());
        answers.append(answer);
    }
    root.insert(QStringLiteral("answers"), answers);
    return root;
}

bool Database::addRiskEvent(int examId,
                            int studentId,
                            const QString &abnormalType,
                            int riskScore,
                            const QString &description,
                            QString *error,
                            QJsonObject *result)
{
    // 允许 riskScore == 0：模拟监考、模拟核验、网络异常等事件只留痕、不加作弊分，
    // 仍需写入 exam_abnormal_logs 供教师端复核。仅拒绝负分这种非法值。
    if (examId <= 0 || studentId <= 0 || abnormalType.trimmed().isEmpty() || riskScore < 0) {
        if (error) {
            *error = QStringLiteral("风险事件参数不完整。");
        }
        return false;
    }

    QSqlQuery examQuery(db);
    examQuery.prepare(QStringLiteral("SELECT id, name, course FROM exams WHERE id = :id"));
    examQuery.bindValue(QStringLiteral(":id"), examId);
    if (!examQuery.exec()) {
        if (error) {
            *error = queryError(examQuery);
        }
        return false;
    }
    if (!examQuery.next()) {
        if (error) {
            *error = QStringLiteral("考试不存在，无法记录风险。");
        }
        return false;
    }

    QSqlQuery studentQuery(db);
    studentQuery.prepare(QStringLiteral("SELECT id FROM users WHERE id = :id AND role = 'student'"));
    studentQuery.bindValue(QStringLiteral(":id"), studentId);
    if (!studentQuery.exec()) {
        if (error) {
            *error = queryError(studentQuery);
        }
        return false;
    }
    if (!studentQuery.next()) {
        if (error) {
            *error = QStringLiteral("学生账号不存在，无法记录风险。");
        }
        return false;
    }

    if (!db.transaction()) {
        if (error) {
            *error = db.lastError().text();
        }
        return false;
    }

    int submissionId = 0;
    int currentScore = 0;
    QSqlQuery submissionQuery(db);
    submissionQuery.prepare(QStringLiteral(
        "SELECT id, risk_score FROM submissions "
        "WHERE exam_id = :exam_id AND student_id = :student_id "
        "ORDER BY CASE WHEN status = 'draft' THEN 0 ELSE 1 END, id DESC LIMIT 1"));
    submissionQuery.bindValue(QStringLiteral(":exam_id"), examId);
    submissionQuery.bindValue(QStringLiteral(":student_id"), studentId);
    if (!submissionQuery.exec()) {
        db.rollback();
        if (error) {
            *error = queryError(submissionQuery);
        }
        return false;
    }
    if (submissionQuery.next()) {
        submissionId = fieldValue(submissionQuery, "id").toInt();
        currentScore = fieldValue(submissionQuery, "risk_score").toInt();
    } else {
        QSqlQuery insertSubmission(db);
        insertSubmission.prepare(QStringLiteral(
            "INSERT INTO submissions (exam_id, exam_name, course, student_id, status, risk_score, risk_level, created_at) "
            "VALUES (:exam_id, :exam_name, :course, :student_id, 'draft', 0, '低风险', :created_at)"));
        insertSubmission.bindValue(QStringLiteral(":exam_id"), examId);
        insertSubmission.bindValue(QStringLiteral(":exam_name"), fieldValue(examQuery, "name").toString());
        insertSubmission.bindValue(QStringLiteral(":course"), fieldValue(examQuery, "course").toString());
        insertSubmission.bindValue(QStringLiteral(":student_id"), studentId);
        insertSubmission.bindValue(QStringLiteral(":created_at"), nowUtc());
        if (!insertSubmission.exec()) {
            db.rollback();
            if (error) {
                *error = queryError(insertSubmission);
            }
            return false;
        }
        submissionId = insertSubmission.lastInsertId().toInt();
    }

    const int newScore = currentScore + riskScore;
    const QString newLevel = riskLevelForScore(newScore);
    const QString createdAt = nowUtc();

    QSqlQuery insertLog(db);
    insertLog.prepare(QStringLiteral(
        "INSERT INTO exam_abnormal_logs (exam_id, student_id, abnormal_type, risk_score, description, create_time) "
        "VALUES (:exam_id, :student_id, :abnormal_type, :risk_score, :description, :create_time)"));
    insertLog.bindValue(QStringLiteral(":exam_id"), examId);
    insertLog.bindValue(QStringLiteral(":student_id"), studentId);
    insertLog.bindValue(QStringLiteral(":abnormal_type"), abnormalType.trimmed().toUpper());
    insertLog.bindValue(QStringLiteral(":risk_score"), riskScore);
    insertLog.bindValue(QStringLiteral(":description"), description);
    insertLog.bindValue(QStringLiteral(":create_time"), createdAt);
    if (!insertLog.exec()) {
        db.rollback();
        if (error) {
            *error = queryError(insertLog);
        }
        return false;
    }

    QSqlQuery updateSubmission(db);
    updateSubmission.prepare(QStringLiteral(
        "UPDATE submissions SET risk_score = :risk_score, risk_level = :risk_level WHERE id = :id"));
    updateSubmission.bindValue(QStringLiteral(":risk_score"), newScore);
    updateSubmission.bindValue(QStringLiteral(":risk_level"), newLevel);
    updateSubmission.bindValue(QStringLiteral(":id"), submissionId);
    if (!updateSubmission.exec()) {
        db.rollback();
        if (error) {
            *error = queryError(updateSubmission);
        }
        return false;
    }

    if (!db.commit()) {
        if (error) {
            *error = db.lastError().text();
        }
        return false;
    }

    if (result) {
        result->insert(QStringLiteral("submissionId"), submissionId);
        result->insert(QStringLiteral("examId"), examId);
        result->insert(QStringLiteral("studentId"), studentId);
        result->insert(QStringLiteral("addedScore"), riskScore);
        result->insert(QStringLiteral("totalScore"), newScore);
        result->insert(QStringLiteral("riskLevel"), newLevel);
        result->insert(QStringLiteral("createTime"), createdAt);
    }
    return true;
}

QJsonObject Database::currentRisk(int examId, int studentId, QString *error)
{
    QJsonObject result;
    result.insert(QStringLiteral("examId"), examId);
    result.insert(QStringLiteral("studentId"), studentId);
    result.insert(QStringLiteral("addedScore"), 0);
    result.insert(QStringLiteral("totalScore"), 0);
    result.insert(QStringLiteral("riskLevel"), QStringLiteral("低风险"));

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, risk_score, risk_level FROM submissions "
        "WHERE exam_id = :exam_id AND student_id = :student_id "
        "ORDER BY CASE WHEN status = 'draft' THEN 0 ELSE 1 END, id DESC LIMIT 1"));
    query.bindValue(QStringLiteral(":exam_id"), examId);
    query.bindValue(QStringLiteral(":student_id"), studentId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return {};
    }
    if (query.next()) {
        result.insert(QStringLiteral("submissionId"), fieldValue(query, "id").toInt());
        result.insert(QStringLiteral("totalScore"), fieldValue(query, "risk_score").toInt());
        result.insert(QStringLiteral("riskLevel"), fieldValue(query, "risk_level").toString());
    }
    return result;
}

QJsonArray Database::listRiskRecords(QString *error)
{
    QJsonArray records;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT s.exam_id, s.exam_name, s.course, s.student_id, stu.username AS student_username, "
        "stu.display_name AS student_name, s.risk_score, s.risk_level, "
        "COUNT(l.id) AS abnormal_count, MAX(l.create_time) AS recent_time "
        "FROM submissions s "
        "JOIN users stu ON stu.id = s.student_id "
        "LEFT JOIN exam_abnormal_logs l ON l.exam_id = s.exam_id AND l.student_id = s.student_id "
        "WHERE s.risk_score > 0 OR l.id IS NOT NULL "
        "GROUP BY s.exam_id, s.exam_name, s.course, s.student_id, stu.username, stu.display_name, s.risk_score, s.risk_level "
        "ORDER BY s.risk_score DESC, recent_time DESC"));
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return records;
    }

    while (query.next()) {
        QJsonObject record;
        record.insert(QStringLiteral("examId"), fieldValue(query, "exam_id").toInt());
        record.insert(QStringLiteral("examName"), fieldValue(query, "exam_name").toString());
        record.insert(QStringLiteral("course"), fieldValue(query, "course").toString());
        record.insert(QStringLiteral("studentId"), fieldValue(query, "student_id").toInt());
        record.insert(QStringLiteral("studentUsername"), fieldValue(query, "student_username").toString());
        record.insert(QStringLiteral("studentName"), fieldValue(query, "student_name").toString());
        record.insert(QStringLiteral("riskScore"), fieldValue(query, "risk_score").toInt());
        record.insert(QStringLiteral("riskLevel"), fieldValue(query, "risk_level").toString());
        record.insert(QStringLiteral("abnormalCount"), fieldValue(query, "abnormal_count").toInt());
        record.insert(QStringLiteral("recentTime"), fieldValue(query, "recent_time").toString());
        records.append(record);
    }
    return records;
}

QJsonArray Database::listRiskLogs(int examId, int studentId, QString *error)
{
    QJsonArray logs;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, exam_id, student_id, abnormal_type, risk_score, description, create_time "
        "FROM exam_abnormal_logs "
        "WHERE exam_id = :exam_id AND student_id = :student_id "
        "ORDER BY create_time DESC, id DESC"));
    query.bindValue(QStringLiteral(":exam_id"), examId);
    query.bindValue(QStringLiteral(":student_id"), studentId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return logs;
    }
    while (query.next()) {
        QJsonObject log;
        log.insert(QStringLiteral("id"), fieldValue(query, "id").toInt());
        log.insert(QStringLiteral("examId"), fieldValue(query, "exam_id").toInt());
        log.insert(QStringLiteral("studentId"), fieldValue(query, "student_id").toInt());
        log.insert(QStringLiteral("abnormalType"), fieldValue(query, "abnormal_type").toString());
        log.insert(QStringLiteral("riskScore"), fieldValue(query, "risk_score").toInt());
        log.insert(QStringLiteral("description"), fieldValue(query, "description").toString());
        log.insert(QStringLiteral("createTime"), fieldValue(query, "create_time").toString());
        logs.append(log);
    }
    return logs;
}

bool Database::lockSubmission(int submissionId, int graderId, QString *error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE submissions SET status = 'grading', grader_id = :grader_id, locked_at = :locked_at "
        "WHERE id = :id AND status = 'pending'"));
    query.bindValue(QStringLiteral(":grader_id"), graderId);
    query.bindValue(QStringLiteral(":locked_at"), nowUtc());
    query.bindValue(QStringLiteral(":id"), submissionId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        if (error) {
            *error = QStringLiteral("该答卷不是待批阅状态，可能已被其他教师领取。");
        }
        return false;
    }
    return true;
}

bool Database::releaseSubmission(int submissionId, int graderId, bool force, QString *error)
{
    QSqlQuery query(db);
    if (force) {
        query.prepare(QStringLiteral(
            "UPDATE submissions SET status = 'pending', grader_id = NULL, locked_at = NULL "
            "WHERE id = :id AND status = 'grading'"));
    } else {
        query.prepare(QStringLiteral(
            "UPDATE submissions SET status = 'pending', grader_id = NULL, locked_at = NULL "
            "WHERE id = :id AND grader_id = :grader_id AND status = 'grading'"));
        query.bindValue(QStringLiteral(":grader_id"), graderId);
    }
    query.bindValue(QStringLiteral(":id"), submissionId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        if (error) {
            *error = QStringLiteral("无法释放该答卷锁，可能不是当前教师锁定。");
        }
        return false;
    }
    return true;
}

bool Database::submitGrade(int submissionId,
                           int graderId,
                           const QJsonArray &answerGrades,
                           double totalScore,
                           const QString &comment,
                           QString *error)
{
    QSqlQuery guard(db);
    guard.prepare(QStringLiteral("SELECT id FROM submissions WHERE id = :id AND grader_id = :grader_id AND status = 'grading'"));
    guard.bindValue(QStringLiteral(":id"), submissionId);
    guard.bindValue(QStringLiteral(":grader_id"), graderId);
    if (!guard.exec()) {
        if (error) {
            *error = queryError(guard);
        }
        return false;
    }
    if (!guard.next()) {
        if (error) {
            *error = QStringLiteral("提交评分失败，该答卷可能未由当前教师锁定。");
        }
        return false;
    }

    if (!db.transaction()) {
        if (error) {
            *error = db.lastError().text();
        }
        return false;
    }

    double computedTotal = 0;
    QSqlQuery updateAnswer(db);
    updateAnswer.prepare(QStringLiteral(
        "UPDATE submission_answers SET score = :score, comment = :comment "
        "WHERE id = :answer_id AND submission_id = :submission_id"));
    for (const QJsonValue &value : answerGrades) {
        const QJsonObject grade = value.toObject();
        const double score = jsonNumber(grade, QStringLiteral("score"), 0);
        computedTotal += score;
        updateAnswer.bindValue(QStringLiteral(":score"), score);
        updateAnswer.bindValue(QStringLiteral(":comment"), grade.value(QStringLiteral("comment")).toString());
        updateAnswer.bindValue(QStringLiteral(":answer_id"), grade.value(QStringLiteral("answerId")).toInt());
        updateAnswer.bindValue(QStringLiteral(":submission_id"), submissionId);
        if (!updateAnswer.exec()) {
            db.rollback();
            if (error) {
                *error = queryError(updateAnswer);
            }
            return false;
        }
    }

    if (totalScore < 0) {
        totalScore = computedTotal;
    }

    QSqlQuery updateSubmission(db);
    updateSubmission.prepare(QStringLiteral(
        "UPDATE submissions SET status = 'graded', total_score = :total_score, comment = :comment, graded_at = :graded_at "
        "WHERE id = :id AND grader_id = :grader_id AND status = 'grading'"));
    updateSubmission.bindValue(QStringLiteral(":total_score"), totalScore);
    updateSubmission.bindValue(QStringLiteral(":comment"), comment);
    updateSubmission.bindValue(QStringLiteral(":graded_at"), nowUtc());
    updateSubmission.bindValue(QStringLiteral(":id"), submissionId);
    updateSubmission.bindValue(QStringLiteral(":grader_id"), graderId);
    if (!updateSubmission.exec()) {
        db.rollback();
        if (error) {
            *error = queryError(updateSubmission);
        }
        return false;
    }

    if (!db.commit()) {
        if (error) {
            *error = db.lastError().text();
        }
        return false;
    }
    return true;
}

bool Database::execSql(const QString &sql, QString *error)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        if (error) {
            *error = queryError(query);
        }
        return false;
    }
    return true;
}

bool Database::addColumnIfMissing(const QString &table, const QString &column, const QString &definition, QString *error)
{
    if (isMySql()) {
        QSqlQuery info(db);
        info.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table AND COLUMN_NAME = :column"));
        info.bindValue(QStringLiteral(":table"), table);
        info.bindValue(QStringLiteral(":column"), column);
        if (!info.exec()) {
            if (error) {
                *error = queryError(info);
            }
            return false;
        }
        if (info.next() && info.value(0).toInt() > 0) {
            return true;
        }
        return execSql(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(table, definition), error);
    }

    QSqlQuery info(db);
    if (!info.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        if (error) {
            *error = queryError(info);
        }
        return false;
    }
    while (info.next()) {
        if (fieldValue(info, "name").toString() == column) {
            return true;
        }
    }
    return execSql(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(table, definition), error);
}

bool Database::ensurePublishRecordUniqueness(QString *error)
{
    if (!execSql(QStringLiteral(
            "DELETE FROM exam_publish_records "
            "WHERE id NOT IN (SELECT min_id FROM (SELECT MIN(id) AS min_id FROM exam_publish_records GROUP BY exam_id) kept)"),
            error)) {
        return false;
    }

    if (isMySql()) {
        QSqlQuery info(db);
        info.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM information_schema.STATISTICS "
            "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'exam_publish_records' "
            "AND INDEX_NAME = 'ux_exam_publish_records_exam_id'"));
        if (!info.exec()) {
            if (error) {
                *error = queryError(info);
            }
            return false;
        }
        if (info.next() && info.value(0).toInt() > 0) {
            return true;
        }
        return execSql(QStringLiteral(
                           "CREATE UNIQUE INDEX ux_exam_publish_records_exam_id "
                           "ON exam_publish_records (exam_id)"),
                       error);
    }

    return execSql(QStringLiteral(
                       "CREATE UNIQUE INDEX IF NOT EXISTS ux_exam_publish_records_exam_id "
                       "ON exam_publish_records (exam_id)"),
                   error);
}

void Database::ensureHotIndexes()
{
    // 高频查询字段的热点索引。说明：
    // - exam_publish_records(exam_id) 已由唯一索引 ux_exam_publish_records_exam_id 覆盖，故不重复创建；
    // - 复合索引 (exam_id, student_id) 的最左前缀已可服务单独按 exam_id 的查询，故不再单建 (exam_id)。
    struct HotIndex {
        const char *name;
        const char *table;
        const char *columns;
    };
    static const QList<HotIndex> hotIndexes = {
        {"idx_exam_abnormal_logs_exam_student", "exam_abnormal_logs", "exam_id, student_id"},
        {"idx_exam_abnormal_logs_student", "exam_abnormal_logs", "student_id"},
        {"idx_submissions_exam_student", "submissions", "exam_id, student_id"},
        {"idx_submissions_student", "submissions", "student_id"},
        {"idx_exam_questions_exam", "exam_questions", "exam_id"}
    };

    const bool mysql = isMySql();
    qDebug() << "[DB_INDEX_INIT] 开始创建热点索引, 驱动 ="
             << (mysql ? "mysql" : "sqlite") << ", 数量 =" << hotIndexes.size();

    for (const HotIndex &index : hotIndexes) {
        const QString name = QString::fromLatin1(index.name);
        const QString table = QString::fromLatin1(index.table);
        const QString columns = QString::fromLatin1(index.columns);
        const QString target = table + QStringLiteral(" (") + columns + QStringLiteral(")");

        // 先判断索引是否已存在：MySQL 的 CREATE INDEX 无可移植的 IF NOT EXISTS 写法，必须先查 information_schema。
        QSqlQuery info(db);
        if (mysql) {
            info.prepare(QStringLiteral(
                "SELECT COUNT(*) FROM information_schema.STATISTICS "
                "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :table AND INDEX_NAME = :name"));
            info.bindValue(QStringLiteral(":table"), table);
            info.bindValue(QStringLiteral(":name"), name);
        } else {
            info.prepare(QStringLiteral(
                "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = :name"));
            info.bindValue(QStringLiteral(":name"), name);
        }
        if (!info.exec()) {
            qWarning() << "[DB_INDEX_ERROR] 检查索引是否存在失败:" << name << "->" << queryError(info);
            continue;
        }
        if (info.next() && info.value(0).toInt() > 0) {
            qDebug() << "[DB_INDEX_SKIP] 索引已存在:" << name;
            continue;
        }

        // MySQL 已确认不存在，使用裸 CREATE INDEX；SQLite 额外带 IF NOT EXISTS 双保险。
        const QString createSql = mysql
                                      ? QStringLiteral("CREATE INDEX %1 ON %2 (%3)").arg(name, table, columns)
                                      : QStringLiteral("CREATE INDEX IF NOT EXISTS %1 ON %2 (%3)").arg(name, table, columns);
        QSqlQuery create(db);
        if (!create.exec(createSql)) {
            qWarning() << "[DB_INDEX_ERROR] 创建索引失败:" << name << "ON" << target << "->" << queryError(create);
            continue;
        }
        qDebug() << "[DB_INDEX_OK] 已创建索引:" << name << "ON" << target;
    }
}

bool Database::seedExistingPublishRecords(QString *error)
{
    const QString statement = isMySql()
                                  ? QStringLiteral(
                                        "INSERT IGNORE INTO exam_publish_records (exam_id, teacher_id, publish_time, status) "
                                        "SELECT id, creator_id, COALESCE(published_at, created_at), 'published' "
                                        "FROM exams WHERE status = 'published'")
                                  : QStringLiteral(
                                        "INSERT OR IGNORE INTO exam_publish_records (exam_id, teacher_id, publish_time, status) "
                                        "SELECT id, creator_id, COALESCE(published_at, created_at), 'published' "
                                        "FROM exams WHERE status = 'published'");
    return execSql(statement, error);
}

bool Database::seedDefaultUsers(QString *error)
{
    QSqlQuery countQuery(db);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM users"))) {
        if (error) {
            *error = queryError(countQuery);
        }
        return false;
    }
    if (countQuery.next() && countQuery.value(0).toInt() > 0) {
        return true;
    }

    struct DefaultUser {
        QString username;
        QString password;
        QString displayName;
        QString role;
        QString note;
    };

    const QList<DefaultUser> defaults = {
        {QStringLiteral("student001"), QStringLiteral("123456"), QStringLiteral("演示学生甲"), QStringLiteral("student"), QStringLiteral("演示班级")},
        {QStringLiteral("student002"), QStringLiteral("123456"), QStringLiteral("演示学生乙"), QStringLiteral("student"), QStringLiteral("演示班级")},
        {QStringLiteral("teacher001"), QStringLiteral("123456"), QStringLiteral("演示教师甲"), QStringLiteral("teacher"), QStringLiteral("C++ 程序设计")},
        {QStringLiteral("teacher002"), QStringLiteral("123456"), QStringLiteral("演示教师乙"), QStringLiteral("teacher"), QStringLiteral("高等数学 A")},
        {QStringLiteral("admin"), QStringLiteral("admin123"), QStringLiteral("系统管理员"), QStringLiteral("admin"), QStringLiteral("教务管理")}
    };

    QSqlQuery insertQuery(db);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO users (username, password_hash, display_name, role, note, created_at) "
        "VALUES (:username, :password_hash, :display_name, :role, :note, :created_at)"));
    for (const DefaultUser &user : defaults) {
        insertQuery.bindValue(QStringLiteral(":username"), user.username);
        insertQuery.bindValue(QStringLiteral(":password_hash"), passwordHash(user.password));
        insertQuery.bindValue(QStringLiteral(":display_name"), user.displayName);
        insertQuery.bindValue(QStringLiteral(":role"), user.role);
        insertQuery.bindValue(QStringLiteral(":note"), user.note);
        insertQuery.bindValue(QStringLiteral(":created_at"), nowUtc());
        if (!insertQuery.exec()) {
            if (error) {
                *error = queryError(insertQuery);
            }
            return false;
        }
    }
    return true;
}

bool Database::seedSampleQuestions(QString *error)
{
    QSqlQuery countQuery(db);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM questions"))) {
        if (error) {
            *error = queryError(countQuery);
        }
        return false;
    }
    if (countQuery.next() && countQuery.value(0).toInt() > 0) {
        return true;
    }

    QSqlQuery teacherQuery(db);
    int teacherId = 0;
    if (teacherQuery.exec(QStringLiteral("SELECT id FROM users WHERE username = 'teacher001'")) && teacherQuery.next()) {
        teacherId = teacherQuery.value(0).toInt();
    }

    QJsonArray questions;
    QJsonObject q1;
    q1.insert(QStringLiteral("type"), QStringLiteral("程序题"));
    q1.insert(QStringLiteral("course"), QStringLiteral("C++ 程序设计"));
    q1.insert(QStringLiteral("content"), QStringLiteral("定义运算回调函数类型，实现加减两个运算函数，封装统一计算接口，通过函数指针动态调用不同运算。"));
    q1.insert(QStringLiteral("answer"), QStringLiteral("能定义函数指针类型，完成 add/sub 函数，并通过统一接口调用。"));
    q1.insert(QStringLiteral("score"), 30);
    q1.insert(QStringLiteral("difficulty"), QStringLiteral("中等"));
    questions.append(q1);

    QJsonObject q2;
    q2.insert(QStringLiteral("type"), QStringLiteral("程序题"));
    q2.insert(QStringLiteral("course"), QStringLiteral("C++ 程序设计"));
    q2.insert(QStringLiteral("content"), QStringLiteral("设计 Array 类，内部用 int* 动态开辟数组；自定义拷贝构造、赋值重载和析构，手动实现深拷贝。"));
    q2.insert(QStringLiteral("answer"), QStringLiteral("需要正确实现三/五法则中的拷贝控制，避免浅拷贝重复释放。"));
    q2.insert(QStringLiteral("score"), 30);
    q2.insert(QStringLiteral("difficulty"), QStringLiteral("困难"));
    questions.append(q2);

    return insertQuestions(questions, teacherId, error);
}

bool Database::seedSampleExam(QString *error)
{
    QSqlQuery countQuery(db);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM exams"))) {
        if (error) {
            *error = queryError(countQuery);
        }
        return false;
    }
    if (countQuery.next() && countQuery.value(0).toInt() > 0) {
        return true;
    }

    QSqlQuery teacherQuery(db);
    int teacherId = 0;
    if (teacherQuery.exec(QStringLiteral("SELECT id FROM users WHERE username = 'teacher001'")) && teacherQuery.next()) {
        teacherId = teacherQuery.value(0).toInt();
    }
    if (teacherId <= 0) {
        return true;
    }

    QJsonArray questionIds;
    QSqlQuery questionQuery(db);
    if (!questionQuery.exec(QStringLiteral("SELECT id FROM questions WHERE is_active = 1 ORDER BY id LIMIT 2"))) {
        if (error) {
            *error = queryError(questionQuery);
        }
        return false;
    }
    while (questionQuery.next()) {
        questionIds.append(questionQuery.value(0).toInt());
    }
    if (questionIds.isEmpty()) {
        return true;
    }

    QJsonObject exam;
    exam.insert(QStringLiteral("name"), QStringLiteral("C++ 程序设计模拟考试"));
    exam.insert(QStringLiteral("course"), QStringLiteral("C++ 程序设计"));
    exam.insert(QStringLiteral("startAt"), QDateTime::currentDateTimeUtc().addSecs(-3600).toString(Qt::ISODate));
    exam.insert(QStringLiteral("endAt"), QDateTime::currentDateTimeUtc().addDays(7).toString(Qt::ISODate));
    exam.insert(QStringLiteral("durationMinutes"), 90);
    exam.insert(QStringLiteral("maxSubmissions"), 1);
    const int examId = createExam(exam, questionIds, teacherId, error);
    if (examId <= 0) {
        return false;
    }
    return publishExam(examId, teacherId, error);
}

int Database::existingDraftSubmission(int examId, int studentId, QString *error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT id FROM submissions WHERE exam_id = :exam_id AND student_id = :student_id AND status = 'draft' ORDER BY id DESC LIMIT 1"));
    query.bindValue(QStringLiteral(":exam_id"), examId);
    query.bindValue(QStringLiteral(":student_id"), studentId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return 0;
    }
    return query.next() ? query.value(0).toInt() : 0;
}

int Database::finalSubmissionCount(int examId, int studentId, QString *error)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM submissions WHERE exam_id = :exam_id AND student_id = :student_id AND status <> 'draft'"));
    query.bindValue(QStringLiteral(":exam_id"), examId);
    query.bindValue(QStringLiteral(":student_id"), studentId);
    if (!query.exec()) {
        if (error) {
            *error = queryError(query);
        }
        return 0;
    }
    return query.next() ? query.value(0).toInt() : 0;
}

QString Database::passwordHash(const QString &password)
{
    const QByteArray salt = QByteArrayLiteral("qt-exam-system-v1:");
    return QString::fromLatin1(QCryptographicHash::hash(salt + password.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool Database::isMySql() const
{
    return activeDriver == QStringLiteral("mysql") || db.driverName().compare(QStringLiteral("QMYSQL"), Qt::CaseInsensitive) == 0;
}
