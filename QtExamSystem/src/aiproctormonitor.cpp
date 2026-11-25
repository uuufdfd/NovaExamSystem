#include "aiproctormonitor.h"

#include <QBuffer>
#include <QByteArray>
#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QMediaDevices>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#include <algorithm>
#include <deque>

#if defined(Q_OS_LINUX) && defined(QTEXAM_USE_OPENCV_CAMERA)
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace {

struct ProctorConfig {
    int detectIntervalFrames = 5;
    int recognizeIntervalFrames = 10;
    int resizeDetectWidth = 320;
    int maxFrameQueueSize = 1;
    int displayIntervalMs = 100;
    int missingFrameLimit = 30;
    int stableRecognizeCount = 2;
    int abnormalConfirmSeconds = 8;
    int jpegQuality = 45;
    double minFaceScore = 0.82;
    double matchThreshold = 0.36;
};

struct PreviewFace {
    QRect rect;
    double score = 0;
};

struct RecognitionSample {
    bool matched = false;
    double confidence = 0;
};

const ProctorConfig kConfig;
const QString kTemplatePrefix = QStringLiteral("SFACE1:");

QString noCameraDeviceMessage()
{
    return QStringLiteral(
        "未检测到摄像头设备（/dev/video* 不存在）。"
        "请在 VMware/VirtualBox 设置中将 Win11 主机摄像头连接到麒麟虚拟机，"
        "连接后重新点击「启动摄像头监考」。"
        "若无法连接摄像头，请点击「模拟监考模式」按钮进行答辩演示。"
        "（也可在 Windows 主机运行 tools/windows_camera_server.py 后设置环境变量 "
        "QTEXAM_CAMERA_URL=http://主机IP:8765/camera.mjpg 走 Windows 摄像头桥接。）"
    );
}

// Windows Host Bridge：设置环境变量后客户端不再依赖本地 /dev/video*，而是把这个
// URL 交给 camera_capture_bridge.py 通过 OpenCV/HTTP 取帧。优先读 QTEXAM_CAMERA_URL
// （新方案，指向 windows_camera_server.py 的 .../camera.mjpg），其次兼容旧的
// QTEXAM_CAMERA_SOURCE（指向 win_camera_proxy.py 的 .../snapshot.jpg）。
// 未设置（或不是 http/https）时返回空串，保持原有 /dev/video0 回退逻辑。
QString cameraSourceOverride()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &name : {QStringLiteral("QTEXAM_CAMERA_URL"),
                                QStringLiteral("QTEXAM_CAMERA_SOURCE")}) {
        const QString src = env.value(name).trimmed();
        if (src.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
            || src.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
            return src;
        }
    }
    return QString();
}

QString cameraProxyUnavailableMessage()
{
    return QStringLiteral("Windows 摄像头代理不可用，请检查 http://127.0.0.1:8765/health");
}

int videoDeviceIndex(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    if (!name.startsWith(QStringLiteral("video"))) {
        return 9999;
    }

    bool ok = false;
    const int index = name.mid(5).toInt(&ok);
    return ok ? index : 9999;
}

QStringList existingVideoDevices()
{
    QStringList devices;
#ifdef Q_OS_LINUX
    // Primary: enumerate /dev for video[0-9]* nodes. QDir::System is required for
    // character-device nodes to show up at all.
    const auto entries = QDir(QStringLiteral("/dev")).entryInfoList(QStringList{QStringLiteral("video[0-9]*")},
                                                                     QDir::System | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (videoDeviceIndex(entry.fileName()) != 9999) {
            devices << entry.absoluteFilePath();
        }
    }
    // Fallback: some Qt builds do not surface character devices through the type
    // filter above, which would make us wrongly report "no camera" even though
    // /dev/video0 exists. Probe the common node names directly so detection does
    // not depend on QDir filter subtleties.
    for (int i = 0; i < 16; ++i) {
        const QString path = QStringLiteral("/dev/video%1").arg(i);
        if (!devices.contains(path) && QFileInfo::exists(path)) {
            devices << path;
        }
    }
    std::sort(devices.begin(), devices.end(), [](const QString &left, const QString &right) {
        return videoDeviceIndex(left) < videoDeviceIndex(right);
    });
    qInfo().noquote() << QStringLiteral("[CameraDiag] /dev/video* detected: %1")
                             .arg(devices.isEmpty() ? QStringLiteral("none") : devices.join(QLatin1Char(' ')));
#endif
    return devices;
}

QString pythonProgram()
{
    const QString configuredPython = QProcessEnvironment::systemEnvironment().value(QStringLiteral("QTEXAM_PYTHON")).trimmed();
    if (!configuredPython.isEmpty() && QFileInfo::exists(configuredPython)) {
        return configuredPython;
    }

    // Fallback: auto-discover the project's .venv interpreter so the camera and
    // face-recognition bridges use the OpenCV-equipped virtualenv even when the
    // app is launched directly (Qt Creator / desktop shortcut) without the
    // run_client_kylin.sh wrapper that normally exports QTEXAM_PYTHON.
#ifdef Q_OS_WIN
    const QStringList venvRelatives = {QStringLiteral(".venv/Scripts/python.exe"),
                                       QStringLiteral("venv/Scripts/python.exe")};
#else
    const QStringList venvRelatives = {QStringLiteral(".venv/bin/python"),
                                       QStringLiteral("venv/bin/python")};
#endif
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList venvBases = {
        appDir,
        QDir(appDir).filePath(QStringLiteral("..")),
        QDir(appDir).filePath(QStringLiteral("../..")),
        QDir(appDir).filePath(QStringLiteral("../../..")),
        QDir::currentPath()
    };
    for (const QString &base : venvBases) {
        for (const QString &venvRelative : venvRelatives) {
            const QString venvPython = QDir(base).filePath(venvRelative);
            if (QFileInfo::exists(venvPython)) {
                return QFileInfo(venvPython).absoluteFilePath();
            }
        }
    }

    QStringList candidates;
#ifdef Q_OS_WIN
    candidates = {
        QStringLiteral("python"),
        QStringLiteral("python3"),
        QStringLiteral("py")
    };
#else
    candidates = {
        QStringLiteral("python3"),
        QStringLiteral("python")
    };
#endif
    for (const QString &candidate : candidates) {
        const QString executable = QStandardPaths::findExecutable(candidate);
        if (!executable.isEmpty()) {
            return executable;
        }
    }
    return QStringLiteral("python");
}

QCameraFormat chooseCameraFormat(const QCameraDevice &device)
{
    QCameraFormat best;
    int bestScore = -1;
    for (const QCameraFormat &format : device.videoFormats()) {
        const QSize size = format.resolution();
        if (!size.isValid()) {
            continue;
        }
        const int width = size.width();
        const int height = size.height();
        if ((width != 640 || height != 480) && (width != 800 || height != 600)) {
            continue;
        }
        const int fps = qRound(format.maxFrameRate());
        const int fpsScore = qMin(fps, 30);
        const int sizeScore = width == 640 ? 20 : 10;
        const int score = fpsScore + sizeScore;
        if (score > bestScore) {
            bestScore = score;
            best = format;
        }
    }
    return best;
}

QRect scaledRect(const QRect &rect, double sx, double sy)
{
    return QRect(qRound(rect.x() * sx),
                 qRound(rect.y() * sy),
                 qRound(rect.width() * sx),
                 qRound(rect.height() * sy));
}

QRect smoothedRect(const QRect &previous, const QRect &current)
{
    if (!previous.isValid()) {
        return current;
    }
    constexpr double oldWeight = 0.68;
    constexpr double newWeight = 1.0 - oldWeight;
    return QRect(qRound(previous.x() * oldWeight + current.x() * newWeight),
                 qRound(previous.y() * oldWeight + current.y() * newWeight),
                 qRound(previous.width() * oldWeight + current.width() * newWeight),
                 qRound(previous.height() * oldWeight + current.height() * newWeight));
}

QImage drawPreview(const QImage &image,
                   const QVector<PreviewFace> &faces,
                   const QString &text,
                   const QString &state,
                   const QString &statsText)
{
    QImage preview = image.scaled(760, 430, Qt::KeepAspectRatio, Qt::FastTransformation)
                         .convertToFormat(QImage::Format_RGB32);
    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing);

    const double sx = static_cast<double>(preview.width()) / qMax(1, image.width());
    const double sy = static_cast<double>(preview.height()) / qMax(1, image.height());
    const QColor color = state == QStringLiteral("error")
                             ? QColor(QStringLiteral("#be123c"))
                             : (state == QStringLiteral("warning")
                                    ? QColor(QStringLiteral("#b45309"))
                                    : QColor(QStringLiteral("#0f766e")));
    painter.setPen(QPen(color, 3));
    for (const PreviewFace &face : faces) {
        const QRect rect = scaledRect(face.rect, sx, sy);
        painter.drawRoundedRect(rect, 8, 8);
        if (face.score > 0) {
            const QRect badge(rect.left(), qMax(0, rect.top() - 24), 82, 22);
            painter.fillRect(badge, QColor(17, 28, 26, 190));
            painter.setPen(Qt::white);
            painter.drawText(badge.adjusted(6, 0, 0, 0),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             QString::number(face.score, 'f', 2));
            painter.setPen(QPen(color, 3));
        }
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(17, 28, 26, 190));
    painter.drawRoundedRect(QRect(12, 12, preview.width() - 24, 52), 8, 8);
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(11);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(24, 14, preview.width() - 48, 24),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     text);
    font.setPointSize(9);
    font.setBold(false);
    painter.setFont(font);
    painter.setPen(QColor(QStringLiteral("#cfe7e2")));
    painter.drawText(QRect(24, 38, preview.width() - 48, 20),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     statsText);
    return preview;
}

QString cameraDescription(const QCameraDevice &device)
{
    const QString id = QString::fromUtf8(device.id());
    const QString description = device.description().trimmed();
    if (id.isEmpty()) {
        return description.isEmpty() ? QStringLiteral("unknown camera") : description;
    }
    if (description.isEmpty()) {
        return id;
    }
    return QStringLiteral("%1 (%2)").arg(description, id);
}

int preferredCameraIndex(const QList<QCameraDevice> &cameras, int requestedIndex)
{
    for (int i = 0; i < cameras.size(); ++i) {
        const QString id = QString::fromUtf8(cameras.at(i).id()).toLower();
        const QString description = cameras.at(i).description().toLower();
        if (id.contains(QStringLiteral("video0")) || description.contains(QStringLiteral("video0"))) {
            return i;
        }
    }
    return qBound(0, requestedIndex, cameras.size() - 1);
}

} // namespace

class FacePipelineWorker : public QObject
{
public:
    FacePipelineWorker(AiProctorMonitor *owner, QString scriptPath, QString baselineTemplate)
        : owner(owner)
        , scriptPath(std::move(scriptPath))
        , baselineTemplate(std::move(baselineTemplate))
        , baselineReady(this->baselineTemplate.startsWith(kTemplatePrefix))
    {
        faceSeenTimer.invalidate();
        recognizedTimer.invalidate();
        requestTimer.invalidate();
        statsTimerClock.invalidate();
    }

