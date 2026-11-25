#include "examserver.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTimer>
#include <QtGlobal>
#include <QUuid>

ExamServer::ExamServer(QObject *parent)
    : QObject(parent)
    , riskScoreManager(&database)
{
    connect(&tcpServer, &QTcpServer::newConnection, this, &ExamServer::onNewConnection);
}

bool ExamServer::start(const QHostAddress &address, quint16 port, const DatabaseConfig &databaseConfig, QString *error)
{
    if (!database.open(databaseConfig, error)) {
        return false;
    }
    if (!database.initialize(error)) {
        return false;
    }
    if (!tcpServer.listen(address, port)) {
        if (error) {
            *error = tcpServer.errorString();
        }
        return false;
    }

    qInfo().noquote() << QStringLiteral("QtExamServer started on %1:%2").arg(tcpServer.serverAddress().toString()).arg(tcpServer.serverPort());
    if (databaseConfig.driver.trimmed().toLower() == QStringLiteral("mysql")) {
        qInfo().noquote() << QStringLiteral("Database: mysql://%1:%2/%3")
                                  .arg(databaseConfig.host)
                                  .arg(databaseConfig.port)
                                  .arg(databaseConfig.databaseName);
    } else {
        qInfo().noquote() << QStringLiteral("Database: %1").arg(databaseConfig.sqlitePath);
    }
    return true;
}

void ExamServer::onNewConnection()
{
    while (QTcpSocket *socket = tcpServer.nextPendingConnection()) {
        buffers.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, &ExamServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &ExamServer::onDisconnected);
        qInfo().noquote() << QStringLiteral("Client connected: %1:%2")
                                  .arg(socket->peerAddress().toString())
                                  .arg(socket->peerPort());
    }
}

void ExamServer::onReadyRead()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    QByteArray &buffer = buffers[socket];
    buffer.append(socket->readAll());

    int newlineIndex = buffer.indexOf('\n');
    while (newlineIndex >= 0) {
        const QByteArray line = buffer.left(newlineIndex).trimmed();
        buffer.remove(0, newlineIndex + 1);

        if (!line.isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                sendResponse(socket, fail(QStringLiteral("error"), QStringLiteral("请求不是合法 JSON 对象。")));
            } else {
                sendResponse(socket, handleRequest(document.object()));
            }
        }

        newlineIndex = buffer.indexOf('\n');
    }
}

void ExamServer::onDisconnected()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }

    buffers.remove(socket);
    socket->deleteLater();
    qInfo().noquote() << QStringLiteral("Client disconnected: %1:%2")
                              .arg(socket->peerAddress().toString())
                              .arg(socket->peerPort());
}

