#include "mainwindow.h"

#include "aiproctormonitor.h"
#include "apiclient.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QEvent>
#include <QEventLoop>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPainter>
#include <QStyleOptionTab>
#include <QPainterPath>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace {

// 信号连接一律使用 Qt6 类型安全的成员函数指针写法（&Class::signal），
// 不再使用任何返回信号的 helper，从根上避免 connect 失败 / "signal not found"。

QLabel *mutedLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("mutedLabel"));
    label->setWordWrap(true);
    return label;
}

QFrame *card(QWidget *content = nullptr)
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("card"));
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);
    if (content) {
        layout->addWidget(content);
    }
    return frame;
}

QFrame *metricCard(const QString &title, const QString &value, const QString &accent)
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("metricCard"));
    frame->setProperty("accent", accent);
    auto *layout = new QHBoxLayout(frame);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);

    auto *mark = new QLabel;
    mark->setObjectName(QStringLiteral("metricMark"));
    mark->setFixedSize(42, 42);
    mark->setProperty("accent", accent);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(4);
    auto *valueLabel = new QLabel(value);
    valueLabel->setObjectName(QStringLiteral("metricValue"));
    auto *titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("metricTitle"));
    texts->addWidget(valueLabel);
    texts->addWidget(titleLabel);
    layout->addWidget(mark);
    layout->addLayout(texts, 1);
    return frame;
}

QLabel *metricValueLabel(QFrame *frame)
{
    return frame ? frame->findChild<QLabel *>(QStringLiteral("metricValue")) : nullptr;
}

QPushButton *navButton(const QString &text)
{
    auto *button = new QPushButton(text);
    button->setCheckable(true);
    button->setMinimumHeight(46);
    button->setObjectName(QStringLiteral("navButton"));
    return button;
}

QLabel *sectionTitle(const QString &title, const QString &subtitle = {})
{
    auto *container = new QLabel(subtitle.isEmpty()
                                     ? QStringLiteral("<h3>%1</h3>").arg(title)
                                     : QStringLiteral("<h3>%1</h3><p>%2</p>").arg(title, subtitle));
    container->setObjectName(QStringLiteral("sectionTitle"));
    container->setTextFormat(Qt::RichText);
    container->setWordWrap(true);
    return container;
}

QString statusText(const QString &status)
{
    if (status == QStringLiteral("draft")) {
        return QStringLiteral("未发布");
    }
    if (status == QStringLiteral("unpublished")) {
        return QStringLiteral("未发布");
    }
    if (status == QStringLiteral("pending")) {
        return QStringLiteral("待批阅");
    }
    if (status == QStringLiteral("grading")) {
        return QStringLiteral("正在批阅");
    }
    if (status == QStringLiteral("graded")) {
        return QStringLiteral("已批阅");
    }
    if (status == QStringLiteral("running")) {
        return QStringLiteral("可答题");
    }
    if (status == QStringLiteral("not_started")) {
        return QStringLiteral("未开始");
    }
    if (status == QStringLiteral("expired")) {
        return QStringLiteral("已截止");
    }
    if (status == QStringLiteral("published")) {
        return QStringLiteral("已发布");
    }
    if (status == QStringLiteral("closed")) {
        return QStringLiteral("已关闭");
    }
    return status.isEmpty() ? QStringLiteral("-") : status;
}

QString publishStatusText(const QString &publishStatus)
{
    return publishStatus == QStringLiteral("published") ? QStringLiteral("已发布") : QStringLiteral("未发布");
}

QString abnormalTypeText(const QString &type)
{
    if (type == QStringLiteral("NO_FACE")) {
        return QStringLiteral("无人脸");
    }
    if (type == QStringLiteral("MULTI_FACE")) {
        return QStringLiteral("多人脸");
    }
    if (type == QStringLiteral("FACE_MISMATCH")) {
        return QStringLiteral("人脸不匹配");
    }
    if (type == QStringLiteral("SCREEN_SWITCH") || type == QStringLiteral("FOCUS_LOST")) {
        return QStringLiteral("窗口失焦/切屏");
    }
    if (type == QStringLiteral("TIME_ABNORMAL")) {
        return QStringLiteral("时间异常");
    }
    if (type == QStringLiteral("FULLSCREEN_EXIT")) {
        return QStringLiteral("退出全屏");
    }
    if (type == QStringLiteral("CAMERA_MISSING")) {
        return QStringLiteral("摄像头设备缺失");
    }
    if (type == QStringLiteral("CAMERA_ERROR")) {
        return QStringLiteral("摄像头启动失败");
    }
    if (type == QStringLiteral("SIMULATION_MODE")) {
        return QStringLiteral("模拟监考模式");
    }
    if (type == QStringLiteral("SIMULATION_VERIFY")) {
        return QStringLiteral("模拟核验通过");
    }
    if (type == QStringLiteral("NETWORK_ERROR")) {
        return QStringLiteral("网络异常");
    }
    return type.isEmpty() ? QStringLiteral("-") : type;
}

// Stage 3 监考看板：把异常类型归类，供看板用颜色区分"作弊风险 / 演示留痕 / 设备网络"。
// 返回 0=作弊风险(红) 1=演示留痕(灰) 2=设备/网络(蓝)。
int abnormalCategory(const QString &type)
{
    if (type == QStringLiteral("SIMULATION_MODE") || type == QStringLiteral("SIMULATION_VERIFY")) {
        return 1; // 演示留痕
    }
    if (type == QStringLiteral("CAMERA_MISSING") || type == QStringLiteral("CAMERA_ERROR")
        || type == QStringLiteral("NETWORK_ERROR")) {
        return 2; // 设备/网络环境
    }
    return 0; // 作弊风险类（无人脸/多人脸/不匹配/切屏/全屏/时间）
}

QString abnormalCategoryName(const QString &type)
{
    switch (abnormalCategory(type)) {
    case 1:
        return QStringLiteral("演示留痕");
    case 2:
        return QStringLiteral("设备/网络");
    default:
        return QStringLiteral("作弊风险");
    }
}

QColor abnormalCategoryColor(const QString &type)
{
    switch (abnormalCategory(type)) {
    case 1:
        return QColor(QStringLiteral("#6b7280")); // 灰：演示留痕
    case 2:
        return QColor(QStringLiteral("#2563eb")); // 蓝：设备/网络
    default:
        return QColor(QStringLiteral("#b91c1c")); // 红：作弊风险
    }
}

QColor statusColor(const QString &status)
{
    if (status == QStringLiteral("pending") || status == QStringLiteral("running")) {
        return QColor(QStringLiteral("#0ea5e9"));
    }
    if (status == QStringLiteral("grading")) {
        return QColor(QStringLiteral("#f59e0b"));
    }
    if (status == QStringLiteral("graded")) {
        return QColor(QStringLiteral("#10b981"));
    }
    if (status == QStringLiteral("expired") || status == QStringLiteral("closed")) {
        return QColor(QStringLiteral("#ef4444"));
    }
    return QColor(QStringLiteral("#64748b"));
}

QString studentExamBlockReason(const QJsonObject &exam)
{
    const QString state = exam.value(QStringLiteral("state")).toString();
    const int maxSubmissions = exam.value(QStringLiteral("maxSubmissions")).toInt(1);
    const int mySubmissionCount = exam.value(QStringLiteral("mySubmissionCount")).toInt();

    if (maxSubmissions > 0 && mySubmissionCount >= maxSubmissions) {
        return maxSubmissions <= 1 ? QStringLiteral("已提交") : QStringLiteral("次数已用完");
    }
    if (state == QStringLiteral("expired") || state == QStringLiteral("closed")) {
        return QStringLiteral("已截止");
    }
    if (state == QStringLiteral("not_started")) {
        return QStringLiteral("未开始");
    }
    if (state != QStringLiteral("running")) {
        return statusText(state);
    }
    return {};
}

QString studentExamBlockMessage(const QString &reason)
{
    if (reason == QStringLiteral("已提交")) {
        return QStringLiteral("该考试已提交，不能重复进入答题。");
    }
    if (reason == QStringLiteral("次数已用完")) {
        return QStringLiteral("该考试提交次数已用完，不能再次进入答题。");
    }
    if (reason == QStringLiteral("已截止")) {
        return QStringLiteral("该考试已截止，不能进入答题。");
    }
    if (reason == QStringLiteral("未开始")) {
        return QStringLiteral("该考试尚未开始，不能进入答题。");
    }
    return QStringLiteral("该考试当前状态为“%1”，不能进入答题。").arg(reason);
}

void repolish(QWidget *widget)
{
    if (!widget) {
        return;
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void polishTable(QTableWidget *table)
{
    if (!table) {
        return;
    }
    table->setAlternatingRowColors(true);
    table->setWordWrap(true);
    table->setShowGrid(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setTextElideMode(Qt::ElideRight);
    table->setCornerButtonEnabled(false);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->verticalHeader()->setDefaultSectionSize(48);
}

QString projectDataDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("data")),
        QDir(appDir).filePath(QStringLiteral("../data")),
        QDir(appDir).filePath(QStringLiteral("../../data"))
    };
#ifdef Q_OS_WIN
#endif

    for (const QString &candidate : candidates) {
        if (QDir(candidate).exists()) {
            return QDir(candidate).absolutePath();
        }
    }
    return QDir(appDir).filePath(QStringLiteral("data"));
}

QString valueString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 12);
    }
    return value.toString();
}

struct DuplicateQuestionEntry
{
    int id = 0;
    QuestionRecord question;
    QString key;
};

struct DuplicateQuestionGroup
{
    DuplicateQuestionEntry keep;
    QList<DuplicateQuestionEntry> duplicates;
};

QString normalizeDuplicatePart(QString text)
{
    text.replace(QChar(0x3000), QLatin1Char(' '));
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QChar('\n'));
    text = text.trimmed().toLower();
    text.replace(QStringLiteral("，"), QStringLiteral(","));
    text.replace(QStringLiteral("。"), QStringLiteral("."));
    text.replace(QStringLiteral("．"), QStringLiteral("."));
    text.replace(QStringLiteral("、"), QStringLiteral(","));
    text.replace(QStringLiteral("："), QStringLiteral(":"));
    text.replace(QStringLiteral("；"), QStringLiteral(";"));
    text.replace(QStringLiteral("？"), QStringLiteral("?"));
    text.replace(QStringLiteral("！"), QStringLiteral("!"));
    text.replace(QStringLiteral("（"), QStringLiteral("("));
    text.replace(QStringLiteral("）"), QStringLiteral(")"));
    text.replace(QStringLiteral("【"), QStringLiteral("["));
    text.replace(QStringLiteral("】"), QStringLiteral("]"));
    text.replace(QStringLiteral("“"), QStringLiteral("\""));
    text.replace(QStringLiteral("”"), QStringLiteral("\""));
    text.replace(QStringLiteral("‘"), QStringLiteral("'"));
    text.replace(QStringLiteral("’"), QStringLiteral("'"));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

QString questionDuplicateKey(const QuestionRecord &question)
{
    return QStringList{
        normalizeDuplicatePart(question.course),
        normalizeDuplicatePart(question.type),
        normalizeDuplicatePart(question.content),
        normalizeDuplicatePart(question.optionA),
        normalizeDuplicatePart(question.optionB),
        normalizeDuplicatePart(question.optionC),
        normalizeDuplicatePart(question.optionD)
    }.join(QChar(0x1f));
}

QString duplicatePreviewText(QString text, int limit = 60)
{
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    text = text.trimmed();
    if (text.size() <= limit) {
        return text;
    }
    return text.left(limit) + QStringLiteral("...");
}

QString duplicateDeleteIdsText(const QList<DuplicateQuestionEntry> &entries)
{
    QStringList ids;
    for (const DuplicateQuestionEntry &entry : entries) {
        ids.append(QString::number(entry.id));
    }
    return ids.join(QLatin1Char(','));
}

QString readableQuestionContent(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QChar('\n'));
    text.replace(QRegularExpression(QStringLiteral("([^\\n\\r\\s])\\s*([A-Da-d])\\s*[\\.．、]\\s*")),
                 QStringLiteral("\\1\n\\2. "));
    text.replace(QRegularExpression(QStringLiteral("(^|\\n)\\s*([A-Da-d])\\s*[\\.．、]\\s*")),
                 QStringLiteral("\\1\\2. "));
    text.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return text.trimmed();
}

ImportResult importQuestionsWithDocxMode(QWidget *parent, const QString &fileName)
{
    if (QFileInfo(fileName).suffix().toLower() != QStringLiteral("docx")) {
        return ExcelImporter::importFile(fileName);
    }

    // 该模态框出现在 [HOST_PICKER_SUCCESS] 之后；若用户没注意到它，表现就是“界面卡住”。
    // 因此弹出/选择/取消全部打日志，排查时一眼可见流程停在等待用户选择模式。
    qInfo("[IMPORT_DOCX_MODE_DIALOG_SHOW] file=%s", qPrintable(fileName));
    QMessageBox modeBox(parent);
    modeBox.setIcon(QMessageBox::Question);
    modeBox.setWindowTitle(QStringLiteral("Word 导入方式"));
    modeBox.setText(QStringLiteral("这个 Word 文档要按整篇作为一道题上传，还是按题号拆成多道题？"));
    modeBox.setInformativeText(QStringLiteral("如果题干里有 1、2、3 这样的步骤、小问或材料编号，请选“整篇作为一道题”。"));
    auto *singleButton = modeBox.addButton(QStringLiteral("整篇作为一道题"), QMessageBox::AcceptRole);
    auto *splitButton = modeBox.addButton(QStringLiteral("按题号拆成多题"), QMessageBox::ActionRole);
    modeBox.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    modeBox.setDefaultButton(singleButton);
    modeBox.exec();

    if (modeBox.clickedButton() == singleButton) {
        qInfo("[IMPORT_DOCX_MODE_SELECTED] mode=single");
        return ExcelImporter::importDocxAsSingleQuestion(fileName);
    }
    if (modeBox.clickedButton() == splitButton) {
        qInfo("[IMPORT_DOCX_MODE_SELECTED] mode=split");
        return ExcelImporter::importFile(fileName);
    }

    qInfo("[IMPORT_DOCX_MODE_CANCELLED]");
    ImportResult cancelled;
    cancelled.error = QStringLiteral("用户取消导入模式选择");
    return cancelled;
}

// 同步 HTTP GET（用于“从 Windows 获取题库”代理）。成功返回 true 并写入 body；
// 失败/超时返回 false 并写入可读 error。用嵌套事件循环 + 超时，避免界面假死。
// 仅用于题库代理这一处，不影响摄像头/服务端协议。
// 宿主机桥接 HTTP GET（同步等待）。2026-06-10 加固（修“picker 选完文件 Qt 一直转圈”）：
// ① NoProxy——桥接地址是局域网直连，绝不能被系统/环境代理劫持（代理黑洞=无限等待的经典成因）；
// ② 禁 HTTP/2——QNAM 对明文 HTTP 可能发 h2c Upgrade，python http.server 不支持，部分 Qt 版本会挂住；
// ③ finished/errorOccurred/downloadProgress 全量 [HOST_HTTP_*] 日志——出问题能精确指认卡在哪一层；
// ④ 超时必 reply->abort()，调用方收到明确错误，不允许无限转圈。
bool simpleHttpGet(const QString &url, QByteArray *body, QString *error, int timeoutMs = 6000)
{
    qInfo("[HOST_HTTP_START] url=%s timeoutMs=%d", qPrintable(url), timeoutMs);
    QNetworkAccessManager manager;
    manager.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop, [](qint64 received, qint64 total) {
        qInfo("[HOST_HTTP_PROGRESS] received=%lld total=%lld",
              static_cast<long long>(received), static_cast<long long>(total));
    });
    QObject::connect(reply, &QNetworkReply::errorOccurred, &loop, [reply](QNetworkReply::NetworkError code) {
        qWarning("[HOST_HTTP_ERROR] code=%d message=%s", static_cast<int>(code), qPrintable(reply->errorString()));
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    timer.stop();

    bool ok = false;
    if (timedOut) {
        qWarning("[HOST_HTTP_TIMEOUT] ms=%d url=%s", timeoutMs, qPrintable(url));
        reply->abort();  // 必须中止，否则 reply 挂在后台
        if (error) {
            *error = QStringLiteral("连接超时（%1 秒）：%2").arg(timeoutMs / 1000).arg(url);
        }
    } else if (reply->error() != QNetworkReply::NoError) {
        // errorOccurred 已打过 [HOST_HTTP_ERROR]，这里只组装给调用方的文案
        if (error) {
            *error = QStringLiteral("%1（%2）").arg(reply->errorString(), url);
        }
    } else {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 0 && status != 200) {
            qWarning("[HOST_HTTP_ERROR] code=http-%d url=%s", status, qPrintable(url));
            if (error) {
                *error = QStringLiteral("服务器返回 HTTP %1：%2").arg(status).arg(url);
            }
        } else {
            if (body) {
                *body = reply->readAll();
                qInfo("[HOST_HTTP_FINISHED] bytes=%lld status=%d",
                      static_cast<long long>(body->size()), status);
            } else {
                qInfo("[HOST_HTTP_FINISHED] bytes=discarded status=%d", status);
            }
            ok = true;
        }
    }
    delete reply;  // reply 由本函数负责释放；manager 为局部对象，随后析构
    return ok;
}

// Host Picker 专用：阻塞式 QTcpSocket HTTP GET，完全不经 QNetworkAccessManager/事件循环。
// 背景（2026-06-10 实测）：麒麟上 QNAM+嵌套 QEventLoop+QTimer 在按钮 eventFilter 分发
// 上下文内既等不到 finished 也等不到 QTimer 超时（60s 后无任何日志）——事件派发本身不可靠；
// 而本项目 ApiClient 的阻塞 waitFor* 模式在麒麟长期稳定（登录与全部业务请求），故改用同模式。
// 可靠性设计：单次 waitFor ≤1000ms 分片轮询 + QElapsedTimer 总额度硬截止，任何阶段都不可能无限等待。
// 成功返回 body（仅 JSON 正文，不含响应头）；失败返回空并写 errorMessage/failReason
//（failReason: http-timeout / connect-failed / write-failed / invalid-response / incomplete-body / http-status）。
QByteArray simpleHttpGetViaSocket(const QUrl &url, int timeoutMs, QString *errorMessage,
                                  QString *failReason = nullptr)
{
    const auto fail = [&](const QString &reason, const QString &message) -> QByteArray {
        if (failReason) {
            *failReason = reason;
        }
        if (errorMessage) {
            *errorMessage = message;
        }
        return {};
    };

    const QString host = url.host();
    const int port = url.port(80);
    QString path = url.path().isEmpty() ? QStringLiteral("/") : url.path();
    if (url.hasQuery()) {
        path += QLatin1Char('?') + url.query();
    }
    qInfo("[HOST_SOCKET_START] host=%s port=%d path=%s timeoutMs=%d",
          qPrintable(host), port, qPrintable(path), timeoutMs);

    QElapsedTimer elapsed;
    elapsed.start();
    // 剩余额度内的单次等待时长：≤1000ms，额度耗尽返回 0。
    const auto sliceMs = [&]() -> int {
        const qint64 left = timeoutMs - elapsed.elapsed();
        return left > 0 ? static_cast<int>(qMin<qint64>(left, 1000)) : 0;
    };

    QTcpSocket socket;
    socket.connectToHost(host, static_cast<quint16>(port));
    while (socket.state() != QAbstractSocket::ConnectedState) {
        const int slice = sliceMs();
        if (slice <= 0) {
            qWarning("[HOST_SOCKET_TIMEOUT] ms=%d stage=connect", timeoutMs);
            socket.abort();
            return fail(QStringLiteral("http-timeout"),
                        QStringLiteral("连接超时（%1 秒）：%2:%3").arg(timeoutMs / 1000).arg(host).arg(port));
        }
        if (socket.waitForConnected(slice)) {
            break;
        }
        if (socket.error() != QAbstractSocket::SocketTimeoutError) {
            qWarning("[HOST_SOCKET_ERROR] message=%s", qPrintable(socket.errorString()));
            return fail(QStringLiteral("connect-failed"),
                        QStringLiteral("无法连接 %1:%2——%3").arg(host).arg(port).arg(socket.errorString()));
        }
    }
    qInfo("[HOST_SOCKET_CONNECTED]");

    const QByteArray request = "GET " + path.toUtf8() + " HTTP/1.1\r\n"
        "Host: " + host.toUtf8() + ":" + QByteArray::number(port) + "\r\n"
        "Connection: close\r\n"
        "Accept: application/json\r\n"
        "User-Agent: QtExamSystem\r\n"
        "\r\n";
    socket.write(request);
    while (socket.bytesToWrite() > 0) {
        const int slice = sliceMs();
        if (slice <= 0) {
            qWarning("[HOST_SOCKET_TIMEOUT] ms=%d stage=write", timeoutMs);
            socket.abort();
            return fail(QStringLiteral("http-timeout"),
                        QStringLiteral("发送请求超时（%1 秒）").arg(timeoutMs / 1000));
        }
        if (!socket.waitForBytesWritten(slice)
            && socket.error() != QAbstractSocket::SocketTimeoutError) {
            qWarning("[HOST_SOCKET_ERROR] message=%s", qPrintable(socket.errorString()));
            return fail(QStringLiteral("write-failed"),
                        QStringLiteral("发送请求失败：%1").arg(socket.errorString()));
        }
    }
    qInfo("[HOST_SOCKET_WRITE_DONE] bytes=%lld", static_cast<long long>(request.size()));

    // 读响应：先凑齐 \r\n\r\n 解析状态码与 Content-Length；
    // 有 Content-Length 读满即止，没有则读到对端关闭（服务端是 Connection: close）。
    QByteArray buffer;
    int headerEnd = -1;
    int statusCode = 0;
    qint64 contentLength = -1;
    int bodyStart = 0;
    bool disconnected = false;
    for (;;) {
        if (socket.bytesAvailable() > 0) {
            buffer += socket.readAll();
            qInfo("[HOST_SOCKET_READ] total=%lld", static_cast<long long>(buffer.size()));
        }
        if (headerEnd < 0) {
            headerEnd = buffer.indexOf("\r\n\r\n");
            if (headerEnd >= 0) {
                bodyStart = headerEnd + 4;
                const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
                if (!lines.isEmpty()) {
                    const QList<QByteArray> statusParts = lines.first().simplified().split(' ');
                    if (statusParts.size() >= 2) {
                        statusCode = statusParts.at(1).toInt();
                    }
                }
                for (const QByteArray &rawLine : lines) {
                    const QByteArray line = rawLine.trimmed();
                    if (line.toLower().startsWith("content-length:")) {
                        contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
                    }
                }
                qInfo("[HOST_SOCKET_HEADER_DONE] status=%d contentLength=%lld",
                      statusCode, static_cast<long long>(contentLength));
            }
        }
        if (headerEnd >= 0 && contentLength >= 0
            && static_cast<qint64>(buffer.size() - bodyStart) >= contentLength) {
            break;  // 按 Content-Length 收满
        }
        if (disconnected) {
            break;  // 对端已关闭且缓冲读尽：无 Content-Length 时以关闭为响应结束
        }
        if (socket.state() != QAbstractSocket::ConnectedState && socket.bytesAvailable() == 0) {
            qInfo("[HOST_SOCKET_DISCONNECTED]");
            disconnected = true;  // 再绕一圈：循环顶部已无可读字节，下一圈按上面的分支退出
            continue;
        }
        const int slice = sliceMs();
        if (slice <= 0) {
            qWarning("[HOST_SOCKET_TIMEOUT] ms=%d stage=read received=%lld",
                     timeoutMs, static_cast<long long>(buffer.size()));
            socket.abort();
            return fail(QStringLiteral("http-timeout"),
                        QStringLiteral("读取响应超时（%1 秒），已收到 %2 字节").arg(timeoutMs / 1000).arg(buffer.size()));
        }
        // waitForReadyRead 超时分片或对端关闭都可能返回 false：
        // 直接回到循环顶部，先读尽缓冲再依据连接状态/额度决定去留。
        socket.waitForReadyRead(slice);
    }

    if (headerEnd < 0) {
        qWarning("[HOST_SOCKET_ERROR] message=连接关闭但未收到完整响应头 received=%lld",
                 static_cast<long long>(buffer.size()));
        return fail(QStringLiteral("invalid-response"),
                    QStringLiteral("未收到完整 HTTP 响应头（已收 %1 字节）").arg(buffer.size()));
    }

    QByteArray bodyData = buffer.mid(bodyStart);
    if (contentLength >= 0) {
        if (bodyData.size() < contentLength) {
            qWarning("[HOST_SOCKET_ERROR] message=响应体不完整 expected=%lld received=%lld",
                     static_cast<long long>(contentLength), static_cast<long long>(bodyData.size()));
            return fail(QStringLiteral("incomplete-body"),
                        QStringLiteral("响应不完整：应有 %1 字节，仅收到 %2 字节").arg(contentLength).arg(bodyData.size()));
        }
        bodyData.truncate(static_cast<int>(contentLength));
    }
    if (statusCode != 200) {
        qWarning("[HOST_SOCKET_ERROR] message=HTTP状态码异常 status=%d", statusCode);
        return fail(QStringLiteral("http-status"),
                    QStringLiteral("服务器返回 HTTP %1").arg(statusCode));
    }

    qInfo("[HOST_SOCKET_BODY_DONE] bytes=%lld", static_cast<long long>(bodyData.size()));
    return bodyData;
}

// Host Picker 保存专用：阻塞式 QTcpSocket HTTP POST（JSON body），与 simpleHttpGetViaSocket 同款
// 分片轮询 + 总额度硬截止，绕开麒麟上不可靠的 QNAM。成功返回响应 JSON 正文，失败返回空并写
// errorMessage/failReason（http-timeout/connect-failed/write-failed/invalid-response/incomplete-body/http-status）。
// 仅用于成绩 CSV 导出的 /save-file；GET 路径（导入题库/试卷）保持原 simpleHttpGetViaSocket 不动。
QByteArray simpleHttpPostJsonViaSocket(const QUrl &url, const QByteArray &jsonBody, int timeoutMs,
                                       QString *errorMessage, QString *failReason = nullptr)
{
    const auto fail = [&](const QString &reason, const QString &message) -> QByteArray {
        if (failReason) {
            *failReason = reason;
        }
        if (errorMessage) {
            *errorMessage = message;
        }
        return {};
    };

    const QString host = url.host();
    const int port = url.port(80);
    QString path = url.path().isEmpty() ? QStringLiteral("/") : url.path();
    if (url.hasQuery()) {
        path += QLatin1Char('?') + url.query();
    }
    qInfo("[HOST_SOCKET_START] method=POST host=%s port=%d path=%s timeoutMs=%d bodyBytes=%lld",
          qPrintable(host), port, qPrintable(path), timeoutMs, static_cast<long long>(jsonBody.size()));

    QElapsedTimer elapsed;
    elapsed.start();
    const auto sliceMs = [&]() -> int {
        const qint64 left = timeoutMs - elapsed.elapsed();
        return left > 0 ? static_cast<int>(qMin<qint64>(left, 1000)) : 0;
    };

    QTcpSocket socket;
    socket.connectToHost(host, static_cast<quint16>(port));
    while (socket.state() != QAbstractSocket::ConnectedState) {
        const int slice = sliceMs();
        if (slice <= 0) {
            qWarning("[HOST_SOCKET_TIMEOUT] ms=%d stage=connect", timeoutMs);
            socket.abort();
            return fail(QStringLiteral("http-timeout"),
                        QStringLiteral("连接超时（%1 秒）：%2:%3").arg(timeoutMs / 1000).arg(host).arg(port));
        }
        if (socket.waitForConnected(slice)) {
            break;
        }
        if (socket.error() != QAbstractSocket::SocketTimeoutError) {
            qWarning("[HOST_SOCKET_ERROR] message=%s", qPrintable(socket.errorString()));
            return fail(QStringLiteral("connect-failed"),
                        QStringLiteral("无法连接 %1:%2——%3").arg(host).arg(port).arg(socket.errorString()));
        }
    }
    qInfo("[HOST_SOCKET_CONNECTED]");

    QByteArray request = "POST " + path.toUtf8() + " HTTP/1.1\r\n"
        "Host: " + host.toUtf8() + ":" + QByteArray::number(port) + "\r\n"
        "Connection: close\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + QByteArray::number(jsonBody.size()) + "\r\n"
        "Accept: application/json\r\n"
        "User-Agent: QtExamSystem\r\n"
        "\r\n";
    request += jsonBody;
    socket.write(request);
    while (socket.bytesToWrite() > 0) {
        const int slice = sliceMs();
        if (slice <= 0) {
            qWarning("[HOST_SOCKET_TIMEOUT] ms=%d stage=write", timeoutMs);
            socket.abort();
            return fail(QStringLiteral("http-timeout"),
                        QStringLiteral("发送请求超时（%1 秒）").arg(timeoutMs / 1000));
        }
        if (!socket.waitForBytesWritten(slice)
            && socket.error() != QAbstractSocket::SocketTimeoutError) {
            qWarning("[HOST_SOCKET_ERROR] message=%s", qPrintable(socket.errorString()));
            return fail(QStringLiteral("write-failed"),
                        QStringLiteral("发送请求失败：%1").arg(socket.errorString()));
        }
    }
    qInfo("[HOST_SOCKET_WRITE_DONE] bytes=%lld", static_cast<long long>(request.size()));

    QByteArray buffer;
    int headerEnd = -1;
    int statusCode = 0;
    qint64 contentLength = -1;
    int bodyStart = 0;
    bool disconnected = false;
    for (;;) {
        if (socket.bytesAvailable() > 0) {
            buffer += socket.readAll();
            qInfo("[HOST_SOCKET_READ] total=%lld", static_cast<long long>(buffer.size()));
        }
        if (headerEnd < 0) {
            headerEnd = buffer.indexOf("\r\n\r\n");
            if (headerEnd >= 0) {
                bodyStart = headerEnd + 4;
                const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
                if (!lines.isEmpty()) {
                    const QList<QByteArray> statusParts = lines.first().simplified().split(' ');
                    if (statusParts.size() >= 2) {
                        statusCode = statusParts.at(1).toInt();
                    }
                }
                for (const QByteArray &rawLine : lines) {
                    const QByteArray line = rawLine.trimmed();
                    if (line.toLower().startsWith("content-length:")) {
                        contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
                    }
                }
                qInfo("[HOST_SOCKET_HEADER_DONE] status=%d contentLength=%lld",
                      statusCode, static_cast<long long>(contentLength));
            }
        }
        if (headerEnd >= 0 && contentLength >= 0
            && static_cast<qint64>(buffer.size() - bodyStart) >= contentLength) {
            break;
        }
        if (disconnected) {
            break;
        }
        if (socket.state() != QAbstractSocket::ConnectedState && socket.bytesAvailable() == 0) {
            qInfo("[HOST_SOCKET_DISCONNECTED]");
            disconnected = true;
            continue;
        }
        const int slice = sliceMs();
        if (slice <= 0) {
            qWarning("[HOST_SOCKET_TIMEOUT] ms=%d stage=read received=%lld",
                     timeoutMs, static_cast<long long>(buffer.size()));
            socket.abort();
            return fail(QStringLiteral("http-timeout"),
                        QStringLiteral("读取响应超时（%1 秒），已收到 %2 字节").arg(timeoutMs / 1000).arg(buffer.size()));
        }
        socket.waitForReadyRead(slice);
    }

    if (headerEnd < 0) {
        return fail(QStringLiteral("invalid-response"),
                    QStringLiteral("未收到完整 HTTP 响应头（已收 %1 字节）").arg(buffer.size()));
    }
    QByteArray bodyData = buffer.mid(bodyStart);
    if (contentLength >= 0) {
        if (bodyData.size() < contentLength) {
            return fail(QStringLiteral("incomplete-body"),
                        QStringLiteral("响应不完整：应有 %1 字节，仅收到 %2 字节").arg(contentLength).arg(bodyData.size()));
        }
        bodyData.truncate(static_cast<int>(contentLength));
    }
    if (statusCode != 200) {
        return fail(QStringLiteral("http-status"),
                    QStringLiteral("服务器返回 HTTP %1").arg(statusCode));
    }
    qInfo("[HOST_SOCKET_BODY_DONE] bytes=%lld", static_cast<long long>(bodyData.size()));
    return bodyData;
}

bool isChoiceLikeQuestion(const QString &type)
{
    return type.contains(QStringLiteral("单选"))
        || type.contains(QStringLiteral("多选"))
        || type.contains(QStringLiteral("选择"))
        || type.contains(QStringLiteral("判断"));
}

bool isMultiChoiceQuestion(const QString &type)
{
    return type.contains(QStringLiteral("多选"));
}

QList<QPair<QString, QString>> answerOptionsForQuestion(const QJsonObject &question)
{
    QList<QPair<QString, QString>> options;
    const QString questionType = question.value(QStringLiteral("type")).toString();
    if (questionType.contains(QStringLiteral("判断"))) {
        QString optionA = question.value(QStringLiteral("optionA")).toString().trimmed();
        QString optionB = question.value(QStringLiteral("optionB")).toString().trimmed();
        if (optionA.isEmpty()) {
            optionA = QStringLiteral("正确");
        }
        if (optionB.isEmpty()) {
            optionB = QStringLiteral("错误");
        }
        options.append({QStringLiteral("A"), optionA});
        options.append({QStringLiteral("B"), optionB});
        return options;
    }

    for (const QString &key : {QStringLiteral("optionA"), QStringLiteral("optionB"), QStringLiteral("optionC"), QStringLiteral("optionD")}) {
        const QString option = question.value(key).toString().trimmed();
        if (!option.isEmpty()) {
            options.append({key.right(1), option});
        }
    }
    return options;
}

QString objectData(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject objectFromData(const QVariant &data)
{
    return QJsonDocument::fromJson(data.toString().toUtf8()).object();
}

QuestionRecord questionFromJson(const QJsonObject &object)
{
    QuestionRecord question;
    question.type = object.value(QStringLiteral("type")).toString();
    question.course = object.value(QStringLiteral("course")).toString();
    question.content = object.value(QStringLiteral("content")).toString();
    question.optionA = object.value(QStringLiteral("optionA")).toString();
    question.optionB = object.value(QStringLiteral("optionB")).toString();
    question.optionC = object.value(QStringLiteral("optionC")).toString();
    question.optionD = object.value(QStringLiteral("optionD")).toString();
    question.answer = object.value(QStringLiteral("answer")).toString();
    question.score = valueString(object, QStringLiteral("score"));
    question.difficulty = object.value(QStringLiteral("difficulty")).toString();
    question.analysis = object.value(QStringLiteral("analysis")).toString();
    return question;
}

QJsonObject questionToJson(const QuestionRecord &question)
{
    QJsonObject object;
    QString content = question.content;
    QString optionA = question.optionA;
    QString optionB = question.optionB;
    if (question.type.contains(QStringLiteral("判断"))) {
        content.remove(QRegularExpression(QStringLiteral("\\s*[（(]\\s*[）)]\\s*$")));
        if (optionA.trimmed().isEmpty()) {
            optionA = QStringLiteral("正确");
        }
        if (optionB.trimmed().isEmpty()) {
            optionB = QStringLiteral("错误");
        }
    }
    object.insert(QStringLiteral("type"), question.type);
    object.insert(QStringLiteral("course"), question.course);
    object.insert(QStringLiteral("content"), content.trimmed());
    object.insert(QStringLiteral("optionA"), optionA);
    object.insert(QStringLiteral("optionB"), optionB);
    object.insert(QStringLiteral("optionC"), question.optionC);
    object.insert(QStringLiteral("optionD"), question.optionD);
    object.insert(QStringLiteral("answer"), question.answer);
    object.insert(QStringLiteral("score"), question.score);
    object.insert(QStringLiteral("difficulty"), question.difficulty);
    object.insert(QStringLiteral("analysis"), question.analysis);
    return object;
}

bool editQuestionDialog(QWidget *parent, const QString &title, QuestionRecord *question)
{
    if (!question) {
        return false;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(760, 620);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(sectionTitle(title, QStringLiteral("填写题干、选项、答案和分值后保存。")));

    auto *form = new QFormLayout;
    auto *typeCombo = new QComboBox;
    typeCombo->setEditable(true);
    typeCombo->addItems({QStringLiteral("单选题"), QStringLiteral("多选题"), QStringLiteral("判断题"), QStringLiteral("填空题"), QStringLiteral("简答题"), QStringLiteral("综合应用题"), QStringLiteral("程序题")});
    typeCombo->setCurrentText(question->type.isEmpty() ? QStringLiteral("单选题") : question->type);
    auto *courseEdit = new QLineEdit(question->course);
    auto *contentEdit = new QTextEdit(question->content);
    contentEdit->setMinimumHeight(120);
    auto *optionAEdit = new QLineEdit(question->optionA);
    auto *optionBEdit = new QLineEdit(question->optionB);
    auto *optionCEdit = new QLineEdit(question->optionC);
    auto *optionDEdit = new QLineEdit(question->optionD);
    auto *answerEdit = new QLineEdit(question->answer);
    auto *scoreEdit = new QLineEdit(question->score.isEmpty() ? QStringLiteral("10") : question->score);
    auto *difficultyEdit = new QComboBox;
    difficultyEdit->setEditable(true);
    difficultyEdit->addItems({QStringLiteral("简单"), QStringLiteral("中等"), QStringLiteral("困难")});
    difficultyEdit->setCurrentText(question->difficulty.isEmpty() ? QStringLiteral("中等") : question->difficulty);
    auto *analysisEdit = new QTextEdit(question->analysis);
    analysisEdit->setMinimumHeight(80);

    form->addRow(QStringLiteral("题型"), typeCombo);
    form->addRow(QStringLiteral("课程"), courseEdit);
    form->addRow(QStringLiteral("题干"), contentEdit);
    form->addRow(QStringLiteral("选项A"), optionAEdit);
    form->addRow(QStringLiteral("选项B"), optionBEdit);
    form->addRow(QStringLiteral("选项C"), optionCEdit);
    form->addRow(QStringLiteral("选项D"), optionDEdit);
    form->addRow(QStringLiteral("参考答案"), answerEdit);
    form->addRow(QStringLiteral("分值"), scoreEdit);
    form->addRow(QStringLiteral("难度"), difficultyEdit);
    form->addRow(QStringLiteral("解析"), analysisEdit);
    layout->addLayout(form);

    auto *hint = mutedLabel(QStringLiteral("选择题答案填 A/B/C/D，多选题如 AB；判断题默认 A=正确，B=错误。"));
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const QString type = typeCombo->currentText().trimmed();
    const QString content = contentEdit->toPlainText().trimmed();
    const QString answer = answerEdit->text().trimmed();
    if (type.isEmpty() || content.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("题目信息不完整"), QStringLiteral("题型和题干不能为空。"));
        return false;
    }
    if (isChoiceLikeQuestion(type) && !type.contains(QStringLiteral("判断"))
        && optionAEdit->text().trimmed().isEmpty()
        && optionBEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("选项为空"), QStringLiteral("选择题至少需要填写选项 A 和 B。"));
        return false;
    }
    if (isChoiceLikeQuestion(type) && answer.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("答案为空"), QStringLiteral("客观题必须填写参考答案。"));
        return false;
    }
    bool scoreOk = false;
    const double score = scoreEdit->text().trimmed().toDouble(&scoreOk);
    if (!scoreOk || score <= 0) {
        QMessageBox::warning(parent, QStringLiteral("分值错误"), QStringLiteral("分值必须是大于 0 的数字。"));
        return false;
    }

    question->type = type;
    question->course = courseEdit->text().trimmed();
    question->content = content;
    question->optionA = optionAEdit->text().trimmed();
    question->optionB = optionBEdit->text().trimmed();
    question->optionC = optionCEdit->text().trimmed();
    question->optionD = optionDEdit->text().trimmed();
    question->answer = answer;
    question->score = QString::number(score, 'g', 12);
    question->difficulty = difficultyEdit->currentText().trimmed();
    question->analysis = analysisEdit->toPlainText().trimmed();
    return true;
}

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

void setupMySubmissionTable(QTableWidget *table)
{
    if (!table) {
        return;
    }
    table->setObjectName(QStringLiteral("mySubmissionTable"));
    table->setColumnCount(8);
    table->setHorizontalHeaderLabels({QStringLiteral("答卷ID"),
                                      QStringLiteral("考试"),
                                      QStringLiteral("课程"),
                                      QStringLiteral("状态"),
                                      QStringLiteral("总分"),
                                      QStringLiteral("风险分"),
                                      QStringLiteral("风险等级"),
                                      QStringLiteral("阅卷评语")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    polishTable(table);
}

void populateMySubmissionTable(QTableWidget *table, const QJsonArray &submissions)
{
    if (!table) {
        return;
    }
    table->setRowCount(0);
    for (const QJsonValue &value : submissions) {
        const QJsonObject submission = value.toObject();
        const int row = table->rowCount();
        table->insertRow(row);
        const QString status = submission.value(QStringLiteral("status")).toString();
        const QStringList values = {
            QString::number(submission.value(QStringLiteral("id")).toInt()),
            submission.value(QStringLiteral("examName")).toString(),
            submission.value(QStringLiteral("course")).toString(),
            statusText(status),
            valueString(submission, QStringLiteral("totalScore")),
            QString::number(submission.value(QStringLiteral("riskScore")).toInt()),
            submission.value(QStringLiteral("riskLevel")).toString(QStringLiteral("低风险")),
            submission.value(QStringLiteral("comment")).toString()
        };
        for (int col = 0; col < values.size(); ++col) {
            auto *item = readOnlyItem(values.at(col));
            if (col == 0) {
                item->setData(Qt::UserRole, submission.value(QStringLiteral("id")).toInt());
                item->setData(Qt::UserRole + 1, status);
            }
            table->setItem(row, col, item);
        }
    }
}

double scoreToOneDecimal(double value)
{
    return std::round(value * 10.0) / 10.0;
}

QString scoreText(double value)
{
    return QString::number(scoreToOneDecimal(value), 'f', 1);
}

bool scoreTextHasAtMostOneDecimal(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    int dot = trimmed.indexOf(QLatin1Char('.'));
    if (dot < 0) {
        dot = trimmed.indexOf(QChar(0xff0e));
    }
    if (dot < 0) {
        return true;
    }

    int decimals = 0;
    for (int i = dot + 1; i < trimmed.size(); ++i) {
        if (trimmed.at(i).isDigit()) {
            ++decimals;
        }
    }
    return decimals <= 1;
}

void updateGradeTotalScore(QDoubleSpinBox *scoreSpinBox, const QVector<QLineEdit *> &scoreEdits)
{
    if (!scoreSpinBox) {
        return;
    }

    double total = 0;
    for (auto *edit : scoreEdits) {
        bool ok = false;
        const double value = edit ? edit->text().trimmed().toDouble(&ok) : 0;
        if (ok) {
            total += value;
        }
    }
    const double normalizedTotal = scoreToOneDecimal(total);
    scoreSpinBox->setValue(normalizedTotal);
}

void clearLayout(QLayout *layout)
{
    if (!layout) {
        return;
    }

    while (QLayoutItem *item = layout->takeAt(0)) {
        if (item->layout()) {
            clearLayout(item->layout());
            delete item->layout();
        }
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

QString localTimeText(const QString &iso)
{
    const QDateTime dateTime = QDateTime::fromString(iso, Qt::ISODate);
    return dateTime.isValid() ? dateTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : iso;
}

class FaceProfileDialog : public QDialog
{
public:
    enum class Mode {
        Enroll,
        Verify
    };

    FaceProfileDialog(Mode mode, const QString &faceTemplate, QWidget *parent = nullptr)
        : QDialog(parent)
        , mode(mode)
    {
        setWindowTitle(mode == Mode::Enroll ? QStringLiteral("采集人脸信息") : QStringLiteral("考试身份核验"));
        resize(860, 620);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 18, 18, 18);
        layout->setSpacing(12);
        layout->addWidget(sectionTitle(mode == Mode::Enroll
                                           ? QStringLiteral("采集人脸信息")
                                           : QStringLiteral("考试前身份核验"),
                                       mode == Mode::Enroll
                                           ? QStringLiteral("请正对摄像头，检测到单人脸后点击保存。人脸模板会保存到服务器数据库。")
                                           : QStringLiteral("请正对摄像头，系统会与服务器数据库中的人脸模板比对，通过后才能进入考试。")));

        preview = new QLabel(QStringLiteral("正在打开摄像头..."));
        preview->setObjectName(QStringLiteral("facePreview"));
        preview->setAlignment(Qt::AlignCenter);
        preview->setMinimumSize(760, 430);
        preview->setScaledContents(false);
        layout->addWidget(preview, 1);

        status = new QLabel(QStringLiteral("摄像头启动中"));
        status->setObjectName(QStringLiteral("aiProctorStatusLabel"));
        layout->addWidget(status);

        auto *buttons = new QHBoxLayout;
        primaryButton = new QPushButton(mode == Mode::Enroll ? QStringLiteral("等待检测到人脸") : QStringLiteral("等待身份核验"));
        primaryButton->setObjectName(QStringLiteral("primaryButton"));
        primaryButton->setEnabled(false);
        if (mode == Mode::Verify) {
            reenrollButton = new QPushButton(QStringLiteral("重新采集"));
            reenrollButton->setToolTip(QStringLiteral("如果本人一直无法识别，可重新采集一张当前光线下的人脸模板。"));
        }
        simulateButton = new QPushButton(QStringLiteral("模拟核验通过（无摄像头演示模式）"));
        simulateButton->setObjectName(QStringLiteral("quietButton"));
        simulateButton->setVisible(false);
        simulateButton->setToolTip(QStringLiteral(
            "摄像头不可用时，跳过人脸采集/核验直接进入考试。\n"
            "此模式仅用于无摄像头环境下的答辩演示，不代表真实身份验证通过。"
        ));
        cancelButton = new QPushButton(QStringLiteral("取消"));
        buttons->addStretch();
        buttons->addWidget(primaryButton);
        if (reenrollButton) {
            buttons->addWidget(reenrollButton);
        }
        buttons->addWidget(simulateButton);
        buttons->addWidget(cancelButton);

        simulateWarning = new QLabel(QStringLiteral(
            "未检测到摄像头设备。虚拟机可能未连接 Win11 主机摄像头。"
            "可连接摄像头后重启程序重试，或点击下方「模拟核验通过」进行答辩演示（模拟模式不验证真实身份）。"
        ));
        simulateWarning->setObjectName(QStringLiteral("aiProctorStatusLabel"));
        simulateWarning->setProperty("state", QStringLiteral("warning"));
        simulateWarning->setWordWrap(true);
        simulateWarning->setVisible(false);
        layout->addWidget(simulateWarning);
        layout->addLayout(buttons);

        if (mode == Mode::Verify) {
            monitor.setBaselineTemplate(faceTemplate);
        }

        connect(&monitor, &AiProctorMonitor::previewReady, this, [this](const QImage &image) {
            const QSize labelSize = preview->size();
            const QPixmap pixmap = QPixmap::fromImage(image).scaled(labelSize, Qt::KeepAspectRatio, Qt::FastTransformation);
            previewSlotCount++;
            const bool logThis = previewSlotCount <= 3 || previewSlotCount % 30 == 0;
            if (logThis) {
                // 内容指纹：对角线采样 8 个像素。指纹随帧变 = 链路通且画面活；
                // 指纹恒定 = 帧在到达但上游(快照/代理)给的是同一张静止图。
                quint32 fingerprint = 0;
                const int step = qMax(1, qMin(image.width(), image.height()) / 8);
                for (int i = 0; i < 8; ++i) {
                    const int x = qMin(image.width() - 1, i * step);
                    const int y = qMin(image.height() - 1, i * step);
                    fingerprint = fingerprint * 31 + image.pixel(x, y);
                }
                qInfo().noquote() << QStringLiteral("[PreviewDebug] slot received image=%1x%2 label=%3x%4 frame#%5 content=0x%6")
                                         .arg(image.width()).arg(image.height())
                                         .arg(labelSize.width()).arg(labelSize.height())
                                         .arg(previewSlotCount)
                                         .arg(fingerprint, 0, 16);
            }
            preview->setPixmap(pixmap);
            preview->update();
            if (logThis) {
                qInfo().noquote() << QStringLiteral("[PreviewDebug] label updated frame#%1").arg(previewSlotCount);
            }
        });
        connect(&monitor, &AiProctorMonitor::statusChanged, this, [this](const QString &text, const QString &state) {
            status->setText(text);
            status->setProperty("state", state);
            repolish(status);
            if (state == QStringLiteral("error") && simulateButton && !simulateButton->isVisible()) {
                simulateWarning->setVisible(true);
                simulateButton->setVisible(true);
            }
        });
        connect(&monitor, &AiProctorMonitor::faceResult, this, [this](bool hasFace, int faceCount, bool matched, double confidence) {
            Q_UNUSED(confidence)
            if (this->mode == Mode::Enroll) {
                const bool canSave = hasFace && faceCount == 1 && monitor.hasCurrentFace();
                primaryButton->setEnabled(canSave);
                primaryButton->setText(canSave ? QStringLiteral("保存人脸信息") : QStringLiteral("等待检测到人脸"));
                return;
            }

            if (hasFace && faceCount == 1 && matched) {
                ++matchedFrames;
                if (matchedFrames >= 1) {
                    verified = true;
                    primaryButton->setEnabled(true);
                    primaryButton->setText(QStringLiteral("验证通过，进入考试"));
                } else {
                    primaryButton->setEnabled(false);
                    primaryButton->setText(QStringLiteral("正在确认本人身份"));
                }
            } else {
                matchedFrames = 0;
                verified = false;
                primaryButton->setEnabled(false);
                primaryButton->setText(QStringLiteral("等待身份核验"));
            }
        });
        // clicked 在麒麟上不可靠，保留作非麒麟平台/键盘激活的冗余通路；麒麟下由下方 eventFilter 兜底。
        // 4 段动作已提炼为同名 handler，clicked 与 eventFilter 共用，enroll/verify/simulation 语义保持不变。
        connect(primaryButton, &QAbstractButton::clicked, this, &FaceProfileDialog::handlePrimaryButton);
        if (reenrollButton) {
            connect(reenrollButton, &QAbstractButton::clicked, this, &FaceProfileDialog::handleReenrollButton);
        }
        connect(cancelButton, &QAbstractButton::clicked, this, &FaceProfileDialog::handleCancelButton);
        connect(simulateButton, &QAbstractButton::clicked, this, &FaceProfileDialog::handleSimulateButton);

        // 麒麟桌面 clicked 兜底：给本对话框按钮装事件过滤器，直接拦“左键在按钮内释放”触发动作（见 eventFilter）。
        const auto registerFaceButton = [this](QPushButton *button, const QString &name) {
            if (!button) {
                return;
            }
            button->installEventFilter(this);
            qInfo().noquote() << QStringLiteral("[FACE_BUTTON_REGISTER] button=%1 text=%2")
                                     .arg(name, button->text());
        };
        registerFaceButton(primaryButton, QStringLiteral("primary"));
        registerFaceButton(reenrollButton, QStringLiteral("reenroll"));
        registerFaceButton(cancelButton, QStringLiteral("cancel"));
        registerFaceButton(simulateButton, QStringLiteral("simulate"));

        monitor.start(QString());
    }

    ~FaceProfileDialog() override
    {
        monitor.stop();
    }

    QString templateData() const
    {
        return capturedTemplate;
    }

    bool wantsReEnroll() const
    {
        return reEnrollRequested;
    }

    bool simulationAccepted() const
    {
        return m_simulationAccepted;
    }

private:
    // 麒麟桌面 QPushButton::clicked 不稳定的兜底：拦“左键在按钮矩形内释放”直接触发动作并吃掉事件，
    // 与 LoginDialog::eventFilter / MainWindow 按钮动作注册表同思路。原 clicked 连接保留作冗余通路。
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event && event->type() == QEvent::MouseButtonRelease) {
            if (auto *button = qobject_cast<QPushButton *>(watched)) {
                // 只接管本对话框注册过的 4 颗按钮，未注册的交还默认处理。
                QString actionName;
                if (button == primaryButton) {
                    actionName = QStringLiteral("primary");
                } else if (button == reenrollButton) {
                    actionName = QStringLiteral("reenroll");
                } else if (button == cancelButton) {
                    actionName = QStringLiteral("cancel");
                } else if (button == simulateButton) {
                    actionName = QStringLiteral("simulate");
                }
                if (!actionName.isEmpty()) {
                    auto *mouseEvent = static_cast<QMouseEvent *>(event);
                    const QPoint localPos = mouseEvent->position().toPoint();
                    const bool leftReleaseInside = mouseEvent->button() == Qt::LeftButton
                                                   && button->isEnabled()
                                                   && button->isVisible()
                                                   && button->rect().contains(localPos);
                    if (leftReleaseInside) {
                        qInfo().noquote() << QStringLiteral("[FACE_BUTTON_ACTION] button=%1 text=%2")
                                                 .arg(actionName, button->text());
                        if (button == primaryButton) {
                            handlePrimaryButton();
                        } else if (button == reenrollButton) {
                            handleReenrollButton();
                        } else if (button == cancelButton) {
                            handleCancelButton();
                        } else {
                            handleSimulateButton();
                        }
                        return true;  // 吃掉本次 release，避免随后 clicked 再触发一次造成双重执行
                    }
                }
            }
        }
        return QDialog::eventFilter(watched, event);
    }

    // 以下 4 个 handler 是原 clicked lambda 的等价提炼，供 clicked 与 eventFilter 共用。
    void handlePrimaryButton()
    {
        if (mode == Mode::Enroll) {
            capturedTemplate = monitor.currentFaceTemplate();
            if (capturedTemplate.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("未检测到人脸"), QStringLiteral("请正对摄像头，检测到单人脸后再保存。"));
                return;
            }
            qInfo().noquote() << QStringLiteral("[FACE_PROFILE_CAPTURE_START] mode=enroll templateLen=%1").arg(capturedTemplate.size());
            accept();
            return;
        }
        if (!verified) {
            QMessageBox::warning(this, QStringLiteral("未通过验证"), QStringLiteral("请正对摄像头，等待身份核验通过。"));
            return;
        }
        qInfo().noquote() << QStringLiteral("[FACE_PROFILE_VERIFY_ACCEPT]");
        accept();
    }

    void handleReenrollButton()
    {
        qInfo().noquote() << QStringLiteral("[FACE_PROFILE_RECAPTURE_START]");
        reEnrollRequested = true;
        reject();
    }

    void handleCancelButton()
    {
        qInfo().noquote() << QStringLiteral("[FACE_PROFILE_CANCEL]");
        reject();
    }

    void handleSimulateButton()
    {
        qInfo().noquote() << QStringLiteral("[FACE_PROFILE_SIMULATION_ACCEPT]");
        m_simulationAccepted = true;
        capturedTemplate = QStringLiteral("SIMULATION_MODE");
        accept();
    }

    Mode mode = Mode::Enroll;
    QLabel *preview = nullptr;
    QLabel *status = nullptr;
    QLabel *simulateWarning = nullptr;
    QPushButton *primaryButton = nullptr;
    QPushButton *reenrollButton = nullptr;
    QPushButton *cancelButton = nullptr;
    QPushButton *simulateButton = nullptr;
    AiProctorMonitor monitor;
    QString capturedTemplate;
    int matchedFrames = 0;
    bool reEnrollRequested = false;
    bool verified = false;
    bool m_simulationAccepted = false;
    int previewSlotCount = 0;
};

} // namespace

MainWindow::~MainWindow()
{
    if (qApp) {
        qApp->removeEventFilter(this);
    }
    // registerButtonAction 给每个按钮连了 button->destroyed → 清理 m_buttonActions/
    // m_activeButtonActions 的 lambda。析构顺序为：本析构体 → 成员(含这两个容器)析构 →
    // 基类 ~QWidget 删除子按钮并触发其 destroyed（此时连接尚未被 ~QObject 断开）。
    // 若不先断开，子按钮的 destroyed 会回调到已析构的成员容器，造成堆损坏崩溃——
    // 这正是"退出登录关窗后进程直接消失、回不到登录页"的真实根因(gdb 实证 QHash::removeImpl)。
    // 故在成员析构前主动断开：子按钮再 destroyed 也不会回调到已死成员。
    for (auto it = m_buttonActions.cbegin(); it != m_buttonActions.cend(); ++it) {
        if (it.key()) {
            disconnect(it.key(), &QObject::destroyed, this, nullptr);
        }
    }
    m_buttonActions.clear();
    m_activeButtonActions.clear();
    stopAiProctor();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 程序退出（含退出登录）前尽量保存本地草稿，避免未提交答案丢失。
    if (currentUser.role == UserRole::Student && currentExamId > 0 && !currentExamQuestions.isEmpty()) {
        saveLocalDraft();
    }
    QMainWindow::closeEvent(event);
}

MainWindow::MainWindow(const UserAccount &account, QWidget *parent)
    : QMainWindow(parent)
    , currentUser(account)
{
    setWindowTitle(QStringLiteral("新星考试管理系统"));
    setWindowIcon(QIcon(QStringLiteral(":/branding/app_icon.png")));
    resize(1320, 780);
    setMinimumSize(1060, 680);
    if (qApp) {
        qApp->installEventFilter(this);
    }

    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("appRoot"));
    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // 右侧唯一内容容器 contentHost：单容器动态替换页面，彻底不用栈控件做主导航，
    // 从根上绕开 index / currentWidget / stack 层级问题。先建 contentHost，再建页面存指针。
    contentHost = new QWidget(this);
    contentHost->setObjectName(QStringLiteral("contentHost"));
    contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    pageDashboard = createDashboardPage();
    pageExam = createExamPage();
    pageGrading = createGradingPage();
    pageQuestion = createQuestionPage();
    pagePublish = createPublishPage();
    pageStats = createStatsPage();
    pageUsers = createUserManagementPage();
    // 窗口角色固定（每次登录新建窗口），按角色给可达页面命名，便于切页日志核对。
    const QString roleTag = currentUser.role == UserRole::Student ? QStringLiteral("student")
                          : currentUser.role == UserRole::Teacher ? QStringLiteral("teacher")
                                                                   : QStringLiteral("admin");
    pageDashboard->setObjectName(roleTag + QStringLiteral("_dashboard"));
    pageExam->setObjectName(QStringLiteral("student_exam_center"));
    pageGrading->setObjectName(QStringLiteral("teacher_review"));
    pageQuestion->setObjectName(roleTag + QStringLiteral("_questions"));
    pagePublish->setObjectName(roleTag + QStringLiteral("_exams"));
    pageStats->setObjectName(currentUser.role == UserRole::Student ? QStringLiteral("student_exam_records")
                                                                   : roleTag + QStringLiteral("_scores"));
    pageUsers->setObjectName(currentUser.role == UserRole::Admin
                                 ? QStringLiteral("admin_accounts")
                                 : QStringLiteral("admin_users"));
    // 管理员端新增页面：学生管理 / 教师管理（按角色的用户管理）、考试监控、系统日志。
    // 账号权限复用 pageUsers，成绩总览复用 pageStats。仅 admin 角色创建，省资源。
    if (currentUser.role == UserRole::Admin) {
        pageAdminStudents = createUserAdminPage(roleToCode(UserRole::Student), QStringLiteral("admin_students"),
            QStringLiteral("学生管理"), QStringLiteral("学生考试档案与参与情况概览。"), &studentMgmtTable);
        pageAdminStudents->setObjectName(QStringLiteral("admin_students"));
        pageAdminTeachers = createUserAdminPage(roleToCode(UserRole::Teacher), QStringLiteral("admin_teachers"),
            QStringLiteral("教师管理"), QStringLiteral("教师教学工作台与工作量概览。"), &teacherMgmtTable);
        pageAdminTeachers->setObjectName(QStringLiteral("admin_teachers"));
        pageAdminExamMonitor = createAdminExamMonitorPage();
        pageAdminExamMonitor->setObjectName(QStringLiteral("admin_exam_monitor"));
        pageAdminLogs = createAdminLogsPage();
        pageAdminLogs->setObjectName(QStringLiteral("admin_logs"));
    }
    // 暂时挂到 contentHost 下并隐藏，避免成为游离顶层窗口；点击导航时才加入 contentLayout 显示。
    for (QWidget *page : {pageDashboard, pageExam, pageGrading, pageQuestion, pagePublish, pageStats, pageUsers,
                          pageAdminStudents, pageAdminTeachers, pageAdminExamMonitor, pageAdminLogs}) {
        if (page) {
            page->setParent(contentHost);
            page->hide();
        }
    }
    if (currentUser.role == UserRole::Teacher || currentUser.role == UserRole::Admin) {
        discoverTeacherButtons(pageQuestion, QStringLiteral("teacher_questions"));
        discoverTeacherButtons(pagePublish, QStringLiteral("teacher_exams"));
        discoverTeacherButtons(pageStats, QStringLiteral("teacher_scores"));
    }

    rootLayout->addWidget(createSidebar());

    auto *mainArea = new QWidget;
    mainArea->setObjectName(QStringLiteral("mainArea"));
    auto *mainLayout = new QVBoxLayout(mainArea);
    mainLayout->setContentsMargins(28, 20, 28, 24);  // 答辩版：内容区外留白更舒展（极简留白）
    mainLayout->setSpacing(16);

    auto *topbar = new QHBoxLayout;
    topbar->setSpacing(10);
    auto *titleBox = new QVBoxLayout;
    auto *eyebrow = new QLabel(QStringLiteral("在线考试管理平台 · 已登录"));
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    titleLabel = new QLabel(QStringLiteral("工作台"));
    titleLabel->setObjectName(QStringLiteral("pageTitle"));
    titleBox->addWidget(eyebrow);
    titleBox->addWidget(titleLabel);

    auto *currentRole = new QLabel(QStringLiteral("%1端 · %2").arg(roleToText(currentUser.role), currentUser.displayName));
    currentRole->setObjectName(QStringLiteral("currentRoleBadge"));
    auto *passwordButton = new QPushButton(QStringLiteral("修改密码"));
    passwordButton->setObjectName(QStringLiteral("quietButton"));
    auto *logoutButton = new QPushButton(QStringLiteral("退出登录/切换账号"));
    logoutButton->setObjectName(QStringLiteral("dangerOutlineButton"));
    logoutButton->setToolTip(QStringLiteral("退出当前账号并返回登录界面，可切换其它账号（不会关闭整个程序）。"));
    connect(passwordButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=topbar button=change_password");
        changeOwnPassword();
    });
    // 退出登录：返回登录界面（切换账号），而不是直接关程序；先停监考再关窗口。
    const auto runLogout = [this] {
        if (currentUser.role == UserRole::Admin) {
            qDebug() << "[LOGOUT_BUTTON_CLICKED]" << "role=admin";
        }
        qInfo("[UI_CLICK] page=topbar button=logout");
        requestLogout();
    };
    const bool logoutClickedOk = static_cast<bool>(
        connect(logoutButton, &QAbstractButton::clicked, this, runLogout));
    // 麒麟桌面 clicked 不稳定，所有角色都接入按钮动作注册表；管理员退出按钮本轮补齐。
    if (currentUser.role == UserRole::Student || currentUser.role == UserRole::Teacher
        || currentUser.role == UserRole::Admin) {
        registerButtonAction(logoutButton, QStringLiteral("topbar"), QStringLiteral("logout"),
                             runLogout);
    }
    if (currentUser.role == UserRole::Admin) {
        qDebug() << "[LOGOUT_BUTTON_CONNECT]" << "role=admin" << "ok=" << logoutClickedOk;
    }

    topbar->addLayout(titleBox);
    topbar->addStretch();
    topbar->addWidget(currentRole);
    topbar->addWidget(passwordButton);
    topbar->addWidget(logoutButton);
    mainLayout->addLayout(topbar);

    // 右侧主区域只放唯一的 contentHost（不再放第二个内容容器）。
    mainLayout->addWidget(contentHost, 1);

    rootLayout->addWidget(mainArea, 1);
    setCentralWidget(root);
    createMenus();
    setStyle();
    examTimer = new QTimer(this);
    examTimer->setInterval(1000);
    // 倒计时的 timeout 连接移到 startExamCountdown()：那里先尝试新式 connect 并校验返回值，
    // 失败/静默（麒麟 PMF “signal not found”）则退化为 singleShot 自调度链。详见 [EXAM_TIMER_*] 日志。
    connect(qApp, &QGuiApplication::applicationStateChanged, this, &MainWindow::handleApplicationStateChanged);
    aiProctorMonitor = new AiProctorMonitor(this);
    connect(aiProctorMonitor, &AiProctorMonitor::statusChanged, this, &MainWindow::setAiProctorStatus);
    // 学生考试界面摄像头预览：真实帧与模拟帧都走 previewReady，直接贴到预览 QLabel。
    connect(aiProctorMonitor, &AiProctorMonitor::previewReady, this, [this](const QImage &image) {
        if (!studentProctorPreview) {
            return;
        }
        studentProctorPreview->setPixmap(QPixmap::fromImage(image).scaled(
            studentProctorPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (!m_previewLogged) {
            qInfo("[CAMERA_PREVIEW] first frame received width=%d height=%d", image.width(), image.height());
            m_previewLogged = true;
        }
    });
    connect(aiProctorMonitor, &AiProctorMonitor::faceResult, this, [this](bool hasFace, int faceCount, bool faceMatched, double confidence) {
        Q_UNUSED(confidence)
        onAiFaceResult(hasFace, faceCount, faceMatched);
    });
    connect(aiProctorMonitor, &AiProctorMonitor::runningChanged, this, [this](bool running) {
        const bool canStart = !running && currentExamId > 0 && !currentExamQuestions.isEmpty();
        if (startAiProctorButton) {
            startAiProctorButton->setEnabled(canStart);
        }
        if (simulateProctorButton) {
            simulateProctorButton->setEnabled(canStart);
        }
        if (stopAiProctorButton) {
            stopAiProctorButton->setEnabled(running);
        }
    });
    qInfo("[LOGIN] user=%s role=%s display=%s",
          qPrintable(currentUser.username),
          qPrintable(roleToText(currentUser.role)),
          qPrintable(currentUser.displayName));
    statusBar()->showMessage(QStringLiteral("%1端已登录：%2").arg(roleToText(currentUser.role), currentUser.username));
    refreshInitialData();

    // 默认显示工作台（把 pageDashboard 放进 contentHost 并显示）。第一个按钮已在 createSidebar 高亮。
    switchContentPage(pageDashboard, QStringLiteral("工作台"));

    // 启动时打印导航按钮列表，便于核对按钮↔页面映射与 navIndex。
    qDebug() << "[NAV-BUTTON-LIST]" << "count=" << navButtons.size() << "pages=" << navPages.size();
    for (int i = 0; i < navButtons.size(); ++i) {
        QPushButton *button = navButtons.value(i);
        QWidget *page = navPages.value(i);
        qDebug() << "[NAV-BUTTON]"
                 << "index=" << i
                 << "button=" << button
                 << "text=" << (button ? button->text() : QStringLiteral("<null>"))
                 << "objectName=" << (button ? button->objectName() : QStringLiteral("<null>"))
                 << "navLabel=" << (button ? button->property("navLabel").toString() : QStringLiteral("<null>"))
                 << "page=" << page
                 << "pageName=" << (page ? page->objectName() : QStringLiteral("<null>"));
    }
}

QWidget *MainWindow::createSidebar()
{
    auto *sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(208);  // 紧凑后台风：左侧固定导航栏 ~200

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(14, 18, 14, 16);
    layout->setSpacing(10);

    auto *brandRow = new QHBoxLayout;
    brandRow->setSpacing(10);
    auto *brandLogo = new QLabel;
    brandLogo->setObjectName(QStringLiteral("brandLogo"));
    brandLogo->setFixedSize(48, 48);
    brandLogo->setAlignment(Qt::AlignCenter);
    const QPixmap brandPixmap(QStringLiteral(":/branding/app_icon.png"));
    if (!brandPixmap.isNull()) {
        brandLogo->setPixmap(brandPixmap.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    auto *brand = new QLabel(QStringLiteral("<b>新星考试管理系统</b><br><span>Nova Exam System</span>"));
    brand->setObjectName(QStringLiteral("brand"));
    brand->setTextFormat(Qt::RichText);
    brandRow->addWidget(brandLogo);
    brandRow->addWidget(brand, 1);
    layout->addLayout(brandRow);

    auto *account = new QLabel(QStringLiteral("%1<br><span>%2端 · %3</span>")
                                   .arg(currentUser.displayName, roleToText(currentUser.role), currentUser.username));
    account->setObjectName(QStringLiteral("accountBox"));
    account->setTextFormat(Qt::RichText);
    layout->addWidget(account);

    auto *buttonGroup = new QButtonGroup(this);
    buttonGroup->setExclusive(true);

    // 导航按钮直接绑定页面指针（QWidget*）。点击时用 contentHost 动态替换页面，不经任何 stack。
    navButtons.clear();
    navPages.clear();
    auto addNavButton = [this, buttonGroup, layout](const QString &label, QWidget *page) {
        // 保持 objectName="navButton"（navButton() 内部设置）以保留 QSS 高亮样式 #navButton:checked。
        // 每个按钮的标签放进 navLabel 属性，日志里用 label 区分。
        auto *button = navButton(label);
        buttonGroup->addButton(button);
        layout->addWidget(button);

        // navIndex 必须与 navButtons 即将插入的下标一致（先取 size 再 append）。
        const int navIndex = navButtons.size();
        button->setProperty("navLabel", label);
        button->setProperty("navIndex", navIndex);
        navButtons.append(button);
        navPages.append(page);

        connect(button, &QAbstractButton::clicked, this, [this, button, page, label]() {
            qDebug() << "[NAV-CLICK]"
                     << "label=" << label
                     << "pageName=" << (page ? page->objectName() : QStringLiteral("<null>"))
                     << "currentName=" << (currentContentPage ? currentContentPage->objectName() : QStringLiteral("<null>"));
            if (!page) {
                qWarning() << "[NAV-ERROR] clicked null page label=" << label;
                updateNavHighlight(button);
                return;
            }
            updateNavHighlight(button);
            if (currentContentPage == page) {
                qDebug() << "[NAV-CLICK-SKIP]"
                         << "reason=same-page"
                         << "label=" << label
                         << "pageName=" << page->objectName();
                return;
            }
            switchContentPage(page, label);
        });
    };

    if (currentUser.role == UserRole::Student) {
        addNavButton(QStringLiteral("工作台"), pageDashboard);
        addNavButton(QStringLiteral("考试中心"), pageExam);
        addNavButton(QStringLiteral("考试记录"), pageStats);
    } else if (currentUser.role == UserRole::Teacher) {
        addNavButton(QStringLiteral("工作台"), pageDashboard);
        addNavButton(QStringLiteral("阅卷中心"), pageGrading);
        addNavButton(QStringLiteral("题目管理"), pageQuestion);
        addNavButton(QStringLiteral("试卷管理"), pagePublish);
        addNavButton(QStringLiteral("成绩管理"), pageStats);
    } else {
        // 管理员端：管人 / 权限 / 全局状态 / 日志；教学内容管理保留在教师端。
        addNavButton(QStringLiteral("工作台"), pageDashboard);
        addNavButton(QStringLiteral("学生管理"), pageAdminStudents);
        addNavButton(QStringLiteral("教师管理"), pageAdminTeachers);
        addNavButton(QStringLiteral("账号权限"), pageUsers);
        addNavButton(QStringLiteral("成绩总览"), pageStats);
        addNavButton(QStringLiteral("考试监控"), pageAdminExamMonitor);
        addNavButton(QStringLiteral("系统日志"), pageAdminLogs);
    }
    // 默认高亮第一个按钮（默认页在构造函数末尾通过 switchContentPage 显示）。
    if (!navButtons.isEmpty()) {
        navButtons.first()->setChecked(true);
        currentNavButton = navButtons.first();
    }

    layout->addStretch();
    return sidebar;
}

QWidget *MainWindow::createDashboardPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);

    auto *hero = new QFrame;
    hero->setObjectName(QStringLiteral("dashboardHero"));
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(24, 20, 24, 20);
    heroLayout->setSpacing(16);
    auto *heroText = sectionTitle(QStringLiteral("系统工作台"), QStringLiteral("集中查看考试、题库、答卷和批阅进度，按左侧模块进入对应业务。"));
    auto *refresh = new QPushButton(QStringLiteral("刷新数据"));
    refresh->setObjectName(QStringLiteral("primaryButton"));
    heroLayout->addWidget(heroText, 1);
    heroLayout->addWidget(refresh);
    layout->addWidget(hero);

    dashboardMetricLabels.clear();
    auto *metricRow = new QHBoxLayout;
    metricRow->setSpacing(18);
    const QList<QPair<QString, QString>> metrics = currentUser.role == UserRole::Student
        ? QList<QPair<QString, QString>>{
              {QStringLiteral("可参加考试"), QStringLiteral("blue")},
              {QStringLiteral("我的答卷"), QStringLiteral("cyan")},
              {QStringLiteral("待批阅"), QStringLiteral("amber")},
              {QStringLiteral("已出成绩"), QStringLiteral("green")}
          }
        : QList<QPair<QString, QString>>{
              {QStringLiteral("考试总数"), QStringLiteral("blue")},
              {QStringLiteral("题目总数"), QStringLiteral("cyan")},
              {QStringLiteral("待批阅答卷"), QStringLiteral("amber")},
              {QStringLiteral("已批阅答卷"), QStringLiteral("green")}
          };
    for (const auto &metric : metrics) {
        auto *item = metricCard(metric.first, QStringLiteral("--"), metric.second);
        dashboardMetricLabels.append(metricValueLabel(item));
        metricRow->addWidget(item, 1);
    }
    layout->addLayout(metricRow);

    auto *infoCard = card();
    infoCard->setObjectName(QStringLiteral("activityCard"));
    infoCard->layout()->addWidget(sectionTitle(QStringLiteral("当前概览")));
    auto *dashboardStatsTable = new QTableWidget(0, 3);
    dashboardStatsTable->setObjectName(QStringLiteral("dashboardStatsTable"));
    dashboardStatsTable->setHorizontalHeaderLabels({
        QStringLiteral("项目"),
        QStringLiteral("数值"),
        QStringLiteral("备注")
    });
    dashboardStatsTable->verticalHeader()->setVisible(false);
    dashboardStatsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    dashboardStatsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    dashboardStatsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    dashboardStatsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    dashboardStatsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    dashboardStatsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    dashboardStatsTable->setFixedHeight(190);
    polishTable(dashboardStatsTable);
    infoCard->layout()->addWidget(dashboardStatsTable);
    dashboardLabel = mutedLabel(QStringLiteral("正在读取系统数据..."));
    infoCard->layout()->addWidget(dashboardLabel);
    layout->addWidget(infoCard);

    layout->addStretch();

    connect(refresh, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=dashboard button=refresh_stats");
        loadStats();
    });
    // 阶段2A：仅学生端把“刷新数据”接入按钮动作注册表（教师/管理员保持原生 clicked，不动）。
    if (currentUser.role == UserRole::Student) {
        registerButtonAction(refresh, QStringLiteral("dashboard"), QStringLiteral("refresh_stats"),
                             [this] { loadStats(); });
    }
    return page;
}

QWidget *MainWindow::createExamPage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    // ============ 左列：考试选择（窄）============
    auto *listCard = card();
    listCard->setObjectName(QStringLiteral("sidePanelCard"));
    listCard->setFixedWidth(260);
    listCard->layout()->addWidget(sectionTitle(QStringLiteral("考试中心"), QStringLiteral("选择考试后开始答题")));
    auto *refreshButton = new QPushButton(QStringLiteral("刷新列表"));
    refreshButton->setObjectName(QStringLiteral("primaryButton"));
    listCard->layout()->addWidget(refreshButton);
    studentExamList = new QListWidget;
    studentExamList->setObjectName(QStringLiteral("examCardList"));
    qobject_cast<QVBoxLayout *>(listCard->layout())->addWidget(studentExamList, 1);

    // ============ 中列：答题主区（题目 + 作答 + 按钮，stretch）============
    auto *workCard = card();
    workCard->setObjectName(QStringLiteral("paperCard"));
    auto *workLayout = qobject_cast<QVBoxLayout *>(workCard->layout());
    studentExamTitleLabel = sectionTitle(QStringLiteral("请选择一场考试"));
    studentExamStateLabel = mutedLabel(QStringLiteral("左侧选择考试，在题目下方作答，完成后点“提交试卷”。"));
    workLayout->addWidget(studentExamTitleLabel);
    workLayout->addWidget(studentExamStateLabel);
    // 风险状态紧凑一行（不再和监考按钮/预览挤在一起）。
    auto *riskRow = new QHBoxLayout;
    riskRow->setSpacing(12);
    studentRiskStatusLabel = new QLabel(QStringLiteral("当前考试状态：正常"));
    studentRiskStatusLabel->setObjectName(QStringLiteral("riskStatusLabel"));
    studentRiskLevelLabel = new QLabel(QStringLiteral("风险等级：低风险"));
    studentRiskLevelLabel->setObjectName(QStringLiteral("riskLevelLabel"));
    riskRow->addWidget(studentRiskStatusLabel, 1);
    riskRow->addWidget(studentRiskLevelLabel);
    workLayout->addLayout(riskRow);

    // 题目滚动区：占据中列主要空间。
    studentQuestionScroll = new QScrollArea;
    studentQuestionScroll->setWidgetResizable(true);
    studentQuestionScroll->setObjectName(QStringLiteral("questionScroll"));
    auto *questionHost = new QWidget;
    studentQuestionLayout = new QVBoxLayout(questionHost);
    studentQuestionLayout->setContentsMargins(0, 0, 0, 0);
    studentQuestionLayout->setSpacing(12);
    studentQuestionLayout->addWidget(mutedLabel(QStringLiteral("请先选择左侧考试。")));
    studentQuestionScroll->setWidget(questionHost);
    workLayout->addWidget(studentQuestionScroll, 1);

    // 操作按钮行：上一题 / 下一题 / 保存草稿 / 提交试卷。
    auto *studentActionPanel = new QFrame;
    studentActionPanel->setObjectName(QStringLiteral("studentActionPanel"));
    auto *actions = new QHBoxLayout(studentActionPanel);
    actions->setContentsMargins(16, 12, 16, 12);
    actions->setSpacing(10);
    studentActionHintLabel = new QLabel(QStringLiteral("可切换题目并保存草稿，完成后提交试卷。"));
    studentActionHintLabel->setObjectName(QStringLiteral("reviewTip"));
    previousQuestionButton = new QPushButton(QStringLiteral("上一题"));
    nextQuestionButton = new QPushButton(QStringLiteral("下一题"));
    auto *draftButton = new QPushButton(QStringLiteral("保存草稿"));
    auto *submitButton = new QPushButton(QStringLiteral("提交试卷"));
    draftButton->setObjectName(QStringLiteral("primaryBigButton"));
    submitButton->setObjectName(QStringLiteral("successBigButton"));
    previousQuestionButton->setEnabled(false);
    nextQuestionButton->setEnabled(false);
    actions->addWidget(studentActionHintLabel, 1);
    actions->addWidget(previousQuestionButton);
    actions->addWidget(nextQuestionButton);
    actions->addWidget(draftButton);
    actions->addWidget(submitButton);
    workLayout->addWidget(studentActionPanel);

    draftStatusLabel = new QLabel(QStringLiteral("草稿：进入考试后自动保存到本地"));
    draftStatusLabel->setObjectName(QStringLiteral("aiProctorStatusLabel"));
    draftStatusLabel->setProperty("state", QStringLiteral("normal"));
    workLayout->addWidget(draftStatusLabel);

    // 我的提交记录：紧凑放在中列底部（限高，避免挤压题目区）。
    auto *resultTitle = sectionTitle(QStringLiteral("我的提交记录"));
    workLayout->addWidget(resultTitle);
    mySubmissionTable = new QTableWidget(0, 8);
    setupMySubmissionTable(mySubmissionTable);
    mySubmissionTable->setMaximumHeight(150);
    workLayout->addWidget(mySubmissionTable);

    // Stage 4 本地草稿定时器（防抖 2s + 周期 30s 兜底）。
    localDraftDebounceTimer = new QTimer(this);
    localDraftDebounceTimer->setSingleShot(true);
    localDraftDebounceTimer->setInterval(2000);
    connect(localDraftDebounceTimer, &QTimer::timeout, this, [this] { saveLocalDraft(); });
    localDraftAutosaveTimer = new QTimer(this);
    localDraftAutosaveTimer->setInterval(30000);
    connect(localDraftAutosaveTimer, &QTimer::timeout, this, [this] {
        if (currentExamId > 0 && !currentExamQuestions.isEmpty()) {
            saveLocalDraft();
        }
    });

    // ============ 右列：信息栏（固定宽度，三张卡片：监考预览 / 倒计时 / 答题卡）============
    auto *infoCol = new QWidget;
    infoCol->setObjectName(QStringLiteral("examInfoColumn"));
    infoCol->setFixedWidth(340);
    auto *infoLayout = new QVBoxLayout(infoCol);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(16);

    // 卡片①：监考预览 + 状态 + 竖排小按钮（不再三个大按钮横向挤）。
    auto *previewCard = card();
    previewCard->setObjectName(QStringLiteral("sidePanelCard"));
    auto *previewLayout = qobject_cast<QVBoxLayout *>(previewCard->layout());
    previewLayout->addWidget(sectionTitle(QStringLiteral("监考预览")));
    studentProctorPreview = new QLabel(QStringLiteral("监考预览\n（未启动监考）"));
    studentProctorPreview->setObjectName(QStringLiteral("proctorPreview"));
    studentProctorPreview->setMinimumSize(288, 200);
    studentProctorPreview->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(studentProctorPreview);
    qDebug() << "[STUDENT_CAMERA_WIDGET_CREATE]";
    aiProctorStatusLabel = new QLabel(QStringLiteral("C++摄像头监考：未启动"));
    aiProctorStatusLabel->setObjectName(QStringLiteral("aiProctorStatusLabel"));
    aiProctorStatusLabel->setWordWrap(true);
    previewLayout->addWidget(aiProctorStatusLabel);
    startAiProctorButton = new QPushButton(QStringLiteral("启动摄像头监考"));
    startAiProctorButton->setObjectName(QStringLiteral("primaryButton"));
    simulateProctorButton = new QPushButton(QStringLiteral("模拟监考模式"));
    simulateProctorButton->setObjectName(QStringLiteral("quietButton"));
    simulateProctorButton->setEnabled(false);
    simulateProctorButton->setToolTip(QStringLiteral(
        "摄像头不可用时，使用模拟数据演示监考功能（答辩用）。\n"
        "模拟模式会生成虚假的摄像头画面和人脸识别结果，仅用于展示。"));
    stopAiProctorButton = new QPushButton(QStringLiteral("停止监考"));
    stopAiProctorButton->setEnabled(false);
    previewLayout->addWidget(startAiProctorButton);
    previewLayout->addWidget(simulateProctorButton);
    previewLayout->addWidget(stopAiProctorButton);
    infoLayout->addWidget(previewCard);

    // 卡片②：倒计时。
    auto *countdownCard = card();
    countdownCard->setObjectName(QStringLiteral("sidePanelCard"));
    countdownCard->layout()->addWidget(sectionTitle(QStringLiteral("倒计时")));
    studentCountdownLabel = new QLabel(QStringLiteral("倒计时：--:--:--"));
    studentCountdownLabel->setObjectName(QStringLiteral("countdownBadge"));
    countdownCard->layout()->addWidget(studentCountdownLabel);
    infoLayout->addWidget(countdownCard);

    // 卡片③：答题卡（独立卡片 + 滚动，stretch 占满剩余高度，不再被挤塌）。
    auto *answerCardFrame = card();
    answerCardFrame->setObjectName(QStringLiteral("sidePanelCard"));
    auto *answerCardLayout = qobject_cast<QVBoxLayout *>(answerCardFrame->layout());
    answerCardLayout->addWidget(sectionTitle(QStringLiteral("答题卡"), QStringLiteral("蓝绿=已答，浅色=未答，点击跳题")));
    auto *navHost = new QWidget;
    navHost->setObjectName(QStringLiteral("answerSheet"));
    studentQuestionNavGrid = new QGridLayout(navHost);
    studentQuestionNavGrid->setContentsMargins(8, 8, 8, 8);
    studentQuestionNavGrid->setSpacing(8);
    studentQuestionNavGrid->setAlignment(Qt::AlignTop);
    auto *navScroll = new QScrollArea;
    navScroll->setObjectName(QStringLiteral("answerSheetScroll"));
    navScroll->setWidgetResizable(true);
    navScroll->setFrameShape(QFrame::NoFrame);
    navScroll->setMinimumHeight(140);
    navScroll->setWidget(navHost);
    answerCardLayout->addWidget(navScroll, 1);
    infoLayout->addWidget(answerCardFrame, 1);

    // 三列组装：左选择 | 中答题(stretch) | 右信息栏。
    layout->addWidget(listCard);
    layout->addWidget(workCard, 1);
    layout->addWidget(infoCol);

    connect(refreshButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=student_exam_center button=refresh_exams");
        refreshStudentExams();
    });
    connect(studentExamList, &QListWidget::currentRowChanged, this, &MainWindow::loadSelectedExam);
    connect(previousQuestionButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=student_exam_center button=previous_question");
        moveStudentQuestion(-1);
    });
    connect(nextQuestionButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=student_exam_center button=next_question");
        moveStudentQuestion(1);
    });
    connect(draftButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=student_exam_center button=save_draft");
        saveDraft();
    });
    connect(submitButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=student_exam_center button=submit_exam");
        submitExam();
    });
    const auto runStartAiProctor = [this] {
        qInfo("[UI_CLICK] page=student_exam_center button=start_ai_proctor");
        startAiProctor();
    };
    const auto runStopAiProctor = [this] {
        qInfo("[UI_CLICK] page=student_exam_center button=stop_ai_proctor");
        stopAiProctor();
    };
    const auto runSimulateProctor = [this] {
        qInfo("[UI_CLICK] page=student_exam_center button=simulate_proctor");
        if (!aiProctorMonitor) {
            return;
        }
        if (aiProctorMonitor->isRunning()) {
            showToast(QStringLiteral("请先停止当前监考再切换模式。"), false);
            return;
        }
        setAiProctorStatus(QStringLiteral("【模拟监考模式】正在启动模拟监考..."), QStringLiteral("warning"));
        qInfo("[SIMULATION_MODE] [PROCTOR_START] mode=simulation exam=%d user=%s",
              currentExamId, qPrintable(currentUser.username));
        m_previewLogged = false;
        m_proctorRunning = true;
        if (studentProctorPreview) {
            studentProctorPreview->setText(QStringLiteral("正在启动模拟监考..."));
        }
        aiProctorMonitor->startSimulation();
        // 留痕：启用模拟监考模式（仅记录事件，不加作弊分），供教师端复核。
        recordAbnormalEvent(QStringLiteral("SIMULATION_MODE"),
                            QStringLiteral("学生启用了模拟监考模式（无摄像头演示），本场监考画面为模拟数据"));
    };
    connect(startAiProctorButton, &QAbstractButton::clicked, this, runStartAiProctor);
    connect(stopAiProctorButton, &QAbstractButton::clicked, this, runStopAiProctor);
    connect(simulateProctorButton, &QAbstractButton::clicked, this, runSimulateProctor);
    registerButtonAction(startAiProctorButton,
                         QStringLiteral("student_exam_center"),
                         QStringLiteral("start_ai_proctor"),
                         runStartAiProctor);
    registerButtonAction(stopAiProctorButton,
                         QStringLiteral("student_exam_center"),
                         QStringLiteral("stop_ai_proctor"),
                         runStopAiProctor);
    registerButtonAction(simulateProctorButton,
                         QStringLiteral("student_exam_center"),
                         QStringLiteral("simulate_proctor"),
                         runSimulateProctor);
    return page;
}

QWidget *MainWindow::createGradingPage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto *listCard = card();
    listCard->setObjectName(QStringLiteral("sidePanelCard"));
    listCard->setFixedWidth(360);
    listCard->layout()->addWidget(sectionTitle(QStringLiteral("阅卷中心"), QStringLiteral("选择待批阅答卷后领取评分")));
    auto *toolbar = new QHBoxLayout;
    submissionStatusFilter = new QComboBox;
    submissionStatusFilter->addItem(QStringLiteral("全部状态"), QStringLiteral("all"));
    submissionStatusFilter->addItem(QStringLiteral("待批阅"), QStringLiteral("pending"));
    submissionStatusFilter->addItem(QStringLiteral("正在批阅"), QStringLiteral("grading"));
    submissionStatusFilter->addItem(QStringLiteral("已批阅"), QStringLiteral("graded"));
    submissionStatusFilter->setCurrentIndex(1);
    auto *refreshButton = new QPushButton(QStringLiteral("刷新列表"));
    toolbar->addWidget(submissionStatusFilter, 1);
    toolbar->addWidget(refreshButton);
    listCard->layout()->addItem(toolbar);
    submissionList = new QListWidget;
    submissionList->setObjectName(QStringLiteral("submissionQueueList"));
    qobject_cast<QVBoxLayout *>(listCard->layout())->addWidget(submissionList, 1);
    layout->addWidget(listCard);

    auto *workspace = card();
    workspace->setObjectName(QStringLiteral("gradingPaperCard"));
    submissionTitleLabel = sectionTitle(QStringLiteral("请选择答卷"));
    submissionStateLabel = new QLabel(QStringLiteral("状态：-"));
    submissionStateLabel->setObjectName(QStringLiteral("lockBadge"));
    workspace->layout()->addWidget(submissionTitleLabel);
    workspace->layout()->addWidget(submissionStateLabel);

    auto *reviewActionPanel = new QFrame;
    reviewActionPanel->setObjectName(QStringLiteral("reviewActionPanel"));
    auto *reviewActionLayout = new QHBoxLayout(reviewActionPanel);
    reviewActionLayout->setContentsMargins(16, 14, 16, 14);
    reviewActionLayout->setSpacing(12);
    auto *reviewTip = new QLabel(QStringLiteral("选择左侧答卷后领取批阅，逐题录入得分并提交成绩。"));
    reviewTip->setObjectName(QStringLiteral("reviewTip"));
    lockSubmissionButton = new QPushButton(QStringLiteral("先选左侧答卷"));
    lockSubmissionButton->setObjectName(QStringLiteral("primaryBigButton"));
    submitGradeButton = new QPushButton(QStringLiteral("提交成绩"));
    submitGradeButton->setObjectName(QStringLiteral("successBigButton"));
    releaseSubmissionButton = new QPushButton(QStringLiteral("退出批阅"));
    lockSubmissionButton->setEnabled(false);
    submitGradeButton->setEnabled(false);
    releaseSubmissionButton->setEnabled(false);
    reviewActionLayout->addWidget(reviewTip, 1);
    reviewActionLayout->addWidget(lockSubmissionButton);
    reviewActionLayout->addWidget(submitGradeButton);
    reviewActionLayout->addWidget(releaseSubmissionButton);
    workspace->layout()->addWidget(reviewActionPanel);

    gradingQuestionScroll = new QScrollArea;
    gradingQuestionScroll->setWidgetResizable(true);
    gradingQuestionScroll->setObjectName(QStringLiteral("gradingQuestionScroll"));
    auto *gradingHost = new QWidget;
    gradingQuestionLayout = new QVBoxLayout(gradingHost);
    gradingQuestionLayout->setContentsMargins(0, 0, 0, 0);
    gradingQuestionLayout->setSpacing(12);
    gradingQuestionLayout->addWidget(mutedLabel(QStringLiteral("请选择左侧答卷，领取后即可逐题评分。")));
    gradingQuestionScroll->setWidget(gradingHost);
    qobject_cast<QVBoxLayout *>(workspace->layout())->addWidget(gradingQuestionScroll, 1);

    auto *gradePanel = new QFrame;
    gradePanel->setObjectName(QStringLiteral("gradeActionPanel"));
    auto *gradePanelLayout = new QVBoxLayout(gradePanel);
    gradePanelLayout->setContentsMargins(16, 14, 16, 14);
    gradePanelLayout->setSpacing(10);
    gradePanelLayout->addWidget(sectionTitle(QStringLiteral("评分汇总"), QStringLiteral("逐题给分后系统会自动汇总总分。")));
    auto *form = new QFormLayout;
    scoreSpinBox = new QDoubleSpinBox;
    scoreSpinBox->setRange(0, 1000);
    scoreSpinBox->setDecimals(1);
    scoreSpinBox->setSingleStep(0.5);
    scoreSpinBox->setSuffix(QStringLiteral(" 分"));
    teacherCommentEdit = new QTextEdit;
    teacherCommentEdit->setFixedHeight(82);
    form->addRow(QStringLiteral("总分"), scoreSpinBox);
    form->addRow(QStringLiteral("总评"), teacherCommentEdit);
    gradePanelLayout->addItem(form);
    workspace->layout()->addWidget(gradePanel);

    layout->addWidget(workspace, 1);

    connect(submissionStatusFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::populateSubmissionList);
    connect(refreshButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=teacher_review button=refresh_submissions");
        populateSubmissionList();
    });
    connect(submissionList, &QListWidget::currentRowChanged, this, &MainWindow::setSubmission);
    connect(lockSubmissionButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=teacher_review button=lock_submission");
        lockCurrentSubmission();
    });
    connect(releaseSubmissionButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=teacher_review button=release_submission");
        releaseCurrentSubmission();
    });
    connect(submitGradeButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=teacher_review button=submit_grade");
        submitCurrentGrade();
    });
    // 阶段2B：仅教师端把阅卷中心的“刷新提交/锁定答卷/退出批阅/提交成绩”接入注册表（handler 复用原业务函数）。
    // 锁定/退出批阅必须与提交成绩同批：麒麟下 clicked 不稳定时若锁不了卷，提交成绩会一直停在“请先领取答卷”。
    if (currentUser.role == UserRole::Teacher) {
        registerButtonAction(refreshButton, QStringLiteral("teacher_review"), QStringLiteral("refresh_submissions"), [this] { populateSubmissionList(); });
        registerButtonAction(lockSubmissionButton, QStringLiteral("teacher_review"), QStringLiteral("lock_submission"), [this] { lockCurrentSubmission(); });
        registerButtonAction(releaseSubmissionButton, QStringLiteral("teacher_review"), QStringLiteral("release_submission"), [this] { releaseCurrentSubmission(); });
        registerButtonAction(submitGradeButton, QStringLiteral("teacher_review"), QStringLiteral("submit_grade"), [this] { submitCurrentGrade(); });
    }
    return page;
}

namespace {
// 静态环境光缓存：仅绘制应用自己生成的背景，不抓屏、不读取壁纸、不处理前景内容。
class QuestionGlassPage final : public QWidget {
public:
    const QImage &ambientImage()
    {
        if (cachedSize == size() && !ambient.isNull()) return ambient;
        cachedSize = size();
        const QSize imageSize = size().scaled(1280, 900, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
        ambient = QImage(imageSize, QImage::Format_RGB32);
        QPainter p(&ambient);
        p.scale(imageSize.width() / qreal(qMax(1, width())), imageSize.height() / qreal(qMax(1, height())));
        QLinearGradient base(0, 0, 0, height());
        base.setColorAt(0, QColor("#EDF3F8"));
        base.setColorAt(.50, QColor("#E3EAF0"));
        base.setColorAt(1, QColor("#EDF4F8"));
        p.fillRect(rect(), base);
        const auto light = [&](qreal x, qreal y, qreal radius, QColor color) {
            // 以短边控制光斑范围，避免宽窗口把暖光拉成横贯页面的色带。
            QRadialGradient glow(QPointF(width()*x, height()*y), qMin(width(),height())*radius);
            // 平缓衰减到透明，不形成光斑轮廓；所有玻璃容器仍采样同一背景。
            const int alpha = color.alpha();
            glow.setColorAt(0, color);
            color.setAlpha(qRound(alpha * .82)); glow.setColorAt(.25, color);
            color.setAlpha(qRound(alpha * .40)); glow.setColorAt(.55, color);
            color.setAlpha(qRound(alpha * .08)); glow.setColorAt(.82, color);
            color.setAlpha(0); glow.setColorAt(1, color);
            p.fillRect(rect(), glow);
        };
        light(-.06, .04, 1.05, QColor(161,195,223,110));
        light(.91, .30, 1.12, QColor(238,211,184,88));
        light(.22, 1.08, .95, QColor(175,206,227,68));
        light(.40, .45, .92, QColor(249,252,255,55));
        return ambient;
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this); p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(rect(), ambientImage());
    }
private:
    QSize cachedSize;
    QImage ambient;
};

class QuestionGlassPanel final : public QFrame {
public:
    QuestionGlassPanel(QuestionGlassPage *page, QWidget *parent) : QFrame(parent), backgroundPage(page) {}
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF area = QRectF(rect()).adjusted(2, 2, -2, -3);
        const bool primary = objectName() == QStringLiteral("questionGlassTitlePanel")
                          || objectName() == QStringLiteral("questionGlassCategories");
        const bool content = objectName() == QStringLiteral("questionGlassTableCard");
        // 仅区分白色叠层与边缘强度，不改变共用环境背景或子控件样式。
        const int topAlpha = content ? 220 : (primary ? 174 : 142);
        const int middleAlpha = content ? 206 : (primary ? 146 : 112);
        const int bottomAlpha = content ? 214 : (primary ? 154 : 120);
        // 阴影/高光在背景阶段绘制，Qt 随后正常绘制清晰的子控件。
        for (int i=3; i>0; --i) {
            p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor(47,59,76,8), i*2));
            p.drawRoundedRect(area.translated(0, 1), 20, 20);
        }
        QPainterPath clip; clip.addRoundedRect(area,20,20); p.save(); p.setClipPath(clip);
        const QImage &image = backgroundPage->ambientImage();
        const QPoint origin = mapTo(backgroundPage, QPoint(0,0));
        const qreal sx = image.width()/qreal(qMax(1,backgroundPage->width()));
        const qreal sy = image.height()/qreal(qMax(1,backgroundPage->height()));
        p.drawImage(rect(),image,QRectF(origin.x()*sx,origin.y()*sy,width()*sx,height()*sy));
        QLinearGradient milk(area.topLeft(), area.bottomRight());
        milk.setColorAt(0,QColor(255,255,255,topAlpha)); milk.setColorAt(.48,QColor(255,255,255,middleAlpha)); milk.setColorAt(1,QColor(239,241,244,bottomAlpha));
        p.fillRect(area,milk);
        QLinearGradient gloss(0,area.top(),0,area.top()+qMin(60.,area.height()));
        gloss.setColorAt(0,QColor(255,255,255,primary ? 100 : (content ? 35 : 65))); gloss.setColorAt(1,QColor(255,255,255,0));p.fillRect(area,gloss);
        p.restore();p.setBrush(Qt::NoBrush);
        QLinearGradient rim(area.topLeft(),area.bottomRight());
        p.setPen(QPen(QColor(255,255,255,185),1));p.drawRoundedRect(area,20,20);
        rim.setColorAt(0,QColor(255,255,255,primary ? 255 : 225));rim.setColorAt(.48,QColor(255,255,255,primary ? 175 : 110));rim.setColorAt(1,QColor(67,83,103,primary ? 90 : (content ? 55 : 76)));
        p.setPen(QPen(QBrush(rim),1));p.drawRoundedRect(area,20,20);
        p.setPen(QPen(QColor(255,255,255,primary ? 100 : (content ? 40 : 65)),1));p.drawRoundedRect(area.adjusted(2,2,-2,-2),18,18);
    }
private:
    QuestionGlassPage *backgroundPage;
};

// 仅绘制下拉指示，不接管输入、键盘或 currentIndexChanged。
class QuestionGlassCombo final : public QComboBox {
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QComboBox::paintEvent(event);
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#345777"),1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        const qreal x=width()-17, y=height()/2.;
        QPainterPath arrow;arrow.moveTo(x-4,y-2);arrow.lineTo(x,y+2);arrow.lineTo(x+4,y-2);p.drawPath(arrow);
    }
};

// 题库为整行只读浏览：保留选择/键盘焦点，仅取消单元格的编辑框式焦点描边。
class QuestionBrowseDelegate : public QStyledItemDelegate {
public:
    explicit QuestionBrowseDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        option->state &= ~QStyle::State_HasFocus;
    }
};

// 只读绘制委托：不改模型值、角色、ID、行列或编辑行为。
class QuestionGlassBadgeDelegate final : public QuestionBrowseDelegate {
public:
    explicit QuestionGlassBadgeDelegate(QObject *parent) : QuestionBrowseDelegate(parent) {}
    void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);initStyleOption(&opt,index);
        const QString text=opt.text, key=text.trimmed();
        QColor ink("#48627B"), fill(220,231,243,205);
        if(index.column()==10) {
            if(key==QStringLiteral("简单")){ink=QColor("#247352");fill=QColor(208,239,223,210);}
            else if(key==QStringLiteral("中等")){ink=QColor("#8B5919");fill=QColor(248,232,204,210);}
            else if(key==QStringLiteral("困难")){ink=QColor("#A23E46");fill=QColor(247,220,223,210);}
        } else {
            if(key.contains(QStringLiteral("单"))){ink=QColor("#315F99");fill=QColor(214,232,253,210);}
            else if(key.contains(QStringLiteral("多"))){ink=QColor("#635898");fill=QColor(228,224,247,210);}
            else if(key.startsWith(QStringLiteral("判断"))){ink=QColor("#944955");fill=QColor(247,224,229,210);}
            else if(key.startsWith(QStringLiteral("填空"))){ink=QColor("#94601D");fill=QColor(249,234,208,210);}
            else if(key.startsWith(QStringLiteral("简答"))){ink=QColor("#2D745C");fill=QColor(211,237,223,210);}
            else if(key.startsWith(QStringLiteral("综合"))){ink=QColor("#545FA0");fill=QColor(225,229,251,210);}
            else if(key.contains(QStringLiteral("程序"))||key.contains(QStringLiteral("编程"))){ink=QColor("#26737E");fill=QColor(210,238,239,210);}
        }
        opt.text.clear();const QWidget *widget=option.widget;
        (widget?widget->style():QApplication::style())->drawControl(QStyle::CE_ItemViewItem,&opt,p,widget);
        p->save();p->setRenderHint(QPainter::Antialiasing);p->setClipRect(option.rect);
        const int w=qMin(option.rect.width()-10,option.fontMetrics.horizontalAdvance(text)+16);
        const QRectF badge(option.rect.left()+5,option.rect.center().y()-13,w,26);
        QLinearGradient surface(badge.topLeft(),badge.bottomRight());surface.setColorAt(0,fill.lighter(105));surface.setColorAt(1,fill);
        p->setPen(QPen(QColor(255,255,255,185),1));p->setBrush(surface);p->drawRoundedRect(badge,9,9);
        p->setFont(option.font);p->setPen(ink);p->drawText(badge,Qt::AlignCenter,option.fontMetrics.elidedText(text,Qt::ElideRight,w-8));p->restore();
    }
};

// 只在题库页显示期间覆盖共享外框的外观；隐藏时逐项恢复原样式，不修改导航事件。
class QuestionGlassChrome final : public QObject {
public:
    explicit QuestionGlassChrome(QWidget *page) : QObject(page), page(page) {page->installEventFilter(this);}
protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(watched==page && event->type()==QEvent::Show) apply();
        if(watched==page && event->type()==QEvent::Hide) restore();
        return QObject::eventFilter(watched,event);
    }
private:
    struct Saved { QPointer<QWidget> widget; QString style; };
    QWidget *page;
    QVector<Saved> saved;
    QPointer<QWidget> sharedTitle;
    bool sharedTitleWasHidden = false;
    QPointer<QLabel> brandLogo;
    QPixmap originalBrandPixmap;
    void set(QWidget *widget,const QString &style) {if(!widget)return;saved.append({widget,widget->styleSheet()});widget->setStyleSheet(style);}
    void restore(){for(const Saved &entry:saved)if(entry.widget)entry.widget->setStyleSheet(entry.style);saved.clear();
        if(sharedTitle)sharedTitle->setVisible(!sharedTitleWasHidden);sharedTitle.clear();
        if(brandLogo)brandLogo->setPixmap(originalBrandPixmap);brandLogo.clear();originalBrandPixmap=QPixmap();}
    void apply()
    {
        if(!saved.isEmpty())return;
        auto *root=page->window();
        brandLogo=root->findChild<QLabel*>(QStringLiteral("brandLogo"));
        if(brandLogo) {
            originalBrandPixmap=brandLogo->pixmap();
            const QPixmap logo(QStringLiteral(":/question-branding/app_icon.png"));
            if(!logo.isNull()) {
                brandLogo->setPixmap(logo.scaled(44,44,Qt::KeepAspectRatio,Qt::SmoothTransformation));
                set(brandLogo,QStringLiteral("QLabel#brandLogo {background:transparent;border:none;padding:0;}"));
            }
        }
        // 仅题库页隐藏共享导航标题，保留内容区“题库管理”；离页恢复原可见性。
        sharedTitle=root->findChild<QWidget*>(QStringLiteral("pageTitle"));
        if(sharedTitle){sharedTitleWasHidden=sharedTitle->isHidden();sharedTitle->hide();}
        set(root->findChild<QWidget*>(QStringLiteral("sidebar")),QStringLiteral(R"(
            QWidget#sidebar { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 rgba(44,57,73,244),stop:.6 rgba(35,45,60,247),stop:1 rgba(48,64,79,244)); border-right:1px solid rgba(255,255,255,95); }
            QPushButton#navButton {color:#E4EFF9;background:transparent;border:1px solid transparent;border-radius:18px;}
            QPushButton#navButton:hover {background:rgba(255,255,255,26);}
            QPushButton#navButton:checked {color:white;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #52B9F9,stop:.35 #168EF5,stop:1 #0872DD);border:1px solid #7BCDFB;border-top-color:#C1E8FF;}
            QLabel#accountBox {background:rgba(220,238,255,20);border:1px solid rgba(255,255,255,80);color:#E8F3FD;}
        )"));
        set(root->findChild<QWidget*>(QStringLiteral("currentRoleBadge")),QStringLiteral("QLabel#currentRoleBadge {background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #FAFDFF,stop:1 #DDEBF8);border:1px solid #FFFFFF;border-bottom-color:#C2D4E6;border-radius:20px;color:#294C70;}"));
        auto *main=root->findChild<QWidget*>(QStringLiteral("mainArea"));
        if(main) {
            const QString base=QStringLiteral("border:1px solid rgba(255,255,255,230);border-bottom-color:#BDCDDD;border-radius:20px;");
            set(main->findChild<QPushButton*>(QStringLiteral("quietButton")),QStringLiteral("QPushButton {background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #FFFFFF,stop:1 #DDE9F5);color:#305277;%1} QPushButton:hover {background:#EDF7FF;}").arg(base));
            set(main->findChild<QPushButton*>(QStringLiteral("dangerOutlineButton")),QStringLiteral("QPushButton {background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #FFFBFB,stop:1 #F8DEE4);color:#B5334A;%1} QPushButton:hover {background:#FFE9EE;}").arg(base));
        }
    }
};
// 只绘制分类外观：沿用原始 tabText/tabData 和 QTabBar 的选择、键盘及滚动行为。
class QuestionGlassTabBar final : public QTabBar {
public:
    explicit QuestionGlassTabBar(QWidget *parent = nullptr) : QTabBar(parent) { setMouseTracking(true); }
protected:
    QSize tabSizeHint(int index) const override
    {
        const QString text = tabText(index);
        const int split = text.lastIndexOf(QLatin1Char(' '));
        const QFontMetrics metrics(font());
        const int badgeWidth = qMax(26, metrics.horizontalAdvance(text.mid(split + 1)) + 14);
        return QSize(metrics.horizontalAdvance(text.left(split)) + badgeWidth + 36, qMax(52, metrics.height() + 32));
    }
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(font());
        for (int index = 0; index < count(); ++index) {
            const QRectF area = QRectF(tabRect(index)).adjusted(2, 2, -2, -2);
            if (!area.intersects(rect())) continue;
            QStyleOptionTab option;
            initStyleOption(&option, index);
            const bool selected = index == currentIndex();
            const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
            painter.setPen(QPen(selected ? QColor(135,215,255,180) : QColor(255,255,255,160),1));
            QLinearGradient surface(area.topLeft(),area.bottomRight());
            surface.setColorAt(0,selected?QColor("#6CCBFA"):QColor(255,255,255,hovered?225:180));
            surface.setColorAt(.40,selected?QColor("#168FF4"):QColor(250,251,253,150));
            surface.setColorAt(1,selected?QColor("#126ACD"):QColor(227,232,238,125));
            painter.setBrush(surface);painter.drawRoundedRect(area,24,24);
            QLinearGradient edge(area.topLeft(),area.bottomRight());
            edge.setColorAt(0,QColor(255,255,255,220));edge.setColorAt(.45,QColor(255,255,255,100));edge.setColorAt(1,QColor(43,72,109,85));
            painter.setBrush(Qt::NoBrush);painter.setPen(QPen(QBrush(edge),1));painter.drawRoundedRect(area,24,24);
            painter.setPen(QPen(QColor(255,255,255,selected?110:75),1));
            painter.drawRoundedRect(area.adjusted(2,2,-2,-2),22,22);
            const QString text = tabText(index);
            const int split = text.lastIndexOf(QLatin1Char(' '));
            const QString number = text.mid(split + 1);
            const int badgeWidth = qMax(26, fontMetrics().horizontalAdvance(number) + 14);
            const QRectF badge(area.right() - badgeWidth - 10, area.center().y() - 12, badgeWidth, 24);
            painter.setBrush(selected ? QColor(255, 255, 255, 48) : QColor(223, 231, 241, 170));
            painter.drawRoundedRect(badge, 10, 10);
            painter.setPen(selected ? Qt::white : QColor("#536278"));
            painter.drawText(badge, Qt::AlignCenter, number);
            painter.setPen(selected ? Qt::white : QColor("#182033"));
            painter.drawText(QRectF(area.left() + 12, area.top(), badge.left() - area.left() - 18, area.height()), Qt::AlignVCenter | Qt::AlignLeft, text.left(split));
            if (selected && hasFocus()) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(255, 255, 255, 210), 1, Qt::DotLine));
                painter.drawRoundedRect(area.adjusted(3, 3, -3, -3), 16, 16);
            }
        }
    }
};
}

QWidget *MainWindow::createQuestionPage()
{
    auto *page = new QuestionGlassPage;
    new QuestionGlassChrome(page);
    page->setProperty("questionGlassTheme", true);
    page->setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    // 题库页局部主题；不修改 QApplication、共享资源或任何业务方法。
    page->setStyleSheet(QStringLiteral(R"QSS(
        QWidget[questionGlassTheme="true"] {
            background: transparent;
            color: #182033;
        }
        QWidget[questionGlassTheme="true"] QLabel { background: transparent; color: #182033; }
        QWidget[questionGlassTheme="true"] QLabel#questionGlassTitle { font-size: 22px; font-weight: 600; }
        QWidget[questionGlassTheme="true"] QLabel#questionGlassSubtitle,
        QWidget[questionGlassTheme="true"] QLabel#mutedLabel { color: #6B7280; font-size: 13px; }
        QFrame#questionGlassCategories, QFrame#questionGlassToolbar, QFrame#questionGlassTableCard, QFrame#questionGlassTitlePanel {background:transparent;border:none;}
        QTabBar#questionTypeTabs { background: transparent; border: none; font-size: 13px; font-weight: 500; }
        QTabBar#questionTypeTabs QToolButton { background: #F2F7FD; color: #186FC5; border: 1px solid #D8E5F3; border-radius: 18px; }
        QTabBar#questionTypeTabs QToolButton:hover { background: #E4F0FE; }
        QWidget[questionGlassTheme="true"] QLineEdit, QWidget[questionGlassTheme="true"] QComboBox {
            min-height: 20px; padding: 12px 12px; border-radius: 22px;
            border: 1px solid rgba(255,255,255,210); border-bottom-color:rgba(80,95,115,85); background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 rgba(255,255,255,205),stop:1 rgba(239,243,248,155));
            color: #182033; font-size: 13px; selection-background-color: #DCEEFF; selection-color: #182033;
        }
        QWidget[questionGlassTheme="true"] QLineEdit { placeholder-text-color: #798698; }
        QWidget[questionGlassTheme="true"] QComboBox { padding-right: 28px; }
        QWidget[questionGlassTheme="true"] QLineEdit:focus, QWidget[questionGlassTheme="true"] QComboBox:focus { border-color: #0A84FF; background: #FFFFFF; }
        QWidget[questionGlassTheme="true"] QComboBox::down-arrow { image:none; }
        QWidget[questionGlassTheme="true"] QComboBox::drop-down { width: 24px; background: transparent; border: none; }
        QWidget[questionGlassTheme="true"] QComboBox QAbstractItemView { background: #FFFFFF; color: #182033; selection-background-color: #E5F1FF; selection-color: #155B9F; }
        QWidget[questionGlassTheme="true"] QPushButton {
            min-height: 20px; padding: 12px 14px; border-radius: 22px; font-size: 13px; font-weight: 500;
            background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 rgba(255,255,255,220),stop:1 rgba(235,240,245,170)); color: #294361; border: 1px solid rgba(255,255,255,215); border-bottom-color:rgba(80,95,115,75);
        }
        QWidget[questionGlassTheme="true"] QPushButton:hover { background: #F4F9FF; border-color: #B4D5F8; }
        QWidget[questionGlassTheme="true"] QPushButton:pressed { background: #E2EEFC; }
        QWidget[questionGlassTheme="true"] QPushButton:focus { border-color: #0A84FF; }
        QWidget[questionGlassTheme="true"] QPushButton[questionGlassRole="primary"] { color: white; background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #49BAFA,stop:.38 #1388F1,stop:1 #0870D7);border-color:#73C9FF;border-top-color:#BAE8FF; }
        QWidget[questionGlassTheme="true"] QPushButton[questionGlassRole="primary"]:hover { background: #0075E8; border-color: #0075E8; }
        QWidget[questionGlassTheme="true"] QPushButton[questionGlassRole="primary"]:pressed { background: #0067CC; }
        QWidget[questionGlassTheme="true"] QPushButton[questionGlassRole="danger"] { color:#B63340;background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #FFFAFA,stop:1 #F5DDE2); }
        QWidget[questionGlassTheme="true"] QPushButton[questionGlassRole="danger"]:hover { background: #FFF0EE; border-color: #FFC9C5; }
        QWidget[questionGlassTheme="true"] QPushButton:disabled { background: #F3F5F8; border-color: #E7ECF2; color: #9AA5B3; }
        QTableWidget#questionManagementTable { background: rgba(255,255,255,248); alternate-background-color: rgba(245,247,250,245); color: #182033;
            border: none; border-radius: 0px; font-size: 13px; selection-background-color: #DEEDFF; selection-color: #173F69; }
        QTableWidget#questionManagementTable::item { padding: 5px 6px; border: none; }
        QTableWidget#questionManagementTable::item:hover { background: #EDF5FF; }
        QTableWidget#questionManagementTable::item:selected { background: #DEEDFF; color: #173F69; }
        QTableWidget#questionManagementTable QHeaderView::section { background: rgba(232,237,243,245); color: #2A4460;
            padding: 8px 6px; border: none; border-bottom: 1px solid #E5ECF5; font-size: 13px; font-weight: 600; }
        QWidget[questionGlassTheme="true"] QLabel#questionGlassPageIndicator { background: #EDF2F8; color: #1865B1; border-radius: 22px; padding: 13px 12px; }
        QWidget[questionGlassTheme="true"] QScrollBar:vertical { background: #F4F7FB; width: 10px; margin: 0; border: none; }
        QWidget[questionGlassTheme="true"] QScrollBar:horizontal { background: #F4F7FB; height: 10px; margin: 0; border: none; }
        QWidget[questionGlassTheme="true"] QScrollBar::handle { background: #C8D5E5; border-radius: 5px; min-width: 24px; min-height: 24px; }
        QWidget[questionGlassTheme="true"] QScrollBar::handle:hover { background: #A8BDD5; }
        QWidget[questionGlassTheme="true"] QScrollBar::add-line, QWidget[questionGlassTheme="true"] QScrollBar::sub-line { width: 0; height: 0; border: none; background: transparent; }
        QWidget[questionGlassTheme="true"] QScrollBar::add-page, QWidget[questionGlassTheme="true"] QScrollBar::sub-page { background: transparent; }
    )QSS"));
    auto *title = new QLabel(QStringLiteral("题库管理"));
    title->setObjectName(QStringLiteral("questionGlassTitle"));
    auto *subtitle = new QLabel(QStringLiteral("管理和维护所有题目，支持按题型分类、搜索和筛选"));
    subtitle->setObjectName(QStringLiteral("questionGlassSubtitle"));
    subtitle->setWordWrap(true);
    auto *titlePanel = new QuestionGlassPanel(page, page);
    titlePanel->setObjectName(QStringLiteral("questionGlassTitlePanel"));
    auto *titleLayout = new QVBoxLayout(titlePanel);
    titleLayout->setContentsMargins(14,8,14,8);
    titleLayout->setSpacing(4);
    titleLayout->addWidget(title);
    titleLayout->addWidget(subtitle);
    layout->addWidget(titlePanel);

    auto *categoryFrame = new QuestionGlassPanel(page, page);
    categoryFrame->setObjectName(QStringLiteral("questionGlassCategories"));
    auto *categoryLayout = new QHBoxLayout(categoryFrame);
    categoryLayout->setContentsMargins(5, 5, 5, 5);
    questionTypeTabs = new QuestionGlassTabBar(categoryFrame);
    questionTypeTabs->setObjectName(QStringLiteral("questionTypeTabs"));
    questionTypeTabs->setExpanding(false);
    questionTypeTabs->setUsesScrollButtons(true);
    questionTypeTabs->setElideMode(Qt::ElideNone);
    questionTypeTabs->setDrawBase(false);
    questionTypeTabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    const QStringList categories = {QStringLiteral("全部题型"), QStringLiteral("选择题"), QStringLiteral("单选题"), QStringLiteral("多选题"), QStringLiteral("判断题"), QStringLiteral("填空题"), QStringLiteral("简答题"), QStringLiteral("综合应用题"), QStringLiteral("程序题"), QStringLiteral("其他")};
    for (const QString &category : categories) {
        const QString title = category == QStringLiteral("全部题型") ? QStringLiteral("全部题目") : category;
        const int index = questionTypeTabs->addTab(title + QStringLiteral(" 0"));
        questionTypeTabs->setTabData(index, category);
    }
    categoryLayout->addWidget(questionTypeTabs);
    layout->addWidget(categoryFrame);

    auto *tableCard = new QuestionGlassPanel(page, page);
    auto *glassTableLayout = new QVBoxLayout(tableCard);
    glassTableLayout->setContentsMargins(18,18,18,18);
    glassTableLayout->setSpacing(14);
    tableCard->setObjectName(QStringLiteral("questionGlassTableCard"));
    auto *tableLayout = static_cast<QVBoxLayout *>(tableCard->layout());
    auto *toolbarFrame = new QuestionGlassPanel(page, tableCard);
    toolbarFrame->setObjectName(QStringLiteral("questionGlassToolbar"));
    auto *filterBar = new QHBoxLayout(toolbarFrame);
    filterBar->setContentsMargins(10, 10, 10, 10);
    filterBar->setSpacing(8);
    questionSearchEdit = new QLineEdit;
    questionSearchEdit->setPlaceholderText(QStringLiteral("搜索题干、课程、答案或解析"));
    questionSearchEdit->setMinimumWidth(180);
    questionDifficultyFilter = new QuestionGlassCombo;
    questionDifficultyFilter->addItems({QStringLiteral("全部难度"), QStringLiteral("简单"), QStringLiteral("中等"), QStringLiteral("困难")});
    auto *applyButton = new QPushButton(QStringLiteral("搜索"));
    applyButton->setProperty("questionGlassRole",QStringLiteral("primary"));
    auto *resetButton = new QPushButton(QStringLiteral("重置筛选"));
    auto *refreshButton = new QPushButton(QStringLiteral("刷新列表"));
    auto *importButton = new QPushButton(QStringLiteral("导入题目"));
    importButton->setObjectName(QStringLiteral("primaryButton"));
    importButton->setProperty("questionGlassRole", QStringLiteral("primary"));
    filterBar->addWidget(questionSearchEdit, 1);
    filterBar->addWidget(questionDifficultyFilter);
    filterBar->addWidget(applyButton);
    filterBar->addWidget(resetButton);
    filterBar->addSpacing(20);
    filterBar->addWidget(refreshButton);
    filterBar->addWidget(importButton);
    tableLayout->addWidget(toolbarFrame);

    // 保留所有已有业务入口；表格的逻辑列号与真实 ID 绑定不变。
    auto *actions = new QHBoxLayout;
    auto *createButton = new QPushButton(QStringLiteral("新增题目"));
    auto *editButton = new QPushButton(QStringLiteral("修改题目"));
    auto *deleteButton = new QPushButton(QStringLiteral("删除题目"));
    auto *detectDuplicateButton = new QPushButton(QStringLiteral("检测重复题"));
    auto *templateButton = new QPushButton(QStringLiteral("打开模板"));
    auto *fetchWinButton = new QPushButton(QStringLiteral("从 Windows 获取题库"));
    createButton->setObjectName(QStringLiteral("primaryButton"));
    deleteButton->setObjectName(QStringLiteral("dangerButton"));
    deleteButton->setProperty("questionGlassRole", QStringLiteral("danger"));
    fetchWinButton->setToolTip(QStringLiteral("通过现有 Windows 题库代理选择文件，再按原有导入流程处理。"));
    actions->addWidget(createButton);
    actions->addWidget(editButton);
    actions->addWidget(deleteButton);
    actions->addWidget(detectDuplicateButton);
    actions->addStretch();
    actions->addWidget(templateButton);
    actions->addWidget(fetchWinButton);
    tableLayout->addLayout(actions);

    questionTable = new QTableWidget(0, 12);
    questionTable->setObjectName(QStringLiteral("questionManagementTable"));
    questionTable->setHorizontalHeaderLabels({
        QStringLiteral("ID"), QStringLiteral("题型"), QStringLiteral("课程"), QStringLiteral("题干"),
        QStringLiteral("选项A"), QStringLiteral("选项B"), QStringLiteral("选项C"), QStringLiteral("选项D"),
        QStringLiteral("参考答案"), QStringLiteral("分值"), QStringLiteral("难度"), QStringLiteral("解析")
    });
    questionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    polishTable(questionTable);
    questionTable->setItemDelegate(new QuestionBrowseDelegate(questionTable));
    questionTable->setItemDelegateForColumn(1,new QuestionGlassBadgeDelegate(questionTable));
    questionTable->setItemDelegateForColumn(10,new QuestionGlassBadgeDelegate(questionTable));
    questionTable->verticalHeader()->setVisible(false);
    questionTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    questionTable->verticalHeader()->setDefaultSectionSize(44);
    questionTable->setMouseTracking(true);
    auto *header = questionTable->horizontalHeader();
    header->setFixedHeight(42);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    const QList<int> widths = {60, 100, 120, 360, 90, 90, 90, 90, 110, 60, 70, 160};
    for (int column = 0; column < widths.size(); ++column) questionTable->setColumnWidth(column, widths.at(column));
    // 仅调整显示顺序，编辑、删除仍按原始逻辑列读取真实题目 ID。
    header->moveSection(header->visualIndex(9), 4);
    header->moveSection(header->visualIndex(10), 5);
    tableLayout->addWidget(questionTable, 1);

    auto *pageBar = new QHBoxLayout;
    questionCountLabel = mutedLabel(QStringLiteral("共0道题，当前显示第0-0条"));
    questionPrevPageButton = new QPushButton(QStringLiteral("上一页"));
    questionNextPageButton = new QPushButton(QStringLiteral("下一页"));
    questionPageLabel = mutedLabel(QStringLiteral("第 1/1 页 · 共 0 题"));
    questionPageLabel->setObjectName(QStringLiteral("questionGlassPageIndicator"));
    pageBar->addWidget(questionCountLabel, 1);
    pageBar->addWidget(questionPrevPageButton);
    pageBar->addWidget(questionPageLabel);
    pageBar->addWidget(questionNextPageButton);
    tableLayout->addLayout(pageBar);
    layout->addWidget(tableCard, 1);
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::filterQuestions);
    connect(questionSearchEdit, &QLineEdit::returnPressed, this, &MainWindow::filterQuestions);

    bindTeacherButton(createButton, QStringLiteral("btn_teacher_create_question"), QStringLiteral("teacher_questions"), QStringLiteral("create_question"), [this] { createQuestion(); });
    bindTeacherButton(editButton, QStringLiteral("btn_teacher_edit_question"), QStringLiteral("teacher_questions"), QStringLiteral("edit_question"), [this] { editSelectedQuestion(); });
    bindTeacherButton(deleteButton, QStringLiteral("btn_teacher_delete_question"), QStringLiteral("teacher_questions"), QStringLiteral("delete_question"), [this] { deleteSelectedQuestion(); });
    bindTeacherButton(importButton, QStringLiteral("btn_teacher_question_import"), QStringLiteral("teacher_questions"), QStringLiteral("import_question_bank"), [this] { importQuestions(); });
    bindTeacherButton(detectDuplicateButton, QStringLiteral("btn_question_detect_duplicates"), QStringLiteral("teacher_questions"), QStringLiteral("question_detect_duplicates"), [this] { detectDuplicateQuestions(); });
    qInfo("[QUESTION_DUPLICATE_UI_READY] page=teacher_questions button=question_detect_duplicates visible=%d objectName=%s",
          detectDuplicateButton && !detectDuplicateButton->isHidden() ? 1 : 0,
          qPrintable(detectDuplicateButton ? detectDuplicateButton->objectName() : QStringLiteral("<null>")));
    connect(fetchWinButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=teacher_questions button=fetch_windows_question_bank");
        importQuestionsFromWindowsProxy();
    });
    connect(templateButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=teacher_questions button=open_question_template");
        openQuestionTemplate();
    });
    bindTeacherButton(refreshButton, QStringLiteral("btn_teacher_question_refresh"), QStringLiteral("teacher_questions"), QStringLiteral("refresh_question_bank"), [this] {
        refreshQuestionBank();
        refreshQuestionBankPage();
    });
    bindTeacherButton(questionPrevPageButton, QStringLiteral("btn_teacher_question_prev_page"), QStringLiteral("teacher_questions"), QStringLiteral("question_prev_page"), [this] { goQuestionPage(-1); });
    bindTeacherButton(questionNextPageButton, QStringLiteral("btn_teacher_question_next_page"), QStringLiteral("teacher_questions"), QStringLiteral("question_next_page"), [this] { goQuestionPage(1); });
    // 阶段2B：仅教师端把题目管理关键按钮接入按钮动作注册表（绕开 Kylin 上不触发的 clicked；handler 复用原业务函数）。
    if (currentUser.role == UserRole::Teacher) {
        registerButtonAction(createButton, QStringLiteral("teacher_questions"), QStringLiteral("create_question"), [this] { createQuestion(); });
        registerButtonAction(editButton, QStringLiteral("teacher_questions"), QStringLiteral("edit_question"), [this] { editSelectedQuestion(); });
        registerButtonAction(deleteButton, QStringLiteral("teacher_questions"), QStringLiteral("delete_question"), [this] { deleteSelectedQuestion(); });
        registerButtonAction(importButton, QStringLiteral("teacher_questions"), QStringLiteral("import_question_bank"), [this] { importQuestions(); });
        registerButtonAction(detectDuplicateButton, QStringLiteral("teacher_questions"), QStringLiteral("question_detect_duplicates"), [this] { detectDuplicateQuestions(); });
        registerButtonAction(refreshButton, QStringLiteral("teacher_questions"), QStringLiteral("refresh_question_bank"), [this] {
            refreshQuestionBank();
            refreshQuestionBankPage();
        });
        registerButtonAction(questionPrevPageButton, QStringLiteral("teacher_questions"), QStringLiteral("question_prev_page"), [this] { goQuestionPage(-1); });
        registerButtonAction(questionNextPageButton, QStringLiteral("teacher_questions"), QStringLiteral("question_next_page"), [this] { goQuestionPage(1); });
    }
    if (currentUser.role == UserRole::Admin) {
        registerButtonAction(questionPrevPageButton, QStringLiteral("teacher_questions"), QStringLiteral("question_prev_page"), [this] { goQuestionPage(-1); });
        registerButtonAction(questionNextPageButton, QStringLiteral("teacher_questions"), QStringLiteral("question_next_page"), [this] { goQuestionPage(1); });
    }
    connect(questionTable, &QTableWidget::cellClicked, this, [](int row, int col) {
        qInfo("[TABLE_CLICK] page=teacher_questions row=%d col=%d", row, col);
    });
    connect(questionTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        qInfo("[TABLE_DOUBLE_CLICK] page=teacher_questions row=%d col=%d", row, col);
        if (questionTable) {
            questionTable->selectRow(row);
        }
        editSelectedQuestion();
    });
    connect(questionSearchEdit, &QLineEdit::textChanged, this, &MainWindow::filterQuestions);
    connect(questionTypeTabs, &QTabBar::currentChanged, this, &MainWindow::filterQuestions);
    connect(questionDifficultyFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::filterQuestions);
    connect(resetButton, &QAbstractButton::clicked, this, [this] {
        qInfo("[UI_CLICK] page=teacher_questions button=reset_question_filters");
        resetQuestionFilters();
    });
    return page;
}

void MainWindow::refreshQuestionCategoryCounts()
{
    if (!questionTypeTabs) return;
    // 分类数量使用全量启用题目，并共同应用关键词和难度，不受当前分类和页码限制。
    const QJsonArray filtered = filteredQuestionObjects(false);
    const QSignalBlocker blocker(questionTypeTabs);
    for (int index = 0; index < questionTypeTabs->count(); ++index) {
        const QString category = questionTypeTabs->tabData(index).toString();
        int count = 0;
        for (const QJsonValue &value : filtered) {
            if (questionMatchesTypeCategory(value.toObject().value(QStringLiteral("type")).toString(), category)) ++count;
        }
        const QString title = category == QStringLiteral("全部题型") ? QStringLiteral("全部题目") : category;
        questionTypeTabs->setTabText(index, QStringLiteral("%1 %2").arg(title).arg(count));
    }
}

QWidget *MainWindow::createPublishPage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto *formCard = card();
    formCard->setObjectName(QStringLiteral("sidePanelCard"));
    formCard->setFixedWidth(420);
    formCard->layout()->addWidget(sectionTitle(QStringLiteral("试卷管理"), QStringLiteral("先创建未发布试卷，再在考试列表中发布给学生。")));
    auto *form = new QFormLayout;
    publishNameEdit = new QLineEdit(QStringLiteral("C++ 程序设计考试"));
    publishCourseEdit = new QLineEdit(QStringLiteral("C++ 程序设计"));
    publishStartEdit = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(-300));
    publishStartEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    publishStartEdit->setCalendarPopup(true);
    publishEndEdit = new QDateTimeEdit(QDateTime::currentDateTime().addDays(7));
    publishEndEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    publishEndEdit->setCalendarPopup(true);
    publishDurationSpin = new QSpinBox;
    publishDurationSpin->setRange(1, 600);
    publishDurationSpin->setValue(90);
    publishMaxSubmissionsSpin = new QSpinBox;
    publishMaxSubmissionsSpin->setRange(1, 5);
    publishMaxSubmissionsSpin->setValue(1);
    form->addRow(QStringLiteral("考试名称"), publishNameEdit);
    form->addRow(QStringLiteral("课程"), publishCourseEdit);
    form->addRow(QStringLiteral("开始时间"), publishStartEdit);
    form->addRow(QStringLiteral("截止时间"), publishEndEdit);
    form->addRow(QStringLiteral("考试时长（分钟）"), publishDurationSpin);
    form->addRow(QStringLiteral("提交次数"), publishMaxSubmissionsSpin);
    formCard->layout()->addItem(form);
    auto *createExamButton = new QPushButton(QStringLiteral("创建试卷"));
    createExamButton->setObjectName(QStringLiteral("primaryButton"));
    auto *importQuestionBankButton = new QPushButton(QStringLiteral("导入题库"));
    importQuestionBankButton->setObjectName(QStringLiteral("primaryButton"));
    auto *importExamButton = new QPushButton(QStringLiteral("导入整套试卷"));
    importExamButton->setObjectName(QStringLiteral("primaryButton"));
    publishSelectedExamButton = new QPushButton(QStringLiteral("发布选中试卷"));
    publishSelectedExamButton->setObjectName(QStringLiteral("primaryButton"));  // 发布是主操作 → 蓝色主按钮
    publishSelectedExamButton->setEnabled(false);
    auto *refreshAllButton = new QPushButton(QStringLiteral("刷新列表"));
    auto *closeExamButton = new QPushButton(QStringLiteral("关闭选中考试"));
    closeExamButton->setObjectName(QStringLiteral("dangerButton"));
    auto *deleteExamButton = new QPushButton(QStringLiteral("删除选中考试"));
    deleteExamButton->setObjectName(QStringLiteral("dangerButton"));
    auto *createGroup = new QGroupBox(QStringLiteral("创建 / 导入"));
    auto *createGroupLayout = new QVBoxLayout(createGroup);
    createGroupLayout->setContentsMargins(12, 12, 12, 12);
    createGroupLayout->setSpacing(10);
    createGroupLayout->addWidget(createExamButton);
    createGroupLayout->addWidget(importQuestionBankButton);
    createGroupLayout->addWidget(importExamButton);
    auto *manageGroup = new QGroupBox(QStringLiteral("管理选中考试"));
    auto *manageGroupLayout = new QVBoxLayout(manageGroup);
    manageGroupLayout->setContentsMargins(12, 12, 12, 12);
    manageGroupLayout->setSpacing(10);
    manageGroupLayout->addWidget(publishSelectedExamButton);
    manageGroupLayout->addWidget(closeExamButton);
    manageGroupLayout->addWidget(deleteExamButton);
    formCard->layout()->addWidget(createGroup);
    formCard->layout()->addWidget(manageGroup);
    formCard->layout()->addWidget(refreshAllButton);
    formCard->layout()->addWidget(mutedLabel(QStringLiteral("未发布考试学生端不可见；发布成功后，学生端刷新即可看到考试入口。")));
    qobject_cast<QVBoxLayout *>(formCard->layout())->addStretch();
    layout->addWidget(formCard);

    auto *questionCard = card();
    questionCard->layout()->addWidget(sectionTitle(QStringLiteral("选择试卷题目"),
                                                   QStringLiteral("手动组卷时在这里勾选题目；导入整套试卷会自动组卷，不需要逐题勾选。")));
    publishQuestionTable = new QTableWidget(0, 6);
    publishQuestionTable->setHorizontalHeaderLabels({QStringLiteral("选择"), QStringLiteral("ID"), QStringLiteral("题型"), QStringLiteral("课程"), QStringLiteral("题干"), QStringLiteral("分值")});
    publishQuestionTable->horizontalHeader()->setStretchLastSection(true);
    publishQuestionTable->verticalHeader()->setVisible(false);
    publishQuestionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    polishTable(publishQuestionTable);
    qobject_cast<QVBoxLayout *>(questionCard->layout())->addWidget(publishQuestionTable, 2);
    questionCard->layout()->addWidget(sectionTitle(QStringLiteral("考试列表"), QStringLiteral("未发布考试可点击发布；已发布考试不会重复生成学生任务。")));
    examManagementTable = new QTableWidget(0, 9);
    examManagementTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("考试名称"), QStringLiteral("课程"), QStringLiteral("发布状态"), QStringLiteral("考试状态"), QStringLiteral("开始时间"), QStringLiteral("截止时间"), QStringLiteral("时长"), QStringLiteral("操作")});
    examManagementTable->setColumnHidden(0, true);
    examManagementTable->horizontalHeader()->setStretchLastSection(true);
    examManagementTable->verticalHeader()->setVisible(false);
    examManagementTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    examManagementTable->setSelectionMode(QAbstractItemView::SingleSelection);
    examManagementTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    examManagementTable->setMinimumHeight(160);
    examManagementTable->setMaximumHeight(320);
    polishTable(examManagementTable);
    qobject_cast<QVBoxLayout *>(questionCard->layout())->addWidget(examManagementTable, 1);
    layout->addWidget(questionCard, 1);

    bindTeacherButton(createExamButton, QStringLiteral("btn_teacher_create_exam"), QStringLiteral("teacher_exams"), QStringLiteral("create_exam"), [this] { createExamDraft(); });
    bindTeacherButton(importQuestionBankButton, QStringLiteral("btn_teacher_import_question_bank"), QStringLiteral("teacher_exams"), QStringLiteral("import_question_bank"), [this] { importQuestions(); });
    bindTeacherButton(importExamButton, QStringLiteral("btn_teacher_import_full_exam"), QStringLiteral("teacher_exams"), QStringLiteral("import_full_exam"), [this] { importExamFromFile(); });
    bindTeacherButton(publishSelectedExamButton, QStringLiteral("btn_teacher_publish_exam"), QStringLiteral("teacher_exams"), QStringLiteral("publish_exam"), [this] { publishSelectedExam(); });
    bindTeacherButton(refreshAllButton, QStringLiteral("btn_teacher_refresh_exam_list"), QStringLiteral("teacher_exams"), QStringLiteral("refresh_exam_list"), [this] {
        refreshQuestionBank();
        refreshExamManagementList();
    });
    bindTeacherButton(closeExamButton, QStringLiteral("btn_teacher_close_exam"), QStringLiteral("teacher_exams"), QStringLiteral("close_exam"), [this] { closeSelectedExam(); });
    bindTeacherButton(deleteExamButton, QStringLiteral("btn_teacher_delete_exam"), QStringLiteral("teacher_exams"), QStringLiteral("delete_exam"), [this] { deleteSelectedExam(); });
    // 阶段2B：仅教师端把试卷管理关键按钮接入注册表（导入题库/关闭考试不在本批，跳过；handler 复用原业务函数）。
    if (currentUser.role == UserRole::Teacher) {
        registerButtonAction(createExamButton, QStringLiteral("teacher_exams"), QStringLiteral("create_exam"), [this] { createExamDraft(); });
        registerButtonAction(importExamButton, QStringLiteral("teacher_exams"), QStringLiteral("import_full_exam"), [this] { importExamFromFile(); });
        registerButtonAction(refreshAllButton, QStringLiteral("teacher_exams"), QStringLiteral("refresh_exam_list"), [this] { refreshQuestionBank(); refreshExamManagementList(); });
        registerButtonAction(publishSelectedExamButton, QStringLiteral("teacher_exams"), QStringLiteral("publish_exam"), [this] { publishSelectedExam(); });
        registerButtonAction(deleteExamButton, QStringLiteral("teacher_exams"), QStringLiteral("delete_exam"), [this] { deleteSelectedExam(); });
    }
    connect(examManagementTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updatePublishExamButton);
    connect(examManagementTable, &QTableWidget::cellClicked, this, [](int row, int col) {
        qInfo("[TABLE_CLICK] page=teacher_exams row=%d col=%d", row, col);
    });
    connect(examManagementTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        qInfo("[TABLE_DOUBLE_CLICK] page=teacher_exams row=%d col=%d", row, col);
        if (examManagementTable) {
            examManagementTable->selectRow(row);
        }
        updatePublishExamButton();
    });
    return page;
}

QWidget *MainWindow::createStatsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto *statsCard = card();
    statsCard->setObjectName(QStringLiteral("activityCard"));
    statsCard->layout()->addWidget(sectionTitle(QStringLiteral("成绩管理"), QStringLiteral("按考试、答卷和批阅状态实时汇总。")));
    statsLabel = mutedLabel(QStringLiteral("正在读取统计信息..."));
    auto *refreshButton = new QPushButton(QStringLiteral("刷新统计"));
    refreshButton->setObjectName(QStringLiteral("primaryButton"));
    statsCard->layout()->addWidget(statsLabel);
    // 学生端：刷新按钮留在成绩卡内（学生没有下方工具条）。教师/管理员的刷新按钮移到下方独立工具条。
    if (currentUser.role == UserRole::Student) {
        statsCard->layout()->addWidget(refreshButton);
    }
    layout->addWidget(statsCard);

    // P1 步骤3（G3）：教师/管理员「成绩管理」顶部工具条（刷新统计 / 导出成绩 CSV / 查看选中详情）。
    // 关键修复：用独立 QWidget + 构造函数设定的 QHBoxLayout，再 layout->addWidget() 直接挂到页面主布局——
    // 与各卡片同一种已验证可见的加入方式。上一版用 statsCard->layout()->addItem(子布局)，在麒麟上整行未渲染、
    // 连「刷新统计」一起消失（卡片高度把内嵌子布局裁掉），故彻底改为独立工具条。位置：成绩说明卡之后、监考看板之前。
    if (currentUser.role == UserRole::Teacher || currentUser.role == UserRole::Admin) {
        auto *scoresToolbar = new QWidget;
        auto *toolbarRow = new QHBoxLayout(scoresToolbar);  // 布局随构造直接绑定到 widget，确保被渲染
        toolbarRow->setContentsMargins(0, 0, 0, 0);
        toolbarRow->setSpacing(10);
        toolbarRow->addWidget(refreshButton);  // 刷新统计（信号在本函数末尾按角色统一 bind/register）

        auto *exportScoresButton = new QPushButton(QStringLiteral("导出成绩 CSV"));
        exportScoresButton->setObjectName(QStringLiteral("primaryButton"));
        exportScoresButton->setToolTip(QStringLiteral("将当前风险学生列表导出为 CSV（UTF-8 带 BOM，Excel/WPS 可直接打开）。"));
        toolbarRow->addWidget(exportScoresButton);

        auto *viewSelectedDetailButton = new QPushButton(QStringLiteral("查看选中详情"));
        viewSelectedDetailButton->setObjectName(QStringLiteral("primaryButton"));
        viewSelectedDetailButton->setToolTip(QStringLiteral("先在下方风险学生列表选中一行，再点此查看该学生异常详情（不依赖双击）。"));
        toolbarRow->addWidget(viewSelectedDetailButton);
        toolbarRow->addStretch();
        layout->addWidget(scoresToolbar);

        // 导出仍导出教师/管理员可见的 riskRecordTable（点击时它已在下方监考看板块构建完成）。
        std::function<void()> exportScoresHandler = [this] {
            const QString fileName = QStringLiteral("scores_%1.csv")
                                         .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd")));
            exportTableToCsv(riskRecordTable, fileName, QStringLiteral("导出成绩 CSV"));
        };
        std::function<void()> viewDetailHandler = [this] { showSelectedRiskRecordDetail(); };
        // 双通路：bindTeacherButton(clicked/Windows) + registerButtonAction(eventFilter/麒麟，Teacher 守卫)。
        bindTeacherButton(exportScoresButton, QStringLiteral("btn_teacher_scores_export_csv"),
                          QStringLiteral("teacher_scores"), QStringLiteral("export_scores_csv"), exportScoresHandler);
        bindTeacherButton(viewSelectedDetailButton, QStringLiteral("btn_teacher_scores_view_selected_detail"),
                          QStringLiteral("teacher_scores"), QStringLiteral("view_selected_risk_detail"), viewDetailHandler);
        if (currentUser.role == UserRole::Teacher) {
            registerButtonAction(exportScoresButton, QStringLiteral("teacher_scores"),
                                 QStringLiteral("export_scores_csv"), exportScoresHandler);
            registerButtonAction(viewSelectedDetailButton, QStringLiteral("teacher_scores"),
                                 QStringLiteral("view_selected_risk_detail"), viewDetailHandler);
        }
        // 可见性诊断：能在日志看到这行=工具条已构建并加入页面（避免再次“编进去了但看不见”）。
        qInfo("[CSV_EXPORT_UI_READY] role=%s page=teacher_scores buttons=refresh_scores_all,export_scores_csv,view_selected_risk_detail",
              currentUser.role == UserRole::Teacher ? "teacher" : "admin");
    }

    if (currentUser.role == UserRole::Student) {
        auto *submissionCard = card();
        submissionCard->setObjectName(QStringLiteral("paperCard"));
        submissionCard->layout()->addWidget(sectionTitle(QStringLiteral("我的考试成绩"),
                                                         QStringLiteral("查看已提交答卷和批阅结果，未批阅完成的答卷暂不开放明细。")));
        mySubmissionTable = new QTableWidget(0, 8);
        setupMySubmissionTable(mySubmissionTable);
        submissionCard->layout()->addWidget(mySubmissionTable);

        auto *detailButton = new QPushButton(QStringLiteral("查看详情"));
        detailButton->setObjectName(QStringLiteral("primaryButton"));
        detailButton->setProperty("submissionDetailButton", true);
        detailButton->setEnabled(false);
        detailButton->setToolTip(QStringLiteral("选中一条已批阅答卷后查看每题得分。"));
        auto *detailButtonRow = new QHBoxLayout;
        detailButtonRow->addStretch();
        detailButtonRow->addWidget(detailButton);
        submissionCard->layout()->addItem(detailButtonRow);
        layout->addWidget(submissionCard, 1);

        connect(mySubmissionTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, [detailButton, table = mySubmissionTable] {
            detailButton->setEnabled(table && table->currentRow() >= 0 && table->item(table->currentRow(), 0));
        });
        connect(detailButton, &QAbstractButton::clicked, this, [this, table = mySubmissionTable] {
            qInfo("[UI_CLICK] page=student_exam_records button=view_submission_detail");
            if (!table || table->currentRow() < 0 || !table->item(table->currentRow(), 0)) {
                QMessageBox::warning(this, QStringLiteral("未选择答卷"), QStringLiteral("请先选择一条成绩记录。"));
                return;
            }

            const int row = table->currentRow();
            const int submissionId = table->item(row, 0)->data(Qt::UserRole).toInt();
            const QString status = table->item(row, 0)->data(Qt::UserRole + 1).toString();
            if (status != QStringLiteral("graded")) {
                QMessageBox::information(this, QStringLiteral("暂不能查看"), QStringLiteral("该答卷暂未批阅完成"));
                return;
            }

            QJsonObject request;
            request.insert(QStringLiteral("type"), QStringLiteral("get_submission_detail"));
            request.insert(QStringLiteral("submissionId"), submissionId);
            QJsonObject response;
            QString error;
            if (!ApiClient::instance().request(request, &response, &error)) {
                showError(QStringLiteral("答卷详情读取失败"), error);
                return;
            }

            const QJsonObject submission = response.value(QStringLiteral("submission")).toObject();
            if (submission.value(QStringLiteral("status")).toString() != QStringLiteral("graded")) {
                QMessageBox::information(this, QStringLiteral("暂不能查看"), QStringLiteral("该答卷暂未批阅完成"));
                return;
            }

            QDialog dialog(this);
            dialog.setWindowTitle(QStringLiteral("成绩详情"));
            dialog.resize(820, 480);
            auto *dialogLayout = new QVBoxLayout(&dialog);
            dialogLayout->setContentsMargins(18, 18, 18, 18);
            dialogLayout->setSpacing(12);
            dialogLayout->addWidget(sectionTitle(QStringLiteral("答卷明细"),
                                                 QStringLiteral("%1 · %2 · 总分 %3")
                                                     .arg(submission.value(QStringLiteral("examName")).toString(),
                                                          submission.value(QStringLiteral("course")).toString(),
                                                          valueString(submission, QStringLiteral("totalScore")))));

            const QJsonArray answers = response.value(QStringLiteral("answers")).toArray();
            auto *detailTable = new QTableWidget(answers.size(), 5);
            detailTable->setHorizontalHeaderLabels({QStringLiteral("题号"),
                                                    QStringLiteral("题干前 30 字"),
                                                    QStringLiteral("满分"),
                                                    QStringLiteral("得分"),
                                                    QStringLiteral("老师评语")});
            detailTable->verticalHeader()->setVisible(false);
            detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
            detailTable->setSelectionBehavior(QAbstractItemView::SelectRows);
            detailTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
            detailTable->horizontalHeader()->setStretchLastSection(true);
            detailTable->setColumnWidth(0, 64);
            detailTable->setColumnWidth(2, 72);
            detailTable->setColumnWidth(3, 72);
            polishTable(detailTable);
            for (int i = 0; i < answers.size(); ++i) {
                const QJsonObject answer = answers.at(i).toObject();
                QString content = answer.value(QStringLiteral("content")).toString().simplified();
                if (content.size() > 30) {
                    content = content.left(30) + QStringLiteral("...");
                }
                const int questionIndex = answer.value(QStringLiteral("questionIndex")).toInt(i + 1);
                detailTable->setItem(i, 0, readOnlyItem(QString::number(questionIndex)));
                detailTable->setItem(i, 1, readOnlyItem(content));
                detailTable->setItem(i, 2, readOnlyItem(valueString(answer, QStringLiteral("maxScore"))));
                detailTable->setItem(i, 3, readOnlyItem(valueString(answer, QStringLiteral("score"))));
                detailTable->setItem(i, 4, readOnlyItem(answer.value(QStringLiteral("comment")).toString()));
            }
            dialogLayout->addWidget(detailTable, 1);

            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
            buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
            dialogLayout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            dialog.exec();
        });
    }

    if (currentUser.role == UserRole::Teacher || currentUser.role == UserRole::Admin) {
        // ===== Stage 3 实时监考看板 =====
        auto *boardCard = card();
        boardCard->setObjectName(QStringLiteral("activityCard"));
        auto *boardHeader = new QHBoxLayout;
        boardHeader->addWidget(sectionTitle(QStringLiteral("实时监考看板"),
                                            QStringLiteral("汇总各考试监考异常，仅作人工复核线索，不自动判定作弊。默认每 10 秒自动刷新。")), 1);
        auto *filterLabel = new QLabel(QStringLiteral("筛选考试："));
        filterLabel->setObjectName(QStringLiteral("fieldLabel"));
        riskExamFilter = new QComboBox;
        riskExamFilter->setMinimumWidth(220);
        riskExamFilter->setToolTip(QStringLiteral("选择某场考试后，看板只统计该场考试；默认「全部考试」汇总所有场次。"));
        riskExamFilter->addItem(QStringLiteral("全部考试"), 0);
        auto *refreshBoardButton = new QPushButton(QStringLiteral("刷新监考数据"));
        refreshBoardButton->setObjectName(QStringLiteral("primaryButton"));
        boardHeader->addWidget(filterLabel, 0, Qt::AlignVCenter);
        boardHeader->addWidget(riskExamFilter, 0, Qt::AlignVCenter);
        boardHeader->addWidget(refreshBoardButton, 0, Qt::AlignVCenter);
        boardCard->layout()->addItem(boardHeader);

        riskMetricLabels.clear();
        auto *metricRow = new QHBoxLayout;
        metricRow->setSpacing(14);
        const QList<QPair<QString, QString>> boardMetrics = {
            {QStringLiteral("异常学生数"), QStringLiteral("blue")},
            {QStringLiteral("异常事件总数"), QStringLiteral("cyan")},
            {QStringLiteral("中风险人数"), QStringLiteral("amber")},
            {QStringLiteral("高风险人数"), QStringLiteral("red")},
            {QStringLiteral("严重风险人数"), QStringLiteral("crimson")}
        };
        for (const auto &metric : boardMetrics) {
            auto *item = metricCard(metric.first, QStringLiteral("0"), metric.second);
            riskMetricLabels.append(metricValueLabel(item));
            metricRow->addWidget(item, 1);
        }
        boardCard->layout()->addItem(metricRow);
        riskBoardHintLabel = mutedLabel(QStringLiteral(
            "图例：作弊风险（红）· 演示留痕（灰，模拟模式/模拟核验，0 分）· 设备/网络（蓝，摄像头缺失/启动失败/网络异常）。双击学生行查看异常时间轴。"));
        boardCard->layout()->addWidget(riskBoardHintLabel);
        layout->addWidget(boardCard);

        // 风险学生列表
        auto *riskCard = card();
        riskCard->setObjectName(QStringLiteral("paperCard"));
        riskCard->layout()->addWidget(sectionTitle(QStringLiteral("风险学生列表"),
                                                   QStringLiteral("按风险分从高到低排列。双击一行查看该生异常事件时间轴。")));
        riskRecordTable = new QTableWidget(0, 8);
        riskRecordTable->setHorizontalHeaderLabels({QStringLiteral("学生姓名"),
                                                    QStringLiteral("学生账号"),
                                                    QStringLiteral("考试名称"),
                                                    QStringLiteral("风险总分"),
                                                    QStringLiteral("风险等级"),
                                                    QStringLiteral("异常次数"),
                                                    QStringLiteral("最近异常时间"),
                                                    QStringLiteral("操作")});
        riskRecordTable->horizontalHeader()->setStretchLastSection(true);
        riskRecordTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        riskRecordTable->verticalHeader()->setVisible(false);
        riskRecordTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        riskRecordTable->setSelectionMode(QAbstractItemView::SingleSelection);
        riskRecordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        polishTable(riskRecordTable);
        riskCard->layout()->addWidget(riskRecordTable);
        layout->addWidget(riskCard, 1);
        auto *scoreDetailButton = new QPushButton(QStringLiteral("查看详情"));
        scoreDetailButton->setObjectName(QStringLiteral("primaryButton"));
        scoreDetailButton->setEnabled(false);
        auto *scoreActionRow = new QHBoxLayout;
        scoreActionRow->addStretch();
        scoreActionRow->addWidget(scoreDetailButton);
        riskCard->layout()->addItem(scoreActionRow);
        connect(riskRecordTable, &QTableWidget::cellClicked, this, [scoreDetailButton](int row, int col) {
            qInfo("[TABLE_CLICK] page=teacher_scores row=%d col=%d", row, col);
            if (scoreDetailButton) {
                scoreDetailButton->setEnabled(row >= 0);
            }
        });
        connect(riskRecordTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
            qInfo("[TABLE_DOUBLE_CLICK] page=teacher_scores row=%d col=%d", row, col);
            showRiskLogsForRow(row, col);
        });
        bindTeacherButton(scoreDetailButton, QStringLiteral("btn_teacher_score_detail"), QStringLiteral("teacher_scores"), QStringLiteral("view_detail"), [this] {
            const int row = riskRecordTable ? riskRecordTable->currentRow() : -1;
            showRiskLogsForRow(row, 0);
        });

        // 异常类型统计
        auto *eventCard = card();
        eventCard->setObjectName(QStringLiteral("paperCard"));
        eventCard->layout()->addWidget(sectionTitle(QStringLiteral("异常类型统计"),
                                                    QStringLiteral("按事件类型汇总全部考试的发生次数与累计风险分。")));
        eventTypeStatTable = new QTableWidget(0, 5);
        eventTypeStatTable->setHorizontalHeaderLabels({QStringLiteral("异常类型"),
                                                       QStringLiteral("中文名称"),
                                                       QStringLiteral("类别"),
                                                       QStringLiteral("次数"),
                                                       QStringLiteral("累计风险分")});
        eventTypeStatTable->horizontalHeader()->setStretchLastSection(true);
        eventTypeStatTable->verticalHeader()->setVisible(false);
        eventTypeStatTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        eventTypeStatTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        eventTypeStatTable->setFixedHeight(220);
        polishTable(eventTypeStatTable);
        eventCard->layout()->addWidget(eventTypeStatTable);
        layout->addWidget(eventCard);

        // 切换筛选考试：仅重新过滤现有数据，保留下拉框选择。
        connect(riskExamFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            refreshRiskRecords();
        });
        // 手动刷新：先重建考试下拉（拾取新考试，保留当前选择），再刷新数据。
        bindTeacherButton(refreshBoardButton, QStringLiteral("btn_teacher_risk_board_refresh"), QStringLiteral("teacher_scores"), QStringLiteral("refresh_risk_board"), [this] {
            refreshRiskExamFilter();
            refreshRiskRecords();
        });
        // 阶段2B：仅教师端把监考看板的“查看详情/刷新监考数据”接入注册表（handler 复用原业务函数）。
        // 看板刷新命名为 refresh_risk_board，与成绩页主刷新 refresh_scores_all 区分，日志可定位是哪颗按钮。
        if (currentUser.role == UserRole::Teacher) {
            registerButtonAction(scoreDetailButton, QStringLiteral("teacher_scores"), QStringLiteral("view_detail"), [this] {
                const int row = riskRecordTable ? riskRecordTable->currentRow() : -1;
                showRiskLogsForRow(row, 0);
            });
            registerButtonAction(refreshBoardButton, QStringLiteral("teacher_scores"), QStringLiteral("refresh_risk_board"), [this] {
                refreshRiskExamFilter();
                refreshRiskRecords();
            });
        }

        // 每 10 秒自动刷新，仅在监考看板可见时请求，避免无意义流量。
        // 自动刷新只重新拉取/过滤数据，不动下拉框，因此当前筛选条件始终保留。
        riskRefreshTimer = new QTimer(this);
        riskRefreshTimer->setInterval(10000);
        connect(riskRefreshTimer, &QTimer::timeout, this, [this] {
            if (riskRecordTable && riskRecordTable->isVisible()) {
                refreshRiskRecords();
            }
        });
        riskRefreshTimer->start();
    }
    layout->addStretch();

    if (currentUser.role == UserRole::Teacher || currentUser.role == UserRole::Admin) {
        bindTeacherButton(refreshButton, QStringLiteral("btn_teacher_scores_refresh_all"), QStringLiteral("teacher_scores"), QStringLiteral("refresh_scores_all"), [this] {
            loadStats();
            refreshMySubmissions();
            refreshRiskRecords();
        });
        // 阶段2B：仅教师端把成绩页主刷新接入注册表（管理员不在本批，保持原生 clicked）。
        if (currentUser.role == UserRole::Teacher) {
            registerButtonAction(refreshButton, QStringLiteral("teacher_scores"), QStringLiteral("refresh_scores_all"), [this] {
                loadStats();
                refreshMySubmissions();
                refreshRiskRecords();
            });
        }
    } else {
        connect(refreshButton, &QAbstractButton::clicked, this, [this] {
            qInfo("[UI_CLICK] page=student_exam_records button=refresh_records");
            loadStats();
            refreshMySubmissions();
        });
    }
    return page;
}

// P1 步骤3（G3 报表/导出）：把一个 QTableWidget 的当前可见数据写成 CSV 文件。
// 关键点：①UTF-8 带 BOM（Excel/WPS 据此识别编码，否则中文乱码）；②逗号/双引号/换行
// 按 RFC4180 转义；③只导可见列、跳过隐藏列；④用户取消不算错误。供成绩页「导出成绩 CSV」复用。
bool MainWindow::simpleHttpPostJsonViaSocket(const QUrl &url,
                                             const QByteArray &jsonBody,
                                             QByteArray *responseBody,
                                             QString *errorMessage)
{
    QString failReason;
    const QByteArray body = ::simpleHttpPostJsonViaSocket(url, jsonBody, 300000, errorMessage, &failReason);
    if (body.isEmpty() && errorMessage && !errorMessage->isEmpty()) {
        return false;
    }
    if (responseBody) {
        *responseBody = body;
    }
    return true;
}

QByteArray MainWindow::tableToCsvBytes(QTableWidget *table) const
{
    const auto escape = [](const QString &field) -> QString {
        if (field.contains(QLatin1Char(',')) || field.contains(QLatin1Char('"'))
            || field.contains(QLatin1Char('\n')) || field.contains(QLatin1Char('\r'))) {
            QString quoted = field;
            quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            return QLatin1Char('"') + quoted + QLatin1Char('"');
        }
        return field;
    };

    QString csv;
    QList<int> columns;
    if (table) {
        for (int c = 0; c < table->columnCount(); ++c) {
            if (!table->isColumnHidden(c)) {
                columns.append(c);
            }
        }
    }

    QStringList headerCells;
    for (int c : columns) {
        QTableWidgetItem *header = table->horizontalHeaderItem(c);
        headerCells << escape(header ? header->text() : QString());
    }
    csv += headerCells.join(QLatin1Char(','));
    csv += QStringLiteral("\r\n");

    if (table) {
        for (int r = 0; r < table->rowCount(); ++r) {
            if (table->isRowHidden(r)) {
                continue;
            }
            QStringList rowCells;
            for (int c : columns) {
                QTableWidgetItem *item = table->item(r, c);
                rowCells << escape(item ? item->text() : QString());
            }
            csv += rowCells.join(QLatin1Char(','));
            csv += QStringLiteral("\r\n");
        }
    }

    QByteArray bytes("\xEF\xBB\xBF", 3);
    bytes += csv.toUtf8();
    return bytes;
}

bool MainWindow::exportCsvViaWindowsHostPicker(const QByteArray &csvBytes,
                                               const QString &defaultFileName,
                                               QString *savedPath,
                                               QString *errorMessage,
                                               bool *cancelled)
{
    if (savedPath) {
        savedPath->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    if (cancelled) {
        *cancelled = false;
    }

    const QString pickerUrl = qEnvironmentVariable("QTEXAM_HOST_FILE_PICKER_URL").trimmed();
    qInfo("[CSV_EXPORT_HOST_TRY] bytes=%lld envSet=%d",
          static_cast<long long>(csvBytes.size()), pickerUrl.isEmpty() ? 0 : 1);
    if (pickerUrl.isEmpty()) {
        const QString message = QStringLiteral("环境变量 QTEXAM_HOST_FILE_PICKER_URL 未设置");
        if (errorMessage) {
            *errorMessage = message;
        }
        qWarning("[CSV_EXPORT_HOST_ERROR] reason=env-empty");
        return false;
    }

    QUrl saveUrl(pickerUrl);
    if (!saveUrl.isValid() || saveUrl.host().isEmpty()) {
        const QString message = QStringLiteral("Windows 导出代理地址无效：%1").arg(pickerUrl);
        if (errorMessage) {
            *errorMessage = message;
        }
        qWarning("[CSV_EXPORT_HOST_ERROR] reason=invalid-url url=%s", qUtf8Printable(pickerUrl));
        return false;
    }
    saveUrl.setPath(QStringLiteral("/save-file"));
    saveUrl.setQuery(QString());
    saveUrl.setFragment(QString());

    QJsonObject request;
    request.insert(QStringLiteral("suggestedName"), defaultFileName);
    request.insert(QStringLiteral("filter"), QStringLiteral("CSV 文件 (*.csv);;所有文件 (*.*)"));
    request.insert(QStringLiteral("contentBase64"), QString::fromLatin1(csvBytes.toBase64()));

    QByteArray responseBody;
    QString httpError;
    if (!simpleHttpPostJsonViaSocket(saveUrl,
                                     QJsonDocument(request).toJson(QJsonDocument::Compact),
                                     &responseBody,
                                     &httpError)) {
        if (errorMessage) {
            *errorMessage = httpError;
        }
        qWarning("[CSV_EXPORT_HOST_ERROR] reason=request-failed detail=%s", qUtf8Printable(httpError));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument responseDoc = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !responseDoc.isObject()) {
        const QString message = QStringLiteral("Windows 导出代理返回的数据无法解析：%1").arg(parseError.errorString());
        if (errorMessage) {
            *errorMessage = message;
        }
        qWarning("[CSV_EXPORT_HOST_ERROR] reason=invalid-json detail=%s", qUtf8Printable(parseError.errorString()));
        return false;
    }

    const QJsonObject response = responseDoc.object();
    if (response.value(QStringLiteral("ok")).toBool(false)) {
        const QString path = response.value(QStringLiteral("path")).toString();
        if (savedPath) {
            *savedPath = path;
        }
        qInfo("[CSV_EXPORT_HOST_DONE] path=%s bytes=%lld",
              qUtf8Printable(path), static_cast<long long>(csvBytes.size()));
        return true;
    }

    if (response.value(QStringLiteral("cancelled")).toBool(false)) {
        if (cancelled) {
            *cancelled = true;
        }
        qInfo("[CSV_EXPORT_HOST_CANCEL]");
        return false;
    }

    const QString message = response.value(QStringLiteral("error")).toString(
        response.value(QStringLiteral("reason")).toString(QStringLiteral("Windows 导出代理保存失败")));
    if (errorMessage) {
        *errorMessage = message;
    }
    qWarning("[CSV_EXPORT_HOST_ERROR] reason=response-not-ok detail=%s", qUtf8Printable(message));
    return false;
}

bool MainWindow::exportTableToCsv(QTableWidget *table,
                                  const QString &defaultFileName,
                                  const QString &title)
{
    if (!table) {
        qWarning("[CSV_EXPORT_ERROR] reason=null-table title=%s", qUtf8Printable(title));
        return false;
    }
    qInfo("[CSV_EXPORT_START] title=%s rows=%d cols=%d",
          qUtf8Printable(title), table->rowCount(), table->columnCount());
    if (table->rowCount() == 0) {
        QMessageBox::information(this, title, QStringLiteral("当前没有可导出的数据。"));
        qInfo("[CSV_EXPORT_CANCEL] reason=empty-table");
        return false;
    }

    QList<int> columns;
    for (int c = 0; c < table->columnCount(); ++c) {
        if (!table->isColumnHidden(c)) {
            columns.append(c);
        }
    }
    int visibleRows = 0;
    for (int r = 0; r < table->rowCount(); ++r) {
        if (!table->isRowHidden(r)) {
            ++visibleRows;
        }
    }

    const QByteArray csvBytes = tableToCsvBytes(table);
    QString windowsPath;
    QString hostError;
    bool hostCancelled = false;
    if (exportCsvViaWindowsHostPicker(csvBytes, defaultFileName, &windowsPath, &hostError, &hostCancelled)) {
        QMessageBox::information(this, title,
                                 QStringLiteral("已导出到 Windows：\n%1").arg(windowsPath));
        qInfo("[CSV_EXPORT_DONE] target=windows rows=%d cols=%d path=%s",
              visibleRows, static_cast<int>(columns.size()), qUtf8Printable(windowsPath));
        return true;
    }
    if (hostCancelled) {
        qInfo("[CSV_EXPORT_CANCEL] reason=host-cancel");
        return false;
    }

    qInfo("[CSV_EXPORT_LOCAL_FALLBACK] reason=%s", qUtf8Printable(hostError));
    QMessageBox::information(this,
                             title,
                             QStringLiteral("Windows 导出代理不可用，将改用麒麟本地保存。\n\n%1").arg(hostError));

    const QString docsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested = QDir(docsDir.isEmpty() ? QDir::homePath() : docsDir).filePath(defaultFileName);
    QString path = QFileDialog::getSaveFileName(this, title, suggested,
                                                QStringLiteral("CSV 文件 (*.csv);;所有文件 (*.*)"));
    if (path.isEmpty()) {
        qInfo("[CSV_EXPORT_CANCEL] reason=user-cancel-local");
        return false;
    }
    if (!path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".csv");
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, title,
                              QStringLiteral("无法写入文件：\n%1\n\n请确认目标位置可写、文件未被占用。").arg(path));
        qWarning("[CSV_EXPORT_ERROR] reason=open-failed path=%s", qUtf8Printable(path));
        return false;
    }

    const qint64 written = file.write(csvBytes);
    file.close();
    if (written != csvBytes.size()) {
        QMessageBox::critical(this, title,
                              QStringLiteral("文件写入不完整：\n%1").arg(path));
        qWarning("[CSV_EXPORT_ERROR] reason=write-incomplete path=%s written=%lld expected=%lld",
                 qUtf8Printable(path), static_cast<long long>(written), static_cast<long long>(csvBytes.size()));
        return false;
    }

    QMessageBox::information(this, title,
                            QStringLiteral("已导出 %1 行数据到：\n%2").arg(visibleRows).arg(path));
    qInfo("[CSV_EXPORT_DONE] target=local rows=%d cols=%d path=%s",
          visibleRows, static_cast<int>(columns.size()), qUtf8Printable(path));
    return true;
}

QWidget *MainWindow::createUserManagementPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto *toolbarCard = card();
    toolbarCard->setObjectName(QStringLiteral("activityCard"));
    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(sectionTitle(QStringLiteral("用户管理"), QStringLiteral("管理员可查看全部账号，维护姓名备注、重置密码或停用账号。")), 1);
    accountSearchEdit = new QLineEdit;
    accountSearchEdit->setPlaceholderText(QStringLiteral("搜索账号、姓名或备注"));
    accountRoleFilter = new QComboBox;
    accountRoleFilter->addItem(QStringLiteral("全部用户"), QStringLiteral("all"));
    accountRoleFilter->addItem(QStringLiteral("只看管理员"), roleToCode(UserRole::Admin));
    accountRoleFilter->addItem(QStringLiteral("只看教师"), roleToCode(UserRole::Teacher));
    accountRoleFilter->addItem(QStringLiteral("只看学生"), roleToCode(UserRole::Student));
    accountCountLabel = mutedLabel(QStringLiteral("0 个用户"));
    auto *refreshButton = new QPushButton(QStringLiteral("刷新列表"));
    auto *createButton = new QPushButton(QStringLiteral("新增用户"));
    createButton->setObjectName(QStringLiteral("primaryButton"));
    toolbar->addWidget(accountSearchEdit);
    toolbar->addWidget(accountRoleFilter);
    toolbar->addWidget(accountCountLabel);
    toolbar->addWidget(refreshButton);
    toolbar->addWidget(createButton);
    toolbarCard->layout()->addItem(toolbar);
    layout->addWidget(toolbarCard);

    auto *content = new QHBoxLayout;
    content->setSpacing(16);
    auto *tableCard = card();
    tableCard->setObjectName(QStringLiteral("paperCard"));
    accountTable = new QTableWidget(0, 7);
    accountTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("角色"), QStringLiteral("账号"),
                                             QStringLiteral("姓名"), QStringLiteral("说明"), QStringLiteral("创建时间"),
                                             QStringLiteral("状态")});
    accountTable->setColumnHidden(0, true);
    accountTable->horizontalHeader()->setStretchLastSection(true);
    accountTable->verticalHeader()->setVisible(false);
    accountTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    accountTable->setSelectionMode(QAbstractItemView::SingleSelection);
    accountTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    polishTable(accountTable);
    tableCard->layout()->addWidget(accountTable);
    content->addWidget(tableCard, 1);

    auto *formCard = card();
    formCard->setObjectName(QStringLiteral("sidePanelCard"));
    formCard->setFixedWidth(360);
    formCard->layout()->addWidget(sectionTitle(QStringLiteral("账号操作"), QStringLiteral("先点左侧用户，再在这里修改资料、重置密码或停用账号。")));
    accountSelectedLabel = mutedLabel(QStringLiteral("当前未选择账号"));
    accountSelectedLabel->setObjectName(QStringLiteral("adminAccountSelectedLabel"));
    accountDisplayNameEdit = new QLineEdit;
    accountDisplayNameEdit->setObjectName(QStringLiteral("adminAccountDisplayNameEdit"));
    accountDisplayNameEdit->setPlaceholderText(QStringLiteral("选中账号后自动填入显示名"));
    accountNoteEdit = new QLineEdit;
    accountNoteEdit->setObjectName(QStringLiteral("adminAccountNoteEdit"));
    accountNoteEdit->setPlaceholderText(QStringLiteral("选中账号后自动填入说明/备注"));
    newPasswordEdit = new QLineEdit;
    newPasswordEdit->setObjectName(QStringLiteral("adminAccountNewPasswordEdit"));
    newPasswordEdit->setEchoMode(QLineEdit::Password);
    newPasswordEdit->setPlaceholderText(QStringLiteral("输入新密码，至少 6 位"));
    confirmPasswordEdit = new QLineEdit;
    confirmPasswordEdit->setObjectName(QStringLiteral("adminAccountConfirmPasswordEdit"));
    confirmPasswordEdit->setEchoMode(QLineEdit::Password);
    confirmPasswordEdit->setPlaceholderText(QStringLiteral("再输入一遍新密码"));
    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(12);
    form->addRow(QStringLiteral("当前选择"), accountSelectedLabel);
    form->addRow(QStringLiteral("显示名"), accountDisplayNameEdit);
    form->addRow(QStringLiteral("说明/备注"), accountNoteEdit);
    form->addRow(QStringLiteral("新密码"), newPasswordEdit);
    form->addRow(QStringLiteral("确认密码"), confirmPasswordEdit);
    auto *formCardLayout = qobject_cast<QVBoxLayout *>(formCard->layout());
    if (formCardLayout) {
        formCardLayout->addLayout(form);
    } else {
        formCard->layout()->addItem(form);
    }
    auto *applyButton = new QPushButton(QStringLiteral("确定修改"));
    applyButton->setObjectName(QStringLiteral("adminAccountApplyButton"));
    applyButton->setEnabled(true);
    applyButton->setVisible(true);
    auto *deleteButton = new QPushButton(QStringLiteral("停用账号"));
    deleteButton->setObjectName(QStringLiteral("adminAccountDisableButton"));
    deleteButton->setEnabled(true);
    deleteButton->setVisible(true);
    formCard->layout()->addWidget(applyButton);
    formCard->layout()->addWidget(deleteButton);
    formCard->layout()->addWidget(mutedLabel(QStringLiteral("提示：停用账号不会删除历史考试、答卷、成绩和日志；当前登录管理员和最后一个管理员受保护。")));
    qobject_cast<QVBoxLayout *>(formCard->layout())->addStretch();
    content->addWidget(formCard);
    layout->addLayout(content, 1);

    bindTeacherButton(refreshButton, QStringLiteral("btn_admin_accounts_refresh"),
                      QStringLiteral("admin_accounts"), QStringLiteral("refresh_accounts"),
                      [this] { populateAccountTable(); });
    bindTeacherButton(createButton, QStringLiteral("btn_admin_accounts_create"),
                      QStringLiteral("admin_accounts"), QStringLiteral("create_account"),
                      [this] { createUser(); });
    // —— 账号权限页“硬触发”接线（2026-06-22）——
    // 现象：右侧按钮点击后无任何反馈。架构事实：本项目已注册按钮的 MouseButtonRelease 被全局
    // eventFilter 消费并经注册表分发，原生 clicked 通常不会触发。故对两颗可见按钮加三层保险，
    // 全部汇入自带 350ms 去重的 onAdminAccount*Clicked()（首行硬日志+硬弹窗，再接真实逻辑）：
    //   1) PMF clicked（Windows 可靠）
    //   2) registerButtonAction（eventFilter 鼠标释放分发，麒麟可靠 —— 即“eventFilter 兜底”层）
    //   3) legacy SIGNAL/SLOT（PMF 失效时兜底）
    // 三路若同时命中，由槽内去重收敛为一次，不会重复弹窗。
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::onAdminAccountApplyClicked);
    connect(deleteButton, &QPushButton::clicked, this, &MainWindow::onAdminAccountDisableClicked);
    registerButtonAction(applyButton, QStringLiteral("admin_accounts"),
                         QStringLiteral("apply_changes"), [this] { onAdminAccountApplyClicked(); });
    registerButtonAction(deleteButton, QStringLiteral("admin_accounts"),
                         QStringLiteral("disable_account"), [this] { onAdminAccountDisableClicked(); });
    connect(applyButton, SIGNAL(clicked()), this, SLOT(onAdminAccountApplyClicked()));
    connect(deleteButton, SIGNAL(clicked()), this, SLOT(onAdminAccountDisableClicked()));
    qDebug() << "[ADMIN_ACCOUNT_BUTTON_REGISTER]"
             << applyButton->objectName() << applyButton
             << "enabled=" << applyButton->isEnabled()
             << "visible=" << applyButton->isVisible()
             << "parent=" << applyButton->parent();
    qDebug() << "[ADMIN_ACCOUNT_BUTTON_REGISTER]"
             << deleteButton->objectName() << deleteButton
             << "enabled=" << deleteButton->isEnabled()
             << "visible=" << deleteButton->isVisible()
             << "parent=" << deleteButton->parent();
    connect(accountRoleFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::populateAccountTable);
    connect(accountSearchEdit, &QLineEdit::textChanged, this, [this] {
        qInfo("[UI_CLICK] page=admin_accounts button=search_accounts");
        populateAccountTable();
    });
    connect(accountTable, &QTableWidget::currentCellChanged, this, [this](int currentRow, int, int, int) {
        Q_UNUSED(currentRow);
        populateAccountEditorFromSelection();
    });
    connect(accountTable, &QTableWidget::cellClicked, this, [this](int row, int col) {
        qInfo("[TABLE_CLICK] page=admin_accounts row=%d col=%d", row, col);
        populateAccountEditorFromSelection();
    });
    return page;
}

void MainWindow::refreshAdminUserTables()
{
    if (accountTable) {
        populateAccountTable();
    }
    if (studentMgmtTable) {
        adminPopulateRoleUsers(studentMgmtTable, roleToCode(UserRole::Student));
    }
    if (teacherMgmtTable) {
        adminPopulateRoleUsers(teacherMgmtTable, roleToCode(UserRole::Teacher));
    }
}

// 管理员端：按角色维度的只读管理页（学生考试档案 / 教师工作量概览）。
QWidget *MainWindow::createUserAdminPage(const QString &roleCode, const QString &pageTag,
                                        const QString &title, const QString &subtitle,
                                        QTableWidget **tableOut)
{
    const bool isStudent = roleCode == roleToCode(UserRole::Student);
    const QString roleText = roleToText(roleFromCode(roleCode));
    auto *page = new QWidget;
    page->setProperty("selectedUserId", 0);
    page->setProperty("selectedUsername", QString());
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto *titleCard = card();
    titleCard->setObjectName(QStringLiteral("activityCard"));
    titleCard->layout()->addWidget(sectionTitle(title, subtitle));
    layout->addWidget(titleCard);

    auto *content = new QHBoxLayout;
    content->setSpacing(16);

    auto *tableCard = card();
    tableCard->setObjectName(QStringLiteral("paperCard"));
    auto *searchEdit = new QLineEdit;
    searchEdit->setPlaceholderText(QStringLiteral("搜索用户名 / 显示名"));
    tableCard->layout()->addWidget(searchEdit);

    auto *table = new QTableWidget(0, 9);
    if (isStudent) {
        table->setHorizontalHeaderLabels({
            QStringLiteral("ID"), QStringLiteral("用户名"), QStringLiteral("显示名"), QStringLiteral("状态"),
            QStringLiteral("已参加考试数"), QStringLiteral("已提交次数"), QStringLiteral("待批阅次数"),
            QStringLiteral("已出分次数"), QStringLiteral("最近考试时间")
        });
    } else {
        table->setHorizontalHeaderLabels({
            QStringLiteral("ID"), QStringLiteral("用户名"), QStringLiteral("显示名"), QStringLiteral("状态"),
            QStringLiteral("创建试卷数"), QStringLiteral("已发布试卷数"), QStringLiteral("待批阅答卷数"),
            QStringLiteral("已批阅答卷数"), QStringLiteral("最近创建时间")
        });
    }
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setColumnWidth(0, 70);
    table->setColumnWidth(1, 130);
    table->setColumnWidth(2, 120);
    table->setColumnWidth(3, 80);
    table->setColumnWidth(4, 105);
    table->setColumnWidth(5, 105);
    table->setColumnWidth(6, 105);
    table->setColumnWidth(7, 105);
    table->setColumnWidth(8, 150);
    polishTable(table);
    tableCard->layout()->addWidget(table);
    content->addWidget(tableCard, 2);
    if (tableOut) {
        *tableOut = table;
    }

    auto *detailCard = card();
    detailCard->setObjectName(QStringLiteral("activityCard"));
    detailCard->setMinimumWidth(360);
    detailCard->layout()->addWidget(sectionTitle(isStudent ? QStringLiteral("学生档案卡")
                                                           : QStringLiteral("教师工作台"),
                                                 isStudent ? QStringLiteral("选中学生后查看考试参与摘要。")
                                                           : QStringLiteral("选中教师后查看教学工作量摘要。")));
    auto *selectedLabel = mutedLabel(QStringLiteral("当前选择：未选择账号"));
    auto *accountLabel = mutedLabel(QStringLiteral("账号：-"));
    auto *displayNameLabel = mutedLabel(QStringLiteral("显示名：-"));
    auto *statusLabel = mutedLabel(QStringLiteral("状态：-"));
    auto *overviewLabel = mutedLabel(QStringLiteral("概览：暂无数据"));
    auto *recentLabel = mutedLabel(QStringLiteral("最近记录：暂无数据"));
    auto *accountHint = mutedLabel(QStringLiteral("账号资料修改、密码重置、停用账号请到【账号权限】页统一处理。"));
    detailCard->layout()->addWidget(selectedLabel);
    detailCard->layout()->addWidget(accountLabel);
    detailCard->layout()->addWidget(displayNameLabel);
    detailCard->layout()->addWidget(statusLabel);
    detailCard->layout()->addWidget(overviewLabel);
    detailCard->layout()->addWidget(recentLabel);
    detailCard->layout()->addWidget(accountHint);

    auto *refreshButton = new QPushButton(QStringLiteral("刷新列表"));
    auto *detailButton = new QPushButton(isStudent ? QStringLiteral("查看该学生提交明细")
                                                   : QStringLiteral("查看该教师试卷明细"));
    auto *accountButton = new QPushButton(QStringLiteral("前往账号权限页处理账号"));
    accountButton->setObjectName(QStringLiteral("primaryButton"));
    detailCard->layout()->addWidget(refreshButton);
    detailCard->layout()->addWidget(detailButton);
    detailCard->layout()->addWidget(accountButton);
    if (auto *box = qobject_cast<QVBoxLayout *>(detailCard->layout())) {
        box->addStretch();
    }
    content->addWidget(detailCard, 1);
    layout->addLayout(content, 1);

    const auto rowText = [table](int row, int column) -> QString {
        if (!table || row < 0 || row >= table->rowCount()
            || column < 0 || column >= table->columnCount()) {
            return {};
        }
        QTableWidgetItem *item = table->item(row, column);
        return item ? item->text() : QString();
    };
    const auto clearDetail = [page, selectedLabel, accountLabel, displayNameLabel, statusLabel, overviewLabel, recentLabel] {
        page->setProperty("selectedUserId", 0);
        page->setProperty("selectedUsername", QString());
        selectedLabel->setText(QStringLiteral("当前选择：未选择账号"));
        accountLabel->setText(QStringLiteral("账号：-"));
        displayNameLabel->setText(QStringLiteral("显示名：-"));
        statusLabel->setText(QStringLiteral("状态：-"));
        overviewLabel->setText(QStringLiteral("概览：暂无数据"));
        recentLabel->setText(QStringLiteral("最近记录：暂无数据"));
    };
    const auto fillDetail = [page, table, isStudent, pageTag, roleText, selectedLabel, accountLabel,
                             displayNameLabel, statusLabel, overviewLabel, recentLabel, clearDetail](int row) {
        if (row < 0 || !table || row >= table->rowCount() || table->isRowHidden(row)) {
            clearDetail();
            return;
        }
        QTableWidgetItem *idItem = table->item(row, 0);
        bool idOk = false;
        const int userId = idItem ? idItem->text().toInt(&idOk) : 0;
        if (!idOk || userId <= 0) {
            clearDetail();
            return;
        }
        const QString username = table->item(row, 1) ? table->item(row, 1)->text() : QString();
        const QString displayName = table->item(row, 2) ? table->item(row, 2)->text() : QString();
        const QString status = table->item(row, 3) ? table->item(row, 3)->text() : QString();
        page->setProperty("selectedUserId", userId);
        page->setProperty("selectedUsername", username);
        selectedLabel->setText(QStringLiteral("当前选择：%1（%2 / %3）")
                                   .arg(username, roleText, status));
        accountLabel->setText(QStringLiteral("账号：%1").arg(username));
        displayNameLabel->setText(QStringLiteral("显示名：%1").arg(displayName.isEmpty() ? QStringLiteral("-") : displayName));
        statusLabel->setText(QStringLiteral("状态：%1").arg(status));
        if (isStudent) {
            overviewLabel->setText(QStringLiteral("考试参与概览：\n已参加考试数：%1\n已提交次数：%2\n待批阅次数：%3\n已出分次数：%4\n最近一次考试：%5")
                                       .arg(table->item(row, 4) ? table->item(row, 4)->text() : QStringLiteral("-"),
                                            table->item(row, 5) ? table->item(row, 5)->text() : QStringLiteral("-"),
                                            table->item(row, 6) ? table->item(row, 6)->text() : QStringLiteral("-"),
                                            table->item(row, 7) ? table->item(row, 7)->text() : QStringLiteral("-"),
                                            table->item(row, 8) ? table->item(row, 8)->text() : QStringLiteral("-")));
        } else {
            overviewLabel->setText(QStringLiteral("工作量概览：\n创建试卷数：%1\n已发布试卷数：%2\n待批阅答卷数：%3\n已批阅答卷数：%4\n最近创建时间：%5")
                                       .arg(table->item(row, 4) ? table->item(row, 4)->text() : QStringLiteral("-"),
                                            table->item(row, 5) ? table->item(row, 5)->text() : QStringLiteral("-"),
                                            table->item(row, 6) ? table->item(row, 6)->text() : QStringLiteral("-"),
                                            table->item(row, 7) ? table->item(row, 7)->text() : QStringLiteral("-"),
                                            table->item(row, 8) ? table->item(row, 8)->text() : QStringLiteral("-")));
        }
        recentLabel->setText(table->item(row, 0)->data(Qt::UserRole).toString());
        qDebug() << "[ADMIN_ROLE_PAGE_SELECTED]" << pageTag
                 << "userId=" << userId << "username=" << username;
    };
    const auto findRowByUserId = [table](int userId) -> int {
        if (!table || userId <= 0) {
            return -1;
        }
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 0) && table->item(row, 0)->text().toInt() == userId) {
                return row;
            }
        }
        return -1;
    };
    const auto firstVisibleRow = [table]() -> int {
        if (!table) {
            return -1;
        }
        for (int row = 0; row < table->rowCount(); ++row) {
            if (!table->isRowHidden(row)) {
                return row;
            }
        }
        return -1;
    };
    const auto applySearchFilter = [table, searchEdit, rowText] {
        if (!table || !searchEdit) {
            return;
        }
        const QString keyword = searchEdit->text().trimmed();
        for (int row = 0; row < table->rowCount(); ++row) {
            const QString combined = rowText(row, 1) + QLatin1Char(' ')
                                     + rowText(row, 2);
            table->setRowHidden(row, !keyword.isEmpty()
                                      && !combined.contains(keyword, Qt::CaseInsensitive));
        }
    };
    const auto refreshAndReselect = [this, table, roleCode, roleText, findRowByUserId, firstVisibleRow,
                                     applySearchFilter, fillDetail, clearDetail](int userId, bool showFeedback) {
        adminPopulateRoleUsers(table, roleCode);
        if (!table || !table->property("lastLoadOk").toBool()) {
            return;
        }
        applySearchFilter();
        int row = findRowByUserId(userId);
        if (row >= 0 && table->isRowHidden(row)) {
            row = -1;
        }
        if (row < 0 && table && table->rowCount() > 0) {
            row = firstVisibleRow();
        }
        if (row >= 0) {
            table->selectRow(row);
            fillDetail(row);
        } else {
            clearDetail();
        }
        if (showFeedback) {
            showToast(QStringLiteral("列表已刷新。"));
            statusBar()->showMessage(QStringLiteral("%1列表已刷新。").arg(roleText), 3000);
        }
    };
    const auto showDetailDialog = [this, table, isStudent, pageTag, rowText] {
        qDebug() << "[ADMIN_ROLE_PAGE_ACTION]" << pageTag << "detail";
        const auto showInfo = [this](const QString &title, const QString &message) {
            auto *box = new QMessageBox(QMessageBox::Information, title, message, QMessageBox::Ok, this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->show();
        };

        int row = -1;
        if (table && table->selectionModel()) {
            const QModelIndexList selectedRows = table->selectionModel()->selectedRows();
            if (!selectedRows.isEmpty() && selectedRows.first().isValid()) {
                row = selectedRows.first().row();
            }
        }
        if (!table || row < 0 || row >= table->rowCount() || table->isRowHidden(row)) {
            showInfo(isStudent ? QStringLiteral("请先选择学生")
                               : QStringLiteral("请先选择教师"),
                     isStudent ? QStringLiteral("请先选择学生。")
                               : QStringLiteral("请先选择教师。"));
            return;
        }

        QTableWidgetItem *idItem = table->item(row, 0);
        bool idOk = false;
        const int userId = idItem ? idItem->text().toInt(&idOk) : 0;
        if (!idOk || userId <= 0) {
            showInfo(isStudent ? QStringLiteral("请先选择学生")
                               : QStringLiteral("请先选择教师"),
                     isStudent ? QStringLiteral("请先选择学生。")
                               : QStringLiteral("请先选择教师。"));
            return;
        }

        const QString selectedUsername = rowText(row, 1);
        const QString selectedDisplayName = rowText(row, 2);

        QJsonObject request;
        request.insert(QStringLiteral("type"), isStudent ? QStringLiteral("list_submissions") : QStringLiteral("list_exams"));
        if (isStudent) {
            request.insert(QStringLiteral("statusFilter"), QStringLiteral("all"));
        }
        QJsonObject response;
        QString error;
        if (!ApiClient::instance().request(request, &response, &error)) {
            showError(isStudent ? QStringLiteral("读取提交明细失败") : QStringLiteral("读取试卷明细失败"), error);
            return;
        }

        QJsonArray detailRows;
        if (isStudent) {
            const QJsonArray submissions = response.value(QStringLiteral("submissions")).toArray();
            for (const QJsonValue &value : submissions) {
                if (!value.isObject()) {
                    continue;
                }
                const QJsonObject submission = value.toObject();
                if (submission.value(QStringLiteral("studentUsername")).toString() != selectedUsername) {
                    continue;
                }
                detailRows.append(submission);
            }
            qDebug() << "[ADMIN_STUDENT_DETAIL]" << "userId=" << userId << "submissionCount=" << detailRows.size();
            if (detailRows.isEmpty()) {
                showInfo(QStringLiteral("学生提交明细"), QStringLiteral("暂无提交记录。"));
                return;
            }
        } else {
            const QJsonArray exams = response.value(QStringLiteral("exams")).toArray();
            for (const QJsonValue &value : exams) {
                if (!value.isObject()) {
                    continue;
                }
                const QJsonObject exam = value.toObject();
                if (exam.value(QStringLiteral("creatorName")).toString() != selectedDisplayName) {
                    continue;
                }
                detailRows.append(exam);
            }
            qDebug() << "[ADMIN_TEACHER_DETAIL]" << "userId=" << userId << "examCount=" << detailRows.size();
            if (detailRows.isEmpty()) {
                showInfo(QStringLiteral("教师试卷明细"), QStringLiteral("暂无试卷记录。"));
                return;
            }
        }

        auto *dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(isStudent ? QStringLiteral("学生提交明细") : QStringLiteral("教师试卷明细"));
        dialog->resize(780, 460);
        auto *dialogLayout = new QVBoxLayout(dialog);
        dialogLayout->addWidget(sectionTitle(isStudent
                                                 ? QStringLiteral("%1 的提交记录").arg(selectedUsername)
                                                 : QStringLiteral("%1 的试卷记录").arg(selectedUsername),
                                             QStringLiteral("仅展示当前角色维度的简要明细。")));
        auto *detailTable = new QTableWidget;
        detailTable->verticalHeader()->setVisible(false);
        detailTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        polishTable(detailTable);
        dialogLayout->addWidget(detailTable, 1);

        if (isStudent) {
            detailTable->setColumnCount(4);
            detailTable->setHorizontalHeaderLabels({QStringLiteral("考试名称"), QStringLiteral("提交状态"),
                                                    QStringLiteral("分数/待批阅"), QStringLiteral("提交时间")});
            for (const QJsonValue &value : detailRows) {
                const QJsonObject submission = value.toObject();
                const int detailRow = detailTable->rowCount();
                detailTable->insertRow(detailRow);
                const QString status = submission.value(QStringLiteral("status")).toString();
                detailTable->setItem(detailRow, 0, readOnlyItem(submission.value(QStringLiteral("examName")).toString()));
                detailTable->setItem(detailRow, 1, readOnlyItem(statusText(status)));
                detailTable->setItem(detailRow, 2, readOnlyItem(status == QStringLiteral("graded")
                    ? QStringLiteral("%1 分").arg(submission.value(QStringLiteral("totalScore")).toDouble(), 0, 'f', 1)
                    : QStringLiteral("待批阅")));
                detailTable->setItem(detailRow, 3, readOnlyItem(localTimeText(submission.value(QStringLiteral("submittedAt")).toString())));
            }
        } else {
            detailTable->setColumnCount(4);
            detailTable->setHorizontalHeaderLabels({QStringLiteral("试卷名称"), QStringLiteral("发布状态"),
                                                    QStringLiteral("考试状态"), QStringLiteral("创建时间")});
            for (const QJsonValue &value : detailRows) {
                const QJsonObject exam = value.toObject();
                const int detailRow = detailTable->rowCount();
                detailTable->insertRow(detailRow);
                detailTable->setItem(detailRow, 0, readOnlyItem(exam.value(QStringLiteral("name")).toString()));
                detailTable->setItem(detailRow, 1, readOnlyItem(publishStatusText(exam.value(QStringLiteral("publishStatus")).toString())));
                detailTable->setItem(detailRow, 2, readOnlyItem(statusText(exam.value(QStringLiteral("state")).toString())));
                detailTable->setItem(detailRow, 3, readOnlyItem(localTimeText(exam.value(QStringLiteral("createdAt")).toString())));
            }
        }
        detailTable->horizontalHeader()->setStretchLastSection(true);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        if (auto *closeButton = buttons->button(QDialogButtonBox::Close)) {
            closeButton->setText(QStringLiteral("关闭"));
            registerButtonAction(closeButton, pageTag, QStringLiteral("detail_close"), [dialog] { dialog->close(); });
        }
        dialogLayout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        dialog->show();
    };
    const auto goAccountPage = [this, page, table, roleCode, pageTag, findRowByUserId] {
        qDebug() << "[ADMIN_ROLE_PAGE_ACTION]" << pageTag << "go_accounts";
        const int selectedUserId = page->property("selectedUserId").toInt();
        const QString selectedUsername = page->property("selectedUsername").toString();
        const int navIndex = navPages.indexOf(pageUsers);
        if (navIndex >= 0) {
            triggerNav(navIndex);
        } else if (pageUsers) {
            switchContentPage(pageUsers, QStringLiteral("账号权限"));
        }
        if (accountRoleFilter) {
            const int roleIndex = accountRoleFilter->findData(roleCode);
            if (roleIndex >= 0) {
                accountRoleFilter->setCurrentIndex(roleIndex);
            }
        }
        if (accountSearchEdit && !selectedUsername.isEmpty()) {
            accountSearchEdit->setText(selectedUsername);
        }
        populateAccountTable();
        if (accountTable && selectedUserId > 0) {
            for (int row = 0; row < accountTable->rowCount(); ++row) {
                if (accountTable->item(row, 0) && accountTable->item(row, 0)->text().toInt() == selectedUserId) {
                    accountTable->selectRow(row);
                    populateAccountEditorFromSelection();
                    break;
                }
            }
        }
        Q_UNUSED(table)
        Q_UNUSED(findRowByUserId)
        showToast(QStringLiteral("已切换到账号权限页。"));
    };

    const QString refreshAction = isStudent ? QStringLiteral("refresh_students") : QStringLiteral("refresh_teachers");
    bindTeacherButton(refreshButton, QStringLiteral("btn_%1_refresh").arg(pageTag), pageTag, refreshAction,
                      [page, pageTag, refreshAndReselect] {
                          qDebug() << "[ADMIN_ROLE_PAGE_ACTION]" << pageTag << "refresh";
                          refreshAndReselect(page->property("selectedUserId").toInt(), true);
                      });
    bindTeacherButton(detailButton, QStringLiteral("btn_%1_detail").arg(pageTag), pageTag,
                      isStudent ? QStringLiteral("student_submission_detail") : QStringLiteral("teacher_exam_detail"),
                      showDetailDialog);
    bindTeacherButton(accountButton, QStringLiteral("btn_%1_go_accounts").arg(pageTag), pageTag,
                      QStringLiteral("go_accounts"), goAccountPage);
    connect(searchEdit, &QLineEdit::textChanged, this, [pageTag, applySearchFilter, firstVisibleRow, fillDetail, clearDetail, table](const QString &) {
        qDebug() << "[ADMIN_ROLE_PAGE_ACTION]" << pageTag << "search";
        applySearchFilter();
        const int current = table ? table->currentRow() : -1;
        if (current >= 0 && !table->isRowHidden(current)) {
            fillDetail(current);
            return;
        }
        const int row = firstVisibleRow();
        if (row >= 0) {
            table->selectRow(row);
            fillDetail(row);
        } else {
            clearDetail();
        }
    });
    connect(table, &QTableWidget::currentCellChanged, this, [fillDetail](int currentRow, int, int, int) {
        fillDetail(currentRow);
    });
    connect(table, &QTableWidget::cellClicked, this, [pageTag, fillDetail](int row, int col) {
        qInfo("[TABLE_CLICK] page=%s row=%d col=%d", qPrintable(pageTag), row, col);
        fillDetail(row);
    });
    connect(table, &QTableWidget::cellDoubleClicked, this, [pageTag, fillDetail](int row, int col) {
        qInfo("[TABLE_DOUBLE_CLICK] page=%s row=%d col=%d", qPrintable(pageTag), row, col);
        fillDetail(row);
    });

    adminPopulateRoleUsers(table, roleCode);
    if (table->rowCount() > 0 && table->currentRow() >= 0) {
        fillDetail(table->currentRow() >= 0 ? table->currentRow() : 0);
    } else {
        clearDetail();
    }
    return page;
}

void MainWindow::adminPopulateRoleUsers(QTableWidget *table, const QString &roleCode)
{
    if (!table) {
        return;
    }
    table->setProperty("lastLoadOk", false);

    const bool isStudent = roleCode == roleToCode(UserRole::Student);
    const bool isTeacher = roleCode == roleToCode(UserRole::Teacher);
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_users"));
    request.insert(QStringLiteral("roleFilter"), roleCode);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        qWarning("[ADMIN_ERROR] page=user_admin reason=%s", qPrintable(error));
        showError(QStringLiteral("用户列表读取失败"), error);
        return;
    }
    const QJsonArray users = response.value(QStringLiteral("users")).toArray();

    QJsonArray submissions;
    QJsonArray exams;
    bool submissionsOk = false;
    bool examsOk = false;
    if (isStudent || isTeacher) {
        QJsonObject submissionRequest;
        submissionRequest.insert(QStringLiteral("type"), QStringLiteral("list_submissions"));
        submissionRequest.insert(QStringLiteral("statusFilter"), QStringLiteral("all"));
        QJsonObject submissionResponse;
        QString submissionError;
        submissionsOk = ApiClient::instance().request(submissionRequest, &submissionResponse, &submissionError);
        if (submissionsOk) {
            submissions = submissionResponse.value(QStringLiteral("submissions")).toArray();
        } else {
            qWarning("[ADMIN_ROLE_STATS_ERROR] role=%s source=submissions reason=%s",
                     qPrintable(roleCode), qPrintable(submissionError));
        }
    }
    if (isTeacher) {
        QJsonObject examRequest;
        examRequest.insert(QStringLiteral("type"), QStringLiteral("list_exams"));
        QJsonObject examResponse;
        QString examError;
        examsOk = ApiClient::instance().request(examRequest, &examResponse, &examError);
        if (examsOk) {
            exams = examResponse.value(QStringLiteral("exams")).toArray();
        } else {
            qWarning("[ADMIN_ROLE_STATS_ERROR] role=%s source=exams reason=%s",
                     qPrintable(roleCode), qPrintable(examError));
        }
    }

    const auto localTimeOrDash = [](const QString &iso) {
        return iso.trimmed().isEmpty() ? QStringLiteral("-") : localTimeText(iso);
    };
    const auto laterIso = [](const QString &left, const QString &right) {
        if (left.isEmpty()) {
            return right;
        }
        if (right.isEmpty()) {
            return left;
        }
        const QDateTime l = QDateTime::fromString(left, Qt::ISODate);
        const QDateTime r = QDateTime::fromString(right, Qt::ISODate);
        if (l.isValid() && r.isValid()) {
            return r > l ? right : left;
        }
        return right > left ? right : left;
    };
    const auto submissionScoreText = [](const QJsonObject &submission) {
        return submission.value(QStringLiteral("status")).toString() == QStringLiteral("graded")
                   ? QStringLiteral("%1 分").arg(submission.value(QStringLiteral("totalScore")).toDouble(), 0, 'f', 1)
                   : QStringLiteral("待批阅");
    };

    table->setRowCount(0);
    for (const QJsonValue &value : users) {
        const QJsonObject u = value.toObject();
        if (!roleCode.isEmpty() && u.value(QStringLiteral("role")).toString() != roleCode) {
            continue;  // 双保险：再按角色过滤一次
        }
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, readOnlyItem(QString::number(u.value(QStringLiteral("id")).toInt())));
        table->setItem(row, 1, readOnlyItem(u.value(QStringLiteral("username")).toString()));
        table->setItem(row, 2, readOnlyItem(u.value(QStringLiteral("displayName")).toString()));
        table->setItem(row, 3, readOnlyItem(u.value(QStringLiteral("isActive")).toBool(true)
                                            ? QStringLiteral("正常")
                                            : QStringLiteral("已停用")));

        const int userId = u.value(QStringLiteral("id")).toInt();
        QString recentText = QStringLiteral("最近记录：暂无数据");
        if (isStudent) {
            if (!submissionsOk) {
                for (int col = 4; col <= 8; ++col) {
                    table->setItem(row, col, readOnlyItem(QStringLiteral("-")));
                }
                recentText = QStringLiteral("最近提交记录：暂无数据");
            } else {
                const QString username = u.value(QStringLiteral("username")).toString();
                int submittedCount = 0;
                int pendingCount = 0;
                int gradedCount = 0;
                QString recentIso;
                QSet<int> examIds;
                QList<QJsonObject> recentSubmissions;
                for (const QJsonValue &submissionValue : submissions) {
                    const QJsonObject submission = submissionValue.toObject();
                    if (submission.value(QStringLiteral("studentUsername")).toString() != username) {
                        continue;
                    }
                    ++submittedCount;
                    examIds.insert(submission.value(QStringLiteral("examId")).toInt());
                    const QString status = submission.value(QStringLiteral("status")).toString();
                    if (status == QStringLiteral("graded")) {
                        ++gradedCount;
                    } else if (status == QStringLiteral("pending") || status == QStringLiteral("grading")) {
                        ++pendingCount;
                    }
                    recentIso = laterIso(recentIso, submission.value(QStringLiteral("submittedAt")).toString());
                    recentSubmissions.append(submission);
                }
                std::sort(recentSubmissions.begin(), recentSubmissions.end(), [](const QJsonObject &a, const QJsonObject &b) {
                    return a.value(QStringLiteral("submittedAt")).toString() > b.value(QStringLiteral("submittedAt")).toString();
                });
                QStringList recentLines;
                const int recentCount = qMin(3, recentSubmissions.size());
                for (int i = 0; i < recentCount; ++i) {
                    const QJsonObject submission = recentSubmissions.at(i);
                    recentLines << QStringLiteral("%1｜%2｜%3｜%4")
                                       .arg(submission.value(QStringLiteral("examName")).toString(),
                                            statusText(submission.value(QStringLiteral("status")).toString()),
                                            submissionScoreText(submission),
                                            localTimeOrDash(submission.value(QStringLiteral("submittedAt")).toString()));
                }
                recentText = recentLines.isEmpty()
                                 ? QStringLiteral("最近提交记录：暂无数据")
                                 : QStringLiteral("最近 3 条提交记录：\n%1").arg(recentLines.join(QStringLiteral("\n")));
                table->setItem(row, 4, readOnlyItem(QString::number(examIds.size())));
                table->setItem(row, 5, readOnlyItem(QString::number(submittedCount)));
                table->setItem(row, 6, readOnlyItem(QString::number(pendingCount)));
                table->setItem(row, 7, readOnlyItem(QString::number(gradedCount)));
                table->setItem(row, 8, readOnlyItem(localTimeOrDash(recentIso)));
            }
        } else if (isTeacher) {
            if (!examsOk) {
                for (int col = 4; col <= 8; ++col) {
                    table->setItem(row, col, readOnlyItem(QStringLiteral("-")));
                }
                recentText = QStringLiteral("最近试卷：暂无数据");
            } else {
                const QString displayName = u.value(QStringLiteral("displayName")).toString();
                int createdCount = 0;
                int publishedCount = 0;
                int pendingCount = 0;
                int gradedCount = 0;
                QString recentIso;
                QSet<int> teacherExamIds;
                QList<QJsonObject> recentExams;
                for (const QJsonValue &examValue : exams) {
                    const QJsonObject exam = examValue.toObject();
                    if (exam.value(QStringLiteral("creatorName")).toString() != displayName) {
                        continue;
                    }
                    ++createdCount;
                    teacherExamIds.insert(exam.value(QStringLiteral("id")).toInt());
                    if (exam.value(QStringLiteral("publishStatus")).toString() == QStringLiteral("published")) {
                        ++publishedCount;
                    }
                    recentIso = laterIso(recentIso, exam.value(QStringLiteral("createdAt")).toString());
                    recentExams.append(exam);
                }
                if (submissionsOk) {
                    for (const QJsonValue &submissionValue : submissions) {
                        const QJsonObject submission = submissionValue.toObject();
                        if (!teacherExamIds.contains(submission.value(QStringLiteral("examId")).toInt())) {
                            continue;
                        }
                        const QString status = submission.value(QStringLiteral("status")).toString();
                        if (status == QStringLiteral("graded")) {
                            ++gradedCount;
                        } else if (status == QStringLiteral("pending") || status == QStringLiteral("grading")) {
                            ++pendingCount;
                        }
                    }
                }
                std::sort(recentExams.begin(), recentExams.end(), [](const QJsonObject &a, const QJsonObject &b) {
                    return a.value(QStringLiteral("createdAt")).toString() > b.value(QStringLiteral("createdAt")).toString();
                });
                QStringList recentLines;
                const int recentCount = qMin(3, recentExams.size());
                for (int i = 0; i < recentCount; ++i) {
                    const QJsonObject exam = recentExams.at(i);
                    recentLines << QStringLiteral("%1｜%2｜%3｜%4")
                                       .arg(exam.value(QStringLiteral("name")).toString(),
                                            publishStatusText(exam.value(QStringLiteral("publishStatus")).toString()),
                                            statusText(exam.value(QStringLiteral("state")).toString()),
                                            localTimeOrDash(exam.value(QStringLiteral("createdAt")).toString()));
                }
                recentText = recentLines.isEmpty()
                                 ? QStringLiteral("最近 3 张试卷：暂无数据")
                                 : QStringLiteral("最近 3 张试卷：\n%1").arg(recentLines.join(QStringLiteral("\n")));
                table->setItem(row, 4, readOnlyItem(QString::number(createdCount)));
                table->setItem(row, 5, readOnlyItem(QString::number(publishedCount)));
                table->setItem(row, 6, readOnlyItem(submissionsOk ? QString::number(pendingCount) : QStringLiteral("-")));
                table->setItem(row, 7, readOnlyItem(submissionsOk ? QString::number(gradedCount) : QStringLiteral("-")));
                table->setItem(row, 8, readOnlyItem(localTimeOrDash(recentIso)));
            }
        } else {
            for (int col = 4; col <= 8; ++col) {
                table->setItem(row, col, readOnlyItem(QStringLiteral("-")));
            }
        }
        if (table->item(row, 0)) {
            table->item(row, 0)->setData(Qt::UserRole, recentText);
        }
    }
    if (table->rowCount() > 0) {
        table->selectRow(0);
    }
    table->setProperty("lastLoadOk", true);
}

void MainWindow::adminAddUser(const QString &roleCode, const QString &pageTag)
{
    const bool isStudent = roleCode == roleToCode(UserRole::Student);
    const QString roleName = isStudent ? QStringLiteral("学生") : QStringLiteral("教师");
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新增%1").arg(roleName));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *username = new QLineEdit;
    auto *displayName = new QLineEdit;
    auto *password = new QLineEdit(QStringLiteral("123456"));
    password->setEchoMode(QLineEdit::Password);
    auto *note = new QLineEdit;
    form->addRow(QStringLiteral("用户名"), username);
    form->addRow(QStringLiteral("显示名"), displayName);
    form->addRow(QStringLiteral("初始密码"), password);
    form->addRow(QStringLiteral("备注/说明"), note);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("创建"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QPushButton *confirmButton = buttons->button(QDialogButtonBox::Ok);
    QPushButton *cancelButton = buttons->button(QDialogButtonBox::Cancel);
    registerButtonAction(confirmButton, pageTag, QStringLiteral("create_dialog_ok"), [&dialog] { dialog.accept(); });
    registerButtonAction(cancelButton, pageTag, QStringLiteral("create_dialog_cancel"), [&dialog] { dialog.reject(); });
    const int dialogResult = dialog.exec();
    unregisterButtonAction(confirmButton);
    unregisterButtonAction(cancelButton);
    if (dialogResult != QDialog::Accepted) {
        return;
    }
    if (username->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("账号为空"), QStringLiteral("请填写账号。"));
        return;
    }
    if (displayName->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("显示名为空"), QStringLiteral("请填写显示名。"));
        return;
    }
    if (note->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("备注为空"), QStringLiteral("请填写备注/说明。"));
        return;
    }
    if (password->text().size() < 6) {
        QMessageBox::warning(this, QStringLiteral("密码太短"), QStringLiteral("初始密码至少需要 6 位。"));
        return;
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("create_user"));
    request.insert(QStringLiteral("role"), roleCode);
    request.insert(QStringLiteral("username"), username->text().trimmed());
    request.insert(QStringLiteral("displayName"), displayName->text().trimmed());
    request.insert(QStringLiteral("password"), password->text());
    request.insert(QStringLiteral("note"), note->text().trimmed());
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        qWarning("[ADMIN_ERROR] page=%s reason=%s", qPrintable(pageTag), qPrintable(error));
        qWarning("[ACCOUNT_UPDATE_ERROR] reason=%s", qPrintable(error));
        showError(QStringLiteral("创建用户失败"), error);
        return;
    }
    qInfo("[ACCOUNT_UPDATE_SUCCESS] action=create role=%s username=%s",
          qPrintable(roleCode), qPrintable(username->text().trimmed()));
    Q_UNUSED(response);
    const QString successText = QStringLiteral("%1账号创建成功。").arg(roleName);
    showToast(successText);
    statusBar()->showMessage(QStringLiteral("%1账号 %2 创建成功。").arg(roleName, username->text().trimmed()), 3000);
    QMessageBox::information(this, QStringLiteral("创建成功"), successText);
    refreshAdminUserTables();
}

void MainWindow::adminEditUser(QTableWidget *table, const QString &pageTag)
{
    if (!table) {
        return;
    }
    const int row = table->currentRow();
    if (row < 0 || !table->item(row, 0)) {
        QMessageBox::warning(this, QStringLiteral("请先选择账号"), QStringLiteral("请先选择要修改的账号。"));
        return;
    }
    const int userId = table->item(row, 0)->text().toInt();
    const QString roleCode = (table == studentMgmtTable) ? roleToCode(UserRole::Student) : roleToCode(UserRole::Teacher);
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("编辑用户"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("账号"), new QLabel(table->item(row, 1) ? table->item(row, 1)->text() : QString()));
    auto *displayName = new QLineEdit(table->item(row, 2) ? table->item(row, 2)->text() : QString());
    auto *note = new QLineEdit(table->item(row, 3) ? table->item(row, 3)->text() : QString());
    form->addRow(QStringLiteral("姓名"), displayName);
    form->addRow(QStringLiteral("备注"), note);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("update_user"));
    request.insert(QStringLiteral("userId"), userId);
    request.insert(QStringLiteral("role"), roleCode);
    request.insert(QStringLiteral("displayName"), displayName->text().trimmed());
    request.insert(QStringLiteral("note"), note->text().trimmed());
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        qWarning("[ACCOUNT_UPDATE_ERROR] reason=%s", qPrintable(error));
        showError(QStringLiteral("保存用户失败"), error);
        return;
    }
    qInfo("[ACCOUNT_UPDATE_SUCCESS] action=update userId=%d", userId);
    showToast(QStringLiteral("用户信息已保存。"));
    refreshAdminUserTables();
}

void MainWindow::adminDeleteUser(QTableWidget *table, const QString &pageTag)
{
    if (!table) {
        return;
    }
    const int row = table->currentRow();
    if (row < 0 || !table->item(row, 0)) {
        QMessageBox::warning(this, QStringLiteral("请先选择账号"), QStringLiteral("请先选择要删除的账号。"));
        return;
    }
    const int userId = table->item(row, 0)->text().toInt();
    const QString username = table->item(row, 1) ? table->item(row, 1)->text() : QString::number(userId);
    if (userId == currentUser.id) {
        QMessageBox::warning(this, QStringLiteral("不能删除"), QStringLiteral("不能删除当前登录账号。"));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("确认删除"),
                              QStringLiteral("确定停用账号 %1 吗？历史考试、答卷、成绩和日志会保留。").arg(username)) != QMessageBox::Yes) {
        return;
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("delete_user"));
    request.insert(QStringLiteral("userId"), userId);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        qWarning("[ADMIN_ERROR] page=%s reason=%s", qPrintable(pageTag), qPrintable(error));
        showError(QStringLiteral("删除用户失败"), error);
        return;
    }
    qInfo("[ACCOUNT_UPDATE_SUCCESS] action=delete userId=%d", userId);
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("账号已停用。")));
    refreshAdminUserTables();
}

void MainWindow::adminResetPassword(QTableWidget *table, const QString &pageTag)
{
    if (!table) {
        return;
    }
    const int row = table->currentRow();
    if (row < 0 || !table->item(row, 0)) {
        QMessageBox::warning(this, QStringLiteral("请先选择账号"), QStringLiteral("请先选择要重置密码的账号。"));
        return;
    }
    const int userId = table->item(row, 0)->text().toInt();
    bool okPwd = false;
    const QString newPassword = QInputDialog::getText(this, QStringLiteral("重置密码"),
        QStringLiteral("为该账号设置新密码（至少 6 位）："), QLineEdit::Password, QStringLiteral("123456"), &okPwd);
    if (!okPwd) {
        return;
    }
    if (newPassword.length() < 6) {
        QMessageBox::warning(this, QStringLiteral("密码太短"), QStringLiteral("新密码至少需要 6 位。"));
        return;
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("reset_password"));
    request.insert(QStringLiteral("userId"), userId);
    request.insert(QStringLiteral("newPassword"), newPassword);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        qWarning("[ACCOUNT_UPDATE_ERROR] reason=%s", qPrintable(error));
        showError(QStringLiteral("重置密码失败"), error);
        return;
    }
    qInfo("[ACCOUNT_UPDATE_SUCCESS] action=reset_password userId=%d", userId);
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("密码已重置。")));
    refreshAdminUserTables();
}

QWidget *MainWindow::createAdminExamMonitorPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto *toolbarCard = card();
    toolbarCard->setObjectName(QStringLiteral("activityCard"));
    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(sectionTitle(QStringLiteral("考试监控"),
                                    QStringLiteral("全局考试状态总览（管理员只监管，不组卷）。")), 1);
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"));
    refreshButton->setObjectName(QStringLiteral("primaryButton"));
    toolbar->addWidget(refreshButton);
    toolbarCard->layout()->addItem(toolbar);
    layout->addWidget(toolbarCard);
    auto *tableCard = card();
    tableCard->setObjectName(QStringLiteral("paperCard"));
    adminExamMonitorTable = new QTableWidget(0, 6);
    adminExamMonitorTable->setHorizontalHeaderLabels({QStringLiteral("考试名称"), QStringLiteral("课程"),
        QStringLiteral("发布状态"), QStringLiteral("考试状态"), QStringLiteral("创建教师"), QStringLiteral("开始时间")});
    adminExamMonitorTable->horizontalHeader()->setStretchLastSection(true);
    adminExamMonitorTable->verticalHeader()->setVisible(false);
    adminExamMonitorTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    adminExamMonitorTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    polishTable(adminExamMonitorTable);
    tableCard->layout()->addWidget(adminExamMonitorTable);
    layout->addWidget(tableCard, 1);
    bindTeacherButton(refreshButton, QStringLiteral("btn_admin_exam_monitor_refresh"),
                      QStringLiteral("admin_exam_monitor"), QStringLiteral("refresh"), [this] { refreshAdminExamMonitor(); });
    connect(adminExamMonitorTable, &QTableWidget::cellDoubleClicked, this, [](int row, int col) {
        qInfo("[TABLE_DOUBLE_CLICK] page=admin_exam_monitor row=%d col=%d", row, col);
    });
    refreshAdminExamMonitor();
    return page;
}

void MainWindow::refreshAdminExamMonitor()
{
    if (!adminExamMonitorTable) {
        return;
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_exams"));
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        qWarning("[ADMIN_ERROR] page=admin_exam_monitor reason=%s", qPrintable(error));
        showError(QStringLiteral("考试列表读取失败"), error);
        return;
    }
    const QJsonArray exams = response.value(QStringLiteral("exams")).toArray();
    adminExamMonitorTable->setRowCount(0);
    for (const QJsonValue &value : exams) {
        const QJsonObject e = value.toObject();
        const int row = adminExamMonitorTable->rowCount();
        adminExamMonitorTable->insertRow(row);
        adminExamMonitorTable->setItem(row, 0, readOnlyItem(e.value(QStringLiteral("name")).toString()));
        adminExamMonitorTable->setItem(row, 1, readOnlyItem(e.value(QStringLiteral("course")).toString()));
        adminExamMonitorTable->setItem(row, 2, readOnlyItem(publishStatusText(e.value(QStringLiteral("publishStatus")).toString())));
        adminExamMonitorTable->setItem(row, 3, readOnlyItem(statusText(e.value(QStringLiteral("state")).toString())));
        adminExamMonitorTable->setItem(row, 4, readOnlyItem(e.value(QStringLiteral("creatorName")).toString()));
        adminExamMonitorTable->setItem(row, 5, readOnlyItem(localTimeText(e.value(QStringLiteral("startAt")).toString())));
    }
}

QWidget *MainWindow::createAdminLogsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto *toolbarCard = card();
    toolbarCard->setObjectName(QStringLiteral("activityCard"));
    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(sectionTitle(QStringLiteral("系统日志"),
                                    QStringLiteral("登录/考试/风险等系统事件记录（基于风险与异常事件汇总）。")), 1);
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"));
    refreshButton->setObjectName(QStringLiteral("primaryButton"));
    toolbar->addWidget(refreshButton);
    toolbarCard->layout()->addItem(toolbar);
    layout->addWidget(toolbarCard);
    auto *tableCard = card();
    tableCard->setObjectName(QStringLiteral("paperCard"));
    adminLogsTable = new QTableWidget(0, 6);
    adminLogsTable->setHorizontalHeaderLabels({QStringLiteral("学生"), QStringLiteral("用户名"), QStringLiteral("考试"),
        QStringLiteral("风险等级"), QStringLiteral("异常次数"), QStringLiteral("最近时间")});
    adminLogsTable->horizontalHeader()->setStretchLastSection(true);
    adminLogsTable->verticalHeader()->setVisible(false);
    adminLogsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    adminLogsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    polishTable(adminLogsTable);
    tableCard->layout()->addWidget(adminLogsTable);
    layout->addWidget(tableCard, 1);
    bindTeacherButton(refreshButton, QStringLiteral("btn_admin_logs_refresh"),
                      QStringLiteral("admin_logs"), QStringLiteral("refresh"), [this] { refreshAdminLogs(); });
    refreshAdminLogs();
    return page;
}

void MainWindow::refreshAdminLogs()
{
    if (!adminLogsTable) {
        return;
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_risk_records"));
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        qWarning("[ADMIN_ERROR] page=admin_logs reason=%s", qPrintable(error));
        showError(QStringLiteral("系统日志读取失败"), error);
        return;
    }
    const QJsonArray records = response.value(QStringLiteral("records")).toArray();
    adminLogsTable->setRowCount(0);
    for (const QJsonValue &value : records) {
        const QJsonObject r = value.toObject();
        const int row = adminLogsTable->rowCount();
        adminLogsTable->insertRow(row);
        adminLogsTable->setItem(row, 0, readOnlyItem(r.value(QStringLiteral("studentName")).toString()));
        adminLogsTable->setItem(row, 1, readOnlyItem(r.value(QStringLiteral("studentUsername")).toString()));
        adminLogsTable->setItem(row, 2, readOnlyItem(r.value(QStringLiteral("examName")).toString()));
        adminLogsTable->setItem(row, 3, readOnlyItem(r.value(QStringLiteral("riskLevel")).toString(QStringLiteral("低风险"))));
        adminLogsTable->setItem(row, 4, readOnlyItem(QString::number(r.value(QStringLiteral("abnormalCount")).toInt())));
        adminLogsTable->setItem(row, 5, readOnlyItem(localTimeText(r.value(QStringLiteral("recentTime")).toString())));
    }
}

void MainWindow::createMenus()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    if (currentUser.role == UserRole::Teacher) {
        auto *importAction = fileMenu->addAction(QStringLiteral("导入题库..."));
        importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
        connect(importAction, &QAction::triggered, this, [this] {
            qInfo("[UI_CLICK] source=menu action=import_question_bank");
            // 切到题目管理页（QWidget* 直连），并同步高亮对应左侧按钮。
            const int navIndex = navPages.indexOf(pageQuestion);
            if (navIndex >= 0) {
                updateNavHighlight(navButtons.at(navIndex));
            }
            switchContentPage(pageQuestion, QStringLiteral("题目管理"));
            importQuestions();
        });
        auto *localImportAction = fileMenu->addAction(QStringLiteral("导入题库（本机/共享目录）..."));
        connect(localImportAction, &QAction::triggered, this, [this] {
            qInfo("[UI_CLICK] source=menu action=import_question_bank_local");
            importQuestionsLocal();
        });
        // 整套试卷的本机文件框只保留这一个显式备用入口；试卷管理页主按钮一律走 Windows 宿主机选择框。
        auto *localExamImportAction = fileMenu->addAction(QStringLiteral("导入整套试卷（本机/共享目录）..."));
        connect(localExamImportAction, &QAction::triggered, this, [this] {
            qInfo("[UI_CLICK] source=menu action=import_full_exam_local");
            importExamFromFileLocal();
        });
    }
    auto *refreshAction = fileMenu->addAction(QStringLiteral("刷新当前数据"));
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshInitialData);
    fileMenu->addSeparator();
    auto *exitAction = fileMenu->addAction(QStringLiteral("退出"));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
}

// 唯一的主导航切页实现：在单容器 contentHost 里动态替换页面。
void MainWindow::switchContentPage(QWidget *targetPage, const QString &label)
{
    if (!contentHost || !contentLayout) {
        qWarning() << "[NAV-ERROR] contentHost/contentLayout is null";
        return;
    }
    if (!targetPage) {
        qWarning() << "[NAV-ERROR] targetPage is null label=" << label;
        return;
    }

    qDebug() << "[NAV-SWITCH-START]"
             << "label=" << label
             << "target=" << targetPage
             << "targetName=" << targetPage->objectName()
             << "current=" << currentContentPage
             << "currentName=" << (currentContentPage ? currentContentPage->objectName() : QStringLiteral("<null>"))
             << "layoutCountBefore=" << contentLayout->count();

    if (currentContentPage && currentContentPage != targetPage) {
        contentLayout->removeWidget(currentContentPage);
        currentContentPage->hide();
    }

    if (contentLayout->indexOf(targetPage) < 0) {
        contentLayout->addWidget(targetPage);
    }

    currentContentPage = targetPage;
    targetPage->show();
    targetPage->raise();
    targetPage->update();
    contentHost->update();
    if (currentUser.role == UserRole::Student && targetPage == pageExam && studentProctorPreview) {
        qDebug() << "[STUDENT_CAMERA_WIDGET_SHOW]" << "visible=" << studentProctorPreview->isVisible();
        if (simulateProctorButton) {
            qDebug() << "[SIMULATION_BUTTON_SHOW]" << "visible=" << simulateProctorButton->isVisible();
        }
    }

    if (titleLabel) {
        titleLabel->setText(label);
    }
    statusBar()->showMessage(QStringLiteral("已切换到：%1 page=%2")
                                 .arg(label, targetPage->objectName()));

    qDebug() << "[NAV-SWITCH-DONE]"
             << "label=" << label
             << "current=" << currentContentPage
             << "currentName=" << (currentContentPage ? currentContentPage->objectName() : QStringLiteral("<null>"))
             << "layoutCountAfter=" << contentLayout->count();

    const QList<QPushButton *> buttons = currentContentPage->findChildren<QPushButton *>();
    qDebug() << "[PAGE_BUTTON_DISCOVERED_COUNT]"
             << "page=" << currentContentPage->objectName()
             << "count=" << buttons.size();
    for (QPushButton *button : buttons) {
        qDebug() << "[PAGE_BUTTON_DISCOVERED]"
                 << "page=" << currentContentPage->objectName()
                 << "objectName=" << (button ? button->objectName() : QStringLiteral("<null>"))
                 << "text=" << (button ? button->text() : QStringLiteral("<null>"))
                 << "enabled=" << (button && button->isEnabled())
                 << "visible=" << (button && button->isVisible());
    }

    // 进入成绩管理/监考看板页时，教师/管理员先填充考试下拉框并立即刷新一次监考数据。
    if (targetPage == pageStats && (currentUser.role == UserRole::Teacher || currentUser.role == UserRole::Admin)) {
        refreshRiskExamFilter();
        refreshRiskRecords();
    }
    if (currentUser.role == UserRole::Teacher || currentUser.role == UserRole::Admin) {
        if (targetPage == pageQuestion) {
            discoverTeacherButtons(pageQuestion, QStringLiteral("teacher_questions"));
        } else if (targetPage == pagePublish) {
            discoverTeacherButtons(pagePublish, QStringLiteral("teacher_exams"));
        } else if (targetPage == pageStats) {
            discoverTeacherButtons(pageStats, QStringLiteral("teacher_scores"));
        }
    }
}

void MainWindow::updateNavHighlight(QPushButton *activeButton)
{
    // 显式管理左侧高亮：只有 activeButton 选中，其余全部取消（不依赖 QButtonGroup 隐式行为）。
    for (QPushButton *button : navButtons) {
        if (button) {
            button->setChecked(button == activeButton);
        }
    }
    currentNavButton = activeButton;
}

void MainWindow::bindTeacherButton(QPushButton *button,
                                   const QString &objectName,
                                   const QString &page,
                                   const QString &action,
                                   std::function<void()> handler)
{
    if (!button) {
        qWarning("[BIND_ERROR] button=%s reason=null", qPrintable(action));
        return;
    }

    button->setObjectName(objectName);
    button->setProperty("teacherButtonBound", true);
    button->setProperty("teacherButtonPage", page);
    button->setProperty("teacherButtonAction", action);
    disconnect(button, nullptr, this, nullptr);
    const std::function<void()> runHandler = [this, button, objectName, page, action, handler = std::move(handler)] {
        qInfo("[UI_CLICK] page=%s button=%s objectName=%s text=%s enabled=%d visible=%d via=bind",
              qPrintable(page),
              qPrintable(action),
              qPrintable(objectName),
              qPrintable(button ? button->text() : QStringLiteral("<null>")),
              button && button->isEnabled() ? 1 : 0,
              button && button->isVisible() ? 1 : 0);
        try {
            if (handler) {
                handler();
            } else {
                qWarning("[BIND_ERROR] button=%s reason=empty-handler", qPrintable(action));
                QMessageBox::warning(this,
                                     QStringLiteral("操作不可用"),
                                     QStringLiteral("按钮“%1”暂未绑定可执行操作。").arg(action));
            }
        } catch (const std::exception &ex) {
            qWarning("[BIND_ERROR] button=%s reason=exception detail=%s", qPrintable(action), ex.what());
            QMessageBox::critical(this,
                                  QStringLiteral("操作失败"),
                                  QStringLiteral("按钮操作发生异常：%1").arg(QString::fromLocal8Bit(ex.what())));
        } catch (...) {
            qWarning("[BIND_ERROR] button=%s reason=unknown-exception", qPrintable(action));
            QMessageBox::critical(this,
                                  QStringLiteral("操作失败"),
                                  QStringLiteral("按钮操作发生未知异常。"));
        }
    };
    connect(button, &QAbstractButton::clicked, this, [runHandler] { runHandler(); });
    registerButtonAction(button, page, action, runHandler);
}

void MainWindow::discoverTeacherButtons(QWidget *page, const QString &pageName) const
{
    if (!page) {
        qWarning("[TEACHER_BUTTON_DISCOVERED_COUNT] page=%s count=0 reason=null-page", qPrintable(pageName));
        return;
    }
    const QList<QPushButton *> buttons = page->findChildren<QPushButton *>();
    qInfo("[TEACHER_BUTTON_DISCOVERED_COUNT] page=%s count=%d", qPrintable(pageName), buttons.size());
    for (QPushButton *button : buttons) {
        if (!button) {
            continue;
        }
        qInfo("[TEACHER_BUTTON_DISCOVERED] page=%s objectName=%s text=%s enabled=%d visible=%d bound=%d",
              qPrintable(pageName),
              qPrintable(button->objectName()),
              qPrintable(button->text()),
              button->isEnabled() ? 1 : 0,
              button->isVisible() ? 1 : 0,
              button->property("teacherButtonBound").toBool() ? 1 : 0);
    }
}

// 导航切页统一走 triggerNav：eventFilter 命中 navButton 时按 navIndex 直接调用
//（麒麟下 button->click() 不会触发已连接的 clicked 槽，故不再依赖 clicked 信号）。
// 重复触发同一页会被 same-page 跳过。
void MainWindow::triggerNav(int index)
{
    if (index < 0 || index >= navButtons.size() || index >= navPages.size()) {
        qWarning() << "[NAV-ERROR] triggerNav bad index=" << index
                   << "navButtons=" << navButtons.size()
                   << "navPages=" << navPages.size();
        return;
    }
    QPushButton *button = navButtons.at(index);
    QWidget *page = navPages.at(index);
    const QString label = button ? button->property("navLabel").toString() : QString();
    triggerNav(button, page, label, QStringLiteral("eventFilter"));
}

void MainWindow::triggerNav(QPushButton *button, QWidget *page, const QString &label, const QString &source)
{
    qDebug() << "[NAV-TRIGGER]"
             << "source=" << source
             << "label=" << label
             << "button=" << button
             << "buttonName=" << (button ? button->objectName() : QStringLiteral("<null>"))
             << "page=" << page
             << "pageName=" << (page ? page->objectName() : QStringLiteral("<null>"))
             << "current=" << currentContentPage
             << "currentName=" << (currentContentPage ? currentContentPage->objectName() : QStringLiteral("<null>"));

    if (!button) {
        qWarning() << "[NAV-ERROR] triggerNav null button label=" << label << "source=" << source;
        return;
    }

    if (!page) {
        qWarning() << "[NAV-ERROR] triggerNav null page label=" << label << "source=" << source;
        updateNavHighlight(button);
        return;
    }

    updateNavHighlight(button);

    if (currentContentPage == page) {
        qDebug() << "[NAV-TRIGGER-SKIP]"
                 << "reason=same-page"
                 << "source=" << source
                 << "label=" << label
                 << "pageName=" << page->objectName();
        return;
    }

    switchContentPage(page, label);
}

// 阶段2A：普通按钮动作注册表。注册 = 记下 按钮→(page,name,handler) 并装事件过滤器；
// 分发 = eventFilter 命中已注册按钮时调用，打印 [BUTTON_ACTION]+[UI_CLICK] 后执行 handler。
void MainWindow::registerButtonAction(QPushButton *button,
                                      const QString &page,
                                      const QString &name,
                                      std::function<void()> handler)
{
    if (!button) {
        qWarning() << "[BUTTON_REGISTER_ERROR]"
                   << "page=" << page
                   << "button=" << name
                   << "reason=null-button";
        return;
    }

    button->installEventFilter(this);

    ButtonAction action;
    action.page = page;
    action.name = name;
    action.handler = std::move(handler);

    m_buttonActions.insert(button, std::move(action));
    connect(button, &QObject::destroyed, this, [this, button] {
        m_buttonActions.remove(button);
        m_activeButtonActions.remove(button);
    });

    qDebug() << "[BUTTON_REGISTER]"
             << "page=" << page
             << "button=" << name
             << "objectName=" << button->objectName()
             << "text=" << button->text();
}

// 临时对话框按钮专用：关窗后立刻从注册表移除，避免 QHash 残留已析构的 QPushButton 指针。
void MainWindow::unregisterButtonAction(QPushButton *button)
{
    if (!button) {
        return;
    }
    const auto it = m_buttonActions.constFind(button);
    if (it == m_buttonActions.constEnd()) {
        return;
    }
    qDebug() << "[BUTTON_UNREGISTER]"
             << "page=" << it->page
             << "button=" << it->name;
    m_buttonActions.remove(button);
    button->removeEventFilter(this);  // 对象级过滤器一并卸载（qApp 级过滤器查表已不命中）
}

bool MainWindow::dispatchRegisteredButtonAction(QPushButton *button)
{
    if (!button || !m_buttonActions.contains(button)) {
        return false;
    }

    // 同按钮防重入：只拦“这颗按钮的 handler 还没跑完又被触发”。
    // 不用全局布尔——那会把注册表 handler 内打开的模态框里的注册按钮
    //（导入预览“确认上传/取消”）也拦成 reentrant 吞掉，对话框按钮就永远没反应。
    if (m_activeButtonActions.contains(button)) {
        qWarning() << "[BUTTON_ACTION_SKIP]"
                   << "reason=reentrant"
                   << "objectName=" << button->objectName()
                   << "text=" << button->text();
        return true;
    }

    m_activeButtonActions.insert(button);
    // RAII 移除：handler 抛异常也保证出集合，否则这颗按钮从此被 reentrant 跳过吞掉。
    const auto handlingGuard = qScopeGuard([this, button] { m_activeButtonActions.remove(button); });

    const ButtonAction action = m_buttonActions.value(button);

    qDebug() << "[BUTTON_ACTION]"
             << "page=" << action.page
             << "button=" << action.name
             << "objectName=" << button->objectName()
             << "text=" << button->text();

    qDebug() << "[UI_CLICK]"
             << "page=" << action.page
             << "button=" << action.name;

    if (action.handler) {
        try {
            action.handler();
        } catch (const std::exception &ex) {
            qWarning() << "[BUTTON_ACTION_ERROR]"
                       << "page=" << action.page
                       << "button=" << action.name
                       << "reason=exception"
                       << "what=" << ex.what();
        } catch (...) {
            qWarning() << "[BUTTON_ACTION_ERROR]"
                       << "page=" << action.page
                       << "button=" << action.name
                       << "reason=exception"
                       << "what=unknown";
        }
    } else {
        qWarning() << "[BUTTON_ACTION_ERROR]"
                   << "page=" << action.page
                   << "button=" << action.name
                   << "reason=null-handler";
    }

    return true;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (questionTable && watched == questionTable->viewport() && event && event->type() == QEvent::Resize) {
        // 题干优先使用剩余空间；窄窗口保留可读宽度并由表格横向滚动。
        int otherWidth = 0;
        for (int column = 0; column < questionTable->columnCount(); ++column) {
            if (column != 3 && !questionTable->isColumnHidden(column)) otherWidth += questionTable->columnWidth(column);
        }
        questionTable->setColumnWidth(3, qMax(360, questionTable->viewport()->width() - otherWidth));
    }
    if (event && event->type() == QEvent::MouseButtonRelease) {
        if (auto *button = qobject_cast<QPushButton *>(watched)) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPoint localPos = mouseEvent->position().toPoint();
            const bool leftReleaseInside = mouseEvent->button() == Qt::LeftButton
                                           && button->isEnabled()
                                           && button->isVisible()
                                           && button->rect().contains(localPos);
            qDebug() << "[RAW_BUTTON_RELEASE]"
                     << "objectName=" << button->objectName()
                     << "text=" << button->text()
                     << "enabled=" << button->isEnabled()
                     << "visible=" << button->isVisible()
                     << "inside=" << leftReleaseInside
                     << "parent=" << (button->parent() ? button->parent()->metaObject()->className() : "<null>");
            // 仅对左侧导航按钮（objectName=="navButton"）做 raw 触发：按 navIndex 直接切页，
            // 绕开 clicked 信号（麒麟下 button->click() 不触发已连接的 clicked 槽）。
            if (leftReleaseInside && button->objectName() == QStringLiteral("navButton")) {
                const int index = button->property("navIndex").toInt();
                qDebug() << "[NAV-RAW-TRIGGER]"
                         << "index=" << index
                         << "text=" << button->text();
                triggerNav(index);
                return true;
            }
            // 阶段2A：注册过的普通按钮在此分发业务（绕开原生 clicked，与 navButton 同样的 raw 触发思路）。
            if (leftReleaseInside && m_buttonActions.contains(button)) {
                return dispatchRegisteredButtonAction(button);
            }
            // 未注册的普通按钮不接管，交给原生 clicked 处理。
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::refreshInitialData()
{
    if (currentUser.role == UserRole::Student) {
        refreshStudentExams();
        refreshMySubmissions();
    } else {
        refreshQuestionBank();
        refreshQuestionBankPage();
        refreshExamManagementList();
        populateSubmissionList();
        if (currentUser.role == UserRole::Admin) {
            populateAccountTable();
        }
        refreshRiskRecords();
    }
    loadStats();
}

void MainWindow::refreshQuestionBank()
{
    QJsonObject response;
    QString error;
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_questions"));
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("题库刷新失败"), error);
        return;
    }
    questionObjects = response.value(QStringLiteral("questions")).toArray();
    fillQuestionTables();
    refreshQuestionCategoryCounts();
    if (!questionTable) {
        refreshQuestionCount();
    }
}

void MainWindow::refreshQuestionBankPage()
{
    // 非分页接口的全部启用题目先筛选，再切页；翻页不再请求服务端分页。
    const QJsonArray filtered = filteredQuestionObjects();
    questionBankPageSize = qBound(1, questionBankPageSize, 100);
    questionBankTotal = static_cast<int>(filtered.size());
    questionBankTotalPages = qMax(1, (questionBankTotal + questionBankPageSize - 1) / questionBankPageSize);
    questionBankPage = qBound(1, questionBankPage, questionBankTotalPages);

    questionManagePageObjects = QJsonArray();
    const int begin = (questionBankPage - 1) * questionBankPageSize;
    const int end = qMin(begin + questionBankPageSize, questionBankTotal);
    for (int i = begin; i < end; ++i) {
        questionManagePageObjects.append(filtered.at(i));
    }

    qInfo("[QUESTION_PAGE_LOADED] page=%d pageSize=%d total=%d totalPages=%d rows=%d",
          questionBankPage,
          questionBankPageSize,
          questionBankTotal,
          questionBankTotalPages,
          questionManagePageObjects.size());

    fillQuestionManageTable();
    updateQuestionPageControls();
}

void MainWindow::fillQuestionTables()
{
    if (publishQuestionTable) {
        publishQuestionTable->setRowCount(0);
        for (const QJsonValue &value : questionObjects) {
            const QJsonObject question = value.toObject();
            const int row = publishQuestionTable->rowCount();
            publishQuestionTable->insertRow(row);
            auto *check = new QTableWidgetItem;
            check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
            check->setCheckState(Qt::Unchecked);
            publishQuestionTable->setItem(row, 0, check);
            publishQuestionTable->setItem(row, 1, readOnlyItem(QString::number(question.value(QStringLiteral("id")).toInt())));
            publishQuestionTable->setItem(row, 2, readOnlyItem(question.value(QStringLiteral("type")).toString()));
            publishQuestionTable->setItem(row, 3, readOnlyItem(question.value(QStringLiteral("course")).toString()));
            publishQuestionTable->setItem(row, 4, readOnlyItem(question.value(QStringLiteral("content")).toString()));
            publishQuestionTable->setItem(row, 5, readOnlyItem(valueString(question, QStringLiteral("score"))));
        }
    }
}

void MainWindow::fillQuestionManageTable()
{
    if (!questionTable) {
        return;
    }
    questionTable->setRowCount(0);
    for (const QJsonValue &value : questionManagePageObjects) {
        addQuestionRow(questionFromJson(value.toObject()));
        const int row = questionTable->rowCount() - 1;
        questionTable->item(row, 0)->setText(QString::number(value.toObject().value(QStringLiteral("id")).toInt()));
    }
    refreshQuestionCount();
}

void MainWindow::updateQuestionPageControls()
{
    const int totalPages = qMax(1, questionBankTotalPages);
    const int page = qBound(1, questionBankPage, totalPages);
    if (questionPageLabel) {
        questionPageLabel->setText(QStringLiteral("第 %1/%2 页 · 共 %3 题")
                                       .arg(page)
                                       .arg(totalPages)
                                       .arg(questionBankTotal));
    }
    if (questionPrevPageButton) {
        questionPrevPageButton->setEnabled(page > 1);
    }
    if (questionNextPageButton) {
        questionNextPageButton->setEnabled(page < totalPages);
    }
}

void MainWindow::goQuestionPage(int delta)
{
    const int totalPages = qMax(1, questionBankTotalPages);
    const int target = qBound(1, questionBankPage + delta, totalPages);
    qInfo("[QUESTION_PAGE_NAV] from=%d to=%d delta=%d pageSize=%d total=%d totalPages=%d rows=%d",
          questionBankPage,
          target,
          delta,
          questionBankPageSize,
          questionBankTotal,
          totalPages,
          questionManagePageObjects.size());
    if (target == questionBankPage) {
        updateQuestionPageControls();
        return;
    }
    questionBankPage = target;
    refreshQuestionBankPage();
}

void MainWindow::addQuestionRow(const QuestionRecord &question)
{
    if (!questionTable) {
        return;
    }

    const int row = questionTable->rowCount();
    questionTable->insertRow(row);
    const QStringList values = {
        QString(),
        question.type,
        question.course,
        question.content,
        question.optionA,
        question.optionB,
        question.optionC,
        question.optionD,
        question.answer,
        question.score,
        question.difficulty,
        question.analysis
    };

    for (int col = 0; col < values.size(); ++col) {
        questionTable->setItem(row, col, readOnlyItem(values.at(col)));
    }
}

void MainWindow::createQuestion()
{
    qInfo("[UI_CLICK] page=teacher_questions button=create_question");
    QuestionRecord question;
    if (!editQuestionDialog(this, QStringLiteral("新增题目"), &question)) {
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("create_question"));
    request.insert(QStringLiteral("question"), questionToJson(question));
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("新增题目失败"), error);
        return;
    }
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("题目已新增。")));
    refreshQuestionBank();
    refreshQuestionBankPage();
}

void MainWindow::editSelectedQuestion()
{
    qInfo("[UI_CLICK] page=teacher_questions button=edit_question");
    const int row = questionTable ? questionTable->currentRow() : -1;
    if (row < 0 || !questionTable->item(row, 0)) {
        QMessageBox::warning(this, QStringLiteral("请先选择题目"), QStringLiteral("请先在题目表格里点选一道题，再点击编辑。"));
        return;
    }

    const auto textAt = [this, row](int col) {
        return questionTable->item(row, col) ? questionTable->item(row, col)->text().trimmed() : QString();
    };

    QuestionRecord question;
    question.type = textAt(1);
    question.course = textAt(2);
    question.content = textAt(3);
    question.optionA = textAt(4);
    question.optionB = textAt(5);
    question.optionC = textAt(6);
    question.optionD = textAt(7);
    question.answer = textAt(8);
    question.score = textAt(9);
    question.difficulty = textAt(10);
    question.analysis = textAt(11);

    if (!editQuestionDialog(this, QStringLiteral("修改题目"), &question)) {
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("update_question"));
    request.insert(QStringLiteral("questionId"), textAt(0).toInt());
    request.insert(QStringLiteral("question"), questionToJson(question));
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("修改题目失败"), error);
        return;
    }
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("题目已保存。")));
    refreshQuestionBank();
    refreshQuestionBankPage();
}

void MainWindow::deleteSelectedQuestion()
{
    qInfo("[UI_CLICK] page=teacher_questions button=delete_question");
    const int row = questionTable ? questionTable->currentRow() : -1;
    if (row < 0 || !questionTable->item(row, 0)) {
        QMessageBox::warning(this, QStringLiteral("请先选择题目"), QStringLiteral("请先在题目表格里点选一道题，再点击删除。"));
        return;
    }
    const int questionId = questionTable->item(row, 0)->text().toInt();
    const QString content = questionTable->item(row, 3) ? questionTable->item(row, 3)->text() : QString();
    if (QMessageBox::question(this,
                              QStringLiteral("确认删除题目"),
                              QStringLiteral("确定删除题目 #%1 吗？\n%2").arg(questionId).arg(content.left(80)))
        != QMessageBox::Yes) {
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("delete_question"));
    request.insert(QStringLiteral("questionId"), questionId);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("删除题目失败"), error);
        return;
    }
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("题目已删除。")));
    refreshQuestionBank();
    refreshQuestionBankPage();
}

bool MainWindow::fetchAllQuestionsForDuplicateCheck(QJsonArray *questions, QString *error)
{
    if (questions) {
        *questions = QJsonArray();
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_questions"));
    QJsonObject response;
    QString requestError;
    if (!ApiClient::instance().request(request, &response, &requestError)) {
        if (error) {
            *error = requestError;
        }
        return false;
    }
    if (questions) {
        *questions = response.value(QStringLiteral("questions")).toArray();
    }
    if (error) {
        error->clear();
    }
    return true;
}

void MainWindow::detectDuplicateQuestions()
{
    qInfo("[QUESTION_DUPLICATE_SCAN_START]");

    QJsonArray allQuestions;
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool loaded = fetchAllQuestionsForDuplicateCheck(&allQuestions, &error);
    QApplication::restoreOverrideCursor();
    if (!loaded) {
        qWarning("[QUESTION_DUPLICATE_SCAN_DONE] groups=0 duplicates=0 rows=0 reason=%s", qPrintable(error));
        showError(QStringLiteral("检测重复题失败"), error);
        return;
    }

    QHash<QString, QList<DuplicateQuestionEntry>> groupsByKey;
    for (const QJsonValue &value : allQuestions) {
        const QJsonObject object = value.toObject();
        DuplicateQuestionEntry entry;
        entry.id = object.value(QStringLiteral("id")).toInt();
        entry.question = questionFromJson(object);
        entry.key = questionDuplicateKey(entry.question);
        if (!entry.key.trimmed().isEmpty()) {
            groupsByKey[entry.key].append(entry);
        }
    }

    QList<DuplicateQuestionGroup> duplicateGroups;
    int duplicateCount = 0;
    for (auto it = groupsByKey.cbegin(); it != groupsByKey.cend(); ++it) {
        QList<DuplicateQuestionEntry> entries = it.value();
        if (entries.size() < 2) {
            continue;
        }
        std::sort(entries.begin(), entries.end(), [](const DuplicateQuestionEntry &a, const DuplicateQuestionEntry &b) {
            return a.id < b.id;
        });
        DuplicateQuestionGroup group;
        group.keep = entries.takeFirst();
        group.duplicates = entries;
        duplicateCount += group.duplicates.size();
        duplicateGroups.append(group);
    }
    std::sort(duplicateGroups.begin(), duplicateGroups.end(), [](const DuplicateQuestionGroup &a, const DuplicateQuestionGroup &b) {
        return a.keep.id < b.keep.id;
    });

    const int duplicateGroupCount = static_cast<int>(duplicateGroups.size());
    const int scannedRows = static_cast<int>(allQuestions.size());
    qInfo("[QUESTION_DUPLICATE_SCAN_DONE] groups=%d duplicates=%d rows=%d",
          duplicateGroupCount, duplicateCount, scannedRows);

    if (duplicateGroups.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("重复题检测"), QStringLiteral("未发现重复题目。"));
        return;
    }

    QStringList detailLines;
    for (const DuplicateQuestionGroup &group : duplicateGroups) {
        const QString deleteIds = duplicateDeleteIdsText(group.duplicates);
        qInfo("[QUESTION_DUPLICATE_GROUP] keepId=%d deleteIds=%s groups=%d duplicates=%d course=%s type=%s preview=%s",
              group.keep.id,
              qPrintable(deleteIds),
              duplicateGroupCount,
              duplicateCount,
              qPrintable(group.keep.question.course),
              qPrintable(group.keep.question.type),
              qPrintable(duplicatePreviewText(group.keep.question.content)));
        detailLines.append(QStringLiteral("保留 id：%1").arg(group.keep.id));
        detailLines.append(QStringLiteral("清理候选 id：%1").arg(deleteIds));
        detailLines.append(QStringLiteral("课程：%1").arg(group.keep.question.course));
        detailLines.append(QStringLiteral("题型：%1").arg(group.keep.question.type));
        detailLines.append(QStringLiteral("题干：%1").arg(duplicatePreviewText(group.keep.question.content)));
        detailLines.append(QString());
    }

    QMessageBox messageBox(this);
    messageBox.setIcon(QMessageBox::Question);
    messageBox.setWindowTitle(QStringLiteral("检测重复题"));
    messageBox.setText(QStringLiteral("发现 %1 组重复题，共 %2 道重复项。是否保留每组最早题目并安全清理其余重复项？\n\n"
                                       "未被使用的重复题将直接删除；\n"
                                       "已被试卷或答卷使用的重复题将被停用，不会影响历史记录；\n"
                                       "停用题不会再参与新建试卷和随机组卷。")
                           .arg(duplicateGroups.size())
                           .arg(duplicateCount));
    messageBox.setDetailedText(detailLines.join(QLatin1Char('\n')));
    messageBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    messageBox.setDefaultButton(QMessageBox::No);
    if (auto *yesButton = messageBox.button(QMessageBox::Yes)) {
        yesButton->setText(QStringLiteral("安全清理重复项"));
    }
    if (auto *noButton = messageBox.button(QMessageBox::No)) {
        noButton->setText(QStringLiteral("取消"));
    }
    const bool confirmed = messageBox.exec() == QMessageBox::Yes;
    qInfo("[QUESTION_DUPLICATE_CLEAN_CONFIRM] accepted=%d groups=%d duplicates=%d",
          confirmed ? 1 : 0, duplicateGroupCount, duplicateCount);
    if (!confirmed) {
        return;
    }

    qInfo("[QUESTION_DUPLICATE_CLEAN_START] groups=%d duplicates=%d", duplicateGroupCount, duplicateCount);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int deleted = 0;
    int disabled = 0;
    int skipped = 0;
    int failed = 0;
    QStringList failureDetails;
    for (const DuplicateQuestionGroup &group : duplicateGroups) {
        for (const DuplicateQuestionEntry &entry : group.duplicates) {
            QJsonObject request;
            request.insert(QStringLiteral("type"), QStringLiteral("cleanup_duplicate_question"));
            request.insert(QStringLiteral("questionId"), entry.id);
            QJsonObject response;
            QString cleanupError;
            if (!ApiClient::instance().request(request, &response, &cleanupError)) {
                ++failed;
                failureDetails.append(QStringLiteral("#%1：%2").arg(entry.id).arg(cleanupError));
                qWarning("[QUESTION_DUPLICATE_CLEAN_ERROR] questionId=%d keepId=%d reason=%s",
                         entry.id, group.keep.id, qPrintable(cleanupError));
                continue;
            }
            const QString action = response.value(QStringLiteral("action")).toString(QStringLiteral("deleted"));
            if (action == QStringLiteral("disabled")) {
                ++disabled;
            } else if (action == QStringLiteral("skipped")) {
                ++skipped;
            } else {
                ++deleted;
            }
        }
    }
    QApplication::restoreOverrideCursor();

    qInfo("[QUESTION_DUPLICATE_CLEAN_DONE] groups=%d duplicates=%d deleted=%d disabled=%d skipped=%d failed=%d",
          duplicateGroupCount, duplicateCount, deleted, disabled, skipped, failed);
    refreshQuestionBank();
    refreshQuestionBankPage();

    QString resultText = QStringLiteral("重复题清理完成：\n"
                                        "已直接删除 %1 道未使用重复题；\n"
                                        "已停用 %2 道已使用重复题；\n"
                                        "失败 %3 道。")
                             .arg(deleted)
                             .arg(disabled)
                             .arg(failed);
    if (skipped > 0) {
        resultText.append(QStringLiteral("\n已跳过 %1 道已停用题。").arg(skipped));
    }
    if (failed > 0) {
        resultText.append(QStringLiteral("\n\n%1").arg(failureDetails.join(QLatin1Char('\n'))));
        QMessageBox::warning(this,
                             QStringLiteral("重复题清理完成但有失败"),
                             resultText);
        return;
    }
    QMessageBox::information(this, QStringLiteral("重复题清理完成"), resultText);
}

bool MainWindow::filterImportedDuplicateQuestions(const QList<QuestionRecord> &confirmed,
                                                  QList<QuestionRecord> *filtered)
{
    if (filtered) {
        filtered->clear();
    }

    const int parsed = confirmed.size();
    qInfo("[IMPORT_DUPLICATE_CHECK_START] parsed=%d", parsed);

    QJsonArray allQuestions;
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool loaded = fetchAllQuestionsForDuplicateCheck(&allQuestions, &error);
    QApplication::restoreOverrideCursor();
    if (!loaded) {
        qWarning("[IMPORT_DUPLICATE_CHECK_DONE] parsed=%d batchSkipped=0 existingSkipped=0 accepted=0 reason=%s",
                 parsed, qPrintable(error));
        showError(QStringLiteral("导入查重失败"), error);
        return false;
    }

    QSet<QString> existingKeys;
    for (const QJsonValue &value : allQuestions) {
        existingKeys.insert(questionDuplicateKey(questionFromJson(value.toObject())));
    }

    QList<QuestionRecord> accepted;
    QSet<QString> batchKeys;
    int batchSkipped = 0;
    int existingSkipped = 0;
    for (int i = 0; i < confirmed.size(); ++i) {
        const QuestionRecord question = confirmed.at(i);
        const QString key = questionDuplicateKey(question);
        if (batchKeys.contains(key)) {
            ++batchSkipped;
            qInfo("[IMPORT_DUPLICATE_SKIP_BATCH] parsed=%d batchSkipped=%d existingSkipped=%d accepted=%d row=%d course=%s type=%s preview=%s",
                  parsed,
                  batchSkipped,
                  existingSkipped,
                  static_cast<int>(accepted.size()),
                  i + 1,
                  qPrintable(question.course),
                  qPrintable(question.type),
                  qPrintable(duplicatePreviewText(question.content)));
            continue;
        }
        batchKeys.insert(key);

        if (existingKeys.contains(key)) {
            ++existingSkipped;
            qInfo("[IMPORT_DUPLICATE_SKIP_EXISTING] parsed=%d batchSkipped=%d existingSkipped=%d accepted=%d row=%d course=%s type=%s preview=%s",
                  parsed,
                  batchSkipped,
                  existingSkipped,
                  static_cast<int>(accepted.size()),
                  i + 1,
                  qPrintable(question.course),
                  qPrintable(question.type),
                  qPrintable(duplicatePreviewText(question.content)));
            continue;
        }
        accepted.append(question);
    }

    qInfo("[IMPORT_DUPLICATE_CHECK_DONE] parsed=%d batchSkipped=%d existingSkipped=%d accepted=%d",
          parsed, batchSkipped, existingSkipped, static_cast<int>(accepted.size()));

    if (accepted.isEmpty()) {
        qInfo("[IMPORT_DUPLICATE_ALL_SKIPPED] parsed=%d batchSkipped=%d existingSkipped=%d accepted=0",
              parsed, batchSkipped, existingSkipped);
        QMessageBox::information(this,
                                 QStringLiteral("没有可导入的新题目"),
                                 QStringLiteral("本次解析 %1 道题；文件内部重复跳过 %2 道，与现有题库重复跳过 %3 道，没有可导入的新题目。")
                                     .arg(parsed)
                                     .arg(batchSkipped)
                                     .arg(existingSkipped));
        return true;
    }

    QMessageBox::information(this,
                             QStringLiteral("导入查重完成"),
                             QStringLiteral("原文件解析题数：%1\n文件内部重复跳过：%2\n与现有题库重复跳过：%3\n实际准备导入：%4")
                                 .arg(parsed)
                                 .arg(batchSkipped)
                                 .arg(existingSkipped)
                                 .arg(accepted.size()));
    if (filtered) {
        *filtered = accepted;
    }
    return true;
}

void MainWindow::importQuestions()
{
    qInfo("[UI_CLICK] button=import_question_bank");
    qInfo("[UI_CLICK] page=teacher_questions button=import_question_bank");
    qInfo("[UI_CLICK] page=teacher_exams button=import_question_bank");
    qDebug() << "[QUESTION_IMPORT] button clicked";
    const QString pickerUrl = qEnvironmentVariable("QTEXAM_HOST_FILE_PICKER_URL").trimmed();
    if (pickerUrl.isEmpty()) {
        qInfo("[HOST_PICKER_ERROR] reason=env-empty fallback=local");
        importQuestionsLocal();
        return;
    }

    if (!importQuestionsViaHostPicker()) {
        return;
    }
}

bool MainWindow::importQuestionsViaHostPicker()
{
    QString localPath;
    QString failReason;
    QString failDetail;
    if (!pickFileFromWindowsHost(QStringLiteral("question_bank"), &localPath, &failReason, &failDetail)) {
        // 题库流程保留既有“本机导入”兜底（用户显式点选，不是 silent fallback）。
        if (failReason == QStringLiteral("request-failed") || failReason == QStringLiteral("http-timeout")) {
            QMessageBox messageBox(this);
            messageBox.setIcon(QMessageBox::Warning);
            messageBox.setWindowTitle(QStringLiteral("无法连接 Windows 题库选择服务"));
            messageBox.setText(QStringLiteral("无法连接 Windows 宿主机题库选择服务，请先运行 tools/windows_file_picker_server.py。"));
            messageBox.setInformativeText(QStringLiteral("%1\n\n可点击“本机导入”临时从麒麟本机或共享目录选择文件。").arg(failDetail));
            auto *localButton = messageBox.addButton(QStringLiteral("本机导入"), QMessageBox::AcceptRole);
            messageBox.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
            messageBox.exec();
            if (messageBox.clickedButton() == localButton) {
                importQuestionsLocal();
            }
        }
        return false;
    }
    return importQuestionFile(localPath);
}

// 通用 Windows 宿主机文件选择：请求 QTEXAM_HOST_FILE_PICKER_URL，宿主机弹资源管理器，
// 文件经 base64 拉回 /tmp/qtexam_imports/<filename>。mode 仅区分日志与文案
// （question_bank=题库 / full_exam=试卷）。env-empty / request-failed 不在此弹框，
// 经 failReason/failDetail 交调用方决定兜底策略；其余失败（取消/JSON/解码/落盘）在此提示。
bool MainWindow::pickFileFromWindowsHost(const QString &mode, QString *localPath,
                                         QString *failReason, QString *failDetail)
{
    const auto setFail = [failReason, failDetail](const QString &reason, const QString &detail) {
        if (failReason) {
            *failReason = reason;
        }
        if (failDetail) {
            *failDetail = detail;
        }
    };
    const bool isFullExam = mode == QStringLiteral("full_exam");
    const QString noun = isFullExam ? QStringLiteral("试卷") : QStringLiteral("题库");

    const QString pickerUrl = qEnvironmentVariable("QTEXAM_HOST_FILE_PICKER_URL").trimmed();
    if (pickerUrl.isEmpty()) {
        qWarning("[HOST_PICKER_ERROR] mode=%s reason=env-empty", qPrintable(mode));
        setFail(QStringLiteral("env-empty"), QStringLiteral("环境变量 QTEXAM_HOST_FILE_PICKER_URL 未设置"));
        return false;
    }

    qInfo("[HOST_PICKER_START] mode=%s url=%s", qPrintable(mode), qPrintable(pickerUrl));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    QString httpFailReason;
    // 改走阻塞式 QTcpSocket（不经 QNAM/事件循环）：麒麟实测 QNAM 链路连 QTimer 超时都等不到。
    // 60 秒总额度覆盖用户在 Windows 端选文件的过程，请在 60 秒内完成选择，超时明确报错不悬挂。
    const QByteArray bodyData = simpleHttpGetViaSocket(QUrl(pickerUrl), 60000, &error, &httpFailReason);
    QApplication::restoreOverrideCursor();
    if (bodyData.isEmpty() && !error.isEmpty()) {
        // http-timeout 单列（按规范输出 reason=http-timeout），其余统一 request-failed。
        const QString reason = httpFailReason == QStringLiteral("http-timeout")
                                   ? QStringLiteral("http-timeout")
                                   : QStringLiteral("request-failed");
        qWarning("[HOST_PICKER_ERROR] mode=%s reason=%s detail=%s",
                 qPrintable(mode), qPrintable(reason), qPrintable(error));
        setFail(reason, error);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bodyData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("[HOST_PICKER_ERROR] mode=%s reason=invalid-json detail=%s", qPrintable(mode), qPrintable(parseError.errorString()));
        setFail(QStringLiteral("invalid-json"), parseError.errorString());
        QMessageBox::warning(this,
                             QStringLiteral("文件选择服务返回异常"),
                             QStringLiteral("Windows 文件选择服务返回的数据无法解析：%1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject response = doc.object();
    if (!response.value(QStringLiteral("ok")).toBool(false)) {
        const QString reason = response.value(QStringLiteral("reason")).toString(QStringLiteral("unknown"));
        setFail(reason, reason);
        if (reason == QStringLiteral("canceled")) {
            qInfo("[HOST_PICKER_CANCELLED] mode=%s", qPrintable(mode));
            QMessageBox::information(this,
                                     QStringLiteral("已取消选择"),
                                     QStringLiteral("已取消选择%1文件。").arg(noun));
        } else {
            qWarning("[HOST_PICKER_ERROR] mode=%s reason=%s", qPrintable(mode), qPrintable(reason));
            QMessageBox::warning(this,
                                 QStringLiteral("Windows %1选择失败").arg(noun),
                                 QStringLiteral("Windows 宿主机%1选择失败：%2").arg(noun, reason));
        }
        return false;
    }

    QString filename = QFileInfo(response.value(QStringLiteral("filename")).toString()).fileName();
    if (filename.trimmed().isEmpty()) {
        filename = isFullExam ? QStringLiteral("full_exam.xlsx") : QStringLiteral("question_bank.xlsx");
    }
    filename.replace(QLatin1Char('/'), QLatin1Char('_'));
    filename.replace(QLatin1Char('\\'), QLatin1Char('_'));
    filename.replace(QLatin1Char(':'), QLatin1Char('_'));

    const QString encoded = response.value(QStringLiteral("content_base64")).toString();
    const QByteArray fileData = QByteArray::fromBase64(encoded.toLatin1());
    if (fileData.isEmpty() && !encoded.isEmpty()) {
        qWarning("[HOST_PICKER_ERROR] mode=%s reason=base64-decode-empty filename=%s", qPrintable(mode), qPrintable(filename));
        setFail(QStringLiteral("base64-decode-empty"), filename);
        QMessageBox::warning(this,
                             QStringLiteral("%1文件接收失败").arg(noun),
                             QStringLiteral("Windows 返回的文件内容无法解码。"));
        return false;
    }

    QDir importDir(QDir::tempPath() + QStringLiteral("/qtexam_imports"));
    if (!importDir.exists() && !importDir.mkpath(QStringLiteral("."))) {
        qWarning("[HOST_PICKER_ERROR] mode=%s reason=mkpath-failed dir=%s", qPrintable(mode), qPrintable(importDir.absolutePath()));
        setFail(QStringLiteral("mkpath-failed"), importDir.absolutePath());
        QMessageBox::warning(this,
                             QStringLiteral("%1文件保存失败").arg(noun),
                             QStringLiteral("无法创建临时目录：%1").arg(importDir.absolutePath()));
        return false;
    }

    const QString savedPath = importDir.filePath(filename);
    QFile file(savedPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("[HOST_PICKER_ERROR] mode=%s reason=write-open-failed file=%s detail=%s",
                 qPrintable(mode),
                 qPrintable(savedPath),
                 qPrintable(file.errorString()));
        setFail(QStringLiteral("write-open-failed"), file.errorString());
        QMessageBox::warning(this,
                             QStringLiteral("%1文件保存失败").arg(noun),
                             QStringLiteral("无法写入临时文件：%1\n%2").arg(savedPath, file.errorString()));
        return false;
    }
    const qint64 written = file.write(fileData);
    file.close();
    if (written != fileData.size()) {
        qWarning("[HOST_PICKER_ERROR] mode=%s reason=write-incomplete file=%s written=%lld expected=%lld",
                 qPrintable(mode),
                 qPrintable(savedPath),
                 static_cast<long long>(written),
                 static_cast<long long>(fileData.size()));
        setFail(QStringLiteral("write-incomplete"), savedPath);
        QMessageBox::warning(this,
                             QStringLiteral("%1文件保存失败").arg(noun),
                             QStringLiteral("临时文件写入不完整，请重试。"));
        return false;
    }

    qInfo("[HOST_PICKER_SUCCESS] mode=%s filename=%s size=%lld path=%s",
          qPrintable(mode),
          qPrintable(filename),
          static_cast<long long>(fileData.size()),
          qPrintable(savedPath));
    if (localPath) {
        *localPath = savedPath;
    }
    return true;
}

void MainWindow::importQuestionsLocal()
{
    // Windows Host Bridge：题库放在 Windows 共享目录 <shared-directory>\题库，
    // 经 VMware 共享文件夹挂载到麒麟 /mnt/hgfs/QtExamShared/题库。
    // 文件对话框优先定位到 /mnt/hgfs，方便直接选 Windows 端题库。
    QString startDir;
    const QStringList sharedCandidates = {
        QStringLiteral("/mnt/hgfs/QtExamShared/题库"),
        QStringLiteral("/mnt/hgfs/QtExamShared"),
        QStringLiteral("/mnt/hgfs")
    };
    for (const QString &candidate : sharedCandidates) {
        if (QFileInfo::exists(candidate)) {
            startDir = candidate;
            break;
        }
    }
#ifdef Q_OS_LINUX
    if (startDir.isEmpty() && !QFileInfo::exists(QStringLiteral("/mnt/hgfs"))) {
        // 仅在 Linux/麒麟上提示共享文件夹未启用（Windows 本机不提示）。
        QMessageBox::information(this, QStringLiteral("从 Windows 导入题库"),
            QStringLiteral("未发现 VMware 共享目录 /mnt/hgfs。\n\n"
                           "如需导入 Windows 端题库，请先在 VMware 启用「共享文件夹」，"
                           "把 Windows 的 <shared-directory> 共享给虚拟机，再从 /mnt/hgfs 选择文件。\n\n"
                           "你也可以继续从麒麟本地目录选择题库文件。"));
    }
#endif

    qDebug() << "[QUESTION_IMPORT] opening file dialog parent=" << this << "startDir=" << startDir;
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择题库文件（支持 Excel/CSV/Word）"),
        startDir,
        QStringLiteral("题库文件 (*.xlsx *.csv *.docx);;Excel 题库 (*.xlsx);;CSV 题库 (*.csv);;Word 试卷 (*.docx);;所有文件 (*.*)"));
    if (fileName.isEmpty()) {
        qDebug() << "[QUESTION_IMPORT] cancelled";
        qInfo("[IMPORT_ERROR] mode=question_bank reason=canceled");
        return;
    }
    qDebug() << "[QUESTION_IMPORT] selected file:" << fileName;
    importQuestionFile(fileName);
}

// 从“文件路径”导入题库的统一入口：客户端解析 -> 人工确认 -> 上传到 Ubuntu 服务端。
// 本地 QFileDialog 导入和“从 Windows 获取题库”都复用它，避免重复解析代码。
bool MainWindow::importQuestionFile(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    qInfo("[IMPORT_START] mode=question_bank file=%s suffix=%s", qPrintable(filePath), qPrintable(suffix));
    qDebug() << "[QUESTION_IMPORT] suffix:" << suffix;

    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        qWarning("[IMPORT_ERROR] reason=file-not-found file=%s", qPrintable(filePath));
        QMessageBox::warning(this, QStringLiteral("文件不存在"), QStringLiteral("找不到题库文件：\n%1").arg(filePath));
        return false;
    }
    if (QFileInfo(filePath).size() == 0) {
        qWarning("[IMPORT_ERROR] reason=empty-file file=%s", qPrintable(filePath));
        QMessageBox::warning(this, QStringLiteral("题库文件为空"),
                             QStringLiteral("所选题库文件为 0 字节，请选择包含题目内容的文件。"));
        return false;
    }
    // 明确支持的格式，避免界面误导用户选了不支持的类型。
    static const QStringList kSupported = {QStringLiteral("xlsx"), QStringLiteral("csv"), QStringLiteral("docx")};
    if (!kSupported.contains(suffix)) {
        qWarning("[IMPORT_ERROR] reason=unsupported-format suffix=%s", qPrintable(suffix));
        QMessageBox::warning(this, QStringLiteral("格式不支持"),
                             QStringLiteral("当前仅支持 Excel(.xlsx)、CSV(.csv)、Word(.docx) 题库文件。\n"
                                            "你选择的是 .%1 文件，请改用受支持的模板（可点「下载模板」）。").arg(suffix));
        return false;
    }

    qDebug() << "[QUESTION_IMPORT] parser start";
    const ImportResult result = importQuestionsWithDocxMode(this, filePath);
    qDebug() << "[QUESTION_IMPORT] parser done success=" << result.success;
    qDebug() << "[QUESTION_IMPORT] data rows:" << result.questions.size();
    if (!result.success) {
        const QString reason = result.error.isEmpty() ? QStringLiteral("文件解析失败（格式或内容不符合模板）") : result.error;
        qWarning("[IMPORT_ERROR] reason=parse-failed detail=%s", qPrintable(reason));
        QMessageBox::warning(this, QStringLiteral("解析失败"),
                             QStringLiteral("题库文件解析失败：\n%1\n\n请确认文件是按模板填写的 .xlsx/.csv/.docx。").arg(reason));
        return false;
    }
    if (result.questions.isEmpty()) {
        qWarning("[IMPORT_ERROR] reason=no-questions-parsed file=%s", qPrintable(filePath));
        QMessageBox::warning(this, QStringLiteral("没有可导入的题目"),
                             QStringLiteral("文件解析成功，但没有读取到任何题目。\n请检查表头与内容是否符合模板。"));
        return false;
    }

    QList<QuestionRecord> confirmed;
    qDebug() << "[QUESTION_IMPORT] preview start";
    if (!confirmImportedQuestions(result.questions, result.warnings, &confirmed)) {
        qDebug() << "[QUESTION_IMPORT] preview done accepted=false";
        qInfo("[IMPORT_ERROR] reason=user-cancelled-confirm");
        return false;
    }
    qDebug() << "[QUESTION_IMPORT] preview done accepted=true rows:" << confirmed.size();

    QList<QuestionRecord> filtered;
    if (!filterImportedDuplicateQuestions(confirmed, &filtered)) {
        return false;
    }
    if (filtered.isEmpty()) {
        return true;
    }

    QJsonArray questionIds;
    QString error;
    if (!uploadQuestions(filtered, &questionIds, &error)) {
        qWarning("[IMPORT_ERROR] reason=server-write-failed detail=%s", qPrintable(error));
        showError(QStringLiteral("题目写入服务端失败"),
                  QStringLiteral("解析成功但写入服务端失败：\n%1\n\n请确认已连接到 Ubuntu 服务端(127.0.0.1:9000)。").arg(error));
        return false;
    }
    qInfo("[IMPORT_SUCCESS] count=%d", filtered.size());
    showToast(QStringLiteral("已导入 %1 道题到服务端题库。").arg(filtered.size()));
    refreshQuestionBank();
    refreshQuestionBankPage();
    qDebug() << "[QUESTION_IMPORT] refresh done questionTable=" << questionTable;
    return true;
}

// “从 Windows 获取题库”：列出 Windows 题库代理目录文件 -> 下载到麒麟临时目录 ->
// 复用 importQuestionFile() 解析并写入 Ubuntu 服务端。代理不可用时仅在此处报错，
// 不影响本地“导入题库”按钮，也不让客户端崩溃。
void MainWindow::importQuestionsFromWindowsProxy()
{
    const QString defaultSource = qEnvironmentVariable("QTEXAM_QUESTION_BANK_SOURCE",
                                                       QStringLiteral("http://127.0.0.1:9200"));

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("从 Windows 获取题库"));
    dialog.resize(700, 470);
    auto *layout = new QVBoxLayout(&dialog);

    auto *addrRow = new QHBoxLayout;
    addrRow->addWidget(new QLabel(QStringLiteral("Windows 题库代理地址：")));
    auto *addrEdit = new QLineEdit(defaultSource);
    addrEdit->setPlaceholderText(QStringLiteral("http://127.0.0.1:9200"));
    auto *refreshBtn = new QPushButton(QStringLiteral("刷新列表"));
    addrRow->addWidget(addrEdit, 1);
    addrRow->addWidget(refreshBtn);
    layout->addLayout(addrRow);

    auto *hint = new QLabel(QStringLiteral(
        "需先在 Windows 主机运行 tools/win_question_bank_proxy.py，并把 .xlsx/.xls/.csv/.docx/.txt/.json "
        "题库文件放入共享目录。下载的文件只是临时解析用，最终题目写入 Ubuntu 服务端数据库。"));
    hint->setObjectName(QStringLiteral("mutedLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *table = new QTableWidget(0, 4, &dialog);
    table->setHorizontalHeaderLabels({QStringLiteral("文件名"), QStringLiteral("类型"),
                                      QStringLiteral("大小"), QStringLiteral("修改时间")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(0, 330);
    table->setColumnWidth(1, 70);
    table->setColumnWidth(2, 100);
    layout->addWidget(table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *importBtn = buttons->addButton(QStringLiteral("下载并导入"), QDialogButtonBox::AcceptRole);
    importBtn->setEnabled(false);
    layout->addWidget(buttons);

    const auto normalizedBase = [](QString s) {
        s = s.trimmed();
        while (s.endsWith(QLatin1Char('/'))) {
            s.chop(1);
        }
        return s;
    };

    const auto doRefresh = [&]() {
        const QString base = normalizedBase(addrEdit->text());
        if (base.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("地址为空"), QStringLiteral("请输入 Windows 题库代理地址。"));
            return;
        }
        refreshBtn->setEnabled(false);
        importBtn->setEnabled(false);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QByteArray bodyData;
        QString error;
        const bool ok = simpleHttpGet(base + QStringLiteral("/files"), &bodyData, &error, 6000);
        QApplication::restoreOverrideCursor();
        refreshBtn->setEnabled(true);
        if (!ok) {
            QMessageBox::warning(&dialog, QStringLiteral("连接 Windows 题库代理失败"),
                QStringLiteral("%1\n\n请确认：\n1) Windows 已运行 win_question_bank_proxy.py\n"
                               "2) 地址/端口正确（默认 9200）\n3) Windows 防火墙已放行该端口").arg(error));
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bodyData, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(&dialog, QStringLiteral("数据异常"), QStringLiteral("题库代理返回的数据无法解析。"));
            return;
        }
        const QJsonArray files = doc.object().value(QStringLiteral("files")).toArray();
        table->setRowCount(0);
        for (const QJsonValue &value : files) {
            const QJsonObject f = value.toObject();
            const int row = table->rowCount();
            table->insertRow(row);
            auto *nameItem = new QTableWidgetItem(f.value(QStringLiteral("name")).toString());
            nameItem->setData(Qt::UserRole, f.value(QStringLiteral("name")).toString());
            table->setItem(row, 0, nameItem);
            table->setItem(row, 1, new QTableWidgetItem(f.value(QStringLiteral("type")).toString()));
            const double sizeKb = f.value(QStringLiteral("size")).toDouble() / 1024.0;
            table->setItem(row, 2, new QTableWidgetItem(QStringLiteral("%1 KB").arg(sizeKb, 0, 'f', 1)));
            table->setItem(row, 3, new QTableWidgetItem(f.value(QStringLiteral("mtime")).toString()));
        }
        if (table->rowCount() == 0) {
            QMessageBox::information(&dialog, QStringLiteral("没有题库文件"),
                QStringLiteral("Windows 题库目录里没有可导入的题库文件（支持 .xlsx .xls .csv .docx .txt .json）。"));
        }
        importBtn->setEnabled(table->currentRow() >= 0 && table->rowCount() > 0);
    };

    connect(refreshBtn, &QAbstractButton::clicked, &dialog, doRefresh);
    connect(table, &QTableWidget::itemSelectionChanged, &dialog, [&]() {
        importBtn->setEnabled(table->currentRow() >= 0);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const int row = table->currentRow();
        if (row < 0 || !table->item(row, 0)) {
            QMessageBox::warning(&dialog, QStringLiteral("未选择文件"), QStringLiteral("请先选择一个题库文件。"));
            return;
        }
        const QString name = table->item(row, 0)->data(Qt::UserRole).toString();
        const QString base = normalizedBase(addrEdit->text());

        const QString tmpDir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                   .filePath(QStringLiteral("NovaExam/imports"));
        if (!QDir().mkpath(tmpDir)) {
            QMessageBox::warning(&dialog, QStringLiteral("无法创建临时目录"), tmpDir);
            return;
        }

        QUrl url(base + QStringLiteral("/download"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("name"), name);
        url.setQuery(query);

        importBtn->setEnabled(false);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QByteArray fileData;
        QString error;
        const bool ok = simpleHttpGet(url.toString(QUrl::FullyEncoded), &fileData, &error, 20000);
        QApplication::restoreOverrideCursor();
        importBtn->setEnabled(true);
        if (!ok) {
            QMessageBox::warning(&dialog, QStringLiteral("下载失败"),
                QStringLiteral("从 Windows 题库代理下载文件失败：\n%1").arg(error));
            return;
        }
        if (fileData.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("下载失败"), QStringLiteral("下载到的文件内容为空。"));
            return;
        }

        const QString localPath = QDir(tmpDir).filePath(QFileInfo(name).fileName());
        QFile out(localPath);
        if (!out.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(&dialog, QStringLiteral("写入失败"), QStringLiteral("无法写入临时文件：%1").arg(localPath));
            return;
        }
        out.write(fileData);
        out.close();

        // 复用统一导入入口：解析 -> 确认 -> 写入 Ubuntu 服务端。
        if (importQuestionFile(localPath)) {
            dialog.accept();
        }
        // 失败时保留对话框（错误已在 importQuestionFile 内提示），方便重选/重试。
    });

    // 打开即自动拉一次文件列表。
    QTimer::singleShot(0, &dialog, doRefresh);
    dialog.exec();
}

void MainWindow::importExamFromFile()
{
    qInfo("[UI_CLICK] page=teacher_exams button=import_full_exam");
    // 主按钮强制走 Windows 宿主机选择框（绝不默认弹麒麟本机 QFileDialog）；
    // 本机/共享目录导入仅保留在 文件 菜单作为显式备用入口（importExamFromFileLocal）。
    QString localPath;
    QString failReason;
    QString failDetail;
    if (!pickFileFromWindowsHost(QStringLiteral("full_exam"), &localPath, &failReason, &failDetail)) {
        // 服务不可用/超时给出明确指引，不做 silent fallback。
        if (failReason == QStringLiteral("env-empty") || failReason == QStringLiteral("request-failed")
            || failReason == QStringLiteral("http-timeout")) {
            QMessageBox::critical(this,
                                  QStringLiteral("Windows 文件选择服务不可用"),
                                  QStringLiteral("Windows 宿主机文件选择服务不可用。\n\n"
                                                 "请先在 Windows PowerShell 运行：\n"
                                                 "cd \"<client-source-directory>\"\n"
                                                 "python tools\\windows_file_picker_server.py --host 0.0.0.0 --port 8766\n\n"
                                                 "详情：%1").arg(failDetail));
        }
        return;
    }
    importExamFile(localPath);
}

// 显式备用入口（仅文件菜单触发）：从麒麟本机/VMware 共享目录选整套试卷文件。
void MainWindow::importExamFromFileLocal()
{
    // 优先定位到 VMware 共享目录，便于从 Windows 端选整套试卷文件。
    QString startDir;
    for (const QString &candidate : {QStringLiteral("/mnt/hgfs/QtExamShared/题库"),
                                     QStringLiteral("/mnt/hgfs/QtExamShared"),
                                     QStringLiteral("/mnt/hgfs")}) {
        if (QFileInfo::exists(candidate)) { startDir = candidate; break; }
    }
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择整套试卷文件（Excel/CSV/Word）"),
        startDir,
        QStringLiteral("试卷文件 (*.xlsx *.csv *.docx);;Excel 试卷 (*.xlsx);;CSV 试卷 (*.csv);;Word 试卷 (*.docx);;所有文件 (*.*)"));
    if (fileName.isEmpty()) {
        return;
    }
    importExamFile(fileName);
}

// 整套试卷导入主流程：解析 → 预览确认 → 上传题目 → 创建未发布试卷（两个入口共用）。
void MainWindow::importExamFile(const QString &filePath)
{
    qInfo("[IMPORT_EXAM_PARSE_START] file=%s", qPrintable(filePath));
    qInfo("[IMPORT_EXAM_START] file=%s", qPrintable(filePath));
    if (publishEndEdit->dateTime() <= publishStartEdit->dateTime()) {
        QMessageBox::warning(this, QStringLiteral("时间不合法"), QStringLiteral("截止时间必须晚于开始时间。"));
        return;
    }
    if (publishNameEdit->text().trimmed().isEmpty()) {
        publishNameEdit->setText(QFileInfo(filePath).completeBaseName());
    }

    const ImportResult result = importQuestionsWithDocxMode(this, filePath);
    if (!result.success) {
        // 空 error 通常是用户在 Word 模式选择框点了取消；其余给出可操作的具体原因。
        const QString reason = result.error.isEmpty()
            ? QStringLiteral("未能解析出题目（可能已取消，或文件不含可识别的题号/表头）。\n"
                             "请确认：①是 .xlsx/.csv/.docx 文本文件（不支持图片/扫描 PDF/OCR）；"
                             "②Word 试卷含题号或章节标题，表格含“题干”列或符合无表头列顺序。")
            : result.error;
        qWarning("[IMPORT_EXAM_ERROR] reason=%s", qPrintable(reason));
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("整套试卷解析失败：\n%1").arg(reason));
        return;
    }
    double totalScore = 0;
    for (const QuestionRecord &q : result.questions) {
        totalScore += q.score.toDouble();
    }
    qInfo("[IMPORT_EXAM_PARSED] questions=%d totalScore=%.1f", result.questions.size(), totalScore);

    QList<QuestionRecord> confirmed;
    if (!confirmImportedQuestions(result.questions, result.warnings, &confirmed)) {
        qInfo("[IMPORT_EXAM_ERROR] reason=user-cancelled");
        return;
    }

    QJsonArray questionIds;
    QString error;
    if (!uploadQuestions(confirmed, &questionIds, &error)) {
        qWarning("[IMPORT_EXAM_ERROR] reason=upload-questions-failed detail=%s", qPrintable(error));
        showError(QStringLiteral("上传试卷题目失败"), error);
        return;
    }
    if (questionIds.size() != confirmed.size()) {
        qWarning("[IMPORT_EXAM_ERROR] reason=questionId-count-mismatch");
        showError(QStringLiteral("创建试卷失败"), QStringLiteral("服务端返回的题目编号数量不完整，请刷新题库后重试。"));
        refreshQuestionBank();
        refreshQuestionBankPage();
        return;
    }

    QJsonObject exam;
    exam.insert(QStringLiteral("name"), publishNameEdit->text().trimmed());
    exam.insert(QStringLiteral("course"), publishCourseEdit->text().trimmed());
    exam.insert(QStringLiteral("startAt"), publishStartEdit->dateTime().toUTC().toString(Qt::ISODate));
    exam.insert(QStringLiteral("endAt"), publishEndEdit->dateTime().toUTC().toString(Qt::ISODate));
    exam.insert(QStringLiteral("durationMinutes"), publishDurationSpin->value());
    exam.insert(QStringLiteral("maxSubmissions"), publishMaxSubmissionsSpin->value());

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("create_exam"));
    request.insert(QStringLiteral("exam"), exam);
    request.insert(QStringLiteral("questionIds"), questionIds);

    QJsonObject response;
    if (!ApiClient::instance().request(request, &response, &error)) {
        qWarning("[IMPORT_EXAM_ERROR] reason=create-exam-failed detail=%s", qPrintable(error));
        showError(QStringLiteral("创建试卷失败"), error);
        refreshQuestionBank();
        refreshQuestionBankPage();
        return;
    }
    const int newExamId = response.value(QStringLiteral("examId")).toInt();
    qInfo("[EXAM_CREATE_SUCCESS] examId=%d", newExamId);
    qInfo("[EXAM_LINK_SUCCESS] examId=%d questionCount=%d", newExamId, confirmed.size());

    showToast(QStringLiteral("已导入 %1 道题，并创建一张未发布试卷（在考试列表中点“发布选中试卷”即可对学生开放）。").arg(confirmed.size()));
    refreshQuestionBank();
    refreshQuestionBankPage();
    refreshExamManagementList();
    loadStats();
    if (newExamId > 0 && examManagementTable) {
        for (int r = 0; r < examManagementTable->rowCount(); ++r) {
            if (examManagementTable->item(r, 0) && examManagementTable->item(r, 0)->text().toInt() == newExamId) {
                examManagementTable->selectRow(r);
                break;
            }
        }
    }
}

bool MainWindow::confirmImportedQuestions(const QList<QuestionRecord> &imported,
                                          const QStringList &warnings,
                                          QList<QuestionRecord> *confirmed)
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Word/Excel 题库导入预览"));
    dialog.resize(1180, 680);
    dialog.setObjectName(QStringLiteral("importPreviewDialog"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);
    auto *hero = card();
    hero->layout()->addWidget(sectionTitle(QStringLiteral("导入预览与人工校对"),
                                           QStringLiteral("已完成宽松识别。上传前可以直接修改题型、题干、选项、参考答案和分值。")));
    hero->layout()->addWidget(mutedLabel(QStringLiteral("识别到 %1 道题。分值为空或识别不准时，请在表格中修正后再上传。").arg(imported.size())));
    if (!warnings.isEmpty()) {
        auto *warning = mutedLabel(warnings.join(QStringLiteral("\n")));
        warning->setObjectName(QStringLiteral("warningText"));
        hero->layout()->addWidget(warning);
    }
    layout->addWidget(hero);

    auto *table = new QTableWidget(imported.size(), 11);
    qDebug() << "[QUESTION_IMPORT] tableWidget=" << table << "rows=" << imported.size();
    table->setObjectName(QStringLiteral("previewTable"));
    table->setHorizontalHeaderLabels({
        QStringLiteral("题型"), QStringLiteral("课程"), QStringLiteral("题干"), QStringLiteral("选项A"),
        QStringLiteral("选项B"), QStringLiteral("选项C"), QStringLiteral("选项D"), QStringLiteral("参考答案"),
        QStringLiteral("分值"), QStringLiteral("难度"), QStringLiteral("解析")
    });
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setColumnWidth(0, 90);
    table->setColumnWidth(2, 360);
    table->setColumnWidth(7, 180);
    table->setColumnWidth(8, 70);
    for (int row = 0; row < imported.size(); ++row) {
        const QuestionRecord question = imported.at(row);
        const QStringList values = {
            question.type, question.course, question.content, question.optionA, question.optionB,
            question.optionC, question.optionD, question.answer, question.score, question.difficulty, question.analysis
        };
        for (int col = 0; col < values.size(); ++col) {
            table->setItem(row, col, new QTableWidgetItem(values.at(col)));
        }
    }
    layout->addWidget(table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认上传"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("primaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // 麒麟上对话框按钮的 clicked 同样不可靠：确认/取消接入按钮动作注册表，
    // 鼠标点击走 raw 分发（handler 只做 accept/reject，真实业务仍在 exec 返回后），
    // 键盘 Enter/Esc 继续走上面的原生 accepted/rejected 连接。关窗后立即注销，防悬空指针。
    QPushButton *confirmButton = buttons->button(QDialogButtonBox::Ok);
    QPushButton *cancelButton = buttons->button(QDialogButtonBox::Cancel);
    registerButtonAction(confirmButton, QStringLiteral("import_preview"), QStringLiteral("confirm_upload"),
                         [&dialog] { dialog.accept(); });
    registerButtonAction(cancelButton, QStringLiteral("import_preview"), QStringLiteral("cancel"),
                         [&dialog] { dialog.reject(); });
    const int dialogResult = dialog.exec();
    unregisterButtonAction(confirmButton);
    unregisterButtonAction(cancelButton);

    if (dialogResult != QDialog::Accepted) {
        return false;
    }
    qInfo("[IMPORT_PREVIEW_CONFIRM] questions=%d", table->rowCount());

    confirmed->clear();
    for (int row = 0; row < table->rowCount(); ++row) {
        QuestionRecord question;
        const auto textAt = [table, row](int col) {
            return table->item(row, col) ? table->item(row, col)->text().trimmed() : QString();
        };
        question.type = textAt(0);
        question.course = textAt(1);
        question.content = textAt(2);
        question.optionA = textAt(3);
        question.optionB = textAt(4);
        question.optionC = textAt(5);
        question.optionD = textAt(6);
        question.answer = textAt(7);
        question.score = textAt(8);
        question.difficulty = textAt(9);
        question.analysis = textAt(10);
        if (!question.type.isEmpty() && !question.content.isEmpty()) {
            confirmed->append(question);
        }
    }
    return !confirmed->isEmpty();
}

void MainWindow::uploadQuestions(const QList<QuestionRecord> &items)
{
    QJsonArray questionIds;
    QString error;
    if (!uploadQuestions(items, &questionIds, &error)) {
        showError(QStringLiteral("题目上传失败"), error);
        return;
    }

    showToast(QStringLiteral("题目上传完成"));
    refreshQuestionBank();
    refreshQuestionBankPage();
}

bool MainWindow::uploadQuestions(const QList<QuestionRecord> &items, QJsonArray *questionIds, QString *error)
{
    qInfo("[IMPORT_PREVIEW_UPLOAD_START] questions=%lld", static_cast<long long>(items.size()));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    statusBar()->showMessage(QStringLiteral("正在上传 %1 道题到服务器...").arg(items.size()));
    QCoreApplication::processEvents();

    QJsonArray array;
    for (const QuestionRecord &question : items) {
        array.append(questionToJson(question));
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("import_questions"));
    request.insert(QStringLiteral("questions"), array);

    QJsonObject response;
    QString requestError;
    if (!ApiClient::instance().request(request, &response, &requestError)) {
        qWarning("[IMPORT_PREVIEW_UPLOAD_ERROR] reason=%s", qPrintable(requestError));
        QApplication::restoreOverrideCursor();
        statusBar()->showMessage(QStringLiteral("题目上传失败"));
        if (error) {
            *error = requestError;
        }
        return false;
    }
    if (questionIds) {
        *questionIds = response.value(QStringLiteral("questionIds")).toArray();
    }
    // 仅在上传成功后回到题库第一页，保留当前题型、关键词和难度条件。
    questionBankPage = 1;
    qInfo("[IMPORT_PREVIEW_UPLOAD_DONE] questions=%lld", static_cast<long long>(items.size()));
    QApplication::restoreOverrideCursor();
    statusBar()->showMessage(QStringLiteral("题目上传完成"));
    return true;
}

void MainWindow::openQuestionTemplate()
{
    const QString dataDir = projectDataDir();
    const QString xlsxTemplate = QDir(dataDir).filePath(QStringLiteral("question_bank_template.xlsx"));
    const QString csvTemplate = QDir(dataDir).filePath(QStringLiteral("question_bank_template.csv"));
    if (QFileInfo::exists(xlsxTemplate)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(xlsxTemplate));
        return;
    }
    if (QFileInfo::exists(csvTemplate)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(csvTemplate));
        return;
    }
    QMessageBox::warning(this, QStringLiteral("模板不存在"), QStringLiteral("没有找到题库模板目录：%1").arg(dataDir));
}

void MainWindow::refreshQuestionCount()
{
    if (!questionCountLabel) {
        return;
    }
    int visible = 0;
    if (questionTable) {
        for (int row = 0; row < questionTable->rowCount(); ++row) {
            if (!questionTable->isRowHidden(row)) {
                ++visible;
            }
        }
    }
    const int first = visible > 0 ? (questionBankPage - 1) * questionBankPageSize + 1 : 0;
    const int last = visible > 0 ? qMin(first + visible - 1, questionBankTotal) : 0;
    questionCountLabel->setText(QStringLiteral("共%1道题，当前显示第%2-%3条").arg(questionBankTotal).arg(first).arg(last));
}

bool MainWindow::questionMatchesTypeCategory(const QString &rawType, const QString &selectedCategory) const
{
    if (selectedCategory == QStringLiteral("全部题型")) {
        return true;
    }

    // 只做显示分类的精确别名匹配，不改原始 type，也不按题干推断。
    // 不复用 isChoiceLikeQuestion()：该函数包含判断题，不适用于本分类。
    static const QHash<QString, QString> categories = {
        {QStringLiteral("单选题"), QStringLiteral("单选题")},
        {QStringLiteral("单选"), QStringLiteral("单选题")},
        {QStringLiteral("单项选择"), QStringLiteral("单选题")},
        {QStringLiteral("单项选择题"), QStringLiteral("单选题")},
        {QStringLiteral("多选题"), QStringLiteral("多选题")},
        {QStringLiteral("多选"), QStringLiteral("多选题")},
        {QStringLiteral("多项选择"), QStringLiteral("多选题")},
        {QStringLiteral("多项选择题"), QStringLiteral("多选题")},
        {QStringLiteral("判断题"), QStringLiteral("判断题")},
        {QStringLiteral("判断"), QStringLiteral("判断题")},
        {QStringLiteral("简答题"), QStringLiteral("简答题")},
        {QStringLiteral("简答"), QStringLiteral("简答题")},
        {QStringLiteral("填空题"), QStringLiteral("填空题")},
        {QStringLiteral("填空"), QStringLiteral("填空题")},
        {QStringLiteral("综合应用题"), QStringLiteral("综合应用题")},
        {QStringLiteral("综合应用"), QStringLiteral("综合应用题")},
        {QStringLiteral("程序题"), QStringLiteral("程序题")},
        {QStringLiteral("程序"), QStringLiteral("程序题")},
        {QStringLiteral("编程题"), QStringLiteral("程序题")},
        {QStringLiteral("编程"), QStringLiteral("程序题")}
    };
    const QString type = rawType.trimmed();
    const QString category = categories.value(type);
    if (selectedCategory == QStringLiteral("选择题")) {
        return category == QStringLiteral("单选题") || category == QStringLiteral("多选题");
    }
    if (selectedCategory == QStringLiteral("其他")) {
        return !type.isEmpty() && category.isEmpty();
    }
    return !category.isEmpty() && category == selectedCategory;
}

QJsonArray MainWindow::filteredQuestionObjects(bool includeTypeFilter) const
{
    const QString keyword = questionSearchEdit ? questionSearchEdit->text().trimmed() : QString();
    const QString type = questionTypeTabs ? questionTypeTabs->tabData(questionTypeTabs->currentIndex()).toString() : QStringLiteral("全部题型");
    const QString difficulty = questionDifficultyFilter ? questionDifficultyFilter->currentText() : QStringLiteral("全部难度");
    QJsonArray filtered;
    for (const QJsonValue &value : questionObjects) {
        const QJsonObject object = value.toObject();
        const QuestionRecord question = questionFromJson(object);
        // 保留原表格搜索语义：按原来的12列（含真实ID）拼接，忽略大小写。
        const QStringList values = {
            QString::number(object.value(QStringLiteral("id")).toInt()),
            question.type, question.course, question.content,
            question.optionA, question.optionB, question.optionC, question.optionD,
            question.answer, question.score, question.difficulty, question.analysis
        };
        const QString rowText = values.join(QLatin1Char(' ')) + QLatin1Char(' ');
        const bool matchKeyword = keyword.isEmpty() || rowText.contains(keyword, Qt::CaseInsensitive);
        const bool matchType = !includeTypeFilter || questionMatchesTypeCategory(question.type, type);
        const bool matchDifficulty = difficulty == QStringLiteral("全部难度") || question.difficulty == difficulty;
        if (matchKeyword && matchType && matchDifficulty) {
            filtered.append(value);
        }
    }
    return filtered;
}

void MainWindow::filterQuestions()
{
    questionBankPage = 1;
    refreshQuestionBankPage();
    refreshQuestionCategoryCounts();
}

void MainWindow::resetQuestionFilters()
{
    if (questionSearchEdit) {
        const QSignalBlocker blocker(questionSearchEdit);
        questionSearchEdit->clear();
    }
    if (questionTypeTabs) {
        const QSignalBlocker blocker(questionTypeTabs);
        questionTypeTabs->setCurrentIndex(0);
    }
    if (questionDifficultyFilter) {
        const QSignalBlocker blocker(questionDifficultyFilter);
        questionDifficultyFilter->setCurrentIndex(0);
    }
    filterQuestions();
}

QJsonArray MainWindow::selectedPublishQuestionIds() const
{
    QJsonArray ids;
    if (!publishQuestionTable) {
        return ids;
    }
    for (int row = 0; row < publishQuestionTable->rowCount(); ++row) {
        auto *check = publishQuestionTable->item(row, 0);
        auto *idItem = publishQuestionTable->item(row, 1);
        if (check && idItem && check->checkState() == Qt::Checked) {
            ids.append(idItem->text().toInt());
        }
    }
    return ids;
}

void MainWindow::createExamDraft()
{
    qInfo("[UI_CLICK] page=teacher_exams button=create_exam");
    qInfo("[EXAM_CREATE_START]");
    const QJsonArray ids = selectedPublishQuestionIds();
    if (ids.isEmpty()) {
        qWarning("[EXAM_CREATE_ERROR] reason=no-questions-selected");
        QMessageBox::warning(this, QStringLiteral("请先勾选题目"), QStringLiteral("请在右侧“选择试卷题目”表格里至少勾选一道题，再点击创建试卷。"));
        return;
    }
    if (publishNameEdit->text().trimmed().isEmpty()) {
        qWarning("[EXAM_CREATE_ERROR] reason=empty-name");
        QMessageBox::warning(this, QStringLiteral("考试名称为空"), QStringLiteral("请填写考试名称。"));
        return;
    }
    if (publishEndEdit->dateTime() <= publishStartEdit->dateTime()) {
        qWarning("[EXAM_CREATE_ERROR] reason=invalid-time");
        QMessageBox::warning(this, QStringLiteral("时间不合法"), QStringLiteral("截止时间必须晚于开始时间。"));
        return;
    }

    QJsonObject exam;
    exam.insert(QStringLiteral("name"), publishNameEdit->text().trimmed());
    exam.insert(QStringLiteral("course"), publishCourseEdit->text().trimmed());
    exam.insert(QStringLiteral("startAt"), publishStartEdit->dateTime().toUTC().toString(Qt::ISODate));
    exam.insert(QStringLiteral("endAt"), publishEndEdit->dateTime().toUTC().toString(Qt::ISODate));
    exam.insert(QStringLiteral("durationMinutes"), publishDurationSpin->value());
    exam.insert(QStringLiteral("maxSubmissions"), publishMaxSubmissionsSpin->value());

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("create_exam"));
    request.insert(QStringLiteral("exam"), exam);
    request.insert(QStringLiteral("questionIds"), ids);

    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        qWarning("[EXAM_CREATE_ERROR] reason=%s", qPrintable(error));
        showError(QStringLiteral("创建试卷失败"), error);
        return;
    }
    const int newExamId = response.value(QStringLiteral("examId")).toInt();
    qInfo("[EXAM_CREATE_SUCCESS] examId=%d questions=%d", newExamId, ids.size());
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("试卷已创建，请在考试列表中发布。")));
    refreshExamManagementList();
    loadStats();
    // 自动选中新创建的考试，方便直接发布。
    if (newExamId > 0 && examManagementTable) {
        for (int r = 0; r < examManagementTable->rowCount(); ++r) {
            if (examManagementTable->item(r, 0) && examManagementTable->item(r, 0)->text().toInt() == newExamId) {
                examManagementTable->selectRow(r);
                break;
            }
        }
    }
}

void MainWindow::publishSelectedExam()
{
    qInfo("[UI_CLICK] page=teacher_exams button=publish_exam");
    const int row = examManagementTable ? examManagementTable->currentRow() : -1;
    if (row < 0 || !examManagementTable->item(row, 0)) {
        qWarning("[PUBLISH_ERROR] reason=no-exam-selected");
        QMessageBox::warning(this, QStringLiteral("请先选择一场考试"), QStringLiteral("请先在下方考试列表中点选一场考试，再点击发布。"));
        return;
    }

    const int examId = examManagementTable->item(row, 0)->text().toInt();
    const QString publishStatus = examManagementTable->item(row, 3) ? examManagementTable->item(row, 3)->data(Qt::UserRole).toString() : QString();
    publishExamById(examId, publishStatus);
}

void MainWindow::publishExamById(int examId, const QString &publishStatus)
{
    qInfo("[PUBLISH_START] examId=%d", examId);
    if (publishStatus == QStringLiteral("published")) {
        qInfo("[PUBLISH_ERROR] reason=already-published examId=%d", examId);
        QMessageBox::information(this, QStringLiteral("已发布"), QStringLiteral("这场考试已经发布过了，学生端刷新即可看到。"));
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("publish_exam"));
    request.insert(QStringLiteral("examId"), examId);

    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        qWarning("[PUBLISH_ERROR] reason=%s examId=%d", qPrintable(error), examId);
        if (error == QStringLiteral("您已发布此次考试")) {
            QMessageBox::information(this, QStringLiteral("已发布"), error);
        } else {
            showError(QStringLiteral("发布失败"), error);
        }
        refreshExamManagementList();
        return;
    }

    qInfo("[PUBLISH_SUCCESS] examId=%d", examId);
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("试卷发布成功，学生端刷新即可看到。")));
    refreshExamManagementList();
    loadStats();
}

void MainWindow::updatePublishExamButton()
{
    if (!publishSelectedExamButton || !examManagementTable) {
        return;
    }
    const int row = examManagementTable->currentRow();
    const QString publishStatus = row >= 0 && examManagementTable->item(row, 3)
                                      ? examManagementTable->item(row, 3)->data(Qt::UserRole).toString()
                                      : QString();
    publishSelectedExamButton->setEnabled(row >= 0 && publishStatus != QStringLiteral("published"));
}

void MainWindow::refreshExamManagementList()
{
    if (!examManagementTable) {
        return;
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_exams"));
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("考试列表读取失败"), error);
        return;
    }

    examManagementTable->setRowCount(0);
    const QJsonArray exams = response.value(QStringLiteral("exams")).toArray();
    for (const QJsonValue &value : exams) {
        const QJsonObject exam = value.toObject();
        const int row = examManagementTable->rowCount();
        examManagementTable->insertRow(row);
        const QString publishStatus = exam.value(QStringLiteral("publishStatus")).toString();
        examManagementTable->setItem(row, 0, readOnlyItem(QString::number(exam.value(QStringLiteral("id")).toInt())));
        examManagementTable->setItem(row, 1, readOnlyItem(exam.value(QStringLiteral("name")).toString()));
        examManagementTable->setItem(row, 2, readOnlyItem(exam.value(QStringLiteral("course")).toString()));
        auto *publishStatusItem = readOnlyItem(publishStatusText(publishStatus));
        publishStatusItem->setData(Qt::UserRole, publishStatus);
        examManagementTable->setItem(row, 3, publishStatusItem);
        examManagementTable->setItem(row, 4, readOnlyItem(statusText(exam.value(QStringLiteral("status")).toString())));
        examManagementTable->setItem(row, 5, readOnlyItem(localTimeText(exam.value(QStringLiteral("startAt")).toString())));
        examManagementTable->setItem(row, 6, readOnlyItem(localTimeText(exam.value(QStringLiteral("endAt")).toString())));
        examManagementTable->setItem(row, 7, readOnlyItem(QStringLiteral("%1 分钟").arg(exam.value(QStringLiteral("durationMinutes")).toInt())));

        auto *rowPublishButton = new QPushButton(QStringLiteral("发布试卷"));
        rowPublishButton->setEnabled(publishStatus != QStringLiteral("published"));
        rowPublishButton->setProperty("smallButton", true);
        const int examId = exam.value(QStringLiteral("id")).toInt();
        bindTeacherButton(rowPublishButton, QStringLiteral("btn_teacher_publish_exam"), QStringLiteral("teacher_exams"), QStringLiteral("publish_exam"), [this, examId, publishStatus] {
            publishExamById(examId, publishStatus);
        });
        examManagementTable->setCellWidget(row, 8, rowPublishButton);
    }
    if (examManagementTable->rowCount() > 0) {
        examManagementTable->selectRow(0);
    }
    updatePublishExamButton();
}

void MainWindow::closeSelectedExam()
{
    qInfo("[UI_CLICK] page=teacher_exams button=close_exam");
    const int row = examManagementTable ? examManagementTable->currentRow() : -1;
    if (row < 0 || !examManagementTable->item(row, 0)) {
        qWarning("[EXAM_ERROR] reason=no-exam-selected action=close");
        QMessageBox::warning(this, QStringLiteral("请先选择一场考试"), QStringLiteral("请先在考试列表中点选一场考试，再点击关闭。"));
        return;
    }
    const int examId = examManagementTable->item(row, 0)->text().toInt();
    const QString examName = examManagementTable->item(row, 1) ? examManagementTable->item(row, 1)->text() : QString::number(examId);
    if (QMessageBox::question(this,
                              QStringLiteral("确认关闭考试"),
                              QStringLiteral("确定关闭考试“%1”吗？关闭后学生不能再答题或提交。").arg(examName))
        != QMessageBox::Yes) {
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("close_exam"));
    request.insert(QStringLiteral("examId"), examId);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("关闭考试失败"), error);
        return;
    }
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("考试已关闭。")));
    refreshExamManagementList();
    loadStats();
}

void MainWindow::deleteSelectedExam()
{
    qInfo("[UI_CLICK] page=teacher_exams button=delete_exam");
    const int row = examManagementTable ? examManagementTable->currentRow() : -1;
    if (row < 0 || !examManagementTable->item(row, 0)) {
        qWarning("[EXAM_ERROR] reason=no-exam-selected action=delete");
        QMessageBox::warning(this, QStringLiteral("请先选择一场考试"), QStringLiteral("请先在考试列表中点选一场考试，再点击删除。"));
        return;
    }
    const int examId = examManagementTable->item(row, 0)->text().toInt();
    const QString examName = examManagementTable->item(row, 1) ? examManagementTable->item(row, 1)->text() : QString::number(examId);
    const QString status = examManagementTable->item(row, 4) ? examManagementTable->item(row, 4)->text() : QString();
    const QString prompt = status.isEmpty()
                               ? QStringLiteral("确定永久删除考试“%1”吗？关联的发布记录、答卷、成绩和风险记录也会一并删除。").arg(examName)
                               : QStringLiteral("确定永久删除考试“%1”吗？当前状态：%2。关联的发布记录、答卷、成绩和风险记录也会一并删除。")
                                     .arg(examName, status);
    if (QMessageBox::question(this, QStringLiteral("确认删除考试"), prompt) != QMessageBox::Yes) {
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("delete_exam"));
    request.insert(QStringLiteral("examId"), examId);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("删除考试失败"), error);
        return;
    }

    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("考试已删除。")));
    refreshExamManagementList();
    populateSubmissionList();
    refreshRiskRecords();
    refreshStudentExams();
    loadStats();
}

void MainWindow::refreshStudentExams()
{
    if (!studentExamList) {
        return;
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_exams"));
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("考试列表读取失败"), error);
        return;
    }

    studentExamObjects = response.value(QStringLiteral("exams")).toArray();
    int firstAvailableRow = -1;
    {
        const QSignalBlocker blocker(studentExamList);
        studentExamList->clear();
        for (const QJsonValue &value : studentExamObjects) {
            const QJsonObject exam = value.toObject();
            const QString state = exam.value(QStringLiteral("state")).toString();
            const int maxSubmissions = exam.value(QStringLiteral("maxSubmissions")).toInt(1);
            const int mySubmissionCount = exam.value(QStringLiteral("mySubmissionCount")).toInt();
            const QString blockReason = studentExamBlockReason(exam);
            const bool canEnter = blockReason.isEmpty();
            const QString availabilityText = canEnter ? QStringLiteral("可参加") : blockReason;
            const QString attemptText = QStringLiteral("提交 %1/%2 次").arg(mySubmissionCount).arg(maxSubmissions);
            const QString line = QStringLiteral("%1  【%2】\n%3\n%4 至 %5  ·  %6  ·  %7")
                                     .arg(exam.value(QStringLiteral("name")).toString(),
                                          availabilityText,
                                          exam.value(QStringLiteral("course")).toString(),
                                          localTimeText(exam.value(QStringLiteral("startAt")).toString()),
                                          localTimeText(exam.value(QStringLiteral("endAt")).toString()),
                                          statusText(state),
                                          attemptText);
            auto *item = new QListWidgetItem(line);
            item->setForeground(QBrush(canEnter ? statusColor(state) : QColor(QStringLiteral("#94a3b8"))));
            item->setSizeHint(QSize(292, 106));
            item->setData(Qt::UserRole, exam.value(QStringLiteral("id")).toInt());
            item->setData(Qt::UserRole + 1, objectData(exam));
            item->setData(Qt::UserRole + 2, canEnter);
            item->setData(Qt::UserRole + 3, blockReason);
            if (canEnter && firstAvailableRow < 0) {
                firstAvailableRow = studentExamList->count();
            }
            studentExamList->addItem(item);
        }
        if (firstAvailableRow >= 0) {
            studentExamList->setCurrentRow(firstAvailableRow);
        } else {
            studentExamList->setCurrentRow(-1);
        }
    }
    if (firstAvailableRow >= 0) {
        loadSelectedExam();
    } else if (studentExamList->count() > 0) {
        stopAiProctor();
        currentExamId = 0;
        currentExamQuestions = QJsonArray();
        currentExamStartTime = QDateTime();
        currentExamEndTime = QDateTime();
        noFaceStartTime = QDateTime();
        faceMismatchStartTime = QDateTime();
        screenSwitchCount = 0;
        stopExamCountdown();
        remainingSeconds = 0;
        studentExamTitleLabel->setText(QStringLiteral("<h3>暂无可参加考试</h3>"));
        studentExamStateLabel->setText(QStringLiteral("已显示教师发布过的考试，灰色考试当前不可进入，点击可查看原因。"));
        updateRiskHint({});
        if (studentCountdownLabel) {
            studentCountdownLabel->setText(QStringLiteral("倒计时：--:--:--"));
            studentCountdownLabel->setProperty("urgent", false);
            repolish(studentCountdownLabel);
        }
        clearLayout(studentQuestionLayout);
        clearLayout(studentQuestionNavGrid);
        studentAnswerEdits.clear();
        studentQuestionButtons.clear();
        studentQuestionWidgets.clear();
        currentQuestionIndex = -1;
        if (previousQuestionButton) {
            previousQuestionButton->setEnabled(false);
        }
        if (nextQuestionButton) {
            nextQuestionButton->setEnabled(false);
        }
        if (startAiProctorButton) {
            startAiProctorButton->setEnabled(false);
        }
        studentQuestionLayout->addWidget(mutedLabel(QStringLiteral("当前没有可进入的考试。")));
    } else {
        stopAiProctor();
        currentExamId = 0;
        currentExamQuestions = QJsonArray();
        currentExamStartTime = QDateTime();
        currentExamEndTime = QDateTime();
        noFaceStartTime = QDateTime();
        faceMismatchStartTime = QDateTime();
        screenSwitchCount = 0;
        stopExamCountdown();
        remainingSeconds = 0;
        studentExamStateLabel->setText(QStringLiteral("当前没有可参加的考试，可能尚未发布、已截止，或提交次数已用完。"));
        updateRiskHint({});
        if (studentCountdownLabel) {
            studentCountdownLabel->setText(QStringLiteral("倒计时：--:--:--"));
            studentCountdownLabel->setProperty("urgent", false);
            repolish(studentCountdownLabel);
        }
        clearLayout(studentQuestionLayout);
        clearLayout(studentQuestionNavGrid);
        studentAnswerEdits.clear();
        studentQuestionButtons.clear();
        studentQuestionWidgets.clear();
        currentQuestionIndex = -1;
        if (previousQuestionButton) {
            previousQuestionButton->setEnabled(false);
        }
        if (nextQuestionButton) {
            nextQuestionButton->setEnabled(false);
        }
        if (startAiProctorButton) {
            startAiProctorButton->setEnabled(false);
        }
        studentQuestionLayout->addWidget(mutedLabel(QStringLiteral("暂无可答题目。")));
    }
}

void MainWindow::loadSelectedExam()
{
    auto *item = studentExamList ? studentExamList->currentItem() : nullptr;
    if (!item) {
        return;
    }
    const auto canEnterData = item->data(Qt::UserRole + 2);
    if (canEnterData.isValid() && !canEnterData.toBool()) {
        const QString reason = item->data(Qt::UserRole + 3).toString();
        const QString message = studentExamBlockMessage(reason);
        stopAiProctor();
        currentExamId = 0;
        currentExamQuestions = QJsonArray();
        currentExamStartTime = QDateTime();
        currentExamEndTime = QDateTime();
        noFaceStartTime = QDateTime();
        faceMismatchStartTime = QDateTime();
        screenSwitchCount = 0;
        stopExamCountdown();
        remainingSeconds = 0;
        updateRiskHint({});
        if (studentExamTitleLabel) {
            studentExamTitleLabel->setText(QStringLiteral("<h3>暂不能参加</h3>"));
        }
        if (studentExamStateLabel) {
            studentExamStateLabel->setText(message);
        }
        if (studentCountdownLabel) {
            studentCountdownLabel->setText(QStringLiteral("倒计时：--:--:--"));
            studentCountdownLabel->setProperty("urgent", false);
            repolish(studentCountdownLabel);
        }
        clearLayout(studentQuestionLayout);
        clearLayout(studentQuestionNavGrid);
        studentAnswerEdits.clear();
        studentQuestionButtons.clear();
        studentQuestionWidgets.clear();
        currentQuestionIndex = -1;
        if (previousQuestionButton) {
            previousQuestionButton->setEnabled(false);
        }
        if (nextQuestionButton) {
            nextQuestionButton->setEnabled(false);
        }
        if (startAiProctorButton) {
            startAiProctorButton->setEnabled(false);
        }
        if (studentQuestionLayout) {
            studentQuestionLayout->addWidget(mutedLabel(message));
        }
        QMessageBox::information(this, QStringLiteral("暂不能参加考试"), message);
        return;
    }
    if (!ensureStudentFaceProfile()) {
        return;
    }
    stopAiProctor();
    currentExamId = item->data(Qt::UserRole).toInt();
    currentExamStartTime = QDateTime::currentDateTime();
    fiveMinuteAlerted = false;
    noFaceStartTime = QDateTime();
    faceMismatchStartTime = QDateTime();
    screenSwitchCount = 0;
    updateRiskHint({});
    const QJsonObject listExam = objectFromData(item->data(Qt::UserRole + 1));

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("get_exam_detail"));
    request.insert(QStringLiteral("examId"), currentExamId);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("考试加载失败"), error);
        return;
    }

    studentExamTitleLabel->setText(QStringLiteral("<h3>%1</h3><p>%2 · %3 分钟 · 提交 %4/%5 次</p>")
                                       .arg(response.value(QStringLiteral("exam")).toObject().value(QStringLiteral("name")).toString(),
                                            response.value(QStringLiteral("exam")).toObject().value(QStringLiteral("course")).toString())
                                       .arg(response.value(QStringLiteral("exam")).toObject().value(QStringLiteral("durationMinutes")).toInt())
                                       .arg(listExam.value(QStringLiteral("mySubmissionCount")).toInt())
                                       .arg(response.value(QStringLiteral("exam")).toObject().value(QStringLiteral("maxSubmissions")).toInt()));
    const QJsonObject examObject = response.value(QStringLiteral("exam")).toObject();
    const QDateTime endAt = QDateTime::fromString(examObject.value(QStringLiteral("endAt")).toString(), Qt::ISODate).toLocalTime();
    const QDateTime durationEnd = QDateTime::currentDateTime().addSecs(examObject.value(QStringLiteral("durationMinutes")).toInt(90) * 60);
    currentExamEndTime = endAt.isValid() && endAt < durationEnd ? endAt : durationEnd;
    // 进入/切换考试前先彻底停掉上一场倒计时（失效旧 tick 链 + 停 examTimer），
    // 否则下面初值渲染与草稿恢复弹窗期间会有旧倒计时在跑、并被自动保存覆盖草稿。
    stopExamCountdown();
    // 倒计时改为自减模型：初始剩余 = min(截止, 现在+时长) − 现在。
    // 先设初值并渲染标签，但等草稿恢复决定后再正式启动 tick（见下方 startExamCountdown），
    // 避免恢复弹窗弹着时倒计时空跑、自动保存把空答案覆盖掉本地草稿（保住崩溃恢复窗口期的数据）。
    const qint64 initialSeconds = qMax<qint64>(0, QDateTime::currentDateTime().secsTo(currentExamEndTime));
    remainingSeconds = static_cast<int>(initialSeconds);
    updateExamCountdown();  // 仅渲染初值（此时 examCountdownActive=false，不触发到期/自动提交）
    studentExamStateLabel->setText(QStringLiteral("状态：%1，开始：%2，截止：%3")
                                       .arg(statusText(listExam.value(QStringLiteral("state")).toString()),
                                            localTimeText(response.value(QStringLiteral("exam")).toObject().value(QStringLiteral("startAt")).toString()),
                                            localTimeText(response.value(QStringLiteral("exam")).toObject().value(QStringLiteral("endAt")).toString())));
    renderExamQuestions(response.value(QStringLiteral("questions")).toArray());
    if (!currentExamQuestions.isEmpty()) {
        if (startAiProctorButton) {
            startAiProctorButton->setEnabled(true);
        }
        if (simulateProctorButton) {
            simulateProctorButton->setEnabled(true);
            qDebug() << "[SIMULATION_BUTTON_SHOW]" << "visible=" << simulateProctorButton->isVisible();
        }
        // 断网缓存：题目渲染完成后，检测并提示恢复本地草稿。
        setDraftStatus(QStringLiteral("草稿：答题过程中自动保存到本地"), QStringLiteral("normal"));
        qInfo("[EXAM_DRAFT_PATH] path=%s", qUtf8Printable(localDraftFilePath(currentExamId, currentUser.id)));
        maybeRestoreLocalDraft(currentExamId);  // 选“是”会把 remainingSeconds 改成草稿里的剩余秒数
        // 草稿恢复决定后再正式启动倒计时（一次性，用当前 remainingSeconds：恢复=草稿值，否则=初值）。
        // 自动保存已并入倒计时 tick（每 5 秒落盘），不再依赖 30s QTimer（麒麟下 timeout PMF 失效）。
        startExamCountdown(remainingSeconds);
        // 留痕：本场身份核验通过模拟核验（无摄像头演示）。ensureStudentFaceProfile()
        // 运行在 currentExamId 设置之前，因此在考试加载完成、examId 就绪后补记一条
        // SIMULATION_VERIFY 事件（仅记录、不加作弊分），供教师端复核。
        if (currentStudentFaceTemplate == QStringLiteral("SIMULATION_MODE")) {
            recordAbnormalEvent(QStringLiteral("SIMULATION_VERIFY"),
                                QStringLiteral("本场考试通过模拟人脸核验进入（无摄像头演示模式，未做真实身份比对）"));
        }
        startAiProctor();
    }
    // 进入考试后移除考试列表的焦点框，避免该行一直保持突兀的深色选中/焦点态。
    if (studentExamList) {
        studentExamList->clearFocus();
    }
}

void MainWindow::renderExamQuestions(const QJsonArray &questions)
{
    qInfo("[ANSWER_CARD] questions=%d", questions.size());
    qInfo("[ANSWER_CARD_BUILD] questions=%d", questions.size());
    if (!studentQuestionNavGrid) {
        qWarning("[ANSWER_CARD_ERROR] reason=navGrid-null");
    }
    currentExamQuestions = questions;
    studentAnswerEdits.clear();
    // 重建答题卡前，反注册旧题号按钮，避免注册表残留已析构指针（再分配到同址会误派发）。
    for (QPushButton *oldBtn : studentQuestionButtons) {
        unregisterButtonAction(oldBtn);
    }
    studentQuestionButtons.clear();
    studentQuestionWidgets.clear();
    currentQuestionIndex = -1;
    clearLayout(studentQuestionLayout);
    clearLayout(studentQuestionNavGrid);

    if (questions.isEmpty()) {
        qWarning("[ANSWER_CARD_ERROR] reason=no-questions");
        studentQuestionLayout->addWidget(mutedLabel(QStringLiteral("这场考试还没有题目。")));
        if (studentActionHintLabel) {
            studentActionHintLabel->setText(QStringLiteral("这场考试暂时没有题目，无法提交。"));
        }
        if (studentCountdownLabel) {
            studentCountdownLabel->setText(QStringLiteral("倒计时：--:--:--"));
        }
        if (previousQuestionButton) {
            previousQuestionButton->setEnabled(false);
        }
        if (nextQuestionButton) {
            nextQuestionButton->setEnabled(false);
        }
        return;
    }

    const int totalQ = questions.size();
    const int cols = totalQ <= 10 ? 5 : (totalQ <= 30 ? 6 : 8);
    for (int i = 0; i < questions.size(); ++i) {
        const QJsonObject question = questions.at(i).toObject();
        const QString questionType = question.value(QStringLiteral("type")).toString();
        auto *navButton = new QPushButton(QString::number(i + 1));
        navButton->setFixedSize(38, 34);
        navButton->setObjectName(QStringLiteral("answerNavButton"));
        navButton->setProperty("answerState", QStringLiteral("unanswered"));
        studentQuestionButtons.append(navButton);
        studentQuestionNavGrid->addWidget(navButton, i / cols, i % cols);

        auto *box = new QFrame;
        box->setObjectName(QStringLiteral("paperQuestion"));
        auto *boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(18, 18, 18, 18);
        boxLayout->setSpacing(12);
        auto *questionTop = new QHBoxLayout;
        auto *questionTitle = new QLabel(QStringLiteral("%1. %2").arg(i + 1).arg(questionType));
        questionTitle->setObjectName(QStringLiteral("paperQuestionTitle"));
        auto *questionScore = new QLabel(QStringLiteral("%1 分").arg(valueString(question, QStringLiteral("score"))));
        questionScore->setObjectName(QStringLiteral("scoreBadge"));
        questionTop->addWidget(questionTitle);
        questionTop->addStretch();
        questionTop->addWidget(questionScore);
        boxLayout->addLayout(questionTop);
        auto *content = new QLabel(readableQuestionContent(question.value(QStringLiteral("content")).toString()));
        content->setObjectName(QStringLiteral("questionContent"));
        content->setTextFormat(Qt::PlainText);
        content->setWordWrap(true);
        boxLayout->addWidget(content);
        const auto options = answerOptionsForQuestion(question);
        auto *answer = new QTextEdit(box);
        answer->hide();
        studentAnswerEdits.append(answer);
        connect(answer, &QTextEdit::textChanged, this, &MainWindow::updateAnswerNavStates);
        // 任何答案变动（含选择题写入隐藏框）都触发本地草稿防抖保存。
        connect(answer, &QTextEdit::textChanged, this, &MainWindow::scheduleLocalDraftSave);

        if (isChoiceLikeQuestion(questionType) && !options.isEmpty()) {
            auto *choiceHost = new QWidget;
            choiceHost->setObjectName(QStringLiteral("choiceHost"));
            auto *choiceLayout = new QVBoxLayout(choiceHost);
            choiceLayout->setContentsMargins(0, 0, 0, 0);
            choiceLayout->setSpacing(10);

            if (isMultiChoiceQuestion(questionType)) {
                for (const auto &option : options) {
                    auto *check = new QCheckBox(QStringLiteral("%1. %2").arg(option.first, option.second));
                    check->setObjectName(QStringLiteral("choiceButton"));
                    check->setProperty("answerKey", option.first);
                    choiceLayout->addWidget(check);
                    connect(check, &QAbstractButton::toggled, this, [this, choiceHost, answer] {
                        QString value;
                        const auto checks = choiceHost->findChildren<QCheckBox *>();
                        for (auto *item : checks) {
                            if (item && item->isChecked()) {
                                value.append(item->property("answerKey").toString());
                            }
                        }
                        answer->setPlainText(value);
                        updateAnswerNavStates();
                    });
                }
            } else {
                for (const auto &option : options) {
                    auto *radio = new QRadioButton(QStringLiteral("%1. %2").arg(option.first, option.second));
                    radio->setObjectName(QStringLiteral("choiceButton"));
                    radio->setProperty("answerKey", option.first);
                    choiceLayout->addWidget(radio);
                    connect(radio, &QAbstractButton::clicked, this, [this, radio, answer] {
                        answer->setPlainText(radio->property("answerKey").toString());
                        updateAnswerNavStates();
                    });
                }
            }
            boxLayout->addWidget(choiceHost);
            auto *hint = mutedLabel(isMultiChoiceQuestion(questionType)
                                        ? QStringLiteral("多选题可选择多个选项，系统会自动记录答案。")
                                        : QStringLiteral("点击选项即可作答，系统会自动记录答案。"));
            hint->setObjectName(QStringLiteral("choiceHint"));
            boxLayout->addWidget(hint);
        } else {
            QStringList optionTexts;
            for (const auto &option : options) {
                optionTexts.append(QStringLiteral("%1. %2").arg(option.first, option.second));
            }
            if (!optionTexts.isEmpty()) {
                auto *optionLabel = mutedLabel(optionTexts.join(QStringLiteral("\n")));
                optionLabel->setObjectName(QStringLiteral("optionText"));
                boxLayout->addWidget(optionLabel);
            }

            auto *answerEdit = answer;
            answerEdit->show();
            answerEdit->setParent(nullptr);
            boxLayout->addWidget(answerEdit);
            answerEdit->setPlaceholderText(QStringLiteral("在这里填写答案。"));
            answerEdit->setObjectName(questionType.contains(QStringLiteral("程序")) || questionType.contains(QStringLiteral("编程")) || questionType.contains(QStringLiteral("代码"))
                                           ? QStringLiteral("codeAnswerEdit")
                                           : QStringLiteral("answerEdit"));
            answerEdit->setMinimumHeight(answerEdit->objectName() == QStringLiteral("codeAnswerEdit") ? 150 : 110);
            if (answerEdit->objectName() == QStringLiteral("codeAnswerEdit")) {
                QFont codeFont(QStringLiteral("Cascadia Mono"));
                codeFont.setStyleHint(QFont::Monospace);
                codeFont.setPointSize(10);
                answerEdit->setFont(codeFont);
                answerEdit->setPlaceholderText(QStringLiteral("// 在这里填写代码或算法思路"));
            }
        }
        studentQuestionLayout->addWidget(box);
        studentQuestionWidgets.append(box);
        // 麒麟下 QPushButton clicked 新式 PMF 连接失效 → 用普通按钮动作注册表（eventFilter 分发）可靠触发；
        // eventFilter 消费 MouseButtonRelease，不会与原生 clicked 双触发。绑定题目索引 i，而非按钮文字。
        registerButtonAction(navButton, QStringLiteral("studentExam"),
                             QStringLiteral("answerCard_%1").arg(i + 1),
                             [this, i] { jumpToQuestion(i); });
        qInfo("[ANSWER_CARD_REGISTER] index=%d", i);
    }
    studentQuestionLayout->addStretch();
    if (studentActionHintLabel) {
        studentActionHintLabel->setText(QStringLiteral("填写完成后，直接点击“提交试卷”。"));
    }
    qInfo("[ANSWER_CARD] buttons=%d grid created (cols=%d)", studentQuestionButtons.size(), cols);
    setCurrentStudentQuestion(0);
    updateAnswerNavStates();
}

QJsonArray MainWindow::collectStudentAnswers() const
{
    QJsonArray answers;
    for (int i = 0; i < currentExamQuestions.size(); ++i) {
        const QJsonObject question = currentExamQuestions.at(i).toObject();
        QJsonObject answer;
        answer.insert(QStringLiteral("questionId"), question.value(QStringLiteral("id")).toInt());
        answer.insert(QStringLiteral("questionIndex"), i + 1);
        answer.insert(QStringLiteral("questionType"), question.value(QStringLiteral("type")).toString());
        answer.insert(QStringLiteral("answer"), i < studentAnswerEdits.size() ? studentAnswerEdits.at(i)->toPlainText() : QString());
        answers.append(answer);
    }
    return answers;
}

void MainWindow::applyAnswersToUi(const QJsonArray &answers)
{
    // 把保存的答案按 questionId 建索引，再回填到当前题目控件。
    QHash<int, QString> byQuestionId;
    for (const QJsonValue &value : answers) {
        const QJsonObject answer = value.toObject();
        byQuestionId.insert(answer.value(QStringLiteral("questionId")).toInt(),
                            answer.value(QStringLiteral("answer")).toString());
    }

    for (int i = 0; i < currentExamQuestions.size() && i < studentAnswerEdits.size(); ++i) {
        const int questionId = currentExamQuestions.at(i).toObject().value(QStringLiteral("id")).toInt();
        if (!byQuestionId.contains(questionId)) {
            continue;
        }
        const QString savedAnswer = byQuestionId.value(questionId);

        // 1) 同步可见答题控件（选择题/判断题/多选题）。用 QSignalBlocker 阻断其
        //    toggled/clicked 槽，避免重复回写隐藏框；隐藏框稍后统一设置为真相源。
        QWidget *box = i < studentQuestionWidgets.size() ? studentQuestionWidgets.at(i) : nullptr;
        if (box) {
            const auto checks = box->findChildren<QCheckBox *>();
            for (QCheckBox *check : checks) {
                const QString key = check->property("answerKey").toString();
                QSignalBlocker blocker(check);
                check->setChecked(!key.isEmpty() && savedAnswer.contains(key));
            }
            const auto radios = box->findChildren<QRadioButton *>();
            for (QRadioButton *radio : radios) {
                const QString key = radio->property("answerKey").toString();
                QSignalBlocker blocker(radio);
                radio->setChecked(!key.isEmpty() && savedAnswer == key);
            }
        }

        // 2) 隐藏答案框始终是最终答案源（collectStudentAnswers 读它）；填空题/主观题
        //    也是直接显示这个框，因此 setPlainText 同时完成可见控件回填。
        QSignalBlocker editBlocker(studentAnswerEdits.at(i));
        studentAnswerEdits.at(i)->setPlainText(savedAnswer);
    }
    updateAnswerNavStates();
}

QString MainWindow::localDraftDir() const
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        root = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
    }
    return QDir(root).filePath(QStringLiteral("drafts"));
}

QString MainWindow::localDraftFilePath(int examId, int studentId) const
{
    return QDir(localDraftDir()).filePath(QStringLiteral("draft_exam_%1_student_%2.json")
                                              .arg(examId)
                                              .arg(studentId));
}

void MainWindow::saveLocalDraft()
{
    if (currentUser.role != UserRole::Student || currentExamId <= 0 || currentExamQuestions.isEmpty()) {
        return;
    }
    const int studentId = currentUser.id;
    QDir().mkpath(localDraftDir());
    const QString path = localDraftFilePath(currentExamId, studentId);

    const QString stampIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("username"), currentUser.username);
    root.insert(QStringLiteral("examId"), currentExamId);
    root.insert(QStringLiteral("studentId"), studentId);
    root.insert(QStringLiteral("remainingSeconds"), remainingSeconds);
    root.insert(QStringLiteral("currentQuestionIndex"), currentQuestionIndex);
    root.insert(QStringLiteral("updatedAt"), stampIso);
    root.insert(QStringLiteral("savedAt"), stampIso);
    root.insert(QStringLiteral("submitted"), false);
    root.insert(QStringLiteral("answers"), collectStudentAnswers());

    // QSaveFile 原子写：断电/崩溃时不会写坏旧草稿（落盘要么是旧内容、要么是完整新内容）。
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("[EXAM_DRAFT_SAVE_ERROR] path=%s reason=open-failed", qUtf8Printable(path));
        setDraftStatus(QStringLiteral("草稿：本地保存失败（无法写入文件）"), QStringLiteral("warning"));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        qWarning("[EXAM_DRAFT_SAVE_ERROR] path=%s reason=commit-failed", qUtf8Printable(path));
        setDraftStatus(QStringLiteral("草稿：本地保存失败（提交写入失败）"), QStringLiteral("warning"));
        return;
    }
    qInfo("[EXAM_DRAFT_SAVE] path=%s remaining=%d index=%d answers=%d",
          qUtf8Printable(path), remainingSeconds, currentQuestionIndex, currentExamQuestions.size());

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    setDraftStatus(QStringLiteral("草稿已自动保存到本地（%1）").arg(stamp), QStringLiteral("normal"));
    statusBar()->showMessage(QStringLiteral("草稿已自动保存"), 2000);
}

void MainWindow::scheduleLocalDraftSave()
{
    if (currentUser.role != UserRole::Student || currentExamId <= 0) {
        return;
    }
    // 防抖合并：每次改动自增代号，约 1.5s 后只有最新一次真正落盘。
    // 用 singleShot（麒麟下 QTimer::timeout 新式连接失效，singleShot+functor 免疫）。
    const int gen = ++draftDebounceGeneration;
    QTimer::singleShot(1500, this, [this, gen] {
        if (gen != draftDebounceGeneration) {
            return;
        }
        if (currentUser.role == UserRole::Student && currentExamId > 0 && !currentExamQuestions.isEmpty()) {
            saveLocalDraft();
        }
    });
}

void MainWindow::maybeRestoreLocalDraft(int examId)
{
    if (currentUser.role != UserRole::Student || examId <= 0) {
        return;
    }
    const int studentId = currentUser.id;
    const QString path = localDraftFilePath(examId, studentId);
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning("[EXAM_DRAFT_RESTORE_ERROR] path=%s reason=parse", qUtf8Printable(path));
        return;
    }
    const QJsonObject root = document.object();
    // 已提交成功的草稿（兜底）不再恢复。
    if (root.value(QStringLiteral("submitted")).toBool()) {
        return;
    }
    const QJsonArray answers = root.value(QStringLiteral("answers")).toArray();
    if (answers.isEmpty()) {
        return;
    }

    const int savedRemaining = root.value(QStringLiteral("remainingSeconds")).toInt(-1);
    const int savedIndex = root.value(QStringLiteral("currentQuestionIndex")).toInt(-1);
    qInfo("[EXAM_DRAFT_FOUND] path=%s remaining=%d index=%d answers=%d",
          qUtf8Printable(path), savedRemaining, savedIndex, answers.size());

    const QString updatedAt = localTimeText(root.value(QStringLiteral("updatedAt")).toString());
    const QString message = QStringLiteral("检测到上次未完成的考试草稿（保存于 %1），是否恢复？\n将恢复你的答案、当前题号与剩余时间。")
                                .arg(updatedAt.isEmpty() ? QStringLiteral("上次") : updatedAt);
    if (QMessageBox::question(this, QStringLiteral("恢复未完成的考试"), message) == QMessageBox::Yes) {
        applyAnswersToUi(answers);
        if (savedIndex >= 0 && savedIndex < studentQuestionWidgets.size()) {
            setCurrentStudentQuestion(savedIndex);
        }
        // 从保存的剩余时间继续倒计时：仅写入 remainingSeconds，由 loadSelectedExam 在恢复决定后
        // 统一启动倒计时，避免在恢复弹窗内提前起 tick（弹窗期间不空跑、不覆盖草稿）。
        if (savedRemaining > 0) {
            remainingSeconds = savedRemaining;
        }
        qInfo("[EXAM_DRAFT_RESTORE] index=%d remaining=%d answers=%d", savedIndex, savedRemaining, answers.size());
        setDraftStatus(QStringLiteral("已恢复未完成的考试，可继续作答"), QStringLiteral("normal"));
        statusBar()->showMessage(QStringLiteral("已恢复本地草稿"), 4000);
    } else {
        // 不覆盖服务端数据，也保留本地草稿文件，便于稍后再次恢复。
        setDraftStatus(QStringLiteral("已保留本地草稿（未恢复）"), QStringLiteral("normal"));
    }
}

void MainWindow::clearLocalDraft(int examId, int studentId)
{
    if (examId <= 0) {
        return;
    }
    const QString path = localDraftFilePath(examId, studentId);
    if (QFile::remove(path)) {
        qInfo("[EXAM_DRAFT_CLEAR] path=%s", qUtf8Printable(path));
    }
}

void MainWindow::setDraftStatus(const QString &text, const QString &state)
{
    if (!draftStatusLabel) {
        return;
    }
    draftStatusLabel->setText(text);
    draftStatusLabel->setProperty("state", state);
    repolish(draftStatusLabel);
}

void MainWindow::updateAnswerNavStates()
{
    int answeredCount = 0;
    for (int i = 0; i < studentQuestionButtons.size(); ++i) {
        auto *button = studentQuestionButtons.at(i);
        const bool answered = i < studentAnswerEdits.size() && !studentAnswerEdits.at(i)->toPlainText().trimmed().isEmpty();
        if (answered) {
            ++answeredCount;
        }
        button->setProperty("answerState", answered ? QStringLiteral("answered") : QStringLiteral("unanswered"));
        button->setProperty("current", i == currentQuestionIndex);
        repolish(button);
    }
    if (studentActionHintLabel && !studentQuestionButtons.isEmpty()) {
        studentActionHintLabel->setText(QStringLiteral("已答 %1/%2 题。写完后点击绿色“提交试卷”。")
                                            .arg(answeredCount)
                                            .arg(studentQuestionButtons.size()));
    }
}

void MainWindow::setCurrentStudentQuestion(int index)
{
    if (index < 0 || index >= studentQuestionWidgets.size()) {
        return;
    }
    currentQuestionIndex = index;
    if (studentQuestionScroll && studentQuestionWidgets.at(index)) {
        studentQuestionScroll->ensureWidgetVisible(studentQuestionWidgets.at(index), 0, 18);
    }
    if (previousQuestionButton) {
        previousQuestionButton->setEnabled(index > 0);
    }
    if (nextQuestionButton) {
        nextQuestionButton->setEnabled(index + 1 < studentQuestionWidgets.size());
    }
    updateAnswerNavStates();
}

void MainWindow::moveStudentQuestion(int offset)
{
    if (studentQuestionWidgets.isEmpty()) {
        return;
    }
    const int base = currentQuestionIndex < 0 ? 0 : currentQuestionIndex;
    setCurrentStudentQuestion(qBound(0, base + offset, studentQuestionWidgets.size() - 1));
    // 切换题目时保存一次本地草稿。
    saveLocalDraft();
}

void MainWindow::updateExamCountdown()
{
    if (!studentCountdownLabel) {
        return;
    }

    const qint64 seconds = remainingSeconds;
    if (seconds <= 0) {
        studentCountdownLabel->setText(QStringLiteral("倒计时：00:00:00"));
        studentCountdownLabel->setProperty("urgent", true);
        repolish(studentCountdownLabel);
        if (examCountdownActive) {
            qInfo("[EXAM_TIMER_EXPIRED] exam=%d", currentExamId);
            stopExamCountdown();  // 先停（含失效 tick 链），再自动提交，避免提交弹窗里又被 tick 进来
            if (currentExamId > 0 && !currentExamQuestions.isEmpty()) {
                submitExamInternal(true);
            }
        }
        return;
    }

    if (seconds <= 300 && !fiveMinuteAlerted) {
        fiveMinuteAlerted = true;
        QApplication::beep();
        QMessageBox::warning(this,
                             QStringLiteral("考试时间提醒"),
                             QStringLiteral("距离考试结束还有 5 分钟，请抓紧完成答题并提交。"));
    }

    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 secs = seconds % 60;
    studentCountdownLabel->setText(QStringLiteral("倒计时：%1:%2:%3")
                                       .arg(hours, 2, 10, QLatin1Char('0'))
                                       .arg(minutes, 2, 10, QLatin1Char('0'))
                                       .arg(secs, 2, 10, QLatin1Char('0')));
    studentCountdownLabel->setProperty("urgent", seconds <= 300);
    repolish(studentCountdownLabel);
}

void MainWindow::startExamCountdown(int seconds)
{
    remainingSeconds = qMax(0, seconds);
    examCountdownActive = true;
    examCountdownViaSingleShot = false;
    examTickObserved = 0;
    examAutosaveCounter = 0;
    ++examTickGeneration;  // 失效任何在途的旧 singleShot 链
    const int gen = examTickGeneration;
    qInfo("[EXAM_TIMER_START] exam=%d remaining=%d", currentExamId, remainingSeconds);
    updateExamCountdown();  // 立即渲染初值（若初值即 0，会在此触发到期收尾）
    if (!examCountdownActive) {
        return;
    }

    // 主路径：新式 connect 驱动 examTimer，并用返回值判断是否被麒麟“signal not found”掐断。
    bool connected = false;
    if (examTimer) {
        examTimer->stop();
        examTimer->disconnect(this);  // 清掉旧连接，避免重复 connect 造成一秒多跳
        examTimer->setInterval(1000);
        connected = static_cast<bool>(connect(examTimer, &QTimer::timeout, this, &MainWindow::onExamTick));
    }
    qInfo("[EXAM_TIMER_CONNECT] style=pmf ok=%d", connected ? 1 : 0);
    if (connected) {
        examTimer->start();
        // 看门狗：~1.6s 后若一次都没跳（麒麟静默失效），切到 singleShot 自调度链。
        QTimer::singleShot(1600, this, [this, gen] {
            if (gen != examTickGeneration || !examCountdownActive) {
                return;
            }
            if (examTickObserved == 0 && !examCountdownViaSingleShot) {
                qInfo("[EXAM_TIMER_FALLBACK] reason=timer-silent");
                if (examTimer) {
                    examTimer->stop();
                }
                examCountdownViaSingleShot = true;
                scheduleExamTickSingleShot();
            }
        });
    } else {
        // 新式 connect 直接失败：走 singleShot 自调度链（singleShot+functor 免疫麒麟 PMF bug）。
        qInfo("[EXAM_TIMER_FALLBACK] reason=connect-failed");
        examCountdownViaSingleShot = true;
        scheduleExamTickSingleShot();
    }
}

void MainWindow::scheduleExamTickSingleShot()
{
    const int gen = examTickGeneration;
    QTimer::singleShot(1000, this, [this, gen] {
        if (gen != examTickGeneration || !examCountdownActive) {
            return;  // 已被新一轮考试/停止失效，旧链自然终止，杜绝双跳
        }
        onExamTick();
        if (examCountdownActive && examCountdownViaSingleShot) {
            scheduleExamTickSingleShot();
        }
    });
}

void MainWindow::onExamTick()
{
    if (!examCountdownActive) {
        return;
    }
    ++examTickObserved;
    if (remainingSeconds > 0) {
        --remainingSeconds;  // 每个 tick 只自减一次，单一驱动保证一秒只减一次
    }
    qInfo("[EXAM_TIMER_TICK] remaining=%d", remainingSeconds);
    // 周期自动保存：每 5 个 tick（约 5 秒）落一次本地草稿，取代失效的 30s QTimer。
    if (++examAutosaveCounter >= 5) {
        examAutosaveCounter = 0;
        if (currentExamId > 0 && !currentExamQuestions.isEmpty()) {
            saveLocalDraft();
        }
    }
    updateExamCountdown();  // 渲染剩余时间 + 到期收尾（自动提交）
}

void MainWindow::stopExamCountdown()
{
    examCountdownActive = false;
    examCountdownViaSingleShot = false;
    ++examTickGeneration;  // 失效任何在途 singleShot 链
    if (examTimer) {
        examTimer->stop();
    }
}

void MainWindow::jumpToQuestion(int index)
{
    if (index < 0 || index >= studentQuestionWidgets.size()) {
        return;
    }
    qInfo("[ANSWER_CARD_CLICK] index=%d", index);
    // 跳题前先保存当前答案到本地草稿（隐藏答案框始终是真相源，选择题/输入都已写入其中）。
    qInfo("[ANSWER_SAVE_BEFORE_JUMP] from=%d to=%d", currentQuestionIndex, index);
    saveLocalDraft();
    qInfo("[ANSWER_CARD_JUMP] index=%d", index);
    // 滚动式布局：所有题目常驻同一滚动区，setCurrentStudentQuestion 切题号+滚动到该题+刷新答题卡状态；
    // 目标题的已选/已填内容天然存在，无需单独“恢复渲染”。
    setCurrentStudentQuestion(index);
    qInfo("[ANSWER_CARD_STATE] current=%d total=%d", currentQuestionIndex, studentQuestionWidgets.size());
}

void MainWindow::saveDraft()
{
    if (currentExamId <= 0 || currentExamQuestions.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("未选择考试"), QStringLiteral("请先选择一场考试。"));
        return;
    }

    // 先把答案落本地，保证即使服务端不可用也不丢答案。
    saveLocalDraft();

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("save_draft"));
    request.insert(QStringLiteral("examId"), currentExamId);
    request.insert(QStringLiteral("answers"), collectStudentAnswers());
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        // 服务端保存失败不视为致命：答案已在本地，提示恢复网络后重试。
        setDraftStatus(QStringLiteral("网络异常，草稿已保存到本地，待联网后可重试"), QStringLiteral("warning"));
        showToast(QStringLiteral("网络异常，草稿已保存到本地。请恢复网络后再次保存或提交。"), false);
        return;
    }
    setDraftStatus(QStringLiteral("草稿已保存到服务器"), QStringLiteral("normal"));
    showToast(response.value(QStringLiteral("message")).toString());
    refreshMySubmissions();
}

void MainWindow::submitExam()
{
    submitExamInternal(false);
}

void MainWindow::submitExamInternal(bool autoSubmit)
{
    if (currentExamId <= 0 || currentExamQuestions.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("未选择考试"), QStringLiteral("请先选择一场考试。"));
        return;
    }
    int answeredCount = 0;
    for (auto *edit : studentAnswerEdits) {
        if (edit && !edit->toPlainText().trimmed().isEmpty()) {
            ++answeredCount;
        }
    }
    const QString confirmText = QStringLiteral("你已填写 %1/%2 题。\n确定提交试卷吗？提交后不能修改。")
                                    .arg(answeredCount)
                                    .arg(currentExamQuestions.size());
    if (!autoSubmit && QMessageBox::question(this, QStringLiteral("确认提交试卷"), confirmText) != QMessageBox::Yes) {
        return;
    }

    if (!autoSubmit && currentExamStartTime.isValid()
        && currentExamStartTime.secsTo(QDateTime::currentDateTime()) >= 0
        && currentExamStartTime.secsTo(QDateTime::currentDateTime()) < 180) {
        recordAbnormalEvent(QStringLiteral("TIME_ABNORMAL"),
                            QStringLiteral("考试提交时间异常，答题时间过短"));
    }

    // 断网保护：提交前先把答案落本地，无论提交是否成功都不丢答案。
    saveLocalDraft();

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("submit_exam"));
    request.insert(QStringLiteral("examId"), currentExamId);
    request.insert(QStringLiteral("answers"), collectStudentAnswers());
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        // 网络异常/服务端断开：不清空答案、不删除本地草稿、不当作考试完成。
        setDraftStatus(QStringLiteral("网络异常，答案已保存到本地，待联网后重新提交"), QStringLiteral("warning"));
        QMessageBox::warning(this,
                             QStringLiteral("提交失败"),
                             QStringLiteral("网络异常，答案已保存到本地。\n请恢复网络后重新提交。\n\n（你可以继续留在答题页面，答案不会丢失。）"));
        return;
    }
    const double autoScore = response.value(QStringLiteral("autoScore")).toDouble();
    const double paperScore = response.value(QStringLiteral("paperScore")).toDouble();
    const int correctCount = response.value(QStringLiteral("correctCount")).toInt();
    const int wrongCount = response.value(QStringLiteral("wrongCount")).toInt();
    const int manualCount = response.value(QStringLiteral("manualCount")).toInt();
    const QString resultText = QStringLiteral("%1\n\n客观题得分：%2 / %3\n正确：%4  错误/未答：%5\n%6")
                                   .arg(autoSubmit ? QStringLiteral("考试时间已到，系统已自动提交。")
                                                   : response.value(QStringLiteral("message")).toString(QStringLiteral("试卷已提交。")))
                                   .arg(autoScore, 0, 'g', 12)
                                   .arg(paperScore, 0, 'g', 12)
                                   .arg(correctCount)
                                   .arg(wrongCount)
                                   .arg(manualCount > 0
                                            ? QStringLiteral("还有 %1 道主观题等待老师批阅。").arg(manualCount)
                                            : QStringLiteral("本次考试已自动生成成绩。"));
    // 提交成功：停倒计时、清理本地草稿并停止自动保存，避免下次进入误提示恢复。
    stopExamCountdown();
    clearLocalDraft(currentExamId, currentUser.id);
    if (localDraftAutosaveTimer) {
        localDraftAutosaveTimer->stop();
    }
    if (localDraftDebounceTimer) {
        localDraftDebounceTimer->stop();
    }
    setDraftStatus(QStringLiteral("已提交，本地草稿已清理"), QStringLiteral("normal"));
    QMessageBox::information(this, QStringLiteral("提交成功"), resultText);
    stopAiProctor();
    if (studentProctorPreview) {
        studentProctorPreview->setText(QStringLiteral("监考预览\n（已停止）"));  // 交卷后才显示“已停止”
    }
    refreshStudentExams();
    refreshMySubmissions();
    loadStats();
}

void MainWindow::refreshMySubmissions()
{
    if (!mySubmissionTable) {
        return;
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_my_submissions"));
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("提交记录读取失败"), error);
        return;
    }

    const QJsonArray submissions = response.value(QStringLiteral("submissions")).toArray();
    const QList<QTableWidget *> tables = findChildren<QTableWidget *>(QStringLiteral("mySubmissionTable"));
    if (tables.isEmpty()) {
        populateMySubmissionTable(mySubmissionTable, submissions);
    } else {
        for (QTableWidget *table : tables) {
            populateMySubmissionTable(table, submissions);
        }
    }
    for (QPushButton *button : findChildren<QPushButton *>()) {
        if (button->property("submissionDetailButton").toBool()) {
            button->setEnabled(false);
        }
    }
}

bool MainWindow::ensureStudentFaceProfile()
{
    if (currentUser.role != UserRole::Student) {
        return true;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("get_my_face_profile"));
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("读取人脸信息失败"), error);
        return false;
    }

    if (!response.value(QStringLiteral("hasProfile")).toBool()) {
        QMessageBox::information(this,
                                 QStringLiteral("需要采集人脸"),
                                 QStringLiteral("首次参加考试前需要采集本人脸信息。采集成功后会保存到服务器数据库，下次考试会直接进行身份核验。"));
        return enrollFaceProfile();
    }

    return verifyFaceProfile(response.value(QStringLiteral("faceTemplate")).toString());
}

bool MainWindow::enrollFaceProfile()
{
    FaceProfileDialog dialog(FaceProfileDialog::Mode::Enroll, QString(), this);
    if (dialog.exec() != QDialog::Accepted) {
        showToast(QStringLiteral("已取消人脸采集，不能进入考试。"), false);
        return false;
    }

    const QString faceTemplate = dialog.templateData().trimmed();

    if (faceTemplate == QStringLiteral("SIMULATION_MODE")) {
        currentStudentFaceTemplate = faceTemplate;
        QJsonObject simRequest;
        simRequest.insert(QStringLiteral("type"), QStringLiteral("save_face_profile"));
        simRequest.insert(QStringLiteral("faceTemplate"), faceTemplate);
        QJsonObject simResponse;
        QString simError;
        ApiClient::instance().request(simRequest, &simResponse, &simError);
        showToast(QStringLiteral(
            "【演示模式】已使用模拟人脸信息进入考试。"
            "此模式仅用于无摄像头环境下的答辩演示，不验证真实身份。"
        ));
        return true;
    }

    if (!faceTemplate.startsWith(QStringLiteral("SFACE1:"))) {
        showToast(QStringLiteral("没有采集到有效的OpenCV人脸特征，不能进入考试。"), false);
        return false;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("save_face_profile"));
    request.insert(QStringLiteral("faceTemplate"), faceTemplate);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("保存人脸信息失败"), error);
        return false;
    }
    currentStudentFaceTemplate = faceTemplate;
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("人脸信息已保存到服务器。")));
    return true;
}

bool MainWindow::verifyFaceProfile(const QString &faceTemplate)
{
    const QString normalizedTemplate = faceTemplate.trimmed();
    if (normalizedTemplate.isEmpty()) {
        return enrollFaceProfile();
    }

    if (normalizedTemplate == QStringLiteral("SIMULATION_MODE")) {
        // 当前账号是无摄像头演示模板。给出明确入口让用户切回真实人脸采集，
        // 而不是静默自动通过（方案 A：进考试前弹窗二选一）。
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("演示模式人脸模板"));
        box.setText(QStringLiteral("当前账号使用的是无摄像头演示人脸模板，未做真实身份核验。"));
        box.setInformativeText(QStringLiteral("是否重新采集真实人脸？\n（连接摄像头后采集，可永久覆盖演示模板；无摄像头时仍可继续演示。）"));
        QPushButton *reEnrollButton = box.addButton(QStringLiteral("重新采集真实人脸"), QMessageBox::AcceptRole);
        QPushButton *keepDemoButton = box.addButton(QStringLiteral("继续使用演示模式"), QMessageBox::RejectRole);
        box.setDefaultButton(keepDemoButton);
        box.exec();

        if (box.clickedButton() == reEnrollButton) {
            // 走真实采集：成功则保存 SFACE1 覆盖演示模板；无摄像头时对话框会出现
            // 「模拟核验通过」按钮，点击后 enrollFaceProfile() 仍返回 true 并回到演示模式。
            if (enrollFaceProfile()) {
                return true;
            }
            // 采集被取消或失败：不卡死，回退到演示模式继续进入考试。
            currentStudentFaceTemplate = QStringLiteral("SIMULATION_MODE");
            showToast(QStringLiteral("已取消重新采集，继续使用无摄像头演示模式进入考试。"));
            return true;
        }

        // 继续使用演示模式（SIMULATION_VERIFY 事件由 loadSelectedExam 在 examId 就绪后补记）。
        currentStudentFaceTemplate = normalizedTemplate;
        showToast(QStringLiteral(
            "【演示模式】当前账号使用的是无摄像头演示人脸模板，已跳过真实身份核验。"
        ));
        return true;
    }

    if (!normalizedTemplate.startsWith(QStringLiteral("SFACE1:"))) {
        QMessageBox::information(this,
                                 QStringLiteral("需要重新采集人脸"),
                                 QStringLiteral("当前服务器里的人脸模板是旧版图片模板，已升级为OpenCV SFace特征模板，需要重新采集一次。"));
        return enrollFaceProfile();
    }

    FaceProfileDialog dialog(FaceProfileDialog::Mode::Verify, normalizedTemplate, this);
    if (dialog.exec() != QDialog::Accepted) {
        if (dialog.wantsReEnroll()) {
            return enrollFaceProfile();
        }
        showToast(QStringLiteral("身份核验未通过，不能进入考试。"), false);
        return false;
    }
    if (dialog.simulationAccepted()) {
        currentStudentFaceTemplate = QStringLiteral("SIMULATION_MODE");
        showToast(QStringLiteral(
            "【演示模式】已跳过身份核验，仅用于无摄像头环境下的答辩演示。"
        ));
        return true;
    }
    currentStudentFaceTemplate = normalizedTemplate;
    showToast(QStringLiteral("身份核验通过，可以开始考试。"));
    return true;
}

void MainWindow::handleApplicationStateChanged(Qt::ApplicationState state)
{
    if (currentUser.role != UserRole::Student || currentExamId <= 0 || currentExamQuestions.isEmpty()) {
        return;
    }
    if (state == Qt::ApplicationActive) {
        return;
    }

    ++screenSwitchCount;
    statusBar()->showMessage(QStringLiteral("检测到离开考试界面，请专注答题。"), 5000);
    if (screenSwitchCount % 3 == 0) {
        recordAbnormalEvent(QStringLiteral("SCREEN_SWITCH"),
                            QStringLiteral("考试窗口失去焦点 %1 次").arg(screenSwitchCount));
    }
}

void MainWindow::recordAbnormalEvent(const QString &abnormalType, const QString &description)
{
    if (currentUser.role != UserRole::Student || currentExamId <= 0) {
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("record_abnormal_event"));
    request.insert(QStringLiteral("examId"), currentExamId);
    request.insert(QStringLiteral("abnormalType"), abnormalType);
    request.insert(QStringLiteral("description"), description);

    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        statusBar()->showMessage(QStringLiteral("考试环境异常记录上报失败：%1").arg(error), 6000);
        return;
    }

    updateRiskHint(response);
    const int addedScore = response.value(QStringLiteral("addedScore")).toInt();
    if (addedScore > 0) {
        showToast(QStringLiteral("检测到考试环境异常，请保持本人在摄像头前并专注答题。"));
    } else {
        statusBar()->showMessage(response.value(QStringLiteral("message")).toString(QStringLiteral("异常已记录。")), 4000);
    }
}

void MainWindow::updateRiskHint(const QJsonObject &risk)
{
    if (!studentRiskStatusLabel || !studentRiskLevelLabel) {
        return;
    }

    const int score = risk.value(QStringLiteral("totalScore")).toInt(0);
    const QString level = risk.value(QStringLiteral("riskLevel")).toString(QStringLiteral("低风险"));
    const bool abnormal = score > 0 || level != QStringLiteral("低风险");
    studentRiskStatusLabel->setText(abnormal
                                        ? QStringLiteral("当前考试状态：检测到环境异常")
                                        : QStringLiteral("当前考试状态：正常"));
    studentRiskLevelLabel->setText(QStringLiteral("风险等级：%1（%2 分）").arg(level).arg(score));
    studentRiskStatusLabel->setProperty("riskLevel", level);
    studentRiskLevelLabel->setProperty("riskLevel", level);
    repolish(studentRiskStatusLabel);
    repolish(studentRiskLevelLabel);
}

void MainWindow::onAiFaceResult(bool hasFace, int faceCount, bool faceMatched)
{
    if (currentUser.role != UserRole::Student || currentExamId <= 0 || currentExamQuestions.isEmpty()) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (!hasFace || faceCount <= 0) {
        if (!noFaceStartTime.isValid()) {
            noFaceStartTime = now;
        } else if (noFaceStartTime.secsTo(now) >= 10) {
            recordAbnormalEvent(QStringLiteral("NO_FACE"), QStringLiteral("无人脸超过10秒"));
            noFaceStartTime = now;
        }
        return;
    }

    noFaceStartTime = QDateTime();
    faceMismatchStartTime = QDateTime();
    if (faceCount > 1) {
        recordAbnormalEvent(QStringLiteral("MULTI_FACE"), QStringLiteral("检测到多张人脸"));
        return;
    }
    if (!faceMatched) {
        if (!faceMismatchStartTime.isValid()) {
            faceMismatchStartTime = now;
        } else if (faceMismatchStartTime.secsTo(now) >= 8) {
            recordAbnormalEvent(QStringLiteral("FACE_MISMATCH"), QStringLiteral("人脸与考生身份持续不匹配超过8秒"));
            faceMismatchStartTime = now;
        }
        return;
    }
    faceMismatchStartTime = QDateTime();
}

void MainWindow::startAiProctor()
{
    qDebug() << "[AIPROCTOR_START_REQUEST]";
    if (currentUser.role != UserRole::Student) {
        return;
    }
    if (currentExamId <= 0 || currentExamQuestions.isEmpty()) {
        setAiProctorStatus(QStringLiteral("C++摄像头监考：请先选择考试"), QStringLiteral("warning"));
        return;
    }
    if (!aiProctorMonitor) {
        setAiProctorStatus(QStringLiteral("C++摄像头监考：模块未初始化"), QStringLiteral("error"));
        return;
    }
    if (aiProctorMonitor->isRunning()) {
        setAiProctorStatus(QStringLiteral("C++摄像头监考：正在运行"), QStringLiteral("normal"));
        return;
    }
    setAiProctorStatus(QStringLiteral("C++摄像头监考：正在启动摄像头..."), QStringLiteral("normal"));
    qInfo("[PROCTOR_START] mode=camera exam=%d user=%s", currentExamId, qPrintable(currentUser.username));
    m_previewLogged = false;
    if (studentProctorPreview) {
        studentProctorPreview->setText(QStringLiteral("正在启动摄像头..."));
    }
    aiProctorMonitor->setBaselineTemplate(currentStudentFaceTemplate);
    if (!aiProctorMonitor->start(aiBaselinePath())) {
        m_proctorRunning = false;
        const QString error = aiProctorMonitor->lastStartMissingDevice()
                                  ? QStringLiteral("missing_device")
                                  : QStringLiteral("start_failed");
        qDebug() << "[AIPROCTOR_START_ERROR]" << error;
        qWarning("[CAMERA_ERROR] proctor camera start failed missingDevice=%d",
                 aiProctorMonitor->lastStartMissingDevice() ? 1 : 0);
        if (studentProctorPreview) {
            studentProctorPreview->setText(QStringLiteral("摄像头未连接\n可使用模拟监考模式"));
        }
        if (startAiProctorButton) {
            startAiProctorButton->setEnabled(true);
        }
        if (stopAiProctorButton) {
            stopAiProctorButton->setEnabled(false);
        }
        if (simulateProctorButton) {
            simulateProctorButton->setEnabled(true);
            qDebug() << "[SIMULATION_BUTTON_SHOW]" << "visible=" << simulateProctorButton->isVisible();
        }
        // 留痕：摄像头设备缺失 vs 启动失败，统一进入风险记录链路供教师端复核。
        if (aiProctorMonitor->lastStartMissingDevice()) {
            recordAbnormalEvent(QStringLiteral("CAMERA_MISSING"),
                                QStringLiteral("未检测到摄像头设备（/dev/video* 不存在或无可用摄像头）"));
        } else {
            recordAbnormalEvent(QStringLiteral("CAMERA_ERROR"),
                                QStringLiteral("摄像头启动失败（设备存在但无法读取画面或脚本异常）"));
        }
        showToast(QStringLiteral("摄像头启动失败：未检测到 /dev/video* 设备。\n"
                                 "请在 VMware/VirtualBox 中连接摄像头，或点击「模拟监考模式」进行答辩演示。"),
                  false);
    } else {
        // 启动成功：标记运行中。首帧到达后 previewReady 会把“正在启动”文字替换为真实画面。
        m_proctorRunning = true;
        qDebug() << "[AIPROCTOR_START_OK]";
    }
}

void MainWindow::stopAiProctor()
{
    const bool wasRunning = aiProctorMonitor && aiProctorMonitor->isRunning();
    if (aiProctorMonitor) {
        aiProctorMonitor->stop();
    }
    if (wasRunning) {
        qInfo("[PROCTOR_STOP] proctor stopped exam=%d user=%s", currentExamId, qPrintable(currentUser.username));
    }
    m_previewLogged = false;
    m_proctorRunning = false;
    if (studentProctorPreview) {
        // 停止后是“可重新启动”的状态，显示“未启动”而不是“已停止”，避免一进考试就显示已停止。
        // 真正的“已停止”仅在交卷时显示（见 submitExamInternal）。
        studentProctorPreview->setText(QStringLiteral("监考预览\n（未启动监考）"));  // setText 会清掉旧画面
    }
    if (stopAiProctorButton) {
        stopAiProctorButton->setEnabled(false);
    }
    const bool canRestart = currentExamId > 0 && !currentExamQuestions.isEmpty();
    if (startAiProctorButton && canRestart) {
        startAiProctorButton->setEnabled(true);
    }
    if (simulateProctorButton && canRestart) {
        simulateProctorButton->setEnabled(true);
    }
    setAiProctorStatus(QStringLiteral("C++摄像头监考：已停止"), QStringLiteral("normal"));
}

void MainWindow::requestLogout()
{
    qInfo("[UI_CLICK] button=logout");
    qInfo("[LOGOUT] user=%s role=%s", qPrintable(currentUser.username), qPrintable(roleToText(currentUser.role)));
    qInfo("[PROCTOR_STOP] reason=logout");
    // 1) 停止监考/摄像头，杀掉 camera bridge 子进程，避免后台残留到下一个账号。
    stopAiProctor();
    // 2) 停止与当前用户相关的定时器（本地草稿自动保存、倒计时）。
    if (localDraftAutosaveTimer) {
        localDraftAutosaveTimer->stop();
    }
    if (localDraftDebounceTimer) {
        localDraftDebounceTimer->stop();
    }
    stopExamCountdown();
    // 3) 标记“退出登录”，关闭窗口；main.cpp 会回到登录页（可切换账号）。
    //    每次登录 main.cpp 新建一个 MainWindow，成员状态全新，不会继承上一个用户。
    m_logoutRequested = true;
    close();
    qDebug() << "[LOGOUT_DONE]" << "back_to_login";
}

void MainWindow::setAiProctorStatus(const QString &text, const QString &state)
{
    if (!aiProctorStatusLabel) {
        return;
    }
    aiProctorStatusLabel->setText(text);
    aiProctorStatusLabel->setProperty("state", state);
    repolish(aiProctorStatusLabel);
}

QString MainWindow::aiBaselinePath() const
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        root = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
    }
    QDir().mkpath(root);

    QString username = currentUser.username;
    for (QChar &ch : username) {
        if (!ch.isLetterOrNumber()) {
            ch = QLatin1Char('_');
        }
    }
    return QDir(root).filePath(QStringLiteral("face_baseline_%1.png").arg(username));
}

void MainWindow::populateSubmissionList()
{
    if (!submissionList || !submissionStatusFilter) {
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_gradable_submissions"));
    request.insert(QStringLiteral("statusFilter"), submissionStatusFilter->currentData().toString());
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("答卷列表读取失败"), error);
        return;
    }

    const int previousId = currentSubmissionId;
    submissionList->clear();
    const QJsonArray submissions = response.value(QStringLiteral("submissions")).toArray();
    int selectRow = -1;
    for (const QJsonValue &value : submissions) {
        const QJsonObject submission = value.toObject();
        const QString status = submission.value(QStringLiteral("status")).toString();
        const QString text = QStringLiteral("答卷 #%1  ·  %2\n%3\n%4")
                                 .arg(submission.value(QStringLiteral("id")).toInt())
                                 .arg(submission.value(QStringLiteral("studentName")).toString())
                                 .arg(statusText(status),
                                      submission.value(QStringLiteral("graderName")).toString().isEmpty()
                                          ? QStringLiteral("未锁定")
                                          : QStringLiteral("阅卷：%1").arg(submission.value(QStringLiteral("graderName")).toString()));
        auto *item = new QListWidgetItem(text);
        item->setForeground(QBrush(statusColor(status)));
        item->setSizeHint(QSize(322, 88));
        item->setToolTip(submission.value(QStringLiteral("examName")).toString());
        item->setData(Qt::UserRole, submission.value(QStringLiteral("id")).toInt());
        item->setData(Qt::UserRole + 1, objectData(submission));
        submissionList->addItem(item);
        if (submission.value(QStringLiteral("id")).toInt() == previousId) {
            selectRow = submissionList->count() - 1;
        }
    }
    if (submissionList->count() > 0) {
        submissionList->setCurrentRow(selectRow >= 0 ? selectRow : 0);
    } else {
        currentSubmissionId = 0;
        submissionTitleLabel->setText(QStringLiteral("<h3>暂无答卷</h3><p>当前筛选条件下没有答卷。</p>"));
        submissionStateLabel->setText(QStringLiteral("状态：-"));
        submissionStateLabel->setProperty("status", QStringLiteral("none"));
        repolish(submissionStateLabel);
        clearLayout(gradingQuestionLayout);
        gradingScoreEdits.clear();
        gradingCommentEdits.clear();
        if (gradingQuestionLayout) {
            gradingQuestionLayout->addWidget(mutedLabel(QStringLiteral("当前没有可批阅答卷。学生提交试卷后会出现在这里。")));
        }
        teacherCommentEdit->clear();
        scoreSpinBox->setValue(0);
        if (lockSubmissionButton) {
            lockSubmissionButton->setEnabled(false);
            lockSubmissionButton->setText(QStringLiteral("先选左侧答卷"));
        }
        if (releaseSubmissionButton) {
            releaseSubmissionButton->setEnabled(false);
            releaseSubmissionButton->setText(QStringLiteral("退出批阅"));
        }
        if (submitGradeButton) {
            submitGradeButton->setEnabled(false);
            submitGradeButton->setText(QStringLiteral("提交成绩"));
        }
    }
}

void MainWindow::setSubmission(int index)
{
    if (!submissionList || index < 0 || index >= submissionList->count()) {
        return;
    }
    currentSubmissionId = submissionList->item(index)->data(Qt::UserRole).toInt();
    loadSubmissionDetail(currentSubmissionId);
}

void MainWindow::renderGradingAnswers(bool lockedByMe)
{
    clearLayout(gradingQuestionLayout);
    gradingScoreEdits.clear();
    gradingCommentEdits.clear();

    if (!gradingQuestionLayout) {
        return;
    }

    if (currentSubmissionAnswers.isEmpty()) {
        gradingQuestionLayout->addWidget(mutedLabel(QStringLiteral("这份答卷没有题目记录。")));
        return;
    }

    for (int i = 0; i < currentSubmissionAnswers.size(); ++i) {
        const QJsonObject answer = currentSubmissionAnswers.at(i).toObject();
        const QString questionType = answer.value(QStringLiteral("type")).toString();
        const QString maxScoreText = valueString(answer, QStringLiteral("maxScore"));
        bool maxScoreOk = false;
        const double maxScore = maxScoreText.toDouble(&maxScoreOk);
        const double scoreLimit = maxScoreOk ? maxScore : 1000.0;

        auto *cardFrame = new QFrame;
        cardFrame->setObjectName(QStringLiteral("gradingQuestionCard"));
        auto *layout = new QVBoxLayout(cardFrame);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(12);

        auto *top = new QHBoxLayout;
        auto *title = new QLabel(QStringLiteral("第 %1 题 · %2").arg(answer.value(QStringLiteral("questionIndex")).toInt()).arg(questionType));
        title->setObjectName(QStringLiteral("gradingQuestionTitle"));
        auto *scoreBadge = new QLabel(QStringLiteral("满分 %1 分").arg(maxScoreText));
        scoreBadge->setObjectName(QStringLiteral("scoreBadge"));
        top->addWidget(title);
        top->addStretch();
        top->addWidget(scoreBadge);
        layout->addLayout(top);

        auto *content = new QLabel(answer.value(QStringLiteral("content")).toString());
        content->setObjectName(QStringLiteral("questionContent"));
        content->setTextFormat(Qt::PlainText);
        content->setWordWrap(true);
        layout->addWidget(content);

        QStringList options;
        for (const QString &key : {QStringLiteral("optionA"), QStringLiteral("optionB"), QStringLiteral("optionC"), QStringLiteral("optionD")}) {
            const QString option = answer.value(key).toString();
            if (!option.isEmpty()) {
                options.append(QStringLiteral("%1. %2").arg(key.right(1), option));
            }
        }
        if (!options.isEmpty()) {
            auto *optionLabel = new QLabel(options.join(QStringLiteral("\n\n")));
            optionLabel->setObjectName(QStringLiteral("optionText"));
            optionLabel->setTextFormat(Qt::PlainText);
            optionLabel->setWordWrap(true);
            layout->addWidget(optionLabel);
        }

        auto *answers = new QHBoxLayout;
        answers->setSpacing(12);
        auto *studentBox = new QTextEdit;
        studentBox->setObjectName(QStringLiteral("studentAnswerViewer"));
        studentBox->setReadOnly(true);
        studentBox->setMinimumHeight(96);
        studentBox->setPlainText(answer.value(QStringLiteral("studentAnswer")).toString());
        auto *referenceBox = new QTextEdit;
        referenceBox->setObjectName(QStringLiteral("referenceAnswerViewer"));
        referenceBox->setReadOnly(true);
        referenceBox->setMinimumHeight(96);
        referenceBox->setPlainText(answer.value(QStringLiteral("referenceAnswer")).toString());

        auto *studentWrap = new QVBoxLayout;
        auto *studentLabel = new QLabel(QStringLiteral("学生答案"));
        studentLabel->setObjectName(QStringLiteral("fieldLabel"));
        studentWrap->addWidget(studentLabel);
        studentWrap->addWidget(studentBox);
        auto *referenceWrap = new QVBoxLayout;
        auto *referenceLabel = new QLabel(QStringLiteral("参考答案"));
        referenceLabel->setObjectName(QStringLiteral("fieldLabel"));
        referenceWrap->addWidget(referenceLabel);
        referenceWrap->addWidget(referenceBox);
        answers->addLayout(studentWrap, 1);
        answers->addLayout(referenceWrap, 1);
        layout->addLayout(answers);

        auto *gradeLine = new QHBoxLayout;
        auto *scoreLabel = new QLabel(QStringLiteral("本题得分"));
        scoreLabel->setObjectName(QStringLiteral("fieldLabel"));
        bool initialScoreOk = false;
        double initialScore = valueString(answer, QStringLiteral("score")).toDouble(&initialScoreOk);
        initialScore = initialScoreOk ? qBound(0.0, scoreToOneDecimal(initialScore), scoreLimit) : 0.0;
        auto *scoreEdit = new QLineEdit(scoreText(initialScore));
        scoreEdit->setObjectName(QStringLiteral("gradeScoreEdit"));
        scoreEdit->setPlaceholderText(QStringLiteral("0 - %1").arg(maxScoreText));
        auto *scoreValidator = new QDoubleValidator(0.0, scoreLimit, 1, scoreEdit);
        scoreValidator->setNotation(QDoubleValidator::StandardNotation);
        scoreEdit->setValidator(scoreValidator);
        scoreEdit->setEnabled(lockedByMe);
        auto *scoreStep = new QDoubleSpinBox;
        scoreStep->setRange(0.0, scoreLimit);
        scoreStep->setDecimals(1);
        scoreStep->setSingleStep(0.5);
        scoreStep->setSuffix(QStringLiteral(" 分"));
        scoreStep->setValue(initialScore);
        scoreStep->setEnabled(lockedByMe);
        auto *commentLabel = new QLabel(QStringLiteral("本题评语"));
        commentLabel->setObjectName(QStringLiteral("fieldLabel"));
        gradeLine->addWidget(scoreLabel);
        gradeLine->addWidget(scoreEdit);
        gradeLine->addWidget(scoreStep);
        gradeLine->addWidget(commentLabel);
        layout->addLayout(gradeLine);

        auto *commentEdit = new QTextEdit;
        commentEdit->setObjectName(QStringLiteral("gradeCommentEdit"));
        commentEdit->setMinimumHeight(70);
        commentEdit->setPlaceholderText(QStringLiteral("可填写本题批阅意见"));
        commentEdit->setPlainText(answer.value(QStringLiteral("comment")).toString());
        commentEdit->setReadOnly(!lockedByMe);
        layout->addWidget(commentEdit);

        gradingScoreEdits.append(scoreEdit);
        gradingCommentEdits.append(commentEdit);
        connect(scoreStep,
                static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                this,
                [scoreEdit](double value) {
                    const QString formatted = scoreText(value);
                    if (scoreEdit && scoreEdit->text() != formatted) {
                        scoreEdit->setText(formatted);
                    }
                });
        connect(scoreEdit, &QLineEdit::textChanged, this, [this, scoreStep](const QString &text) {
            bool ok = false;
            const double value = text.trimmed().toDouble(&ok);
            if (ok && scoreTextHasAtMostOneDecimal(text)) {
                QSignalBlocker blocker(scoreStep);
                scoreStep->setValue(scoreToOneDecimal(value));
            }
            updateGradeTotalScore(scoreSpinBox, gradingScoreEdits);
        });

        gradingQuestionLayout->addWidget(cardFrame);
    }
    gradingQuestionLayout->addStretch();
}

void MainWindow::loadSubmissionDetail(int submissionId)
{
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("get_submission_detail"));
    request.insert(QStringLiteral("submissionId"), submissionId);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("答卷详情读取失败"), error);
        return;
    }

    const QJsonObject submission = response.value(QStringLiteral("submission")).toObject();
    currentSubmissionAnswers = response.value(QStringLiteral("answers")).toArray();
    const QString status = submission.value(QStringLiteral("status")).toString();
    const QString graderUsername = submission.value(QStringLiteral("graderUsername")).toString();
    const bool lockedByMe = status == QStringLiteral("grading") && graderUsername == currentUser.username;
    const bool lockedByOther = status == QStringLiteral("grading") && !lockedByMe;
    const bool canLock = status == QStringLiteral("pending");
    submissionTitleLabel->setText(QStringLiteral("<h3>%1 的答卷</h3><p>%2 · %3 · 答卷 #%4</p>")
                                      .arg(submission.value(QStringLiteral("studentName")).toString(),
                                           submission.value(QStringLiteral("examName")).toString(),
                                           submission.value(QStringLiteral("course")).toString())
                                      .arg(submissionId));
    submissionStateLabel->setText(QStringLiteral("状态：%1 · 阅卷教师：%2")
                                      .arg(statusText(status),
                                           submission.value(QStringLiteral("graderName")).toString().isEmpty()
                                               ? QStringLiteral("未锁定")
                                               : submission.value(QStringLiteral("graderName")).toString()));
    submissionStateLabel->setProperty("status", status);
    repolish(submissionStateLabel);
    if (lockSubmissionButton) {
        lockSubmissionButton->setEnabled(canLock);
        if (canLock) {
            lockSubmissionButton->setText(QStringLiteral("开始批阅并打分"));
        } else if (lockedByMe) {
            lockSubmissionButton->setText(QStringLiteral("正在批阅这份答卷"));
        } else if (status == QStringLiteral("graded")) {
            lockSubmissionButton->setText(QStringLiteral("已完成批阅"));
        } else {
            lockSubmissionButton->setText(QStringLiteral("不可领取"));
        }
        lockSubmissionButton->setToolTip(canLock
                                             ? QStringLiteral("领取后这份答卷会分配给你批阅，其他教师不能同时修改。")
                                             : QStringLiteral("只有待批阅状态的答卷可以领取。"));
    }
    if (releaseSubmissionButton) {
        releaseSubmissionButton->setEnabled(status == QStringLiteral("grading") && (lockedByMe || currentUser.role == UserRole::Admin));
        releaseSubmissionButton->setText(lockedByMe ? QStringLiteral("退出批阅") : QStringLiteral("退出批阅"));
        releaseSubmissionButton->setToolTip(lockedByMe
                                                ? QStringLiteral("释放后答卷回到待批阅状态。")
                                                : QStringLiteral("只能释放自己领取的答卷。"));
    }
    if (submitGradeButton) {
        submitGradeButton->setEnabled(lockedByMe);
        submitGradeButton->setText(lockedByMe ? QStringLiteral("提交成绩") : QStringLiteral("提交成绩"));
        submitGradeButton->setToolTip(lockedByMe
                                          ? QStringLiteral("填写每题得分和评语后提交评分。")
                                          : (lockedByOther
                                                 ? QStringLiteral("这份答卷已被其他教师锁定。")
                                                 : QStringLiteral("请先领取答卷，再提交评分。")));
    }
    teacherCommentEdit->setPlainText(submission.value(QStringLiteral("comment")).toString());
    teacherCommentEdit->setReadOnly(!lockedByMe);
    scoreSpinBox->setValue(scoreToOneDecimal(submission.value(QStringLiteral("totalScore")).toDouble()));
    scoreSpinBox->setEnabled(lockedByMe);
    renderGradingAnswers(lockedByMe);
}

void MainWindow::lockCurrentSubmission()
{
    if (currentSubmissionId <= 0) {
        QMessageBox::warning(this, QStringLiteral("未选择答卷"), QStringLiteral("请先选择一份答卷。"));
        return;
    }
    const int lockedId = currentSubmissionId;
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("lock_submission"));
    request.insert(QStringLiteral("submissionId"), lockedId);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("领取失败"), error);
        return;
    }
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("已进入批阅模式，可以逐题给分。")));
    if (submissionStatusFilter) {
        const int gradingIndex = submissionStatusFilter->findData(QStringLiteral("grading"));
        if (gradingIndex >= 0) {
            submissionStatusFilter->setCurrentIndex(gradingIndex);
        }
    }
    currentSubmissionId = lockedId;
    populateSubmissionList();
    currentSubmissionId = lockedId;
    loadSubmissionDetail(lockedId);
}

void MainWindow::releaseCurrentSubmission()
{
    if (currentSubmissionId <= 0) {
        QMessageBox::warning(this, QStringLiteral("未选择答卷"), QStringLiteral("请先选择一份答卷。"));
        return;
    }
    const int releasedId = currentSubmissionId;
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("release_submission"));
    request.insert(QStringLiteral("submissionId"), releasedId);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("释放失败"), error);
        return;
    }
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("答卷锁已释放。")));
    if (submissionStatusFilter) {
        const int pendingIndex = submissionStatusFilter->findData(QStringLiteral("pending"));
        if (pendingIndex >= 0) {
            submissionStatusFilter->setCurrentIndex(pendingIndex);
        }
    }
    currentSubmissionId = releasedId;
    populateSubmissionList();
    currentSubmissionId = releasedId;
    loadSubmissionDetail(releasedId);
}

void MainWindow::submitCurrentGrade()
{
    if (currentSubmissionId <= 0) {
        QMessageBox::warning(this, QStringLiteral("未选择答卷"), QStringLiteral("请先选择一份答卷。"));
        return;
    }

    if (gradingScoreEdits.isEmpty() || currentSubmissionAnswers.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("没有可评分题目"), QStringLiteral("请先选择并领取一份学生答卷。"));
        return;
    }

    QJsonArray grades;
    double total = 0;
    for (int row = 0; row < currentSubmissionAnswers.size(); ++row) {
        const QJsonObject answer = currentSubmissionAnswers.at(row).toObject();
        auto *scoreEdit = row < gradingScoreEdits.size() ? gradingScoreEdits.at(row) : nullptr;
        auto *commentEdit = row < gradingCommentEdits.size() ? gradingCommentEdits.at(row) : nullptr;
        bool ok = false;
        const QString scoreInput = scoreEdit ? scoreEdit->text().trimmed() : QStringLiteral("0");
        const double score = scoreInput.toDouble(&ok);
        bool maxOk = false;
        const double maxScore = valueString(answer, QStringLiteral("maxScore")).toDouble(&maxOk);
        if (!ok || !scoreTextHasAtMostOneDecimal(scoreInput) || score < 0 || (maxOk && score > maxScore)) {
            if (scoreEdit) {
                scoreEdit->setProperty("invalid", true);
                repolish(scoreEdit);
                scoreEdit->setFocus();
            }
            QMessageBox::warning(this,
                                 QStringLiteral("分数格式错误"),
                                 QStringLiteral("第 %1 题得分需要是 0 到满分之间的数字，最多保留 1 位小数。").arg(row + 1));
            return;
        }
        const double normalizedScore = scoreToOneDecimal(score);
        if (scoreEdit) {
            scoreEdit->setProperty("invalid", false);
            const QString normalizedText = scoreText(normalizedScore);
            if (scoreEdit->text() != normalizedText) {
                scoreEdit->setText(normalizedText);
            }
            repolish(scoreEdit);
        }
        total += normalizedScore;
        QJsonObject grade;
        grade.insert(QStringLiteral("answerId"), answer.value(QStringLiteral("answerId")).toInt());
        grade.insert(QStringLiteral("score"), normalizedScore);
        grade.insert(QStringLiteral("comment"), commentEdit ? commentEdit->toPlainText() : QString());
        grades.append(grade);
    }
    const double normalizedTotal = scoreToOneDecimal(total);
    scoreSpinBox->setValue(normalizedTotal);

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("submit_grade"));
    request.insert(QStringLiteral("submissionId"), currentSubmissionId);
    request.insert(QStringLiteral("answerGrades"), grades);
    request.insert(QStringLiteral("totalScore"), normalizedTotal);
    request.insert(QStringLiteral("comment"), teacherCommentEdit->toPlainText());
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("提交评分失败"), error);
        return;
    }
    showToast(response.value(QStringLiteral("message")).toString());
    if (submissionStatusFilter) {
        const int gradedIndex = submissionStatusFilter->findData(QStringLiteral("graded"));
        if (gradedIndex >= 0) {
            submissionStatusFilter->setCurrentIndex(gradedIndex);
        }
    }
    populateSubmissionList();
    loadStats();
}

void MainWindow::populateAccountTable()
{
    if (!accountTable || !accountRoleFilter) {
        return;
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_users"));
    request.insert(QStringLiteral("roleFilter"), accountRoleFilter->currentData().toString());
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("用户列表读取失败"), error);
        return;
    }

    const QString keyword = accountSearchEdit ? accountSearchEdit->text().trimmed() : QString();
    const QJsonArray users = response.value(QStringLiteral("users")).toArray();
    accountTable->setRowCount(0);
    for (const QJsonValue &value : users) {
        const QJsonObject user = value.toObject();
        const QString rowText = QStringLiteral("%1 %2 %3 %4")
                                    .arg(user.value(QStringLiteral("role")).toString(),
                                         user.value(QStringLiteral("username")).toString(),
                                         user.value(QStringLiteral("displayName")).toString(),
                                         user.value(QStringLiteral("note")).toString());
        if (!keyword.isEmpty() && !rowText.contains(keyword, Qt::CaseInsensitive)) {
            continue;
        }
        const int row = accountTable->rowCount();
        accountTable->insertRow(row);
        accountTable->setItem(row, 0, readOnlyItem(QString::number(user.value(QStringLiteral("id")).toInt())));
        accountTable->setItem(row, 1, readOnlyItem(roleToText(roleFromCode(user.value(QStringLiteral("role")).toString()))));
        accountTable->setItem(row, 2, readOnlyItem(user.value(QStringLiteral("username")).toString()));
        accountTable->setItem(row, 3, new QTableWidgetItem(user.value(QStringLiteral("displayName")).toString()));
        accountTable->setItem(row, 4, new QTableWidgetItem(user.value(QStringLiteral("note")).toString()));
        accountTable->setItem(row, 5, readOnlyItem(localTimeText(user.value(QStringLiteral("createdAt")).toString())));
        accountTable->setItem(row, 6, readOnlyItem(user.value(QStringLiteral("isActive")).toBool(true)
                                                   ? QStringLiteral("正常")
                                                   : QStringLiteral("已停用")));
    }
    if (accountTable->rowCount() > 0) {
        accountTable->selectRow(0);
        populateAccountEditorFromSelection();
    } else {
        clearAccountEditor();
    }
    if (accountCountLabel) {
        accountCountLabel->setText(QStringLiteral("%1 个用户").arg(accountTable->rowCount()));
    }
}

void MainWindow::populateAccountEditorFromSelection()
{
    const int row = accountTable ? accountTable->currentRow() : -1;
    if (row < 0 || !accountTable->item(row, 0)) {
        clearAccountEditor();
        return;
    }

    selectedAccountUserId = accountTable->item(row, 0)->text().toInt();
    selectedAccountRole = accountTable->item(row, 1) ? accountTable->item(row, 1)->text() : QString();
    selectedAccountUsername = accountTable->item(row, 2) ? accountTable->item(row, 2)->text() : QString();
    const QString displayName = accountTable->item(row, 3) ? accountTable->item(row, 3)->text() : QString();
    const QString note = accountTable->item(row, 4) ? accountTable->item(row, 4)->text() : QString();
    const QString status = accountTable->item(row, 6) ? accountTable->item(row, 6)->text() : QString();
    qDebug() << "[ADMIN_ACCOUNT_SELECTED]"
             << "id=" << selectedAccountUserId
             << "username=" << selectedAccountUsername
             << "role=" << selectedAccountRole;

    if (accountSelectedLabel) {
        accountSelectedLabel->setText(QStringLiteral("%1（%2，%3）")
                                          .arg(selectedAccountUsername, selectedAccountRole, status));
    }
    if (accountDisplayNameEdit) {
        accountDisplayNameEdit->setText(displayName);
    }
    if (accountNoteEdit) {
        accountNoteEdit->setText(note);
    }
    if (newPasswordEdit) {
        newPasswordEdit->clear();
    }
    if (confirmPasswordEdit) {
        confirmPasswordEdit->clear();
    }
    statusBar()->showMessage(QStringLiteral("已选择账号：%1").arg(selectedAccountUsername), 3000);
}

void MainWindow::clearAccountEditor()
{
    selectedAccountUserId = 0;
    selectedAccountUsername.clear();
    selectedAccountRole.clear();
    if (accountSelectedLabel) {
        accountSelectedLabel->setText(QStringLiteral("当前未选择账号"));
    }
    if (accountDisplayNameEdit) {
        accountDisplayNameEdit->clear();
    }
    if (accountNoteEdit) {
        accountNoteEdit->clear();
    }
    if (newPasswordEdit) {
        newPasswordEdit->clear();
    }
    if (confirmPasswordEdit) {
        confirmPasswordEdit->clear();
    }
}

void MainWindow::createUser()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新增用户"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *roleCombo = new QComboBox;
    roleCombo->addItem(QStringLiteral("管理员"), roleToCode(UserRole::Admin));
    roleCombo->addItem(QStringLiteral("教师"), roleToCode(UserRole::Teacher));
    roleCombo->addItem(QStringLiteral("学生"), roleToCode(UserRole::Student));
    auto *username = new QLineEdit;
    auto *displayName = new QLineEdit;
    auto *password = new QLineEdit(QStringLiteral("123456"));
    password->setEchoMode(QLineEdit::Password);
    auto *note = new QLineEdit;
    form->addRow(QStringLiteral("角色"), roleCombo);
    form->addRow(QStringLiteral("账号"), username);
    form->addRow(QStringLiteral("姓名"), displayName);
    form->addRow(QStringLiteral("初始密码"), password);
    form->addRow(QStringLiteral("说明"), note);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("创建"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (username->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("账号为空"), QStringLiteral("请填写账号。"));
        return;
    }
    if (password->text().size() < 6) {
        QMessageBox::warning(this, QStringLiteral("密码太短"), QStringLiteral("初始密码至少需要 6 位。"));
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("create_user"));
    request.insert(QStringLiteral("role"), roleCombo->currentData().toString());
    request.insert(QStringLiteral("username"), username->text().trimmed());
    request.insert(QStringLiteral("displayName"), displayName->text().trimmed());
    request.insert(QStringLiteral("password"), password->text());
    request.insert(QStringLiteral("note"), note->text().trimmed());
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("创建用户失败"), error);
        return;
    }
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("用户创建成功。")));
    refreshAdminUserTables();
}

void MainWindow::onAdminAccountApplyClicked()
{
    // 账号权限页“确定修改”统一入口：三层保险（clicked / 注册表 / legacy SLOT）都汇入此处，350ms 去重后接真实逻辑。
    static qint64 lastApplyMs = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastApplyMs < 350) {
        qDebug() << "[ADMIN_ACCOUNT_ACTION]" << "apply skip duplicate";
        return;
    }
    lastApplyMs = now;
    qDebug() << "[ADMIN_ACCOUNT_ACTION]" << "apply clicked";
    applyAccountChanges();
}

void MainWindow::onAdminAccountDisableClicked()
{
    static qint64 lastDisableMs = 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastDisableMs < 350) {
        qDebug() << "[ADMIN_ACCOUNT_ACTION]" << "disable skip duplicate";
        return;
    }
    lastDisableMs = now;
    qDebug() << "[ADMIN_ACCOUNT_ACTION]" << "disable clicked";
    deleteSelectedUser();
}

void MainWindow::applyAccountChanges()
{
    qDebug() << "[ADMIN_ACCOUNT_ACTION]" << "apply handler entered";

    // 身份只认“账号权限页”当前选中账号：全部取自选中时缓存的成员（populateAccountEditorFromSelection 写入），
    // 绝不读取学生管理页/教师管理页的表格选中项，也不再依赖临时的 accountTable->currentRow()。
    const int selectedUserId = selectedAccountUserId;
    const QString selectedUsername = selectedAccountUsername;
    const QString selectedRole = selectedAccountRole;

    // (9) selectedUserId 无效（未选中）→ 既不 update_user 也不 reset_password。
    if (selectedUserId <= 0) {
        QMessageBox::information(this, QStringLiteral("请先选择账号"), QStringLiteral("请重新选择要修改的账号。"));
        return;
    }

    // 在“账号权限页”表格中按 selectedUserId 定位该账号所在行，读取其原始显示名/备注/角色。
    int row = -1;
    if (accountTable) {
        for (int r = 0; r < accountTable->rowCount(); ++r) {
            if (accountTable->item(r, 0) && accountTable->item(r, 0)->text().toInt() == selectedUserId) {
                row = r;
                break;
            }
        }
    }
    // (12) 找不到当前选中账号（已被刷新移除/停用）→ 不调用任何接口。
    if (row < 0 || !accountTable->item(row, 0)) {
        QMessageBox::information(this, QStringLiteral("账号不存在"),
                                QStringLiteral("没有找到要修改的账号，或账号已停用。"));
        return;
    }

    const QString roleCode = roleToCode(roleFromText(accountTable->item(row, 1)
                                                         ? accountTable->item(row, 1)->text()
                                                         : selectedRole));
    const QString originalName = accountTable->item(row, 3) ? accountTable->item(row, 3)->text().trimmed() : QString();
    const QString originalNote = accountTable->item(row, 4) ? accountTable->item(row, 4)->text().trimmed() : QString();
    const QString displayName = accountDisplayNameEdit ? accountDisplayNameEdit->text().trimmed() : originalName;
    const QString note = accountNoteEdit ? accountNoteEdit->text().trimmed() : originalNote;
    const bool profileChanged = (displayName != originalName) || (note != originalNote);

    // 密码只读账号权限页右侧两个输入框；两个都空 → passwordRequested=false → 绝不进入重置分支。
    const QString newPassword = newPasswordEdit ? newPasswordEdit->text() : QString();
    const QString confirmPassword = confirmPasswordEdit ? confirmPasswordEdit->text() : QString();
    const bool passwordRequested = !newPassword.isEmpty() || !confirmPassword.isEmpty();

    qDebug() << "[ADMIN_ACCOUNT_APPLY]"
             << "selectedUserId=" << selectedUserId
             << "username=" << selectedUsername
             << "role=" << selectedRole
             << "profileChanged=" << profileChanged
             << "newPasswordLen=" << newPassword.length()
             << "confirmPasswordLen=" << confirmPassword.length()
             << "passwordRequested=" << passwordRequested;

    if (!profileChanged && !passwordRequested) {
        QMessageBox::information(this, QStringLiteral("无需修改"), QStringLiteral("没有需要修改的内容。"));
        return;
    }

    // 刷新列表并按 selectedUserId 重新选中，避免丢失当前选中账号信息。
    const auto refreshAndReselect = [this](int uid) {
        refreshAdminUserTables();
        if (!accountTable) {
            return;
        }
        for (int r = 0; r < accountTable->rowCount(); ++r) {
            if (accountTable->item(r, 0) && accountTable->item(r, 0)->text().toInt() == uid) {
                accountTable->selectRow(r);
                break;
            }
        }
    };

    // (6) 只有填了密码才校验密码（两个都空时这段完全不执行）。
    if (passwordRequested) {
        if (newPassword.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("新密码为空"), QStringLiteral("请输入新密码。"));
            newPasswordEdit->setFocus();
            return;
        }
        if (confirmPassword.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("确认密码为空"), QStringLiteral("请再次输入新密码。"));
            confirmPasswordEdit->setFocus();
            return;
        }
        if (newPassword != confirmPassword) {
            QMessageBox::warning(this, QStringLiteral("密码不一致"), QStringLiteral("两次输入的新密码不一致。"));
            confirmPasswordEdit->setFocus();
            return;
        }
        if (newPassword.length() < 6) {
            QMessageBox::warning(this, QStringLiteral("密码太短"), QStringLiteral("新密码至少需要 6 位。"));
            newPasswordEdit->setFocus();
            return;
        }
    }

    // (1) 资料：仅在显示名/备注变化时调用 update_user，使用 selectedUserId。
    if (profileChanged) {
        QJsonObject request;
        request.insert(QStringLiteral("type"), QStringLiteral("update_user"));
        request.insert(QStringLiteral("userId"), selectedUserId);
        request.insert(QStringLiteral("role"), roleCode);
        request.insert(QStringLiteral("displayName"), displayName);
        request.insert(QStringLiteral("note"), note);
        QJsonObject response;
        QString error;
        if (!ApiClient::instance().request(request, &response, &error)) {
            showError(QStringLiteral("保存用户失败"), error);
            return;
        }
    }

    // (7) 只改资料：到此结束，显式 return，绝不进入任何密码重置逻辑。
    if (!passwordRequested) {
        showToast(QStringLiteral("资料已保存。"));
        statusBar()->showMessage(QStringLiteral("账号 %1：资料已保存。").arg(selectedUsername), 3000);
        QMessageBox::information(this, QStringLiteral("修改成功"), QStringLiteral("资料已保存。"));
        refreshAndReselect(selectedUserId);
        return;
    }

    // (5) 全函数唯一的 reset_password 调用点：用账号权限页当前 selectedUserId 重置密码。
    {
        qDebug() << "[ADMIN_ACCOUNT_RESET_REQUEST]"
                 << "selectedUserId=" << selectedUserId
                 << "username=" << selectedUsername
                 << "role=" << selectedRole
                 << "newPasswordLen=" << newPassword.length()
                 << "confirmPasswordLen=" << confirmPassword.length();
        QJsonObject request;
        request.insert(QStringLiteral("type"), QStringLiteral("reset_password"));
        request.insert(QStringLiteral("userId"), selectedUserId);
        request.insert(QStringLiteral("newPassword"), newPassword);
        QJsonObject response;
        QString error;
        if (!ApiClient::instance().request(request, &response, &error)) {
            // (11) 账号语义错误提示，不出现“教师或学生”。
            showError(QStringLiteral("重置密码失败"), error);
            return;
        }
        if (newPasswordEdit) {
            newPasswordEdit->clear();
        }
        if (confirmPasswordEdit) {
            confirmPasswordEdit->clear();
        }
    }

    // (8) 改了密码（可能也改了资料）的成功提示。
    const QString successText = profileChanged ? QStringLiteral("资料和密码已更新。")
                                               : QStringLiteral("密码已重置。");
    showToast(successText);
    statusBar()->showMessage(QStringLiteral("账号 %1：%2").arg(selectedUsername, successText), 3000);
    QMessageBox::information(this, QStringLiteral("修改成功"), successText);
    refreshAndReselect(selectedUserId);
}

void MainWindow::deleteSelectedUser()
{
    qDebug() << "[ADMIN_ACCOUNT_ACTION]" << "disable handler entered";
    const int row = accountTable ? accountTable->currentRow() : -1;
    if (row < 0 || !accountTable->item(row, 0)) {
        QMessageBox::information(this, QStringLiteral("请先选择账号"), QStringLiteral("请先选择要停用的账号。"));
        return;
    }
    const int userId = accountTable->item(row, 0)->text().toInt();
    const QString username = accountTable->item(row, 2)->text();
    if (userId == currentUser.id) {
        QMessageBox::warning(this, QStringLiteral("不能停用"), QStringLiteral("不能停用当前登录账号。"));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("确认停用"),
                              QStringLiteral("确定停用账号 %1 吗？历史考试、答卷、成绩和日志会保留。").arg(username)) != QMessageBox::Yes) {
        return;
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("delete_user"));
    request.insert(QStringLiteral("userId"), userId);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("停用用户失败"), error);
        return;
    }
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("账号已停用。")));
    statusBar()->showMessage(QStringLiteral("账号 %1 已停用").arg(username), 3000);
    QMessageBox::information(this, QStringLiteral("停用成功"), QStringLiteral("账号已停用。"));
    refreshAdminUserTables();
}

void MainWindow::changeOwnPassword()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("修改登录密码"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(sectionTitle(QStringLiteral("修改登录密码"), QStringLiteral("请输入旧密码，并设置新密码。")));

    auto *form = new QFormLayout;
    auto *oldPassword = new QLineEdit;
    auto *newPassword = new QLineEdit;
    auto *confirmPassword = new QLineEdit;
    oldPassword->setEchoMode(QLineEdit::Password);
    newPassword->setEchoMode(QLineEdit::Password);
    confirmPassword->setEchoMode(QLineEdit::Password);
    oldPassword->setPlaceholderText(QStringLiteral("当前登录密码"));
    newPassword->setPlaceholderText(QStringLiteral("新密码，至少 6 位"));
    confirmPassword->setPlaceholderText(QStringLiteral("再输入一遍新密码"));
    form->addRow(QStringLiteral("旧密码"), oldPassword);
    form->addRow(QStringLiteral("新密码"), newPassword);
    form->addRow(QStringLiteral("确认密码"), confirmPassword);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (oldPassword->text().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("旧密码为空"), QStringLiteral("请输入旧密码。"));
        return;
    }
    if (newPassword->text().size() < 6) {
        QMessageBox::warning(this, QStringLiteral("密码太短"), QStringLiteral("新密码至少需要 6 位。"));
        return;
    }
    if (newPassword->text() != confirmPassword->text()) {
        QMessageBox::warning(this, QStringLiteral("密码不一致"), QStringLiteral("两次输入的新密码不一致。"));
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("change_password"));
    request.insert(QStringLiteral("oldPassword"), oldPassword->text());
    request.insert(QStringLiteral("newPassword"), newPassword->text());
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("修改密码失败"), error);
        return;
    }
    showToast(response.value(QStringLiteral("message")).toString(QStringLiteral("密码修改成功。")));
    QMessageBox::information(this, QStringLiteral("修改成功"), QStringLiteral("密码已修改，下次登录请使用新密码。"));
}

void MainWindow::loadStats()
{
    QStringList lines;
    QJsonObject response;
    QString error;
    auto *dashboardStatsTable = findChild<QTableWidget *>(QStringLiteral("dashboardStatsTable"));
    if (dashboardStatsTable) {
        dashboardStatsTable->setRowCount(0);
    }
    const auto setMetric = [this](int index, const QString &value) {
        if (index >= 0 && index < dashboardMetricLabels.size() && dashboardMetricLabels.at(index)) {
            dashboardMetricLabels.at(index)->setText(value);
        }
    };
    const auto addDashboardRow = [dashboardStatsTable](const QString &item, const QString &value, const QString &note) {
        if (!dashboardStatsTable) {
            return;
        }
        const int row = dashboardStatsTable->rowCount();
        dashboardStatsTable->insertRow(row);
        dashboardStatsTable->setItem(row, 0, readOnlyItem(item));
        dashboardStatsTable->setItem(row, 1, readOnlyItem(value));
        dashboardStatsTable->setItem(row, 2, readOnlyItem(note));
    };

    if (currentUser.role == UserRole::Student) {
        QJsonObject request;
        request.insert(QStringLiteral("type"), QStringLiteral("list_my_submissions"));
        if (ApiClient::instance().request(request, &response, &error)) {
            const QJsonArray submissions = response.value(QStringLiteral("submissions")).toArray();
            int graded = 0;
            double totalScore = 0;
            for (const QJsonValue &value : submissions) {
                if (value.toObject().value(QStringLiteral("status")).toString() == QStringLiteral("graded")) {
                    ++graded;
                    totalScore += value.toObject().value(QStringLiteral("totalScore")).toDouble();
                }
            }
            setMetric(1, QString::number(submissions.size()));
            const int pending = qMax(0, submissions.size() - graded);
            setMetric(2, QString::number(pending));
            setMetric(3, QString::number(graded));
            addDashboardRow(QStringLiteral("答卷"),
                            QStringLiteral("%1 份").arg(submissions.size()),
                            QStringLiteral("已批 %1 份，待批 %2 份").arg(graded).arg(pending));
            lines << QStringLiteral("我的答卷：%1 份").arg(submissions.size());
            lines << QStringLiteral("已批阅：%1 份").arg(graded);
            if (graded > 0) {
                const QString averageScore = QString::number(totalScore / graded, 'f', 1);
                addDashboardRow(QStringLiteral("成绩"),
                                QStringLiteral("%1 分").arg(averageScore),
                                QStringLiteral("基于 %1 份已出成绩答卷").arg(graded));
                lines << QStringLiteral("平均得分：%1 分").arg(averageScore);
            }
        }
        QJsonObject examRequest;
        examRequest.insert(QStringLiteral("type"), QStringLiteral("list_exams"));
        if (ApiClient::instance().request(examRequest, &response, &error)) {
            int running = 0;
            for (const QJsonValue &value : response.value(QStringLiteral("exams")).toArray()) {
                const QJsonObject exam = value.toObject();
                if (exam.value(QStringLiteral("state")).toString() == QStringLiteral("running")
                    && exam.value(QStringLiteral("mySubmissionCount")).toInt() < exam.value(QStringLiteral("maxSubmissions")).toInt(1)) {
                    ++running;
                }
            }
            setMetric(0, QString::number(running));
            addDashboardRow(QStringLiteral("考试"),
                            QStringLiteral("%1 场").arg(running),
                            QStringLiteral("当前可进入且未超过提交次数"));
            lines << QStringLiteral("可参加考试：%1 场").arg(running);
        }
    } else {
        QJsonObject request;
        request.insert(QStringLiteral("type"), QStringLiteral("list_gradable_submissions"));
        request.insert(QStringLiteral("statusFilter"), QStringLiteral("all"));
        if (ApiClient::instance().request(request, &response, &error)) {
            const QJsonArray submissions = response.value(QStringLiteral("submissions")).toArray();
            int pending = 0;
            int grading = 0;
            int graded = 0;
            for (const QJsonValue &value : submissions) {
                const QString status = value.toObject().value(QStringLiteral("status")).toString();
                if (status == QStringLiteral("pending")) {
                    ++pending;
                } else if (status == QStringLiteral("grading")) {
                    ++grading;
                } else if (status == QStringLiteral("graded")) {
                    ++graded;
                }
            }
            setMetric(2, QString::number(pending));
            setMetric(3, QString::number(graded));
            addDashboardRow(QStringLiteral("答卷"),
                            QStringLiteral("%1 份").arg(submissions.size()),
                            QStringLiteral("已批 %1 份，待批 %2 份，批阅中 %3 份").arg(graded).arg(pending).arg(grading));
            lines << QStringLiteral("待批阅：%1 份").arg(pending);
            lines << QStringLiteral("正在批阅：%1 份").arg(grading);
            lines << QStringLiteral("已批阅：%1 份").arg(graded);
        }
        QJsonObject examRequest;
        examRequest.insert(QStringLiteral("type"), QStringLiteral("list_exams"));
        if (ApiClient::instance().request(examRequest, &response, &error)) {
            const QJsonArray exams = response.value(QStringLiteral("exams")).toArray();
            int published = 0;
            int running = 0;
            for (const QJsonValue &value : exams) {
                const QJsonObject exam = value.toObject();
                if (exam.value(QStringLiteral("publishStatus")).toString() == QStringLiteral("published")) {
                    ++published;
                }
                if (exam.value(QStringLiteral("state")).toString() == QStringLiteral("running")) {
                    ++running;
                }
            }
            const int examCount = exams.size();
            setMetric(0, QString::number(examCount));
            addDashboardRow(QStringLiteral("考试"),
                            QStringLiteral("%1 场").arg(examCount),
                            QStringLiteral("已发布 %1 场，进行中 %2 场").arg(published).arg(running));
            lines << QStringLiteral("考试总数：%1 场").arg(examCount);
        }
        QJsonObject questionRequest;
        questionRequest.insert(QStringLiteral("type"), QStringLiteral("list_questions"));
        if (ApiClient::instance().request(questionRequest, &response, &error)) {
            const int questionCount = response.value(QStringLiteral("questions")).toArray().size();
            setMetric(1, QString::number(questionCount));
            addDashboardRow(QStringLiteral("题库"),
                            QStringLiteral("%1 道").arg(questionCount),
                            QStringLiteral("含单选、多选、主观等题型"));
        }
    }

    if (!error.isEmpty() && lines.isEmpty()) {
        lines << QStringLiteral("统计读取失败：%1").arg(error);
    }
    const QString text = lines.isEmpty() ? QStringLiteral("暂无统计数据。") : lines.join(QStringLiteral("\n"));
    if (dashboardStatsTable && dashboardStatsTable->rowCount() == 0) {
        addDashboardRow(lines.isEmpty() ? QStringLiteral("状态") : QStringLiteral("统计"),
                        QStringLiteral("--"),
                        text);
    }
    if (dashboardLabel) {
        dashboardLabel->setText(lines.isEmpty()
                                    ? QStringLiteral("暂无统计数据。")
                                    : QStringLiteral("统计已更新，共 %1 项。").arg(dashboardStatsTable ? dashboardStatsTable->rowCount() : lines.size()));
    }
    if (statsLabel) {
        statsLabel->setText(text);
    }
}

void MainWindow::refreshRiskRecords()
{
    if (!riskRecordTable || (currentUser.role != UserRole::Teacher && currentUser.role != UserRole::Admin)) {
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_risk_records"));
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        showError(QStringLiteral("风险记录读取失败"), error);
        return;
    }

    riskRecordTable->setRowCount(0);
    const QJsonArray records = response.value(QStringLiteral("records")).toArray();

    // 当前筛选的考试（0 = 全部考试）。仅做客户端过滤，不改服务端接口。
    const int filterExamId = riskExamFilter ? riskExamFilter->currentData().toInt() : 0;

    int abnormalStudents = 0;
    int totalEvents = 0;
    int midCount = 0;
    int highCount = 0;
    int severeCount = 0;
    // 异常类型聚合：type -> (次数, 累计风险分)。来自逐个学生的 list_risk_logs。
    QMap<QString, QPair<int, int>> typeStats;

    for (const QJsonValue &value : records) {
        const QJsonObject record = value.toObject();
        // 按考试筛选：选定某场考试时跳过其它考试的记录。
        if (filterExamId > 0 && record.value(QStringLiteral("examId")).toInt() != filterExamId) {
            continue;
        }
        const int row = riskRecordTable->rowCount();
        riskRecordTable->insertRow(row);

        const QString level = record.value(QStringLiteral("riskLevel")).toString(QStringLiteral("低风险"));
        ++abnormalStudents;
        totalEvents += record.value(QStringLiteral("abnormalCount")).toInt();
        if (level == QStringLiteral("中风险")) {
            ++midCount;
        } else if (level == QStringLiteral("高风险")) {
            ++highCount;
        } else if (level == QStringLiteral("严重风险")) {
            ++severeCount;
        }

        const QStringList values = {
            record.value(QStringLiteral("studentName")).toString(),
            record.value(QStringLiteral("studentUsername")).toString(),
            record.value(QStringLiteral("examName")).toString(),
            QString::number(record.value(QStringLiteral("riskScore")).toInt()),
            level,
            QString::number(record.value(QStringLiteral("abnormalCount")).toInt()),
            localTimeText(record.value(QStringLiteral("recentTime")).toString()),
            QStringLiteral("双击查看详情")
        };
        for (int col = 0; col < values.size(); ++col) {
            auto *item = readOnlyItem(values.at(col));
            if (col == 0) {
                item->setData(Qt::UserRole, record.value(QStringLiteral("examId")).toInt());
                item->setData(Qt::UserRole + 1, record.value(QStringLiteral("studentId")).toInt());
            }
            if (col == 4) {
                if (level == QStringLiteral("严重风险")) {
                    item->setForeground(QBrush(QColor(QStringLiteral("#b91c1c"))));
                    QFont f = item->font();
                    f.setBold(true);
                    item->setFont(f);
                } else if (level == QStringLiteral("高风险")) {
                    item->setForeground(QBrush(QColor(QStringLiteral("#dc2626"))));
                } else if (level == QStringLiteral("中风险")) {
                    item->setForeground(QBrush(QColor(QStringLiteral("#d97706"))));
                } else {
                    item->setForeground(QBrush(QColor(QStringLiteral("#059669"))));
                }
            }
            if (col == 7) {
                item->setForeground(QBrush(QColor(QStringLiteral("#2563eb"))));
            }
            riskRecordTable->setItem(row, col, item);
        }

        // 拉取该生异常明细，聚合到类型统计（演示规模下学生数很少，N+1 可接受）。
        QJsonObject logsRequest;
        logsRequest.insert(QStringLiteral("type"), QStringLiteral("list_risk_logs"));
        logsRequest.insert(QStringLiteral("examId"), record.value(QStringLiteral("examId")).toInt());
        logsRequest.insert(QStringLiteral("studentId"), record.value(QStringLiteral("studentId")).toInt());
        QJsonObject logsResponse;
        QString logsError;
        if (ApiClient::instance().request(logsRequest, &logsResponse, &logsError)) {
            for (const QJsonValue &logValue : logsResponse.value(QStringLiteral("logs")).toArray()) {
                const QJsonObject log = logValue.toObject();
                const QString type = log.value(QStringLiteral("abnormalType")).toString().trimmed().toUpper();
                if (type.isEmpty()) {
                    continue;
                }
                auto &entry = typeStats[type];
                entry.first += 1;
                entry.second += log.value(QStringLiteral("riskScore")).toInt();
            }
        }
    }

    updateRiskMetricCards(abnormalStudents, totalEvents, midCount, highCount, severeCount);
    populateEventTypeStats(typeStats);
}

void MainWindow::updateRiskMetricCards(int abnormalStudents, int totalEvents, int midCount, int highCount, int severeCount)
{
    const int values[] = {abnormalStudents, totalEvents, midCount, highCount, severeCount};
    for (int i = 0; i < riskMetricLabels.size() && i < 5; ++i) {
        if (riskMetricLabels.at(i)) {
            riskMetricLabels.at(i)->setText(QString::number(values[i]));
        }
    }
}

void MainWindow::populateEventTypeStats(const QMap<QString, QPair<int, int>> &typeStats)
{
    if (!eventTypeStatTable) {
        return;
    }
    // 按次数降序排列，方便答辩时一眼看出主要异常来源。
    QList<QString> types = typeStats.keys();
    std::sort(types.begin(), types.end(), [&typeStats](const QString &a, const QString &b) {
        return typeStats.value(a).first > typeStats.value(b).first;
    });

    eventTypeStatTable->setRowCount(0);
    for (const QString &type : types) {
        const int row = eventTypeStatTable->rowCount();
        eventTypeStatTable->insertRow(row);
        const QPair<int, int> stat = typeStats.value(type);
        const QColor color = abnormalCategoryColor(type);
        const QStringList values = {
            type,
            abnormalTypeText(type),
            abnormalCategoryName(type),
            QString::number(stat.first),
            QStringLiteral("+%1").arg(stat.second)
        };
        for (int col = 0; col < values.size(); ++col) {
            auto *item = readOnlyItem(values.at(col));
            if (col == 1 || col == 2) {
                item->setForeground(QBrush(color));
            }
            eventTypeStatTable->setItem(row, col, item);
        }
    }
}

void MainWindow::refreshRiskExamFilter()
{
    if (!riskExamFilter || (currentUser.role != UserRole::Teacher && currentUser.role != UserRole::Admin)) {
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_exams"));
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        // 拉取考试列表失败时静默保留现有下拉项（至少有“全部考试”），不打断看板。
        return;
    }

    // 记住当前选择的 examId，重建后尽量恢复，保证手动刷新不丢失筛选条件。
    const int previousExamId = riskExamFilter->currentData().toInt();

    QSignalBlocker blocker(riskExamFilter); // 重建期间不触发 currentIndexChanged
    riskExamFilter->clear();
    riskExamFilter->addItem(QStringLiteral("全部考试"), 0);

    const QJsonArray exams = response.value(QStringLiteral("exams")).toArray();
    for (const QJsonValue &value : exams) {
        const QJsonObject exam = value.toObject();
        const int examId = exam.value(QStringLiteral("id")).toInt();
        const QString name = exam.value(QStringLiteral("name")).toString();
        const QString stateText = statusText(exam.value(QStringLiteral("status")).toString());
        const QString label = stateText.isEmpty()
            ? name
            : QStringLiteral("%1（%2）").arg(name, stateText);
        riskExamFilter->addItem(label, examId);
    }

    // 恢复先前选择；若该考试已不存在则回到“全部考试”（索引 0）。
    int restoreIndex = 0;
    if (previousExamId > 0) {
        const int found = riskExamFilter->findData(previousExamId);
        if (found >= 0) {
            restoreIndex = found;
        }
    }
    riskExamFilter->setCurrentIndex(restoreIndex);
}

void MainWindow::showSelectedRiskRecordDetail()
{
    // 按钮入口（不依赖双击）：读取风险表当前选中行，未选中则提示，再复用 showRiskLogsForRow。
    const int row = riskRecordTable ? riskRecordTable->currentRow() : -1;
    qInfo("[SCORE_DETAIL_SELECTED] row=%d", row);
    if (row < 0) {
        QMessageBox::warning(this,
                             QStringLiteral("未选择记录"),
                             QStringLiteral("请先选择一名学生记录。"));
        return;
    }
    showRiskLogsForRow(row, 0);  // 复用既有异常详情逻辑（内部还会再做越界/缺ID 兜底提示）
}

void MainWindow::showRiskLogsForRow(int row, int column)
{
    Q_UNUSED(column)
    qInfo("[SCORE_DETAIL_START] row=%d", row);
    if (!riskRecordTable || row < 0 || row >= riskRecordTable->rowCount() || !riskRecordTable->item(row, 0)) {
        qWarning("[SCORE_DETAIL_ERROR] reason=no-record row=%d", row);
        QMessageBox::warning(this,
                             QStringLiteral("无法查看详情"),
                             QStringLiteral("请先选择一条记录。"));
        return;
    }

    const int examId = riskRecordTable->item(row, 0)->data(Qt::UserRole).toInt();
    const int studentId = riskRecordTable->item(row, 0)->data(Qt::UserRole + 1).toInt();
    if (examId <= 0 || studentId <= 0) {
        qWarning("[SCORE_DETAIL_ERROR] reason=missing-id row=%d examId=%d studentId=%d", row, examId, studentId);
        QMessageBox::warning(this,
                             QStringLiteral("无法查看详情"),
                             QStringLiteral("无法查看详情：当前记录缺少必要信息。"));
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list_risk_logs"));
    request.insert(QStringLiteral("examId"), examId);
    request.insert(QStringLiteral("studentId"), studentId);
    QJsonObject response;
    QString error;
    if (!ApiClient::instance().request(request, &response, &error)) {
        qWarning("[SCORE_DETAIL_ERROR] reason=request-failed detail=%s", qPrintable(error));
        showError(QStringLiteral("异常明细读取失败"), error);
        QMessageBox::warning(this,
                             QStringLiteral("无法查看详情"),
                             QStringLiteral("接口读取失败：%1").arg(error));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("学生异常事件时间轴"));
    dialog.resize(820, 480);
    auto *layout = new QVBoxLayout(&dialog);
    const QString studentName = riskRecordTable->item(row, 0)->text();
    const QString examName = riskRecordTable->item(row, 2)->text();
    layout->addWidget(sectionTitle(QStringLiteral("学生异常事件时间轴"),
                                   QStringLiteral("%1 · %2。按时间倒序排列，仅供教师人工复核；演示/设备类事件不计作弊分。").arg(studentName, examName)));

    auto *table = new QTableWidget(0, 5);
    table->setHorizontalHeaderLabels({QStringLiteral("发生时间"),
                                      QStringLiteral("异常类型"),
                                      QStringLiteral("中文说明"),
                                      QStringLiteral("本次加分"),
                                      QStringLiteral("事件描述")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    polishTable(table);
    layout->addWidget(table, 1);

    const QJsonArray logs = response.value(QStringLiteral("logs")).toArray();
    if (logs.isEmpty()) {
        qWarning("[SCORE_DETAIL_ERROR] reason=no-detail row=%d examId=%d studentId=%d", row, examId, studentId);
        QMessageBox::information(this,
                                 QStringLiteral("当前记录暂无详情"),
                                 QStringLiteral("当前记录暂无异常详情数据。"));
        return;
    }
    qInfo("[SCORE_DETAIL_SUCCESS] row=%d examId=%d studentId=%d count=%d", row, examId, studentId, logs.size());
    for (const QJsonValue &value : logs) {
        const QJsonObject log = value.toObject();
        const QString type = log.value(QStringLiteral("abnormalType")).toString();
        const int logRow = table->rowCount();
        table->insertRow(logRow);
        const QStringList values = {
            localTimeText(log.value(QStringLiteral("createTime")).toString()),
            type.isEmpty() ? QStringLiteral("-") : type,
            abnormalTypeText(type),
            QStringLiteral("+%1").arg(log.value(QStringLiteral("riskScore")).toInt()),
            log.value(QStringLiteral("description")).toString()
        };
        const QColor color = abnormalCategoryColor(type);
        for (int col = 0; col < values.size(); ++col) {
            auto *item = readOnlyItem(values.at(col));
            if (col == 2) {
                item->setForeground(QBrush(color));
            }
            table->setItem(logRow, col, item);
        }
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::showError(const QString &title, const QString &error)
{
    const QString message = QStringLiteral("%1：%2").arg(title, error);
    statusBar()->showMessage(message, 6000);
    showToast(message, false);
}

void MainWindow::showToast(const QString &message, bool success)
{
    if (message.trimmed().isEmpty()) {
        return;
    }

    statusBar()->showMessage(message, 4500);
    auto *toast = new QLabel(message, this);
    toast->setObjectName(success ? QStringLiteral("toastSuccess") : QStringLiteral("toastError"));
    toast->setAttribute(Qt::WA_DeleteOnClose);
    toast->setWordWrap(true);
    toast->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    toast->setMargin(12);
    toast->adjustSize();

    const int width = qBound(260, toast->sizeHint().width() + 28, 480);
    const int height = qMax(48, toast->sizeHint().height() + 18);
    toast->setFixedSize(width, height);
    toast->move(qMax(24, this->width() - width - 32), qMax(24, this->height() - height - 52));
    toast->raise();
    toast->show();

    QTimer::singleShot(3600, toast, &QLabel::close);
}

void MainWindow::setStyle()
{
    QFile file(QStringLiteral(":/styles/app.qss"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
        return;
    }

    qApp->setStyleSheet(QStringLiteral(R"(
        QWidget, QMainWindow, QDialog {
            font-family: "Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI", Arial;
            font-size: 13px;
            color: #17201f;
            background: #f4f7f8;
        }
        QWidget#appRoot, QWidget#mainArea {
            background: #f4f7f8;
        }
        #sidebar {
            background: #152321;
            color: #edf7f5;
        }
        #brand {
            color: #ffffff;
            font-size: 18px;
            font-weight: 800;
            padding: 8px 4px;
            background: transparent;
        }
        QLabel#brandLogo {
            border-radius: 8px;
            border: 1px solid #d7ebe5;
            background: #ffffff;
        }
        #brand span, #accountBox span, #serverCard span {
            color: #9fb8b2;
            font-size: 12px;
            font-weight: 500;
        }
        #accountBox, #serverCard {
            padding: 14px;
            border-radius: 8px;
            color: #ffffff;
            border: 1px solid #2a4742;
            background: #203632;
        }
        QPushButton#navButton {
            border: none;
            border-radius: 8px;
            padding: 0 14px;
            color: #c8d7d3;
            text-align: left;
            background: transparent;
            font-size: 14px;
            font-weight: 700;
        }
        QPushButton#navButton:hover {
            color: #ffffff;
            background: #243f3a;
        }
        QPushButton#navButton:checked {
            color: #ffffff;
            background: #0f766e;
        }
        #eyebrow {
            color: #60746f;
            font-size: 12px;
            font-weight: 600;
            background: transparent;
        }
        #pageTitle {
            font-size: 24px;
            font-weight: 800;
            color: #111c1a;
            background: transparent;
        }
        #currentRoleBadge {
            border-radius: 8px;
            padding: 8px 12px;
            color: #134e4a;
            background: #d7f2ec;
            border: 1px solid #9bd8cc;
        }
        QFrame#card, QFrame#paperCard, QFrame#sidePanelCard, QFrame#gradingPaperCard, QFrame#activityCard {
            border: 1px solid #d9e3df;
            border-radius: 8px;
            background: #ffffff;
        }
        QFrame#dashboardHero {
            border: 1px solid #cddbd6;
            border-radius: 8px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #edf8f5);
        }
        QFrame#metricCard {
            border: 1px solid #d9e3df;
            border-radius: 8px;
            background: #ffffff;
        }
        QLabel#metricMark {
            border-radius: 6px;
            background: #d7f2ec;
        }
        QLabel#metricMark[accent="cyan"] {
            background: #dbeafe;
        }
        QLabel#metricMark[accent="amber"] {
            background: #fef3c7;
        }
        QLabel#metricMark[accent="green"] {
            background: #dcfce7;
        }
        QLabel#metricValue {
            color: #111c1a;
            font-size: 24px;
            font-weight: 800;
            background: transparent;
        }
        QLabel#metricTitle {
            color: #60746f;
            font-size: 13px;
            font-weight: 600;
            background: transparent;
        }
        #sectionTitle, #sectionTitle h3, #sectionTitle p, #mutedLabel {
            background: transparent;
        }
        #sectionTitle h3 {
            margin: 0;
            font-size: 17px;
            font-weight: 800;
            color: #17201f;
        }
        #sectionTitle p, #mutedLabel {
            color: #66766f;
            font-size: 13px;
            line-height: 150%;
        }
        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QSpinBox, QDateTimeEdit {
            min-height: 38px;
            border: 1px solid #cddbd6;
            border-radius: 8px;
            padding: 6px 10px;
            background: #ffffff;
            selection-background-color: #0f766e;
            selection-color: #ffffff;
            font-size: 13px;
        }
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus, QDateTimeEdit:focus {
            border: 1px solid #0f766e;
            background: #fbfffd;
        }
        QTextEdit#answerEdit {
            font-size: 14px;
            line-height: 150%;
        }
        QTextEdit#codeAnswerEdit {
            border: 1px solid #bae3dc;
            background: #f7fbfa;
            color: #17201f;
            font-family: "Cascadia Mono", "Consolas", monospace;
            font-size: 13px;
        }
        QTableWidget, QListWidget {
            border: 1px solid #d9e3df;
            border-radius: 8px;
            background: #ffffff;
            alternate-background-color: #f7faf9;
            gridline-color: #e5eeeb;
            selection-background-color: #d7f2ec;
            selection-color: #17201f;
        }
        QTableWidget::item {
            padding: 8px 7px;
            font-size: 13px;
        }
        QListWidget::item {
            min-height: 44px;
            padding: 9px 10px;
            border-radius: 8px;
            margin: 3px;
            font-size: 13px;
        }
        QListWidget::item:selected {
            color: #134e4a;
            background: #d7f2ec;
        }
        QListWidget::item:hover {
            background: #eef8f5;
        }
        QHeaderView::section {
            border: none;
            border-bottom: 1px solid #d9e3df;
            padding: 9px;
            background: #eef5f3;
            font-size: 13px;
            font-weight: 700;
            color: #33423f;
        }
        QPushButton {
            min-height: 38px;
            border: 1px solid #cddbd6;
            border-radius: 8px;
            padding: 6px 15px;
            background: #ffffff;
            color: #33423f;
            font-size: 13px;
            font-weight: 700;
        }
        QPushButton:hover {
            background: #f2f7f5;
            border-color: #93ada6;
        }
        QPushButton:pressed {
            background: #e2ebe8;
        }
        QPushButton:disabled {
            color: #93a19d;
            background: #eef2f1;
            border-color: #dfe7e4;
        }
        QPushButton#primaryButton {
            color: #ffffff;
            border-color: #0f766e;
            background: #0f766e;
        }
        QPushButton#primaryButton:hover {
            background: #0b625b;
            border-color: #0b625b;
        }
        QPushButton#quietButton {
            color: #24524d;
            border-color: #b9d3cc;
            background: #f7fbfa;
        }
        QPushButton#quietButton:hover {
            border-color: #0f766e;
            background: #edf8f5;
        }
        QPushButton#dangerOutlineButton {
            color: #9f1239;
            border-color: #fecdd3;
            background: #fff7f8;
        }
        QPushButton#dangerOutlineButton:hover {
            color: #ffffff;
            border-color: #be123c;
            background: #be123c;
        }
        QPushButton#dangerButton {
            color: #ffffff;
            border-color: #be123c;
            background: #be123c;
        }
        QPushButton#dangerButton:hover {
            background: #9f1239;
            border-color: #9f1239;
        }
        QPushButton#primaryBigButton, QPushButton#successBigButton {
            min-height: 46px;
            min-width: 150px;
            border-radius: 8px;
            padding: 8px 18px;
            color: #ffffff;
            font-size: 15px;
            font-weight: 800;
        }
        QPushButton#primaryBigButton {
            border-color: #0f766e;
            background: #0f766e;
        }
        QPushButton#primaryBigButton:hover {
            background: #0b625b;
            border-color: #0b625b;
        }
        QPushButton#successBigButton {
            border-color: #15803d;
            background: #15803d;
        }
        QPushButton#successBigButton:hover {
            background: #166534;
            border-color: #166534;
        }
        QPushButton#primaryBigButton:disabled, QPushButton#successBigButton:disabled {
            color: #93a19d;
            border-color: #dfe7e4;
            background: #eef2f1;
        }
        QScrollArea#questionScroll, QScrollArea#gradingQuestionScroll {
            border: none;
            background: transparent;
        }
        QWidget#answerSheet {
            border: 1px solid #d9e3df;
            border-radius: 8px;
            background: #f7faf9;
        }
        QPushButton#answerNavButton {
            min-width: 38px;
            max-width: 38px;
            min-height: 34px;
            max-height: 34px;
            padding: 0;
            border-radius: 8px;
            color: #52635f;
            border: 1px solid #cddbd6;
            background: #ffffff;
        }
        QPushButton#answerNavButton[answerState="answered"] {
            color: #ffffff;
            border-color: #0f766e;
            background: #0f766e;
        }
        QPushButton#answerNavButton[current="true"] {
            color: #ffffff;
            border-color: #b45309;
            background: #b45309;
            font-weight: 800;
        }
        QPushButton#answerNavButton:hover {
            border-color: #0f766e;
        }
        QLabel#countdownBadge {
            border-radius: 8px;
            padding: 9px 12px;
            color: #134e4a;
            border: 1px solid #9bd8cc;
            background: #d7f2ec;
            font-size: 14px;
            font-weight: 800;
        }
        QLabel#countdownBadge[urgent="true"] {
            color: #9f1239;
            border-color: #fecdd3;
            background: #ffe4e8;
        }
        QLabel#lockBadge {
            border-radius: 8px;
            padding: 9px 12px;
            color: #33423f;
            border: 1px solid #cddbd6;
            background: #f7faf9;
            font-weight: 600;
        }
        QLabel#lockBadge[status="pending"] {
            color: #134e4a;
            border-color: #9bd8cc;
            background: #d7f2ec;
        }
        QLabel#lockBadge[status="grading"] {
            color: #92400e;
            border-color: #fde68a;
            background: #fef3c7;
        }
        QLabel#lockBadge[status="graded"] {
            color: #065f46;
            border-color: #a7f3d0;
            background: #d1fae5;
        }
        QFrame#riskPanel {
            border: 1px solid #b9d3cc;
            border-radius: 8px;
            background: #edf8f5;
        }
        QLabel#riskStatusLabel, QLabel#riskLevelLabel {
            color: #134e4a;
            font-weight: 700;
            background: transparent;
        }
        QLabel#aiProctorStatusLabel {
            color: #134e4a;
            font-weight: 700;
            background: transparent;
        }
        QLabel#aiProctorStatusLabel[state="warning"] {
            color: #92400e;
        }
        QLabel#aiProctorStatusLabel[state="error"] {
            color: #be123c;
        }
        QLabel#facePreview {
            border: 1px solid #cddbd6;
            border-radius: 8px;
            background: #111c1a;
            color: #e6f2ef;
            font-size: 16px;
            font-weight: 700;
        }
        QLabel#riskStatusLabel[riskLevel="中风险"], QLabel#riskLevelLabel[riskLevel="中风险"] {
            color: #92400e;
        }
        QLabel#riskStatusLabel[riskLevel="高风险"], QLabel#riskLevelLabel[riskLevel="高风险"] {
            color: #be123c;
        }
        QLabel#riskStatusLabel[riskLevel="严重风险"], QLabel#riskLevelLabel[riskLevel="严重风险"] {
            color: #881337;
        }
        QFrame#paperQuestion {
            border: 1px solid #d9e3df;
            border-radius: 8px;
            background: #ffffff;
        }
        QLabel#paperQuestionTitle {
            color: #17201f;
            font-size: 16px;
            font-weight: 800;
            background: transparent;
        }
        QGroupBox#questionBox, QGroupBox {
            border: 1px solid #d9e3df;
            border-radius: 8px;
            margin-top: 12px;
            padding: 14px;
            background: #ffffff;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 8px;
            color: #0f766e;
            background: #ffffff;
            font-weight: 700;
        }
        QLabel#questionContent {
            color: #17201f;
            font-size: 14px;
            line-height: 150%;
            background: transparent;
        }
        QLabel#optionText {
            border: 1px solid #d9e3df;
            border-radius: 8px;
            padding: 10px;
            color: #33423f;
            background: #f7faf9;
        }
        QWidget#choiceHost {
            background: transparent;
        }
        QRadioButton#choiceButton, QCheckBox#choiceButton {
            min-height: 40px;
            spacing: 10px;
            padding: 8px 10px;
            color: #17201f;
            background: #f7faf9;
            border: 1px solid #d9e3df;
            border-radius: 8px;
            font-size: 14px;
        }
        QRadioButton#choiceButton:hover, QCheckBox#choiceButton:hover {
            border-color: #9bd8cc;
            background: #edf8f5;
        }
        QRadioButton#choiceButton:checked, QCheckBox#choiceButton:checked {
            border-color: #0f766e;
            background: #d7f2ec;
            color: #134e4a;
            font-weight: 700;
        }
        QLabel#choiceHint {
            color: #66766f;
            background: transparent;
        }
        QFrame#gradingQuestionCard {
            border: 1px solid #d9e3df;
            border-radius: 8px;
            background: #ffffff;
        }
        QFrame#gradeActionPanel {
            border: 1px solid #d9e3df;
            border-radius: 8px;
            background: #f7faf9;
        }
        QFrame#reviewActionPanel, QFrame#studentActionPanel {
            border: 1px solid #b9d3cc;
            border-radius: 8px;
            background: #edf8f5;
        }
        QLabel#reviewTip {
            color: #134e4a;
            font-weight: 700;
            background: transparent;
        }
        QLabel#gradingQuestionTitle {
            color: #17201f;
            font-size: 16px;
            font-weight: 700;
            background: transparent;
        }
        QLabel#scoreBadge {
            color: #134e4a;
            border: 1px solid #9bd8cc;
            border-radius: 8px;
            padding: 6px 10px;
            background: #d7f2ec;
            font-weight: 700;
        }
        QLabel#fieldLabel {
            color: #52635f;
            font-weight: 700;
            background: transparent;
        }
        QTextEdit#studentAnswerViewer {
            border: 1px solid #cddbd6;
            border-radius: 8px;
            background: #f7faf9;
            color: #17201f;
        }
        QTextEdit#referenceAnswerViewer {
            border: 1px solid #bbf7d0;
            border-radius: 8px;
            background: #f0fdf4;
            color: #14532d;
        }
        QTextEdit#gradeCommentEdit {
            border: 1px solid #cddbd6;
            border-radius: 8px;
            background: #ffffff;
        }
        QLineEdit#gradeScoreEdit {
            max-width: 110px;
            font-size: 16px;
            font-weight: 800;
            color: #134e4a;
            border: 1px solid #9bd8cc;
            background: #d7f2ec;
        }
        QLineEdit#gradeScoreEdit[invalid="true"] {
            color: #9f1239;
            border-color: #f43f5e;
            background: #fff1f2;
        }
        QDialog#importPreviewDialog {
            background: #f4f7f8;
        }
        QLabel#warningText {
            color: #92400e;
            background: #fffbeb;
            border: 1px solid #fde68a;
            border-radius: 8px;
            padding: 10px;
        }
        QTableWidget#previewTable {
            background: #ffffff;
        }
        QLabel#toastSuccess, QLabel#toastError {
            border-radius: 8px;
            padding: 0 4px;
            font-weight: 600;
        }
        QLabel#toastSuccess {
            color: #065f46;
            border: 1px solid #a7f3d0;
            background: #ecfdf5;
        }
        QLabel#toastError {
            color: #9f1239;
            border: 1px solid #fecdd3;
            background: #fff1f2;
        }
        QStatusBar {
            color: #60746f;
            background: #ffffff;
            border-top: 1px solid #d9e3df;
        }
        QMenuBar, QMenu {
            background: #ffffff;
            color: #17201f;
        }
        QMenu::item:selected {
            background: #d7f2ec;
        }
        QScrollBar:vertical {
            width: 12px;
            background: #eef2f1;
        }
        QScrollBar::handle:vertical {
            min-height: 28px;
            border-radius: 6px;
            background: #b9c9c4;
        }
        QScrollBar::handle:vertical:hover {
            background: #93ada6;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
    )"));
}