    void start()
    {
        qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_WORKER_START] worker=0x%1 baselineReady=%2 enrollMode=%3")
                                 .arg(reinterpret_cast<quintptr>(this), 0, 16).arg(baselineReady).arg(!baselineReady);
        startBridge();

        displayTimer = new QTimer(this);
        displayTimer->setTimerType(Qt::PreciseTimer);
        displayTimer->setInterval(kConfig.displayIntervalMs);
        // 麒麟上 &QTimer::timeout 这类 Qt 库类 PMF 信号 connect 会失败（signal not found）；保留作非麒麟
        // 冗余通路，真正可靠驱动是下方 scheduleAnalysisTick() 的 singleShot 自调度链（displayTick 有按帧
        // 去重，两路同时驱动也只处理一次）。本类无 Q_OBJECT，legacy SIGNAL/SLOT 不可用，故无 legacy 兜底。
        const QMetaObject::Connection displayConn =
            connect(displayTimer, &QTimer::timeout, displayTimer, [this] { displayTick(); });
        qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_TIMER_CONNECT] timer=display pmf=%1 legacy=NA-no-QOBJECT fallback=singleShot worker=0x%2")
                                 .arg(static_cast<bool>(displayConn)).arg(reinterpret_cast<quintptr>(this), 0, 16);
        displayTimer->start();

        statsTimer = new QTimer(this);
        statsTimer->setInterval(1000);
        const QMetaObject::Connection statsConn =
            connect(statsTimer, &QTimer::timeout, statsTimer, [this] { publishStats(); });
        qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_TIMER_CONNECT] timer=stats pmf=%1 legacy=NA-no-QOBJECT worker=0x%2")
                                 .arg(static_cast<bool>(statsConn)).arg(reinterpret_cast<quintptr>(this), 0, 16);
        statsTimer->start();
        statsTimerClock.start();

        // 兜底主驱动：singleShot 自调度链，不解析任何 Qt 库类信号指针，麒麟上可靠。
        analysisLoopRunning = true;
        scheduleAnalysisTick();
    }

    void stop()
    {
        analysisLoopRunning = false;
        if (displayTimer) {
            displayTimer->stop();
            displayTimer->deleteLater();
            displayTimer = nullptr;
        }
        if (statsTimer) {
            statsTimer->stop();
            statsTimer->deleteLater();
            statsTimer = nullptr;
        }
        stopBridge();
    }

    // 麒麟兜底主驱动：singleShot 自调度链（不阻塞 UI、无 while 死循环、日志限频）。
    // 每 tick：① displayTick() 消费最新帧并按帧间隔触发 sendAnalysis()；
    //          ② 主动轮询 faceProcess 的 stdout/stderr（readyRead 的 PMF connect 在麒麟失效）。
    // 与 AiProctorMonitor::scheduleDrainTick() 同思路，差别是这里还驱动分析触发。
    void scheduleAnalysisTick()
    {
        if (!analysisLoopRunning) {
            return;
        }
        ++analysisTickCount;
        if (analysisTickCount <= 3 || analysisTickCount % 10 == 0) {
            qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_TIMER_TICK] fallback=singleShot count=%1 frameSerial=%2 analysisPending=%3 worker=0x%4")
                                     .arg(analysisTickCount).arg(latestFrameSerial).arg(analysisPending)
                                     .arg(reinterpret_cast<quintptr>(this), 0, 16);
        }

        // 帧停滞检测：serial 长时间不前进 = 没有帧进 worker（限频告警，便于区分“没帧”与“没分析”）。
        if (latestFrameSerial == lastSeenFrameSerial) {
            if (noFrameLogCount < 3 || noFrameLogCount % 40 == 0) {
                qWarning().noquote() << QStringLiteral("[FACE_PIPELINE_ERROR] reason=no-frame frameSerial=%1 tick=%2")
                                            .arg(latestFrameSerial).arg(analysisTickCount);
            }
            noFrameLogCount++;
        } else {
            lastSeenFrameSerial = latestFrameSerial;
            noFrameLogCount = 0;
        }

        displayTick();         // 消费 latestImage → 按帧间隔触发 sendAnalysis()
        handleBridgeOutput();  // 主动排水 stdout（麒麟下 readyReadStandardOutput 不触发）
        handleBridgeError();   // 主动排水 stderr

        QTimer::singleShot(kConfig.displayIntervalMs, this, [this] { scheduleAnalysisTick(); });
    }

    void enqueueFrame(const QVideoFrame &frame)
    {
        if (!frame.isValid()) {
            return;
        }
        enqueueImage(frame.toImage());
    }

    void enqueueImage(const QImage &image)
    {
        if (image.isNull()) {
            return;
        }
        latestImage = image.convertToFormat(QImage::Format_RGB32);
        latestFrameSerial++;
        cameraFrames++;
        // 证明预览帧是否真的送进 worker（限频：前 3 次 + 每 30 帧）。
        if (enqueueLogCount < 3 || enqueueLogCount % 30 == 0) {
            qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_ENQUEUE] size=%1x%2 serial=%3 worker=0x%4")
                                     .arg(latestImage.width()).arg(latestImage.height()).arg(latestFrameSerial)
                                     .arg(reinterpret_cast<quintptr>(this), 0, 16);
        }
        enqueueLogCount++;
        Q_UNUSED(kConfig.maxFrameQueueSize)
    }

    QString currentTemplate() const
    {
        return lastTemplate;
    }

    bool hasCurrentFace() const
    {
        return !lastTemplate.isEmpty();
    }

    // 麒麟主路径自发预览(handleCameraOutput→previewReady)，此时关掉 worker 预览，避免双发闪烁；
    // Windows QCamera 路径不自发预览，worker 是唯一预览源，保持默认 true。
    void setEmitPreviewFrames(bool on)
    {
        emitPreviewFrames.store(on);
    }