QJsonObject ExamServer::handleRequest(const QJsonObject &request)
{
    const QString type = request.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("ping")) {
        QJsonObject response = ok(QStringLiteral("pong"));
        response.insert(QStringLiteral("serverTime"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        return response;
    }
    if (type == QStringLiteral("login")) {
        return handleLogin(request);
    }
    if (type == QStringLiteral("list_users")) {
        return handleListUsers(request);
    }
    if (type == QStringLiteral("create_user")) {
        return handleCreateUser(request);
    }
    if (type == QStringLiteral("update_user")) {
        return handleUpdateUser(request);
    }
    if (type == QStringLiteral("delete_user")) {
        return handleDeleteUser(request);
    }
    if (type == QStringLiteral("change_password")) {
        return handleChangePassword(request);
    }
    if (type == QStringLiteral("reset_password")) {
        return handleResetPassword(request);
    }
    if (type == QStringLiteral("save_face_profile")) {
        return handleSaveFaceProfile(request);
    }
    if (type == QStringLiteral("get_my_face_profile")) {
        return handleGetMyFaceProfile(request);
    }
    if (type == QStringLiteral("import_questions")) {
        return handleImportQuestions(request);
    }
    if (type == QStringLiteral("create_question")) {
        return handleCreateQuestion(request);
    }
    if (type == QStringLiteral("update_question")) {
        return handleUpdateQuestion(request);
    }
    if (type == QStringLiteral("delete_question")) {
        return handleDeleteQuestion(request);
    }
    if (type == QStringLiteral("cleanup_duplicate_question")) {
        return handleCleanupDuplicateQuestion(request);
    }
    if (type == QStringLiteral("list_questions")) {
        return handleListQuestions(request);
    }
    if (type == QStringLiteral("create_exam")) {
        return handleCreateExam(request);
    }
    if (type == QStringLiteral("publish_exam")) {
        return handlePublishExam(request);
    }
    if (type == QStringLiteral("close_exam")) {
        return handleCloseExam(request);
    }
    if (type == QStringLiteral("delete_exam")) {
        return handleDeleteExam(request);
    }
    if (type == QStringLiteral("list_exams")) {
        return handleListExams(request);
    }
    if (type == QStringLiteral("get_exam_detail")) {
        return handleGetExamDetail(request);
    }
    if (type == QStringLiteral("save_draft")) {
        return handleSaveDraft(request);
    }
    if (type == QStringLiteral("submit_exam")) {
        return handleSubmitExam(request);
    }
    if (type == QStringLiteral("list_my_submissions")) {
        return handleListMySubmissions(request);
    }
    if (type == QStringLiteral("list_gradable_submissions") || type == QStringLiteral("list_submissions")) {
        return handleListSubmissions(request);
    }
    if (type == QStringLiteral("get_submission_detail")) {
        return handleGetSubmissionDetail(request);
    }
    if (type == QStringLiteral("lock_submission")) {
        return handleLockSubmission(request);
    }
    if (type == QStringLiteral("release_submission")) {
        return handleReleaseSubmission(request);
    }
    if (type == QStringLiteral("submit_grade")) {
        return handleSubmitGrade(request);
    }
    if (type == QStringLiteral("record_abnormal_event")) {
        return handleRecordAbnormalEvent(request);
    }
    if (type == QStringLiteral("list_risk_records")) {
        return handleListRiskRecords(request);
    }
    if (type == QStringLiteral("list_risk_logs")) {
        return handleListRiskLogs(request);
    }

    return fail(type.isEmpty() ? QStringLiteral("unknown") : type, QStringLiteral("未知请求类型。"));
}

QJsonObject ExamServer::handleLogin(const QJsonObject &request)
{
    ServerUser user;
    QString error;
    if (!database.authenticate(request.value(QStringLiteral("username")).toString(),
                               request.value(QStringLiteral("password")).toString(),
                               request.value(QStringLiteral("role")).toString(),
                               &user,
                               &error)) {
        return fail(QStringLiteral("login_response"), error);
    }

    QString token = QUuid::createUuid().toString();
    token.remove('{');
    token.remove('}');
    sessions.insert(token, user);

    QJsonObject response = ok(QStringLiteral("login_response"));
    response.insert(QStringLiteral("token"), token);
    response.insert(QStringLiteral("user"), userToJson(user));
    return response;
}

QJsonObject ExamServer::handleListUsers(const QJsonObject &request)
{
    ServerUser admin;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("admin")}, &admin, &response)) {
        return response;
    }

    QString error;
    const QJsonArray users = database.listUsers(request.value(QStringLiteral("roleFilter")).toString(), &error);
    if (!error.isEmpty()) {
        return fail(QStringLiteral("list_users_response"), error);
    }

    response = ok(QStringLiteral("list_users_response"));
    response.insert(QStringLiteral("users"), users);
    return response;
}

