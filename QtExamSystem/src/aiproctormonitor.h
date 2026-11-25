#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QCameraDevice>
#include <QList>
#include <QMediaCaptureSession>
#include <QObject>
#include <QString>
#include <atomic>

class QCamera;
class QProcess;
class QThread;
class QTimer;
class QVideoSink;
class FacePipelineWorker;

// AI 监考引擎：摄像头采集 + 人脸检测/比对（YuNet+SFace ONNX）+ 预览帧分发。
// 采集链路按平台自动选择（设备驱动抽象层）：
//   麒麟 → QProcess 拉起 Python camera bridge 子进程（跨进程通信，规避 Qt 摄像头栈在麒麟的兼容问题）；
//   Windows → Qt Multimedia QCamera/QVideoSink；
//   另有 OpenCV 采集线程路径（QThread::create）作为编译开关备选。
// 识别在独立 worker 线程跑（FacePipelineWorker），不阻塞 UI；
// 结果经信号回 MainWindow：状态文字、人脸结果（数量/比对）、预览帧、运行状态。
// 模拟模式（startSimulation）：无摄像头环境的演示通道，事件照常落库但风险分记 0。
class AiProctorMonitor : public QObject
{
    Q_OBJECT

public:
    explicit AiProctorMonitor(QObject *parent = nullptr);
    ~AiProctorMonitor() override;

    // 启动真实监考：baselinePath 为考生人脸底库路径；失败原因经 statusChanged 与
    // lastStartMissingDevice() 区分“无设备”与“设备打开失败”。
    bool start(const QString &baselinePath, int cameraIndex = 0);
    // 启动模拟监考（演示留痕，0 风险分），与真实监考互斥。
    bool startSimulation();
    // 停止监考：终止子进程/线程/相机，幂等可重复调用（退出登录、交卷、析构都会调）。
    void stop();
    bool isRunning() const;
    bool isSimulationMode() const;
    // 上一次 start() 失败是否因为"未检测到摄像头设备"（区分 camera_missing 与 camera_error）。
    bool lastStartMissingDevice() const;
    // 设置/读取人脸底库模板（base64 特征向量，来自服务端 user_face_profiles）。
    void setBaselineTemplate(const QString &faceTemplate);
    QString currentFaceTemplate() const;
    bool hasCurrentFace() const;

signals:
    // 状态文字 + 语义级别（normal/warning/error），MainWindow 直接上屏。
    void statusChanged(const QString &text, const QString &state);
    // 每帧识别结论：是否有脸 / 脸数 / 与底库是否匹配 / 置信度——风险引擎据此计异常。
    void faceResult(bool hasFace, int faceCount, bool faceMatched, double confidence);
    // 预览帧（已转 QImage），学生端考试页的实时画面来源。
    void previewReady(const QImage &image);
    void runningChanged(bool running);

private slots:
    // 这些排水/收尾函数必须声明为“真槽”而非普通成员函数：麒麟系统 Qt(5.15.19,
    // reduce_relocations/-Bsymbolic 链接) 上，应用侧对 Qt 库类信号做新式函数指针
    // connect 时跨库指针比较失败（日志表现为 "QObject::connect: signal not found in
    // QProcess/QTimer..."，connect 返回无效连接），需老式 SIGNAL/SLOT 字符串连接兜底，
    // 而字符串连接只查 moc 槽表、不比较函数指针。
    // 麒麟主路径：Python camera bridge 子进程的 stdout/stderr 排水（帧数据按行传输）。
    void handleCameraOutput();
    void handleCameraError();
    // 兜底排水轮询 tick（outputPollTimer 的槽）：计数 + 限频日志 + 双通道排水。
    void handleCameraPollTick();
    // bridge 子进程退出收尾：先排干残余输出，再停轮询、无帧时上报错误。
    void handleCameraProcessFinished(int exitCode);
    // 模拟监考节拍：绘制假帧并发布（同受麒麟 connect 问题影响，需可被字符串连接）。
    void handleSimulationTick();

private:
    // worker 线程（人脸识别管线）的启停。
    bool startWorker();
    void stopWorker();
    // 麒麟主路径：Python camera bridge 子进程的启停（排水槽见上方 private slots）。
    bool startPythonOpenCvCamera(int cameraIndex);
    void stopPythonOpenCvCamera();
    // singleShot 自调度排水链（最终兜底）：QTimer::singleShot(ms, this, functor) 在
    // QtCore 内部直接回调 functor，全程不做跨库信号索引解析，在新式/老式 connect
    // 都不可用的环境下依然可靠（本机 9 秒看门狗 singleShot 已被日志证实可触发）。
    void scheduleDrainTick();
    QString cameraBridgeScriptPath() const;
    // OpenCV 直采路径（编译开关备选）：独立线程循环抓帧。
    bool startOpenCvCamera(int cameraIndex);
    void stopOpenCvCamera();
    void captureOpenCvLoop(int cameraIndex);
    // Windows Qt Multimedia 路径：打开指定下标相机，失败时调度回退。
    bool openCameraAt(int cameraIndex);
    void closeCameraOnly();
    void scheduleCameraFallback(int cameraIndex);
    QString bridgeScriptPath() const;