private:
    void startBridge()
    {
        if (faceProcess && faceProcess->state() != QProcess::NotRunning) {
            return;
        }
        faceProcess = new QProcess(this);
        faceProcess->setProgram(pythonProgram());
        QStringList args;
        if (QFileInfo(faceProcess->program()).fileName().compare(QStringLiteral("py.exe"), Qt::CaseInsensitive) == 0
            || QFileInfo(faceProcess->program()).fileName().compare(QStringLiteral("py"), Qt::CaseInsensitive) == 0) {
            args << QStringLiteral("-3");
        }
        args << QStringLiteral("-X") << QStringLiteral("utf8");
        args << scriptPath;
        faceProcess->setArguments(args);
        faceProcess->setWorkingDirectory(QFileInfo(scriptPath).absolutePath());
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
        environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
        faceProcess->setProcessEnvironment(environment);
        qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_PROCESS_START] program=%1 script=%2 worker=0x%3")
                                 .arg(faceProcess->program(), scriptPath).arg(reinterpret_cast<quintptr>(this), 0, 16);
        // 麒麟上 &QProcess::readyRead* 这类 Qt 库类 PMF 信号 connect 会失败；保留作非麒麟冗余通路，
        // 真正排水靠 scheduleAnalysisTick() 每 tick 轮询 handleBridgeOutput()/handleBridgeError()。
        const QMetaObject::Connection outConn =
            connect(faceProcess, &QProcess::readyReadStandardOutput, faceProcess, [this] { handleBridgeOutput(); });
        const QMetaObject::Connection errConn =
            connect(faceProcess, &QProcess::readyReadStandardError, faceProcess, [this] { handleBridgeError(); });
        const QMetaObject::Connection finConn =
            connect(faceProcess,
                    qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                    faceProcess,
                    [this](int, QProcess::ExitStatus) {
                        analysisPending = false;
                        if (owner) {
                            emit owner->statusChanged(QStringLiteral("OpenCV人脸识别进程已停止"), QStringLiteral("error"));
                        }
                    });
        const QMetaObject::Connection errorConn =
            connect(faceProcess, &QProcess::errorOccurred, faceProcess, [this](QProcess::ProcessError e) {
                qWarning().noquote() << QStringLiteral("[FACE_PIPELINE_ERROR] reason=process-error code=%1 worker=0x%2")
                                            .arg(static_cast<int>(e)).arg(reinterpret_cast<quintptr>(this), 0, 16);
            });
        qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_PROCESS_CONNECT] signal=stdout pmf=%1 legacy=0 drain=poll").arg(static_cast<bool>(outConn));
        qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_PROCESS_CONNECT] signal=stderr pmf=%1 legacy=0 drain=poll").arg(static_cast<bool>(errConn));
        qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_PROCESS_CONNECT] signal=finished pmf=%1 legacy=0").arg(static_cast<bool>(finConn));
        qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_PROCESS_CONNECT] signal=errorOccurred pmf=%1 legacy=0").arg(static_cast<bool>(errorConn));
        faceProcess->start();
        if (!faceProcess->waitForStarted(3000) && owner) {
            emit owner->statusChanged(QStringLiteral("OpenCV人脸识别进程启动失败，请确认 Python 和 opencv-python 可用"),
                                      QStringLiteral("error"));
        }
    }

    void stopBridge()
    {
        analysisPending = false;
        processBuffer.clear();
        if (!faceProcess) {
            return;
        }
        disconnect(faceProcess, nullptr, nullptr, nullptr);
        if (faceProcess->state() != QProcess::NotRunning) {
            faceProcess->closeWriteChannel();
            faceProcess->terminate();
            if (!faceProcess->waitForFinished(900)) {
                faceProcess->kill();
                faceProcess->waitForFinished(700);
            }
        }
        faceProcess->deleteLater();
        faceProcess = nullptr;
    }

    void displayTick()
    {
        if (latestFrameSerial == displayedFrameSerial || latestImage.isNull()) {
            return;
        }

        const QImage image(latestImage);
        displayedFrameSerial = latestFrameSerial;
        if (image.isNull()) {
            return;
        }

        currentFrame = image.convertToFormat(QImage::Format_RGB32);
        displayedFrames++;
        displayFramesSinceFace++;
        frameIndex++;

        if (!analysisPending
            && (frameIndex % kConfig.detectIntervalFrames == 0
                || frameIndex % kConfig.recognizeIntervalFrames == 0)) {
            const bool recognize = !baselineReady || frameIndex % kConfig.recognizeIntervalFrames == 0;
            sendAnalysis(currentFrame, recognize);
        }

        QVector<PreviewFace> previewFaces = displayFramesSinceFace <= kConfig.missingFrameLimit ? lastFaces : QVector<PreviewFace>();
        // emitPreviewFrames=false（麒麟）：主路径已发预览，这里不重复发，避免与已修好的预览链路双发闪烁。
        if (emitPreviewFrames.load()) {
            emitPreview(drawPreview(currentFrame, previewFaces, stableText, stableState, statsText));
        }
    }

    void sendAnalysis(const QImage &frame, bool recognize)
    {
        if (!faceProcess || faceProcess->state() == QProcess::NotRunning) {
            startBridge();
            if (!faceProcess || faceProcess->state() == QProcess::NotRunning) {
                return;
            }
        }

        QImage analysisImage = frame;
        if (analysisImage.width() > kConfig.resizeDetectWidth) {
            analysisImage = analysisImage.scaledToWidth(kConfig.resizeDetectWidth, Qt::FastTransformation);
        }
        pendingScaleX = static_cast<double>(frame.width()) / qMax(1, analysisImage.width());
        pendingScaleY = static_cast<double>(frame.height()) / qMax(1, analysisImage.height());
        pendingRecognize = recognize;
        pendingRequestId++;
        requestTimer.restart();

        QByteArray imageBytes;
        QBuffer buffer(&imageBytes);
        buffer.open(QIODevice::WriteOnly);
        analysisImage.save(&buffer, "JPG", kConfig.jpegQuality);

        QJsonObject request;
        request.insert(QStringLiteral("requestId"), pendingRequestId);
        request.insert(QStringLiteral("image"), QString::fromLatin1(imageBytes.toBase64()));
        request.insert(QStringLiteral("baselineTemplate"), baselineTemplate);
        request.insert(QStringLiteral("enrollMode"), !baselineReady);
        request.insert(QStringLiteral("recognize"), recognize);
        request.insert(QStringLiteral("minFaceScore"), kConfig.minFaceScore);
        request.insert(QStringLiteral("matchThreshold"), kConfig.matchThreshold);

        if (stableFaceRect.isValid() && displayFramesSinceFace <= kConfig.missingFrameLimit) {
            const QRect roi = scaledRect(stableFaceRect, 1.0 / pendingScaleX, 1.0 / pendingScaleY)
                                  .intersected(QRect(QPoint(0, 0), analysisImage.size()));
            if (roi.isValid()) {
                QJsonObject roiObject;
                roiObject.insert(QStringLiteral("x"), roi.x());
                roiObject.insert(QStringLiteral("y"), roi.y());
                roiObject.insert(QStringLiteral("w"), roi.width());
                roiObject.insert(QStringLiteral("h"), roi.height());
                request.insert(QStringLiteral("roi"), roiObject);
            }
        }

        analysisPending = true;
        detectedRequests++;
        if (recognize) {
            recognizedRequests++;
        }
        // 检测请求发出埋点（限频：前 3 次 + 每 30 次）。enrollMode=无底库（采集）/有底库=核验。
        if (detectStartLogCount < 3 || detectStartLogCount % 30 == 0) {
            qInfo().noquote() << QStringLiteral("[FACE_DETECT_START] requestId=%1 recognize=%2 enrollMode=%3")
                                     .arg(pendingRequestId).arg(recognize).arg(!baselineReady);
            qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_REQUEST] requestId=%1 jpegBytes=%2 recognize=%3 worker=0x%4")
                                     .arg(pendingRequestId).arg(imageBytes.size()).arg(recognize)
                                     .arg(reinterpret_cast<quintptr>(this), 0, 16);
        }
        detectStartLogCount++;
        faceProcess->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    }

    void handleBridgeOutput()
    {
        if (!faceProcess) {
            return;
        }
        const QByteArray chunk = faceProcess->readAllStandardOutput();
        if (!chunk.isEmpty()) {
            processBuffer += chunk;
            // 证明 bridge 的 stdout 是否真的被读到（限频：前 3 次 + 每 20 次）。
            if (stdoutLogCount < 3 || stdoutLogCount % 20 == 0) {
                qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_PROCESS_STDOUT] bytes=%1 buffered=%2 worker=0x%3")
                                         .arg(chunk.size()).arg(processBuffer.size()).arg(reinterpret_cast<quintptr>(this), 0, 16);
            }
            stdoutLogCount++;
        }
        int newline = -1;
        while ((newline = processBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = processBuffer.left(newline).trimmed();
            processBuffer.remove(0, newline + 1);
            if (line.isEmpty()) {
                continue;
            }
            if (!line.startsWith('{')) {
                if (owner) {
                    emit owner->statusChanged(QStringLiteral("OpenCV人脸识别日志：%1")
                                                  .arg(QString::fromLocal8Bit(line).left(120)),
                                              QStringLiteral("warning"));
                }
                continue;
            }

            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                analysisPending = false;
                if (owner) {
                    emit owner->statusChanged(QStringLiteral("OpenCV人脸识别返回数据解析失败"), QStringLiteral("error"));
                }
                continue;
            }

            const QJsonObject result = document.object();
            if (result.value(QStringLiteral("ready")).toBool()) {
                if (owner) {
                    emit owner->statusChanged(result.value(QStringLiteral("message")).toString(QStringLiteral("OpenCV人脸识别模块已就绪")),
                                              QStringLiteral("normal"));
                }
                continue;
            }

            analysisPending = false;
            processResult(result);
        }
    }

    void handleBridgeError()
    {
        if (!faceProcess) {
            return;
        }
        const QString text = QString::fromLocal8Bit(faceProcess->readAllStandardError()).trimmed();
        if (text.isEmpty()) {
            return;
        }
        // 证明 bridge 的 stderr 是否被读到（限频：前 5 次 + 每 20 次）。
        if (stderrLogCount < 5 || stderrLogCount % 20 == 0) {
            qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_PROCESS_STDERR] %1").arg(text.left(160));
        }
        stderrLogCount++;
        if (owner) {
            emit owner->statusChanged(QStringLiteral("OpenCV人脸识别警告：%1").arg(text.left(120)), QStringLiteral("warning"));
        }
    }

    void processResult(const QJsonObject &result)
    {
        // 证明 bridge 响应进了 processResult（限频与下方 FACE_DETECT_RESULT 同步）。
        const bool pipelineLogThis = detectResultLogCount < 5 || detectResultLogCount % 15 == 0;
        if (pipelineLogThis) {
            qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_RESPONSE] worker=0x%1 bytes=%2")
                                     .arg(reinterpret_cast<quintptr>(this), 0, 16)
                                     .arg(QJsonDocument(result).toJson(QJsonDocument::Compact).size());
        }
        const qint64 latency = result.value(QStringLiteral("latencyMs")).toInt(requestTimer.isValid() ? static_cast<int>(requestTimer.elapsed()) : 0);
        latencyTotalMs += latency;
        latencySamples++;

        const bool ok = result.value(QStringLiteral("ok")).toBool();
        const int faceCount = ok ? result.value(QStringLiteral("faceCount")).toInt() : 0;
        const bool hasFace = ok && result.value(QStringLiteral("hasFace")).toBool();
        const bool didRecognize = result.value(QStringLiteral("didRecognize")).toBool();
        const bool recognized = ok && result.value(QStringLiteral("matched")).toBool();
        const double confidence = ok ? result.value(QStringLiteral("confidence")).toDouble() : 0.0;
        if (pipelineLogThis) {
            const QString respTemplate = result.value(QStringLiteral("template")).toString().trimmed();
            qInfo().noquote() << QStringLiteral("[FACE_PIPELINE_PROCESS_RESULT] ok=%1 hasFace=%2 faceCount=%3 enrollMode=%4 templateLen=%5 confidence=%6 threshold=%7")
                                     .arg(ok).arg(hasFace).arg(faceCount).arg(!baselineReady)
                                     .arg(respTemplate.size()).arg(confidence, 0, 'f', 3).arg(kConfig.matchThreshold, 0, 'f', 3);
        }

        QVector<PreviewFace> mappedFaces;
        for (const QJsonValue &value : result.value(QStringLiteral("faces")).toArray()) {
            const QJsonObject face = value.toObject();
            PreviewFace item;
            item.rect = scaledRect(QRect(face.value(QStringLiteral("x")).toInt(),
                                         face.value(QStringLiteral("y")).toInt(),
                                         face.value(QStringLiteral("w")).toInt(),
                                         face.value(QStringLiteral("h")).toInt()),
                                   pendingScaleX,
                                   pendingScaleY);
            item.score = face.value(QStringLiteral("score")).toDouble();
            if (item.rect.isValid()) {
                mappedFaces.append(item);
            }
        }

        double bestFaceScore = 0;
        for (const PreviewFace &mf : mappedFaces) {
            bestFaceScore = qMax(bestFaceScore, mf.score);
        }
        // 检测结论埋点（限频：脸数变化时 + 每 15 帧）。bestScore 对比 minFaceScore 看检出质量。
        if (faceCount != lastLoggedFaceCount || detectResultLogCount % 15 == 0) {
            qInfo().noquote() << QStringLiteral("[FACE_DETECT_RESULT] ok=%1 hasFace=%2 faceCount=%3 bestScore=%4 minFaceScore=%5 enrollMode=%6")
                                     .arg(ok).arg(hasFace).arg(faceCount)
                                     .arg(bestFaceScore, 0, 'f', 3).arg(kConfig.minFaceScore, 0, 'f', 2).arg(!baselineReady);
        }
        lastLoggedFaceCount = faceCount;
        detectResultLogCount++;

        if (hasFace && !mappedFaces.isEmpty()) {
            displayFramesSinceFace = 0;
            missingDetections = 0;
            faceSeenTimer.restart();
            stableFaceCount = faceCount;
            stableFaceRect = smoothedRect(stableFaceRect, mappedFaces.first().rect);
            mappedFaces.first().rect = stableFaceRect;
            lastFaces = mappedFaces;
        } else {
            missingDetections++;
            if (missingDetections * kConfig.detectIntervalFrames >= kConfig.missingFrameLimit) {
                lastFaces.clear();
                stableFaceRect = {};
            }
        }

        if (didRecognize && faceCount == 1) {
            RecognitionSample sample;
            sample.matched = recognized;
            sample.confidence = confidence;
            recognitionWindow.push_back(sample);
            while (recognitionWindow.size() > 10) {
                recognitionWindow.pop_front();
            }

            const QString frameTemplate = result.value(QStringLiteral("template")).toString().trimmed();
            const bool templateReady = !frameTemplate.isEmpty();
            if (templateReady) {
                lastTemplate = frameTemplate;
            }
            // 模板提取埋点（跳变记录，避免逐帧刷屏）：READY=本帧拿到非空特征向量；EMPTY=识别了但没给特征。
            if (templateReady && !templateReadyLogged) {
                qInfo().noquote() << QStringLiteral("[FACE_TEMPLATE_READY] len=%1 prefixOk=%2 enrollMode=%3")
                                         .arg(frameTemplate.size()).arg(frameTemplate.startsWith(kTemplatePrefix)).arg(!baselineReady);
                if (!baselineReady) {
                    qInfo().noquote() << QStringLiteral("[FACE_ENROLL_TEMPLATE_READY] len=%1 prefixOk=%2")
                                             .arg(frameTemplate.size()).arg(frameTemplate.startsWith(kTemplatePrefix));
                }
                templateReadyLogged = true;
            } else if (!templateReady) {
                templateReadyLogged = false;
                if (templateEmptyLogCount < 3 || templateEmptyLogCount % 30 == 0) {
                    qInfo().noquote() << QStringLiteral("[FACE_TEMPLATE_EMPTY] didRecognize=1 faceCount=1 enrollMode=%1").arg(!baselineReady);
                    if (!baselineReady) {
                        qInfo().noquote() << QStringLiteral("[FACE_ENROLL_TEMPLATE_EMPTY] didRecognize=1 faceCount=1");
                    }
                }
                templateEmptyLogCount++;
            }

            // 核验分数埋点（仅 verify 模式，限频：前 5 次 + 每 10 次）：score=本帧相似度，threshold=匹配阈值。
            if (baselineReady) {
                if (verifyScoreLogCount < 5 || verifyScoreLogCount % 10 == 0) {
                    qInfo().noquote() << QStringLiteral("[FACE_VERIFY_SCORE] score=%1 threshold=%2 matchedThisFrame=%3 stableMatched=%4")
                                             .arg(confidence, 0, 'f', 3).arg(kConfig.matchThreshold, 0, 'f', 3).arg(recognized).arg(stableMatched);
                }
                verifyScoreLogCount++;
            }

            const bool prevStableMatched = stableMatched;
            if (consecutiveMatches() >= kConfig.stableRecognizeCount) {
                stableMatched = true;
                recognizedTimer.restart();
            } else if (consecutiveMismatches() >= kConfig.stableRecognizeCount) {
                stableMatched = false;
            }
            // 稳定核验结论跳变埋点（仅 verify 模式）：PASS/FAIL 各只在状态翻转时记一次。
            if (baselineReady && stableMatched != prevStableMatched) {
                if (stableMatched) {
                    qInfo().noquote() << QStringLiteral("[FACE_VERIFY_PASS] avgScore=%1 threshold=%2")
                                             .arg(averageConfidence(), 0, 'f', 3).arg(kConfig.matchThreshold, 0, 'f', 3);
                } else {
                    qInfo().noquote() << QStringLiteral("[FACE_VERIFY_FAIL] avgScore=%1 threshold=%2")
                                             .arg(averageConfidence(), 0, 'f', 3).arg(kConfig.matchThreshold, 0, 'f', 3);
                }
            }
        }

        updateStableText(ok, hasFace, faceCount, confidence);
        publishStableResult();
    }

    int consecutiveMatches() const
    {
        int count = 0;
        for (auto it = recognitionWindow.rbegin(); it != recognitionWindow.rend(); ++it) {
            if (!it->matched) {
                break;
            }
            ++count;
        }
        return count;
    }

    int consecutiveMismatches() const
    {
        int count = 0;
        for (auto it = recognitionWindow.rbegin(); it != recognitionWindow.rend(); ++it) {
            if (it->matched) {
                break;
            }
            ++count;
        }
        return count;
    }

    double averageConfidence() const
    {
        if (recognitionWindow.empty()) {
            return 0;
        }
        double total = 0;
        for (const RecognitionSample &sample : recognitionWindow) {
            total += sample.confidence;
        }
        return total / static_cast<double>(recognitionWindow.size());
    }

    bool stableHasFace() const
    {
        return faceSeenTimer.isValid()
            && faceSeenTimer.elapsed() <= kConfig.abnormalConfirmSeconds * 1000;
    }

    void updateStableText(bool ok, bool hasFace, int faceCount, double confidence)
    {
        if (!ok) {
            stableText = QStringLiteral("OpenCV识别失败，正在等待下一帧");
            stableState = QStringLiteral("error");
            return;
        }
        if (!stableHasFace()) {
            stableText = QStringLiteral("未检测到稳定人脸");
            stableState = QStringLiteral("warning");
            return;
        }
        if (hasFace && faceCount > 1) {
            stableText = QStringLiteral("检测到多张人脸，请保持单人入镜");
            stableState = QStringLiteral("warning");
            return;
        }
        if (!baselineReady) {
            stableText = QStringLiteral("已检测到可信单人脸，可保存模板");
            stableState = QStringLiteral("normal");
            return;
        }
        if (stableMatched || (recognizedTimer.isValid() && recognizedTimer.elapsed() <= kConfig.abnormalConfirmSeconds * 1000)) {
            stableText = QStringLiteral("身份匹配稳定，相似度均值 %1").arg(averageConfidence(), 0, 'f', 2);
            stableState = QStringLiteral("normal");
            return;
        }
        stableText = QStringLiteral("身份尚未稳定通过，最近相似度 %1").arg(confidence, 0, 'f', 2);
        stableState = QStringLiteral("warning");
    }

    void publishStableResult()
    {
        if (!owner) {
            return;
        }
        const bool hasStableFace = stableHasFace();
        const int votedFaceCount = hasStableFace ? qMax(1, stableFaceCount) : 0;
        const bool votedMatched = baselineReady
            ? (stableMatched || (recognizedTimer.isValid() && recognizedTimer.elapsed() <= kConfig.abnormalConfirmSeconds * 1000))
            : hasStableFace;
        emit owner->faceResult(hasStableFace, votedFaceCount, votedMatched, averageConfidence());
    }

    void publishStats()
    {
        const double seconds = statsTimerClock.isValid() ? qMax(0.001, statsTimerClock.elapsed() / 1000.0) : 1.0;
        cameraFps = cameraFrames / seconds;
        displayFps = displayedFrames / seconds;
        detectFps = detectedRequests / seconds;
        recognizeFps = recognizedRequests / seconds;
        averageLatencyMs = latencySamples > 0 ? static_cast<double>(latencyTotalMs) / latencySamples : 0.0;
        statsText = QStringLiteral("camera %1 FPS · preview %2 FPS · detect %3 FPS · recognize %4 FPS · latency %5 ms")
                        .arg(cameraFps, 0, 'f', 1)
                        .arg(displayFps, 0, 'f', 1)
                        .arg(detectFps, 0, 'f', 1)
                        .arg(recognizeFps, 0, 'f', 1)
                        .arg(averageLatencyMs, 0, 'f', 0);

        if (owner) {
            emit owner->statusChanged(QStringLiteral("人脸识别：%1 | %2").arg(stableText, statsText), stableState);
        }
        qInfo().noquote() << QStringLiteral("[FacePerf] %1").arg(statsText);

        cameraFrames = 0;
        displayedFrames = 0;
        detectedRequests = 0;
        recognizedRequests = 0;
        latencyTotalMs = 0;
        latencySamples = 0;
        statsTimerClock.restart();
    }

    void emitPreview(const QImage &image)
    {
        if (owner) {
            emit owner->previewReady(image);
        }
    }

    QPointer<AiProctorMonitor> owner;
    QString scriptPath;
    QString baselineTemplate;
    bool baselineReady = false;

    QTimer *displayTimer = nullptr;
    QTimer *statsTimer = nullptr;
    QProcess *faceProcess = nullptr;
    QByteArray processBuffer;

    QImage latestImage;
    qint64 latestFrameSerial = 0;
    qint64 displayedFrameSerial = 0;
    int frameIndex = 0;
    bool analysisPending = false;
    bool pendingRecognize = false;
    int pendingRequestId = 0;
    double pendingScaleX = 1.0;
    double pendingScaleY = 1.0;

    QImage currentFrame;
    QVector<PreviewFace> lastFaces;
    QRect stableFaceRect;
    QString lastTemplate;
    QString stableText = QStringLiteral("等待摄像头画面");
    QString stableState = QStringLiteral("normal");
    QString statsText = QStringLiteral("camera 0.0 FPS · preview 0.0 FPS · detect 0.0 FPS · recognize 0.0 FPS · latency 0 ms");
    int displayFramesSinceFace = kConfig.missingFrameLimit + 1;
    int missingDetections = 0;
    int stableFaceCount = 0;
    bool stableMatched = false;
    std::deque<RecognitionSample> recognitionWindow;
    QElapsedTimer faceSeenTimer;
    QElapsedTimer recognizedTimer;
    QElapsedTimer requestTimer;
    QElapsedTimer statsTimerClock;

    int cameraFrames = 0;
    int displayedFrames = 0;
    int detectedRequests = 0;
    int recognizedRequests = 0;
    qint64 latencyTotalMs = 0;
    int latencySamples = 0;
    double cameraFps = 0;
    double displayFps = 0;
    double detectFps = 0;
    double recognizeFps = 0;
    double averageLatencyMs = 0;

    // 人脸检测/模板/核验埋点的限频与跳变状态（仅供诊断日志，不参与任何识别决策）。
    int detectStartLogCount = 0;
    int detectResultLogCount = 0;
    int lastLoggedFaceCount = -1;
    bool templateReadyLogged = false;
    int templateEmptyLogCount = 0;
    int verifyScoreLogCount = 0;

    // FacePipelineWorker 分析链路兜底（麒麟 PMF connect 失效）：singleShot 自调度 + 进程轮询排水的状态。
    std::atomic_bool emitPreviewFrames{true};  // false=麒麟主路径已自发预览，worker 仅分析不重复发预览
    bool analysisLoopRunning = false;
    quint64 analysisTickCount = 0;
    qint64 lastSeenFrameSerial = -1;
    int noFrameLogCount = 0;
    int enqueueLogCount = 0;
    int stdoutLogCount = 0;
    int stderrLogCount = 0;
};

