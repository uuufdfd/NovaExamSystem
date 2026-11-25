#pragma once

#include "database.h"
#include "riskscoremanager.h"

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

class ExamServer : public QObject
{
    Q_OBJECT

public:
    explicit ExamServer(QObject *parent = nullptr);
    bool start(const QHostAddress &address, quint16 port, const DatabaseConfig &databaseConfig, QString *error);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    QJsonObject handleRequest(const QJsonObject &request);
    QJsonObject handleLogin(const QJsonObject &request);
    QJsonObject handleListUsers(const QJsonObject &request);
    QJsonObject handleCreateUser(const QJsonObject &request);
    QJsonObject handleUpdateUser(const QJsonObject &request);
    QJsonObject handleDeleteUser(const QJsonObject &request);
    QJsonObject handleChangePassword(const QJsonObject &request);
    QJsonObject handleResetPassword(const QJsonObject &request);
    QJsonObject handleSaveFaceProfile(const QJsonObject &request);
    QJsonObject handleGetMyFaceProfile(const QJsonObject &request);
    QJsonObject handleImportQuestions(const QJsonObject &request);
    QJsonObject handleCreateQuestion(const QJsonObject &request);
    QJsonObject handleUpdateQuestion(const QJsonObject &request);
    QJsonObject handleDeleteQuestion(const QJsonObject &request);
    QJsonObject handleCleanupDuplicateQuestion(const QJsonObject &request);
    QJsonObject handleListQuestions(const QJsonObject &request);
    QJsonObject handleCreateExam(const QJsonObject &request);
    QJsonObject handlePublishExam(const QJsonObject &request);
    QJsonObject handleCloseExam(const QJsonObject &request);
    QJsonObject handleDeleteExam(const QJsonObject &request);
    QJsonObject handleListExams(const QJsonObject &request);
    QJsonObject handleGetExamDetail(const QJsonObject &request);
    QJsonObject handleSaveDraft(const QJsonObject &request);
    QJsonObject handleSubmitExam(const QJsonObject &request);
    QJsonObject handleListMySubmissions(const QJsonObject &request);
    QJsonObject handleListSubmissions(const QJsonObject &request);
    QJsonObject handleGetSubmissionDetail(const QJsonObject &request);
    QJsonObject handleLockSubmission(const QJsonObject &request);
    QJsonObject handleReleaseSubmission(const QJsonObject &request);
    QJsonObject handleSubmitGrade(const QJsonObject &request);
    QJsonObject handleRecordAbnormalEvent(const QJsonObject &request);
    QJsonObject handleListRiskRecords(const QJsonObject &request);
    QJsonObject handleListRiskLogs(const QJsonObject &request);

    bool requireRole(const QJsonObject &request,
                     const QStringList &roles,
                     ServerUser *user,
                     QJsonObject *response) const;

    void sendResponse(QTcpSocket *socket, const QJsonObject &response);
    static QJsonObject ok(const QString &type);
    static QJsonObject fail(const QString &type, const QString &message);
    static QJsonObject userToJson(const ServerUser &user);

    QTcpServer tcpServer;
    Database database;
    RiskScoreManager riskScoreManager;
    QHash<QTcpSocket *, QByteArray> buffers;
    QHash<QString, ServerUser> sessions;
};