QJsonObject ExamServer::handleCreateUser(const QJsonObject &request)
{
    ServerUser admin;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("admin")}, &admin, &response)) {
        return response;
    }

    QString error;
    if (!database.createUser(request, &error)) {
        return fail(QStringLiteral("create_user_response"), error);
    }
    response = ok(QStringLiteral("create_user_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("用户已创建。"));
    return response;
}

QJsonObject ExamServer::handleUpdateUser(const QJsonObject &request)
{
    ServerUser admin;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("admin")}, &admin, &response)) {
        return response;
    }

    QString error;
    if (!database.updateUser(request.value(QStringLiteral("userId")).toInt(), request, &error)) {
        return fail(QStringLiteral("update_user_response"), error);
    }
    response = ok(QStringLiteral("update_user_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("用户资料已保存。"));
    return response;
}

QJsonObject ExamServer::handleDeleteUser(const QJsonObject &request)
{
    ServerUser admin;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("admin")}, &admin, &response)) {
        return response;
    }

    QString error;
    if (!database.deleteUser(request.value(QStringLiteral("userId")).toInt(), admin.id, &error)) {
        return fail(QStringLiteral("delete_user_response"), error);
    }
    response = ok(QStringLiteral("delete_user_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("账号已停用，历史考试与答卷记录已保留。"));
    return response;
}

QJsonObject ExamServer::handleChangePassword(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request,
                     {QStringLiteral("admin"), QStringLiteral("teacher"), QStringLiteral("student")},
                     &user,
                     &response)) {
        return response;
    }

    QString error;
    const QString targetUsername = request.value(QStringLiteral("targetUsername")).toString().trimmed();
    const QString targetRole = request.value(QStringLiteral("targetRole")).toString().trimmed();
    if (!targetUsername.isEmpty() || !targetRole.isEmpty()) {
        if (user.role != QStringLiteral("admin")) {
            return fail(QStringLiteral("change_password_response"), QStringLiteral("只有管理员可以修改其他账号密码。"));
        }
        if (!database.changePassword(targetUsername,
                                     targetRole,
                                     request.value(QStringLiteral("newPassword")).toString(),
                                     &error)) {
            return fail(QStringLiteral("change_password_response"), error);
        }
    } else {
        if (!database.changeOwnPassword(user.id,
                                        request.value(QStringLiteral("oldPassword")).toString(),
                                        request.value(QStringLiteral("newPassword")).toString(),
                                        &error)) {
            return fail(QStringLiteral("change_password_response"), error);
        }
    }

    response = ok(QStringLiteral("change_password_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("密码修改成功。"));
    return response;
}

QJsonObject ExamServer::handleResetPassword(const QJsonObject &request)
{
    ServerUser admin;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("admin")}, &admin, &response)) {
        return response;
    }

    const int resolvedUserId = request.value(QStringLiteral("userId")).toInt();
    qDebug() << "[SERVER_RESET_PASSWORD_REQUEST]"
             << "hasUserId=" << request.contains(QStringLiteral("userId"))
             << "hasId=" << request.contains(QStringLiteral("id"))
             << "userId=" << resolvedUserId
             << "passwordLen=" << request.value(QStringLiteral("newPassword")).toString().length();

    QString error;
    if (!database.resetPassword(resolvedUserId,
                                request.value(QStringLiteral("newPassword")).toString(),
                                &error)) {
        return fail(QStringLiteral("reset_password_response"), error);
    }
    response = ok(QStringLiteral("reset_password_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("密码已重置。"));
    return response;
}

QJsonObject ExamServer::handleSaveFaceProfile(const QJsonObject &request)
{
    ServerUser student;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("student")}, &student, &response)) {
        return response;
    }

    QString error;
    if (!database.saveFaceProfile(student.id, request.value(QStringLiteral("faceTemplate")).toString(), &error)) {
        return fail(QStringLiteral("save_face_profile_response"), error);
    }

    response = ok(QStringLiteral("save_face_profile_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("人脸信息已保存到服务器。"));
    return response;
}

QJsonObject ExamServer::handleGetMyFaceProfile(const QJsonObject &request)
{
    ServerUser student;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("student")}, &student, &response)) {
        return response;
    }

    QString error;
    const QJsonObject profile = database.faceProfile(student.id, &error);
    if (!error.isEmpty()) {
        return fail(QStringLiteral("get_my_face_profile_response"), error);
    }

    response = ok(QStringLiteral("get_my_face_profile_response"));
    for (auto it = profile.begin(); it != profile.end(); ++it) {
        response.insert(it.key(), it.value());
    }
    return response;
}