AiProctorMonitor::AiProctorMonitor(QObject *parent)
    : QObject(parent)
{
}

AiProctorMonitor::~AiProctorMonitor()
{
    stop();
}

bool AiProctorMonitor::start(const QString &baselinePath, int cameraIndex)
{
    // 构建戳：日志里没有这行 = 跑的是旧二进制（麒麟 stale binary 排查锚点）。
    // inst 用于区分身份核验对话框与考试页两个 AiProctorMonitor 实例的日志。
    qInfo().noquote() << QStringLiteral("[BUILD_STAMP] camera-preview-connect-fallback-fix built=%1 %2 inst=0x%3")
                             .arg(QStringLiteral(__DATE__), QStringLiteral(__TIME__))
                             .arg(reinterpret_cast<quintptr>(this), 0, 16);
    if (running) {
        emit statusChanged(QStringLiteral("OpenCV人脸识别已在运行"), QStringLiteral("normal"));
        return true;
    }

    currentBaselinePath = baselinePath;
    cameraFrameSeen = false;
    cameraStartMissingDevice = false;
#ifdef Q_OS_LINUX
    // 设置了 Windows 摄像头代理（QTEXAM_CAMERA_URL=http...）时跳过本地设备检查，
    // 直接走网络取帧；未设置时保持原有 /dev/video* 检测。
    if (cameraSourceOverride().isEmpty() && existingVideoDevices().isEmpty()) {
        cameraStartMissingDevice = true;
        emit statusChanged(noCameraDeviceMessage(), QStringLiteral("error"));
        return false;
    }
#endif
    if (!startWorker()) {
        return false;
    }

#ifdef Q_OS_LINUX
    if (startPythonOpenCvCamera(cameraIndex)) {
        // 麒麟主路径在 handleCameraOutput 里自发 previewReady，关掉 worker 预览只让它做分析，避免预览双发闪烁。
        if (worker) {
            worker->setEmitPreviewFrames(false);
        }
        return true;
    }
#if defined(QTEXAM_USE_OPENCV_CAMERA)
    if (startOpenCvCamera(cameraIndex)) {
        return true;
    }
#endif
    stopWorker();
    return false;
#endif

    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (cameras.isEmpty()) {
        cameraStartMissingDevice = true;
        emit statusChanged(QStringLiteral("没有检测到可用摄像头"), QStringLiteral("error"));
        stopWorker();
        return false;
    }

    availableCameras.clear();
    const int firstIndex = preferredCameraIndex(cameras, cameraIndex);
    for (int i = firstIndex; i < cameras.size(); ++i) {
        availableCameras.append(cameras.at(i));
    }
    for (int i = 0; i < firstIndex; ++i) {
        availableCameras.append(cameras.at(i));
    }

    running = true;
    emit runningChanged(true);
    if (!openCameraAt(0)) {
        running = false;
        emit runningChanged(false);
        stopWorker();
        return false;
    }

    emit statusChanged(QStringLiteral("人脸识别已启动：请保持正脸、光线充足，系统会连续确认后放行"), QStringLiteral("normal"));
    return true;

    const int index = qBound(0, cameraIndex, cameras.size() - 1);
    const QCameraDevice device = cameras.at(index);
    camera = new QCamera(device, this);
    emit statusChanged(QStringLiteral("正在打开摄像头：%1").arg(device.description()), QStringLiteral("normal"));
    const QCameraFormat format = chooseCameraFormat(device);
    if (!format.isNull()) {
        camera->setCameraFormat(format);
    }

    videoSink = new QVideoSink(this);
    captureSession.setCamera(camera);
    captureSession.setVideoSink(videoSink);
    connect(camera,
            &QCamera::errorOccurred,
            this,
            [this](QCamera::Error, const QString &errorString) {
                const QString message = errorString.trimmed().isEmpty()
                    ? QStringLiteral("摄像头启动失败，请检查麒麟系统是否已连接摄像头并允许访问。")
                    : QStringLiteral("摄像头启动失败：%1").arg(errorString.trimmed());
                emit statusChanged(message, QStringLiteral("error"));
            });
    connect(camera,
            &QCamera::activeChanged,
            this,
            [this](bool active) {
                if (active) {
                    emit statusChanged(QStringLiteral("摄像头已打开，正在等待画面..."), QStringLiteral("normal"));
                }
            });
    connect(videoSink,
            &QVideoSink::videoFrameChanged,
            this,
            [this](const QVideoFrame &frame) {
                if (frame.isValid()) {
                    cameraFrameSeen = true;
                }
            });
    connect(videoSink,
            &QVideoSink::videoFrameChanged,
            worker,
            [this](const QVideoFrame &frame) {
                if (worker) {
                    worker->enqueueFrame(frame);
                }
            },
            Qt::QueuedConnection);

    camera->start();
    QTimer::singleShot(6000, this, [this] {
        if (running && !cameraFrameSeen) {
            emit statusChanged(QStringLiteral("摄像头已启动但没有收到画面，请检查 /dev/video*、虚拟机摄像头连接和麒麟摄像头权限。"),
                               QStringLiteral("error"));
        }
    });
    running = true;
    emit runningChanged(true);
    emit statusChanged(QStringLiteral("人脸识别已启动：请保持正脸、光线充足，系统会连续确认后放行"), QStringLiteral("normal"));
    return true;
}

