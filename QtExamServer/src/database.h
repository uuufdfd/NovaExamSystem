#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

struct ServerUser
{
    int id = 0;
    QString username;
    QString displayName;
    QString role;
    QString note;
};

struct DatabaseConfig
{
    QString driver = QStringLiteral("mysql");
    QString sqlitePath;
    QString host = QStringLiteral("127.0.0.1");
    int port = 3306;
    QString databaseName = QStringLiteral("qt_exam_server");
    QString username;
    QString password;
};

class Database
{
public:
    bool open(const DatabaseConfig &config, QString *error);
    bool initialize(QString *error);

    bool authenticate(const QString &username,
                      const QString &password,
                      const QString &role,
                      ServerUser *user,
                      QString *error);

    QJsonArray listUsers(const QString &roleFilter, QString *error);
    bool createUser(const QJsonObject &user, QString *error);
    bool updateUser(int userId, const QJsonObject &user, QString *error);
    bool deleteUser(int userId, int actorId, QString *error);
    bool changePassword(const QString &targetUsername,
                        const QString &targetRole,
                        const QString &newPassword,
                        QString *error);
    bool changeOwnPassword(int userId,
                           const QString &oldPassword,
                           const QString &newPassword,
                           QString *error);
    bool resetPassword(int userId, const QString &newPassword, QString *error);
    bool saveFaceProfile(int userId, const QString &faceTemplate, QString *error);
    QJsonObject faceProfile(int userId, QString *error);

    bool insertQuestions(const QJsonArray &questions,
                         int creatorId,
                         QString *error,
                         QJsonArray *insertedIds = nullptr);
    bool updateQuestion(int questionId, const QJsonObject &question, QString *error);
    bool deleteQuestion(int questionId, QString *error);
    bool cleanupDuplicateQuestion(int questionId, QString *action, QString *error);
    QJsonArray listQuestions(QString *error);
    QJsonArray listQuestionsPaged(int page, int pageSize, int *total, QString *error);

    int createExam(const QJsonObject &exam, const QJsonArray &questionIds, int creatorId, QString *error);
    bool publishExam(int examId, int actorId, QString *error);
    bool closeExam(int examId, int actorId, QString *error);
    bool deleteExam(int examId, QString *error);
    QJsonArray listExams(const ServerUser &user, QString *error);
    QJsonObject examDetail(int examId, QString *error);

    int saveStudentAnswers(int examId,
                           int studentId,
                           const QJsonArray &answers,
                           bool finalSubmit,
                           QString *error,
                           QJsonObject *gradingSummary = nullptr);
    QJsonArray listMySubmissions(int studentId, QString *error);
    QJsonArray listSubmissions(const QString &statusFilter, QString *error);
    QJsonObject submissionDetail(int submissionId, const ServerUser &viewer, QString *error);
    bool addRiskEvent(int examId,
                      int studentId,
                      const QString &abnormalType,
                      int riskScore,
                      const QString &description,
                      QString *error,
                      QJsonObject *result = nullptr);
    QJsonObject currentRisk(int examId, int studentId, QString *error);
    QJsonArray listRiskRecords(QString *error);
    QJsonArray listRiskLogs(int examId, int studentId, QString *error);
    bool lockSubmission(int submissionId, int graderId, QString *error);
    bool releaseSubmission(int submissionId, int graderId, bool force, QString *error);
    bool submitGrade(int submissionId,
                     int graderId,
                     const QJsonArray &answerGrades,
                     double totalScore,
                     const QString &comment,
                     QString *error);

private:
    bool execSql(const QString &sql, QString *error);
    bool addColumnIfMissing(const QString &table, const QString &column, const QString &definition, QString *error);
    bool ensurePublishRecordUniqueness(QString *error);
    void ensureHotIndexes();
    bool seedExistingPublishRecords(QString *error);
    bool seedDefaultUsers(QString *error);
    bool seedSampleQuestions(QString *error);
    bool seedSampleExam(QString *error);
    int existingDraftSubmission(int examId, int studentId, QString *error);
    int finalSubmissionCount(int examId, int studentId, QString *error);
    static QString passwordHash(const QString &password);
    bool isMySql() const;

    QSqlDatabase db;
    QString activeDriver;
};