QJsonObject ExamServer::handleImportQuestions(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    const QJsonArray questions = request.value(QStringLiteral("questions")).toArray();
    QJsonArray questionIds;
    if (!database.insertQuestions(questions, user.id, &error, &questionIds)) {
        return fail(QStringLiteral("import_questions_response"), error);
    }

    response = ok(QStringLiteral("import_questions_response"));
    response.insert(QStringLiteral("imported"), questions.size());
    response.insert(QStringLiteral("questionIds"), questionIds);
    return response;
}

QJsonObject ExamServer::handleCreateQuestion(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QJsonArray questions;
    questions.append(request.value(QStringLiteral("question")).toObject());
    QString error;
    QJsonArray questionIds;
    if (!database.insertQuestions(questions, user.id, &error, &questionIds)) {
        return fail(QStringLiteral("create_question_response"), error);
    }
    response = ok(QStringLiteral("create_question_response"));
    if (!questionIds.isEmpty()) {
        response.insert(QStringLiteral("questionId"), questionIds.first().toInt());
    }
    response.insert(QStringLiteral("message"), QStringLiteral("题目已新增。"));
    return response;
}

QJsonObject ExamServer::handleUpdateQuestion(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    if (!database.updateQuestion(request.value(QStringLiteral("questionId")).toInt(),
                                 request.value(QStringLiteral("question")).toObject(),
                                 &error)) {
        return fail(QStringLiteral("update_question_response"), error);
    }
    response = ok(QStringLiteral("update_question_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("题目已保存。"));
    return response;
}

QJsonObject ExamServer::handleDeleteQuestion(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    if (!database.deleteQuestion(request.value(QStringLiteral("questionId")).toInt(), &error)) {
        return fail(QStringLiteral("delete_question_response"), error);
    }
    response = ok(QStringLiteral("delete_question_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("题目已删除。"));
    return response;
}

QJsonObject ExamServer::handleCleanupDuplicateQuestion(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString action;
    QString error;
    if (!database.cleanupDuplicateQuestion(request.value(QStringLiteral("questionId")).toInt(), &action, &error)) {
        return fail(QStringLiteral("cleanup_duplicate_question_response"), error);
    }

    response = ok(QStringLiteral("cleanup_duplicate_question_response"));
    response.insert(QStringLiteral("action"), action);
    if (action == QStringLiteral("disabled")) {
        response.insert(QStringLiteral("message"), QStringLiteral("重复题已停用。"));
    } else if (action == QStringLiteral("skipped")) {
        response.insert(QStringLiteral("message"), QStringLiteral("重复题已跳过。"));
    } else {
        response.insert(QStringLiteral("message"), QStringLiteral("重复题已删除。"));
    }
    return response;
}

QJsonObject ExamServer::handleListQuestions(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    const bool paged = request.contains(QStringLiteral("page")) || request.contains(QStringLiteral("pageSize"));
    if (paged) {
        const int page = qMax(1, request.value(QStringLiteral("page")).toInt(1));
        const int pageSize = qBound(1, request.value(QStringLiteral("pageSize")).toInt(20), 100);
        int total = 0;
        const QJsonArray questions = database.listQuestionsPaged(page, pageSize, &total, &error);
        if (!error.isEmpty()) {
            return fail(QStringLiteral("list_questions_response"), error);
        }

        const int totalPages = pageSize > 0 ? (total + pageSize - 1) / pageSize : 0;
        response = ok(QStringLiteral("list_questions_response"));
        response.insert(QStringLiteral("questions"), questions);
        response.insert(QStringLiteral("page"), page);
        response.insert(QStringLiteral("pageSize"), pageSize);
        response.insert(QStringLiteral("total"), total);
        response.insert(QStringLiteral("totalPages"), totalPages);
        return response;
    }

    const QJsonArray questions = database.listQuestions(&error);
    if (!error.isEmpty()) {
        return fail(QStringLiteral("list_questions_response"), error);
    }

    response = ok(QStringLiteral("list_questions_response"));
    response.insert(QStringLiteral("questions"), questions);
    return response;
}

QJsonObject ExamServer::handleCreateExam(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    const QJsonObject exam = request.value(QStringLiteral("exam")).isObject()
                                 ? request.value(QStringLiteral("exam")).toObject()
                                 : request;
    QString error;
    const int examId = database.createExam(exam, request.value(QStringLiteral("questionIds")).toArray(), user.id, &error);
    if (examId <= 0) {
        return fail(QStringLiteral("create_exam_response"), error);
    }

    response = ok(QStringLiteral("create_exam_response"));
    response.insert(QStringLiteral("examId"), examId);
    response.insert(QStringLiteral("message"), QStringLiteral("试卷已创建，请在考试列表中发布。"));
    return response;
}

QJsonObject ExamServer::handlePublishExam(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    if (!database.publishExam(request.value(QStringLiteral("examId")).toInt(), user.id, &error)) {
        return fail(QStringLiteral("publish_exam_response"), error);
    }
    response = ok(QStringLiteral("publish_exam_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("试卷发布成功"));
    return response;
}

QJsonObject ExamServer::handleCloseExam(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    if (!database.closeExam(request.value(QStringLiteral("examId")).toInt(), user.id, &error)) {
        return fail(QStringLiteral("close_exam_response"), error);
    }
    response = ok(QStringLiteral("close_exam_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("考试已关闭。"));
    return response;
}

QJsonObject ExamServer::handleDeleteExam(const QJsonObject &request)
{
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, nullptr, &response)) {
        return response;
    }

    QString error;
    if (!database.deleteExam(request.value(QStringLiteral("examId")).toInt(), &error)) {
        return fail(QStringLiteral("delete_exam_response"), error);
    }
    response = ok(QStringLiteral("delete_exam_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("考试已删除。"));
    return response;
}

QJsonObject ExamServer::handleListExams(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("student"), QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    const QJsonArray exams = database.listExams(user, &error);
    if (!error.isEmpty()) {
        return fail(QStringLiteral("list_exams_response"), error);
    }
    response = ok(QStringLiteral("list_exams_response"));
    response.insert(QStringLiteral("exams"), exams);
    return response;
}

QJsonObject ExamServer::handleGetExamDetail(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("student"), QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    const QJsonObject detail = database.examDetail(request.value(QStringLiteral("examId")).toInt(), &error);
    if (!error.isEmpty()) {
        return fail(QStringLiteral("get_exam_detail_response"), error);
    }
    response = ok(QStringLiteral("get_exam_detail_response"));
    response.insert(QStringLiteral("exam"), detail.value(QStringLiteral("exam")).toObject());
    response.insert(QStringLiteral("questions"), detail.value(QStringLiteral("questions")).toArray());
    return response;
}

QJsonObject ExamServer::handleSaveDraft(const QJsonObject &request)
{
    ServerUser student;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("student")}, &student, &response)) {
        return response;
    }

    QString error;
    const int submissionId = database.saveStudentAnswers(request.value(QStringLiteral("examId")).toInt(),
                                                         student.id,
                                                         request.value(QStringLiteral("answers")).toArray(),
                                                         false,
                                                         &error);
    if (submissionId <= 0) {
        return fail(QStringLiteral("save_draft_response"), error);
    }
    response = ok(QStringLiteral("save_draft_response"));
    response.insert(QStringLiteral("submissionId"), submissionId);
    response.insert(QStringLiteral("message"), QStringLiteral("草稿已保存。"));
    return response;
}

QJsonObject ExamServer::handleSubmitExam(const QJsonObject &request)
{
    ServerUser student;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("student")}, &student, &response)) {
        return response;
    }

    QString error;
    QJsonObject gradingSummary;
    const int submissionId = database.saveStudentAnswers(request.value(QStringLiteral("examId")).toInt(),
                                                         student.id,
                                                         request.value(QStringLiteral("answers")).toArray(),
                                                         true,
                                                         &error,
                                                         &gradingSummary);
    if (submissionId <= 0) {
        return fail(QStringLiteral("submit_exam_response"), error);
    }
    response = ok(QStringLiteral("submit_exam_response"));
    response.insert(QStringLiteral("submissionId"), submissionId);
    response.insert(QStringLiteral("autoScore"), gradingSummary.value(QStringLiteral("autoScore")).toDouble());
    response.insert(QStringLiteral("paperScore"), gradingSummary.value(QStringLiteral("paperScore")).toDouble());
    response.insert(QStringLiteral("objectiveCount"), gradingSummary.value(QStringLiteral("objectiveCount")).toInt());
    response.insert(QStringLiteral("manualCount"), gradingSummary.value(QStringLiteral("manualCount")).toInt());
    response.insert(QStringLiteral("correctCount"), gradingSummary.value(QStringLiteral("correctCount")).toInt());
    response.insert(QStringLiteral("wrongCount"), gradingSummary.value(QStringLiteral("wrongCount")).toInt());
    response.insert(QStringLiteral("blankCount"), gradingSummary.value(QStringLiteral("blankCount")).toInt());
    response.insert(QStringLiteral("status"), gradingSummary.value(QStringLiteral("status")).toString());
    response.insert(QStringLiteral("message"),
                    gradingSummary.value(QStringLiteral("manualCount")).toInt() > 0
                        ? QStringLiteral("试卷已提交，客观题已自动判分，主观题进入教师批阅。")
                        : QStringLiteral("试卷已提交，客观题已自动判分完成。"));
    return response;
}

QJsonObject ExamServer::handleListMySubmissions(const QJsonObject &request)
{
    ServerUser student;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("student")}, &student, &response)) {
        return response;
    }

    QString error;
    const QJsonArray submissions = database.listMySubmissions(student.id, &error);
    if (!error.isEmpty()) {
        return fail(QStringLiteral("list_my_submissions_response"), error);
    }
    response = ok(QStringLiteral("list_my_submissions_response"));
    response.insert(QStringLiteral("submissions"), submissions);
    return response;
}