bool AiProctorMonitor::startPythonOpenCvCamera(int cameraIndex)
{
#ifdef Q_OS_LINUX
    const QString script = cameraBridgeScriptPath();
    if (script.isEmpty()) {
        emit statusChanged(QStringLiteral("未找到摄像头桥接脚本 tools/camera_capture_bridge.py"), QStringLiteral("error"));
        return false;
    }

    const QString configuredCameraUrl = QProcessEnvironment::systemEnvironment()
                                            .value(QStringLiteral("QTEXAM_CAMERA_URL"))
                                            .trimmed();
    qInfo().noquote() << QStringLiteral("[CameraDiag] QTEXAM_CAMERA_URL=%1")
                             .arg(configuredCameraUrl.isEmpty() ? QStringLiteral("<empty>") : configuredCameraUrl);

    // 优先使用 Windows 摄像头代理（HTTP URL）；否则枚举本地 /dev/video*。
    const QString cameraUrl = cameraSourceOverride();
    QStringList devices;
    if (!cameraUrl.isEmpty()) {
        devices << cameraUrl;
        pythonCameraUsingUrl = true;
        pythonCameraSource = cameraUrl;
        emit statusChanged(QStringLiteral("使用 Windows 主机摄像头：%1").arg(cameraUrl), QStringLiteral("normal"));
        qInfo().noquote() << QStringLiteral("[PROCTOR_START] mode=URL source=%1 (Windows Host Bridge)").arg(cameraUrl);
    } else {
        pythonCameraUsingUrl = false;
        devices = existingVideoDevices();
        if (devices.isEmpty()) {
            emit statusChanged(noCameraDeviceMessage(), QStringLiteral("error"));
            return false;
        }
        pythonCameraSource = devices.first();
    }

    stopPythonOpenCvCamera();
    cameraFrameSeen = false;
    pythonCameraFrameCount = 0;
    previewEmitCount = 0;
    pythonCameraUsingUrl = !cameraUrl.isEmpty();
    pythonCameraSource = devices.isEmpty() ? QString() : devices.first();
    outputPollTickCount = 0;
    lastCameraPreviewTimer.invalidate();
    cameraBatchGapTimer.invalidate();
    lastCameraLog.clear();
    cameraProcessBuffer.clear();

    cameraProcess = new QProcess(this);
    cameraProcess->setProgram(pythonProgram());
    cameraProcess->setProcessChannelMode(QProcess::SeparateChannels);
    QStringList args;
    args << QStringLiteral("-u") << QStringLiteral("-X") << QStringLiteral("utf8") << script;
    Q_UNUSED(cameraIndex)
    args << devices;
    cameraProcess->setArguments(args);
    cameraProcess->setWorkingDirectory(QFileInfo(script).absolutePath());

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    environment.insert(QStringLiteral("PYTHONFAULTHANDLER"), QStringLiteral("1"));
    environment.remove(QStringLiteral("QT_PLUGIN_PATH"));
    environment.remove(QStringLiteral("QML2_IMPORT_PATH"));
    environment.remove(QStringLiteral("QML_IMPORT_PATH"));
    // The spawned Python loads opencv-python, which links its own libjpeg / libavcodec
    // / libstdc++ (and sometimes Qt). If this app was launched with an LD_LIBRARY_PATH
    // that points at bundled Qt libraries (desktop shortcut / Qt Creator -- i.e. NOT
    // run_client_kylin.sh), those libraries poison cv2 in the child and it returns
    // black frames or crashes. That is exactly the "standalone works, Qt-spawned is
    // black" symptom. Strip a Qt-referencing LD_LIBRARY_PATH for the child so cv2
    // resolves the system libraries it was built against (mirrors run_client_kylin.sh).
    const QString ldLibraryPath = environment.value(QStringLiteral("LD_LIBRARY_PATH"));
    if (ldLibraryPath.contains(QStringLiteral("qt"), Qt::CaseInsensitive)) {
        qInfo().noquote() << QStringLiteral("[CameraDiag] stripping Qt-referencing LD_LIBRARY_PATH from camera subprocess: %1")
                                 .arg(ldLibraryPath);
        environment.remove(QStringLiteral("LD_LIBRARY_PATH"));
    }
    cameraProcess->setProcessEnvironment(environment);
    qInfo().noquote() << QStringLiteral("[CameraDiag] backend=%1 python=%2 script=%3 source=%4")
                             .arg(pythonCameraUsingUrl ? QStringLiteral("PythonBridge(URL)")
                                                       : QStringLiteral("PythonBridge(OpenCV V4L2)"),
                                  cameraProcess->program(),
                                  script,
                                  devices.join(QLatin1Char(' ')));
    qInfo().noquote() << QStringLiteral("[OpenCVCamera] launching bridge program=%1 args=%2 cwd=%3")
                             .arg(cameraProcess->program(),
                                  cameraProcess->arguments().join(QLatin1Char(' ')),
                                  cameraProcess->workingDirectory());
    // 明确打印将要执行的完整命令，便于在日志里核对“Qt 端到底调了哪个 python、哪个设备”。
    qInfo().noquote() << QStringLiteral("[PYTHON_BRIDGE_START] command=%1 %2 cwd=%3 source=%4")
                             .arg(cameraProcess->program(),
                                  cameraProcess->arguments().join(QLatin1Char(' ')),
                                  cameraProcess->workingDirectory(),
                                  devices.isEmpty() ? QStringLiteral("<none>") : devices.first());

    // —— 排水连接：三重保险 ——
    // 麒麟系统 Qt(5.15.19) 以 reduce_relocations（-Bsymbolic-functions）构建，而本客户端
    // 经 CMake 按 PIE 编译，应用侧取 Qt 库类信号的成员函数指针与库内部地址不一致，
    // 新式 PMF connect 在 IndexOfMethod 匹配一步失败、返回无效连接（运行日志里大量
    // "QObject::connect: signal not found in QProcess/QTimer/QPushButton..." 即由此而来）。
    // 因此：① 先试新式 connect（常规平台直连）；② 失败转老式 SIGNAL/SLOT 字符串连接
    //（纯 moc 表查找、不比较指针，不受影响）；③ 无论①②结果，摄像头启动后再起
    // singleShot 自调度排水链兜底（见 scheduleDrainTick）。
    const bool drainPmfOk = static_cast<bool>(
        connect(cameraProcess, &QProcess::readyReadStandardOutput, this, [this] {
            handleCameraOutput();
        }));
    bool drainLegacyOk = false;
    if (!drainPmfOk) {
        drainLegacyOk = static_cast<bool>(
            connect(cameraProcess, SIGNAL(readyReadStandardOutput()),
                    this, SLOT(handleCameraOutput())));
    }
    qInfo().noquote() << QStringLiteral("[DRAIN_CONNECT] readyReadStandardOutput connected ok=%1 pmf=%2 legacy=%3 process=0x%4 inst=0x%5")
                             .arg((drainPmfOk || drainLegacyOk) ? 1 : 0)
                             .arg(drainPmfOk ? 1 : 0)
                             .arg(drainLegacyOk ? 1 : 0)
                             .arg(reinterpret_cast<quintptr>(cameraProcess), 0, 16)
                             .arg(reinterpret_cast<quintptr>(this), 0, 16);
    const bool errPmfOk = static_cast<bool>(
        connect(cameraProcess, &QProcess::readyReadStandardError, this, [this] {
            handleCameraError();
        }));
    bool errLegacyOk = false;
    if (!errPmfOk) {
        errLegacyOk = static_cast<bool>(
            connect(cameraProcess, SIGNAL(readyReadStandardError()),
                    this, SLOT(handleCameraError())));
    }
    qInfo().noquote() << QStringLiteral("[DRAIN_CONNECT] readyReadStandardError connected ok=%1 pmf=%2 legacy=%3 inst=0x%4")
                             .arg((errPmfOk || errLegacyOk) ? 1 : 0)
                             .arg(errPmfOk ? 1 : 0)
                             .arg(errLegacyOk ? 1 : 0)
                             .arg(reinterpret_cast<quintptr>(this), 0, 16);
    // 兜底排水轮询：持久成员定时器（new QTimer(this)，不挂在 cameraProcess 下），
    // 与 readyReadStandardOutput 双保险，保证 handleCameraOutput() 持续被触发；
    // 构造一次、连接一次，重启摄像头时复用，stopPythonOpenCvCamera() 里显式 stop 防残留。
    if (!outputPollTimer) {
        outputPollTimer = new QTimer(this);
        outputPollTimer->setInterval(250);
        outputPollTimer->setTimerType(Qt::PreciseTimer);
        const bool pollPmfOk = static_cast<bool>(
            connect(outputPollTimer, &QTimer::timeout, this, [this] {
                handleCameraPollTick();
            }));
        bool pollLegacyOk = false;
        if (!pollPmfOk) {
            pollLegacyOk = static_cast<bool>(
                connect(outputPollTimer, SIGNAL(timeout()),
                        this, SLOT(handleCameraPollTick())));
        }
        outputPollConnected = pollPmfOk || pollLegacyOk;
        qInfo().noquote() << QStringLiteral("[DRAIN_CONNECT] outputPollTimer first-connect pmf=%1 legacy=%2 inst=0x%3")
                                 .arg(pollPmfOk ? 1 : 0)
                                 .arg(pollLegacyOk ? 1 : 0)
                                 .arg(reinterpret_cast<quintptr>(this), 0, 16);
    }
    qInfo().noquote() << QStringLiteral("[DRAIN_CONNECT] outputPollTimer connected interval=%1 ok=%2 timer=0x%3 active=%4 inst=0x%5")
                             .arg(outputPollTimer->interval())
                             .arg(outputPollConnected ? 1 : 0)
                             .arg(reinterpret_cast<quintptr>(outputPollTimer), 0, 16)
                             .arg(outputPollTimer->isActive() ? 1 : 0)
                             .arg(reinterpret_cast<quintptr>(this), 0, 16);
    const bool finPmfOk = static_cast<bool>(
        connect(cameraProcess,
                qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                this,
                [this](int exitCode, QProcess::ExitStatus) {
                    handleCameraProcessFinished(exitCode);
                }));
    bool finLegacyOk = false;
    if (!finPmfOk) {
        finLegacyOk = static_cast<bool>(
            connect(cameraProcess, SIGNAL(finished(int,QProcess::ExitStatus)),
                    this, SLOT(handleCameraProcessFinished(int))));
    }
    qInfo().noquote() << QStringLiteral("[DRAIN_CONNECT] finished connected ok=%1 pmf=%2 legacy=%3 inst=0x%4")
                             .arg((finPmfOk || finLegacyOk) ? 1 : 0)
                             .arg(finPmfOk ? 1 : 0)
                             .arg(finLegacyOk ? 1 : 0)
                             .arg(reinterpret_cast<quintptr>(this), 0, 16);

    running = true;
    emit runningChanged(true);
    emit statusChanged(pythonCameraUsingUrl
                           ? QStringLiteral("正在连接 Windows 摄像头代理：%1").arg(devices.first())
                           : QStringLiteral("正在使用 OpenCV V4L2 打开摄像头 %1...").arg(devices.first()),
                       QStringLiteral("normal"));
    cameraProcess->start();
    if (!cameraProcess->waitForStarted(3000)) {
        const QString errorText = cameraProcess->errorString();
        qWarning().noquote() << QStringLiteral("[CAMERA_ERROR] reason=bridge-failed-to-start detail=%1").arg(errorText);
        stopPythonOpenCvCamera();
        running = false;
        emit runningChanged(false);
        emit statusChanged(pythonCameraUsingUrl
                               ? QStringLiteral("Windows 摄像头代理进程启动失败：%1").arg(errorText)
                               : QStringLiteral("OpenCV V4L2 摄像头进程启动失败：%1").arg(errorText),
                           QStringLiteral("error"));
        return false;
    }

    qInfo().noquote() << QStringLiteral("[CameraDiag] camera bridge started pid=%1").arg(cameraProcess->processId());
    emit statusChanged(pythonCameraUsingUrl
                           ? QStringLiteral("人脸识别已启动：正在读取 Windows 摄像头代理画面")
                           : QStringLiteral("人脸识别已启动：OpenCV V4L2 正在读取摄像头画面"),
                       QStringLiteral("normal"));
    outputPollTimer->start();
    qInfo().noquote() << QStringLiteral("[DRAIN_TIMER] started active=%1 remaining=%2 inst=0x%3")
                             .arg(outputPollTimer->isActive() ? 1 : 0)
                             .arg(outputPollTimer->remainingTime())
                             .arg(reinterpret_cast<quintptr>(this), 0, 16);
    // 第三重保险：singleShot 自调度排水链。即使上面 readyRead 与轮询定时器两路连接
    // 全部失效，也保证每 250ms 排水一次（机制说明见 scheduleDrainTick）。
    drainChainTickCount = 0;
    drainChainActive = true;
    scheduleDrainTick();
    QTimer::singleShot(9000, this, [this] {
        if (cameraProcess) {
            handleCameraOutput();
            handleCameraError();
        }
        if (running && cameraProcess && !cameraFrameSeen) {
            const QString detail = lastCameraLog.isEmpty()
                ? QStringLiteral("暂无 [OpenCVCamera] 日志输出")
                : lastCameraLog.left(160);
            qWarning().noquote() << QStringLiteral("[OpenCVCamera] no valid frame after 9s state=%1 pid=%2 error=%3 lastLog=%4")
                                    .arg(static_cast<int>(cameraProcess->state()))
                                    .arg(cameraProcess->processId())
                                    .arg(cameraProcess->errorString(), detail);
            emit statusChanged(pythonCameraUsingUrl
                                   ? cameraProxyUnavailableMessage()
                                   : QStringLiteral("OpenCV V4L2 9 秒内没有收到有效帧：%1").arg(detail),
                               QStringLiteral("error"));
        }
    });
    return true;
#else
    Q_UNUSED(cameraIndex)
    return false;
#endif
}