    QCamera *camera = nullptr;                  // Qt Multimedia 相机（Windows 路径）
    QVideoSink *videoSink = nullptr;            // 帧出口：videoFrameChanged → 预览+识别
    QProcess *cameraProcess = nullptr;          // Python bridge 子进程（麒麟路径）
    QThread *opencvCameraThread = nullptr;      // OpenCV 采集线程（备选路径）
    QThread *workerThread = nullptr;            // 人脸识别 worker 所在线程
    FacePipelineWorker *worker = nullptr;
    QMediaCaptureSession captureSession;
    QByteArray cameraProcessBuffer;             // 子进程 stdout 行缓冲（帧协议按行分包）
    QList<QCameraDevice> availableCameras;
    QString baselineTemplate;                   // 人脸底库特征（base64）
    QString currentBaselinePath;
    bool cameraFrameSeen = false;               // 收到首帧前用于黑屏诊断
    int activeCameraIndex = -1;
    std::atomic_bool opencvCameraStop{false};   // 采集线程退出旗标（跨线程，需原子）
    int pythonCameraFrameCount = 0;
    bool pythonCameraUsingUrl = false;          // true 表示 Python bridge 正在读取 QTEXAM_CAMERA_URL
    QString pythonCameraSource;                 // 当前传给 camera_capture_bridge.py 的 URL 或 /dev/video*
    int previewEmitCount = 0;                   // previewReady 实际发射次数（与帧计数区分）
    QElapsedTimer lastCameraPreviewTimer;       // 预览发射节流：距上次 emit >=80ms 才发，目标 8~12fps
    QElapsedTimer cameraBatchGapTimer;          // 两次 handleCameraOutput 的间隔：诊断主线程事件循环被阻塞
    QTimer *outputPollTimer = nullptr;          // 兜底排水轮询（持久成员，new QTimer(this)）：与 readyRead 双保险驱动 handleCameraOutput
    int outputPollTickCount = 0;                // 轮询 tick 计数：[DRAIN_TIMER] 限频日志（前 3 次 + 每 10 次）
    bool outputPollConnected = false;           // 轮询定时器连接是否建立成功（新式或老式任一成功；只在首次创建时连接）
    bool drainChainActive = false;              // singleShot 排水链开关：摄像头启动置 true，stopPythonOpenCvCamera 置 false
    quint64 drainChainTickCount = 0;            // 排水链 tick 计数：限频日志（前 3 次 + 每 20 次 ≈ 每 5 秒）
    QString lastCameraLog;
    bool running = false;
    bool simulationMode = false;
    bool cameraStartMissingDevice = false;
    int simulationFrameIndex = 0;
    QTimer *simulationTimer = nullptr;          // 模拟模式的假帧节拍器
};