QJsonObject ExamServer::handleListSubmissions(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    const QJsonArray submissions = database.listSubmissions(request.value(QStringLiteral("statusFilter")).toString(), &error);
    if (!error.isEmpty()) {
        return fail(QStringLiteral("list_gradable_submissions_response"), error);
    }

    response = ok(QStringLiteral("list_gradable_submissions_response"));
    response.insert(QStringLiteral("submissions"), submissions);
    return response;
}

QJsonObject ExamServer::handleGetSubmissionDetail(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("student"), QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    const QJsonObject detail = database.submissionDetail(request.value(QStringLiteral("submissionId")).toInt(), user, &error);
    if (!error.isEmpty()) {
        return fail(QStringLiteral("get_submission_detail_response"), error);
    }
    response = ok(QStringLiteral("get_submission_detail_response"));
    response.insert(QStringLiteral("submission"), detail.value(QStringLiteral("submission")).toObject());
    response.insert(QStringLiteral("answers"), detail.value(QStringLiteral("answers")).toArray());
    return response;
}

QJsonObject ExamServer::handleLockSubmission(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    if (!database.lockSubmission(request.value(QStringLiteral("submissionId")).toInt(), user.id, &error)) {
        return fail(QStringLiteral("lock_submission_response"), error);
    }

    response = ok(QStringLiteral("lock_submission_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("答卷已锁定。"));
    return response;
}

QJsonObject ExamServer::handleReleaseSubmission(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    const bool force = user.role == QStringLiteral("admin");
    if (!database.releaseSubmission(request.value(QStringLiteral("submissionId")).toInt(), user.id, force, &error)) {
        return fail(QStringLiteral("release_submission_response"), error);
    }

    response = ok(QStringLiteral("release_submission_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("答卷锁已释放。"));
    return response;
}

QJsonObject ExamServer::handleSubmitGrade(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    const double totalScore = request.contains(QStringLiteral("totalScore"))
                                  ? request.value(QStringLiteral("totalScore")).toDouble()
                                  : -1;
    if (!database.submitGrade(request.value(QStringLiteral("submissionId")).toInt(),
                              user.id,
                              request.value(QStringLiteral("answerGrades")).toArray(),
                              totalScore,
                              request.value(QStringLiteral("comment")).toString(),
                              &error)) {
        return fail(QStringLiteral("submit_grade_response"), error);
    }

    response = ok(QStringLiteral("submit_grade_response"));
    response.insert(QStringLiteral("message"), QStringLiteral("评分已提交。"));
    return response;
}

QJsonObject ExamServer::handleRecordAbnormalEvent(const QJsonObject &request)
{
    ServerUser student;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("student")}, &student, &response)) {
        return response;
    }

    QString error;
    QJsonObject result = riskScoreManager.addAbnormalEvent(request.value(QStringLiteral("examId")).toInt(),
                                                          student.id,
                                                          RiskScoreManager::abnormalTypeFromString(request.value(QStringLiteral("abnormalType")).toString()),
                                                          request.value(QStringLiteral("description")).toString(),
                                                          &error);
    if (!error.isEmpty()) {
        return fail(QStringLiteral("record_abnormal_event_response"), error);
    }

    response = ok(QStringLiteral("record_abnormal_event_response"));
    for (auto it = result.begin(); it != result.end(); ++it) {
        response.insert(it.key(), it.value());
    }
    return response;
}

QJsonObject ExamServer::handleListRiskRecords(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    const QJsonArray records = database.listRiskRecords(&error);
    if (!error.isEmpty()) {
        return fail(QStringLiteral("list_risk_records_response"), error);
    }
    response = ok(QStringLiteral("list_risk_records_response"));
    response.insert(QStringLiteral("records"), records);
    return response;
}

QJsonObject ExamServer::handleListRiskLogs(const QJsonObject &request)
{
    ServerUser user;
    QJsonObject response;
    if (!requireRole(request, {QStringLiteral("teacher"), QStringLiteral("admin")}, &user, &response)) {
        return response;
    }

    QString error;
    const QJsonArray logs = database.listRiskLogs(request.value(QStringLiteral("examId")).toInt(),
                                                 request.value(QStringLiteral("studentId")).toInt(),
                                                 &error);
    if (!error.isEmpty()) {
        return fail(QStringLiteral("list_risk_logs_response"), error);
    }
    response = ok(QStringLiteral("list_risk_logs_response"));
    response.insert(QStringLiteral("logs"), logs);
    return response;
}

bool ExamServer::requireRole(const QJsonObject &request,
                             const QStringList &roles,
                             ServerUser *user,
                             QJsonObject *response) const
{
    const QString token = request.value(QStringLiteral("token")).toString();
    if (token.isEmpty() || !sessions.contains(token)) {
        if (response) {
            *response = fail(request.value(QStringLiteral("type")).toString() + QStringLiteral("_response"),
                             QStringLiteral("未登录或登录已失效。"));
        }
        return false;
    }

    const ServerUser sessionUser = sessions.value(token);
    if (!roles.contains(sessionUser.role)) {
        if (response) {
            *response = fail(request.value(QStringLiteral("type")).toString() + QStringLiteral("_response"),
                             QStringLiteral("当前角色没有权限执行该操作。"));
        }
        return false;
    }

    if (user) {
        *user = sessionUser;
    }
    return true;
}

void ExamServer::sendResponse(QTcpSocket *socket, const QJsonObject &response)
{
    const QByteArray payload = QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n';
    socket->write(payload);
    socket->flush();
}

QJsonObject ExamServer::ok(const QString &type)
{
    QJsonObject response;
    response.insert(QStringLiteral("type"), type);
    response.insert(QStringLiteral("success"), true);
    return response;
}

QJsonObject ExamServer::fail(const QString &type, const QString &message)
{
    QJsonObject response;
    response.insert(QStringLiteral("type"), type);
    response.insert(QStringLiteral("success"), false);
    response.insert(QStringLiteral("error"), message);
    return response;
}

QJsonObject ExamServer::userToJson(const ServerUser &user)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), user.id);
    object.insert(QStringLiteral("username"), user.username);
    object.insert(QStringLiteral("displayName"), user.displayName);
    object.insert(QStringLiteral("role"), user.role);
    object.insert(QStringLiteral("note"), user.note);
    return object;
}