void AiProctorMonitor::handleCameraOutput()
{
    if (!cameraProcess) {
        return;
    }

    // 入口间隔诊断：两次调用间隔远超帧间隔(100ms)说明主线程事件循环被阻塞，
    // 帧积压成一大批一次性涌入——这正是"frame#80 才第一次 emit"的根源。
    const qint64 drainGapMs = cameraBatchGapTimer.isValid() ? cameraBatchGapTimer.elapsed() : -1;
    cameraBatchGapTimer.restart();
    int framesThisBatch = 0;
    constexpr qint64 kPreviewEmitIntervalMs = 80;

    cameraProcessBuffer += cameraProcess->readAllStandardOutput();
    while (true) {
        const int newline = cameraProcessBuffer.indexOf('\n');
        if (newline < 0) {
            break;
        }

        const QByteArray line = cameraProcessBuffer.left(newline).trimmed();
        cameraProcessBuffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            qWarning().noquote() << QStringLiteral("[OpenCVCamera] invalid json: %1").arg(QString::fromUtf8(line.left(180)));
            continue;
        }

        const QJsonObject object = document.object();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("opened")) {
            qInfo().noquote() << QStringLiteral("[OpenCVCamera] bridge opened device=%1")
                                     .arg(object.value(QStringLiteral("device")).toString());
            emit statusChanged(pythonCameraUsingUrl
                                   ? QStringLiteral("Windows 摄像头代理已打开：%1").arg(object.value(QStringLiteral("device")).toString())
                                   : QStringLiteral("OpenCV V4L2 摄像头已打开：%1").arg(object.value(QStringLiteral("device")).toString()),
                               QStringLiteral("normal"));
            continue;
        }
        if (type == QStringLiteral("error")) {
            qWarning().noquote() << QStringLiteral("[OpenCVCamera] bridge error: %1")
                                        .arg(object.value(QStringLiteral("message")).toString());
            emit statusChanged(object.value(QStringLiteral("message")).toString(pythonCameraUsingUrl
                                   ? cameraProxyUnavailableMessage()
                                   : QStringLiteral("OpenCV V4L2 摄像头读取失败")),
                               QStringLiteral("error"));
            continue;
        }
        if (type != QStringLiteral("frame")) {
            continue;
        }

        if (!cameraFrameSeen) {
            qInfo().noquote() << QStringLiteral("[OpenCVCamera] first frame json received from %1")
                                     .arg(object.value(QStringLiteral("device")).toString());
        }
        cameraFrameSeen = true;
        // 周期打印帧计数，便于确认 Qt 端确实在持续收到摄像头帧（不是只收到一帧或卡住）。
        if (pythonCameraFrameCount % 30 == 0) {
            qInfo().noquote() << QStringLiteral("[CAMERA_PREVIEW] frame count=%1 device=%2 inst=0x%3")
                                     .arg(pythonCameraFrameCount + 1)
                                     .arg(object.value(QStringLiteral("device")).toString())
                                     .arg(reinterpret_cast<quintptr>(this), 0, 16);
        }

        const QByteArray imageBytes = QByteArray::fromBase64(object.value(QStringLiteral("image")).toString().toLatin1());
        QImage image;
        if (!image.loadFromData(imageBytes, "JPEG") && !image.loadFromData(imageBytes)) {
            qWarning().noquote() << QStringLiteral("[OpenCVCamera] frame decode failed device=%1 imageBytes=%2")
                                    .arg(object.value(QStringLiteral("device")).toString())
                                    .arg(imageBytes.size());
            emit statusChanged(pythonCameraUsingUrl
                                   ? QStringLiteral("Windows 摄像头代理已收到数据，但 JPEG 解码失败：%1 字节").arg(imageBytes.size())
                                   : QStringLiteral("OpenCV V4L2 已收到摄像头数据，但 JPEG 解码失败：%1 字节").arg(imageBytes.size()),
                               QStringLiteral("error"));
            continue;
        }

        const bool firstFrame = pythonCameraFrameCount == 0;
        pythonCameraFrameCount++;
        framesThisBatch++;

        // 逐帧节流发射（方案 A）：emit 在循环内部逐帧判断，不再"整批结束只发一次"。
        //  - 首帧立即发（emitFirst）；
        //  - 距上次发射 >=80ms 发（emitByTime，正常 10fps 输入下即 ~8-12fps）；
        //  - 同批多帧时钟不增长时按帧数兜底（emitByFrame 每 3 帧 1 次），
        //    保证事件循环阻塞后的大批积压也能持续产生预览更新。
        const bool emitFirst = previewEmitCount == 0;
        const bool emitByTime = !lastCameraPreviewTimer.isValid()
                                || lastCameraPreviewTimer.elapsed() >= kPreviewEmitIntervalMs;
        const bool emitByFrame = pythonCameraFrameCount % 3 == 1;
        if (emitFirst || emitByTime || emitByFrame) {
            previewEmitCount++;
            if (previewEmitCount <= 3 || previewEmitCount % 30 == 0) {
                qInfo().noquote() << QStringLiteral("[PreviewDebug] emitting previewReady size=%1x%2 frame#%3 emit#%4 inst=0x%5")
                                         .arg(image.width()).arg(image.height())
                                         .arg(pythonCameraFrameCount).arg(previewEmitCount)
                                         .arg(reinterpret_cast<quintptr>(this), 0, 16);
            }
            // copy()：深拷贝像素，杜绝发出的 QImage 悬空引用本循环的临时解码缓冲。
            emit previewReady(image.convertToFormat(QImage::Format_RGB32).copy());
            lastCameraPreviewTimer.restart();
        }

        if (firstFrame) {
            emit statusChanged((pythonCameraUsingUrl
                                    ? QStringLiteral("已收到 Windows 摄像头代理画面：%1x%2")
                                    : QStringLiteral("OpenCV V4L2 已收到摄像头画面：%1x%2"))
                                   .arg(image.width())
                                   .arg(image.height()),
                               QStringLiteral("normal"));
        }
        if (worker) {
            QMetaObject::invokeMethod(worker,
                                      [target = worker, image] {
                                          if (target) {
                                              target->enqueueImage(image);
                                          }
                                      },
                                      Qt::QueuedConnection);
        }
    }

    // 事件循环饥饿告警：一次涌入 >10 帧或距上次排水 >1 秒，说明主线程在
    // 这段时间被别的代码阻塞（这才是积压的真正根源），打出来便于定位元凶。
    if (framesThisBatch > 0 && (drainGapMs > 1000 || framesThisBatch > 10)) {
        qWarning().noquote() << QStringLiteral("[PreviewDebug] WARN frame backlog drained gap=%1ms batch_frames=%2 total_frames=%3 inst=0x%4")
                                    .arg(drainGapMs).arg(framesThisBatch).arg(pythonCameraFrameCount)
                                    .arg(reinterpret_cast<quintptr>(this), 0, 16);
    }
}

void AiProctorMonitor::handleCameraError()
{
    if (!cameraProcess) {
        return;
    }
    const QString text = QString::fromUtf8(cameraProcess->readAllStandardError()).trimmed();
    if (!text.isEmpty()) {
        const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            lastCameraLog = trimmed;
            qWarning().noquote() << QStringLiteral("[PYTHON_BRIDGE_STDERR] %1").arg(trimmed);
            if (trimmed.contains(QStringLiteral("[OpenCVCamera]"))
                || trimmed.contains(QStringLiteral("[CameraBridge]"))) {
                const QString state = trimmed.contains(QStringLiteral("opened"))
                    ? QStringLiteral("normal")
                    : (trimmed.contains(QStringLiteral("failed")) ? QStringLiteral("warning") : QStringLiteral("normal"));
                emit statusChanged(trimmed.left(180), state);
            }
        }
    }
}

void AiProctorMonitor::stopPythonOpenCvCamera()
{
    // 先停兜底轮询与 singleShot 排水链（均为持久成员状态，不随 cameraProcess 销毁），
    // 避免停摄像头后空转。
    drainChainActive = false;
    if (outputPollTimer) {
        outputPollTimer->stop();
    }
    cameraProcessBuffer.clear();
    if (!cameraProcess) {
        return;
    }
    disconnect(cameraProcess, nullptr, this, nullptr);
    if (cameraProcess->state() != QProcess::NotRunning) {
        cameraProcess->terminate();
        if (!cameraProcess->waitForFinished(900)) {
            cameraProcess->kill();
            cameraProcess->waitForFinished(700);
        }
    }
    cameraProcess->deleteLater();
    cameraProcess = nullptr;
}

void AiProctorMonitor::handleCameraPollTick()
{
    if (!cameraProcess) {
        return;
    }
    outputPollTickCount++;
    if (outputPollTickCount <= 3 || outputPollTickCount % 10 == 0) {
        qInfo().noquote() << QStringLiteral("[DRAIN_TIMER] timeout tick count=%1 active=%2 inst=0x%3")
                                 .arg(outputPollTickCount)
                                 .arg((outputPollTimer && outputPollTimer->isActive()) ? 1 : 0)
                                 .arg(reinterpret_cast<quintptr>(this), 0, 16);
    }
    handleCameraOutput();
    handleCameraError();
}

void AiProctorMonitor::handleCameraProcessFinished(int exitCode)
{
    // 先把残余输出排干（bridge 断流时最后会吐一条 {"type":"error"}），再收尾停轮询。
    handleCameraOutput();
    handleCameraError();
    if (outputPollTimer) {
        outputPollTimer->stop();
    }
    qWarning().noquote() << QStringLiteral("[OpenCVCamera] process exited code=%1").arg(exitCode);
    if (running && !cameraFrameSeen) {
        qWarning().noquote() << QStringLiteral("[CAMERA_ERROR] reason=bridge-exited-no-frame exitCode=%1 lastStderr=%2")
                                    .arg(exitCode)
                                    .arg(lastCameraLog.isEmpty() ? QStringLiteral("<none>") : lastCameraLog);
        emit statusChanged(pythonCameraUsingUrl
                               ? cameraProxyUnavailableMessage()
                               : QStringLiteral("OpenCV V4L2 摄像头进程已退出，且没有收到有效画面。请查看终端 [PYTHON_BRIDGE_STDERR] 日志。"),
                           QStringLiteral("error"));
        running = false;
        emit runningChanged(false);
    }
}

void AiProctorMonitor::scheduleDrainTick()
{
    // singleShot 自调度排水链（最终兜底）：QTimer::singleShot(ms, context, functor) 在
    // QtCore 内部 QSingleShotTimer::timerEvent 里直接调用 functor，全程不做跨库信号
    // 索引解析，因此在新式/老式 connect 都失效的环境下仍然可靠（本机 9 秒看门狗
    // singleShot 已被日志证实可触发）。不用 while 死循环，不阻塞 UI 线程；
    // 链路以 drainChainActive 为生存开关，stopPythonOpenCvCamera() 置 false 即自然终止。
    if (!drainChainActive) {
        return;
    }
    ++drainChainTickCount;
    if (drainChainTickCount <= 3 || drainChainTickCount % 20 == 0) {
        qInfo().noquote() << QStringLiteral("[DRAIN_TIMER] singleShot fallback tick count=%1 inst=0x%2")
                                 .arg(drainChainTickCount)
                                 .arg(reinterpret_cast<quintptr>(this), 0, 16);
    }
    if (cameraProcess) {
        handleCameraOutput();
        handleCameraError();
    }
    QTimer::singleShot(250, this, [this] {
        scheduleDrainTick();
    });
}

bool AiProctorMonitor::startOpenCvCamera(int cameraIndex)
{
#if defined(Q_OS_LINUX) && defined(QTEXAM_USE_OPENCV_CAMERA)
    if (opencvCameraThread) {
        return true;
    }

    if (existingVideoDevices().isEmpty()) {
        emit statusChanged(noCameraDeviceMessage(), QStringLiteral("error"));
        return false;
    }

    cameraFrameSeen = false;
    opencvCameraStop.store(false);
    running = true;
    emit runningChanged(true);
    emit statusChanged(QStringLiteral("正在使用 OpenCV V4L2 打开摄像头..."), QStringLiteral("normal"));

    opencvCameraThread = QThread::create([this, cameraIndex] {
        captureOpenCvLoop(cameraIndex);
    });
    connect(opencvCameraThread, &QThread::finished, opencvCameraThread, &QObject::deleteLater);
    connect(opencvCameraThread, &QThread::finished, this, [this] {
        opencvCameraThread = nullptr;
    });
    opencvCameraThread->start();
    return true;
#else
    Q_UNUSED(cameraIndex)
    return false;
#endif
}

void AiProctorMonitor::captureOpenCvLoop(int cameraIndex)
{
#if defined(Q_OS_LINUX) && defined(QTEXAM_USE_OPENCV_CAMERA)
    Q_UNUSED(cameraIndex)
    const QStringList devices = existingVideoDevices();
    if (devices.isEmpty()) {
        QMetaObject::invokeMethod(this,
                                  [this] {
                                      emit statusChanged(noCameraDeviceMessage(), QStringLiteral("error"));
                                      if (running) {
                                          running = false;
                                          emit runningChanged(false);
                                          stopWorker();
                                      }
                                  },
                                  Qt::QueuedConnection);
        return;
    }

    for (const QString &devicePath : devices) {
        if (opencvCameraStop.load()) {
            return;
        }

        qInfo().noquote() << QStringLiteral("[OpenCVCamera] trying %1 with CAP_V4L2").arg(devicePath);
        cv::VideoCapture cap(devicePath.toStdString(), cv::CAP_V4L2);
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        cap.set(cv::CAP_PROP_FPS, 30);
        cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

        cv::Mat frame;
        bool readOk = false;
        for (int i = 0; i < 12 && cap.isOpened() && !opencvCameraStop.load(); ++i) {
            readOk = cap.read(frame);
            if (readOk && !frame.empty()) {
                break;
            }
            qWarning().noquote() << QStringLiteral("[OpenCVCamera] warmup failed device=%1 isOpened=%2 readOk=%3 frame.empty=%4")
                                    .arg(devicePath)
                                    .arg(cap.isOpened())
                                    .arg(readOk)
                                    .arg(frame.empty());
            QThread::msleep(80);
        }

        if (!cap.isOpened() || !readOk || frame.empty()) {
            qWarning().noquote() << QStringLiteral("[OpenCVCamera] open/read failed device=%1 isOpened=%2 readOk=%3 frame.empty=%4")
                                    .arg(devicePath)
                                    .arg(cap.isOpened())
                                    .arg(readOk)
                                    .arg(frame.empty());
            cap.release();
            continue;
        }

        cameraFrameSeen = true;
        QMetaObject::invokeMethod(this,
                                  [this, devicePath] {
                                      emit statusChanged(QStringLiteral("OpenCV V4L2 摄像头已打开：%1").arg(devicePath),
                                                         QStringLiteral("normal"));
                                  },
                                  Qt::QueuedConnection);

        while (!opencvCameraStop.load()) {
            readOk = cap.read(frame);
            if (!readOk || frame.empty()) {
                qWarning().noquote() << QStringLiteral("[OpenCVCamera] frame read failed device=%1 isOpened=%2 readOk=%3 frame.empty=%4")
                                        .arg(devicePath)
                                        .arg(cap.isOpened())
                                        .arg(readOk)
                                        .arg(frame.empty());
                QThread::msleep(30);
                continue;
            }

            cv::Mat rgb;
            cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
            QImage image(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
            const QImage copy = image.copy();
            if (worker) {
                QMetaObject::invokeMethod(worker,
                                          [target = worker, copy] {
                                              if (target) {
                                                  target->enqueueImage(copy);
                                              }
                                          },
                                          Qt::QueuedConnection);
            }
            QThread::msleep(1);
        }

        cap.release();
        return;
    }

    QMetaObject::invokeMethod(this,
                              [this] {
                                  emit statusChanged(QStringLiteral("OpenCV V4L2 摄像头打开失败：未能从已检测到的 /dev/video* 设备读取有效帧，请查看终端 [OpenCVCamera] 日志。"),
                                                     QStringLiteral("error"));
                                  if (running) {
                                      running = false;
                                      emit runningChanged(false);
                                      stopWorker();
                                  }
                              },
                              Qt::QueuedConnection);
#else
    Q_UNUSED(cameraIndex)
#endif
}

void AiProctorMonitor::stopOpenCvCamera()
{
#if defined(Q_OS_LINUX) && defined(QTEXAM_USE_OPENCV_CAMERA)
    opencvCameraStop.store(true);
    if (opencvCameraThread) {
        opencvCameraThread->quit();
        opencvCameraThread->wait(2000);
        opencvCameraThread = nullptr;
    }
#endif
}

bool AiProctorMonitor::openCameraAt(int cameraIndex)
{
    if (cameraIndex < 0 || cameraIndex >= availableCameras.size()) {
        emit statusChanged(QStringLiteral("没有可用摄像头能输出画面"), QStringLiteral("error"));
        return false;
    }

    closeCameraOnly();
    cameraFrameSeen = false;
    activeCameraIndex = cameraIndex;

    const QCameraDevice device = availableCameras.at(cameraIndex);
    camera = new QCamera(device, this);
    emit statusChanged(QStringLiteral("正在打开摄像头：%1").arg(cameraDescription(device)), QStringLiteral("normal"));
    const QCameraFormat format = chooseCameraFormat(device);
    if (!format.isNull()) {
        camera->setCameraFormat(format);
    }

    videoSink = new QVideoSink(this);
    captureSession.setCamera(camera);
    captureSession.setVideoSink(videoSink);
    connect(camera,
            &QCamera::errorOccurred,
            this,
            [this](QCamera::Error, const QString &errorString) {
                const QString message = errorString.trimmed().isEmpty()
                    ? QStringLiteral("摄像头启动失败，请检查麒麟系统是否已连接摄像头并允许访问。")
                    : QStringLiteral("摄像头启动失败：%1").arg(errorString.trimmed());
                emit statusChanged(message, QStringLiteral("error"));
            });
    connect(camera,
            &QCamera::activeChanged,
            this,
            [this](bool active) {
                if (active) {
                    emit statusChanged(QStringLiteral("摄像头已打开，正在等待画面..."), QStringLiteral("normal"));
                }
            });
    connect(videoSink,
            &QVideoSink::videoFrameChanged,
            this,
            [this](const QVideoFrame &frame) {
                if (frame.isValid()) {
                    cameraFrameSeen = true;
                }
            });
    connect(videoSink,
            &QVideoSink::videoFrameChanged,
            worker,
            [this](const QVideoFrame &frame) {
                if (worker) {
                    worker->enqueueFrame(frame);
                }
            },
            Qt::QueuedConnection);

    camera->start();
    scheduleCameraFallback(cameraIndex);
    return true;
}

void AiProctorMonitor::scheduleCameraFallback(int cameraIndex)
{
    QTimer::singleShot(6000, this, [this, cameraIndex] {
        if (!running || cameraFrameSeen || cameraIndex != activeCameraIndex) {
            return;
        }

        const int nextIndex = cameraIndex + 1;
        if (nextIndex < availableCameras.size()) {
            emit statusChanged(QStringLiteral("当前摄像头没有画面，正在尝试下一个摄像头..."), QStringLiteral("warning"));
            openCameraAt(nextIndex);
            return;
        }

        emit statusChanged(QStringLiteral("摄像头已启动但没有收到画面。/dev/video0 可采集时，请重启客户端或检查 Qt Multimedia/GStreamer 插件。"),
                           QStringLiteral("error"));
    });
}

void AiProctorMonitor::closeCameraOnly()
{
    if (videoSink) {
        disconnect(videoSink, nullptr, nullptr, nullptr);
    }
    if (camera) {
        camera->stop();
    }
    captureSession.setVideoSink(nullptr);
    captureSession.setCamera(nullptr);
    if (videoSink) {
        videoSink->deleteLater();
        videoSink = nullptr;
    }
    if (camera) {
        camera->deleteLater();
        camera = nullptr;
    }
}

bool AiProctorMonitor::startSimulation()
{
    if (running) {
        return true;
    }
    simulationMode = true;
    simulationFrameIndex = 0;
    running = true;
    emit runningChanged(true);
    emit statusChanged(
        QStringLiteral("【模拟监考模式】摄像头不可用，正在使用模拟数据（答辩演示用）"),
        QStringLiteral("warning")
    );

    simulationTimer = new QTimer(this);
    simulationTimer->setInterval(100);
    // 模拟节拍同样受麒麟跨库 PMF connect 失效影响（QTimer::timeout），新式失败转老式
    // SIGNAL/SLOT 字符串连接（答辩演示通道必须保活）。
    const bool simPmfOk = static_cast<bool>(
        connect(simulationTimer, &QTimer::timeout, this, [this] {
            handleSimulationTick();
        }));
    bool simLegacyOk = false;
    if (!simPmfOk) {
        simLegacyOk = static_cast<bool>(
            connect(simulationTimer, SIGNAL(timeout()),
                    this, SLOT(handleSimulationTick())));
    }
    qInfo().noquote() << QStringLiteral("[DRAIN_CONNECT] simulationTimer connected ok=%1 pmf=%2 legacy=%3 inst=0x%4")
                             .arg((simPmfOk || simLegacyOk) ? 1 : 0)
                             .arg(simPmfOk ? 1 : 0)
                             .arg(simLegacyOk ? 1 : 0)
                             .arg(reinterpret_cast<quintptr>(this), 0, 16);
    simulationTimer->start();
    return true;
}

void AiProctorMonitor::handleSimulationTick()
{
    ++simulationFrameIndex;

    QImage frame(640, 480, QImage::Format_RGB32);
    frame.fill(QColor(22, 33, 55));
    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing);

    const int scanY = (simulationFrameIndex * 4) % 480;
    painter.fillRect(0, scanY, 640, 2, QColor(0, 180, 120, 80));

    const int faceOscX = 220 + (simulationFrameIndex % 20 < 10 ? simulationFrameIndex % 10 : 10 - simulationFrameIndex % 10) - 5;
    painter.setPen(QPen(QColor(0, 200, 150), 3));
    painter.drawRoundedRect(QRect(faceOscX, 100, 200, 260), 10, 10);
    painter.fillRect(QRect(faceOscX, 76, 148, 22), QColor(17, 28, 26, 200));
    painter.setPen(Qt::white);
    QFont badgeFont;
    badgeFont.setPointSize(9);
    painter.setFont(badgeFont);
    painter.drawText(QRect(faceOscX + 6, 76, 148, 22),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("模拟人脸 0.94"));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(17, 28, 26, 200));
    painter.drawRoundedRect(QRect(12, 12, 616, 56), 8, 8);
    painter.setPen(Qt::white);
    QFont titleFont;
    titleFont.setPointSize(11);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(QRect(24, 14, 616, 28),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("【模拟监考模式】摄像头设备不可用"));
    QFont subFont;
    subFont.setPointSize(9);
    painter.setFont(subFont);
    painter.setPen(QColor(0, 220, 160));
    painter.drawText(QRect(24, 40, 616, 22),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("正在模拟监考数据用于答辩演示  ·  真实考试需在虚拟机中连接摄像头"));
    painter.end();

    emit previewReady(frame);

    if (simulationFrameIndex % 10 == 0) {
        const double conf = 0.88 + static_cast<double>(simulationFrameIndex % 12) * 0.01;
        emit faceResult(true, 1, true, qMin(conf, 0.99));
    }
    if (simulationFrameIndex % 50 == 0) {
        emit statusChanged(
            QStringLiteral("【模拟监考模式】运行中  已模拟 %1 秒  人脸识别: 身份匹配").arg(simulationFrameIndex / 10),
            QStringLiteral("warning")
        );
    }
}

bool AiProctorMonitor::isSimulationMode() const
{
    return simulationMode;
}

bool AiProctorMonitor::lastStartMissingDevice() const
{
    return cameraStartMissingDevice;
}

void AiProctorMonitor::stop()
{
    if (simulationTimer) {
        simulationTimer->stop();
        simulationTimer->deleteLater();
        simulationTimer = nullptr;
    }
    simulationMode = false;
    simulationFrameIndex = 0;
    stopPythonOpenCvCamera();
    stopOpenCvCamera();
    closeCameraOnly();
    availableCameras.clear();
    activeCameraIndex = -1;
    stopWorker();
    if (running) {
        running = false;
        emit runningChanged(false);
        emit statusChanged(QStringLiteral("OpenCV人脸识别已停止"), QStringLiteral("normal"));
    }
}

bool AiProctorMonitor::isRunning() const
{
    return running;
}

void AiProctorMonitor::setBaselineTemplate(const QString &faceTemplate)
{
    baselineTemplate = faceTemplate.trimmed();
}

QString AiProctorMonitor::currentFaceTemplate() const
{
    if (!workerThread || !worker) {
        return {};
    }
    QString value;
    QMetaObject::invokeMethod(worker,
                              [&value, this] {
                                  if (worker) {
                                      value = worker->currentTemplate();
                                  }
                              },
                              Qt::BlockingQueuedConnection);
    return value;
}

bool AiProctorMonitor::hasCurrentFace() const
{
    if (!workerThread || !worker) {
        return false;
    }
    bool value = false;
    QMetaObject::invokeMethod(worker,
                              [&value, this] {
                                  if (worker) {
                                      value = worker->hasCurrentFace();
                                  }
                              },
                              Qt::BlockingQueuedConnection);
    return value;
}

bool AiProctorMonitor::startWorker()
{
    const QString script = bridgeScriptPath();
    if (script.isEmpty()) {
        emit statusChanged(QStringLiteral("未找到OpenCV人脸识别脚本 tools/face_recognition_bridge.py"), QStringLiteral("error"));
        return false;
    }

    workerThread = new QThread(this);
    worker = new FacePipelineWorker(this, script, baselineTemplate);
    worker->moveToThread(workerThread);
    connect(workerThread, &QThread::finished, worker, &QObject::deleteLater);
    workerThread->start();
    QMetaObject::invokeMethod(worker, [this] {
        if (worker) {
            worker->start();
        }
    }, Qt::QueuedConnection);
    return true;
}

void AiProctorMonitor::stopWorker()
{
    if (!workerThread) {
        worker = nullptr;
        return;
    }
    if (worker) {
        QMetaObject::invokeMethod(worker, [this] {
            if (worker) {
                worker->stop();
            }
        }, Qt::BlockingQueuedConnection);
    }
    workerThread->quit();
    workerThread->wait(2000);
    workerThread->deleteLater();
    workerThread = nullptr;
    worker = nullptr;
}

QString AiProctorMonitor::bridgeScriptPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("../tools/face_recognition_bridge.py")),
        QDir(appDir).filePath(QStringLiteral("../../tools/face_recognition_bridge.py")),
        QDir(appDir).filePath(QStringLiteral("tools/face_recognition_bridge.py")),
        QDir::current().filePath(QStringLiteral("tools/face_recognition_bridge.py"))
    };
#ifdef Q_OS_WIN

#endif
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

QString AiProctorMonitor::cameraBridgeScriptPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("../tools/camera_capture_bridge.py")),
        QDir(appDir).filePath(QStringLiteral("../../tools/camera_capture_bridge.py")),
        QDir(appDir).filePath(QStringLiteral("tools/camera_capture_bridge.py")),
        QDir::current().filePath(QStringLiteral("tools/camera_capture_bridge.py"))
    };
#ifdef Q_OS_WIN

#endif
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}
