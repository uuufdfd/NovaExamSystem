#pragma once

#include "database.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>

enum class AbnormalType
{
    NoFace,
    MultiFace,
    FaceMismatch,
    ScreenSwitch,     // 窗口失焦 / 切屏（focus_lost）
    TimeAbnormal,
    FullscreenExit,   // 退出全屏
    Screenshot,       // 考试期间截屏（截图工具启动 / 截屏文件落盘）
    CameraMissing,    // 摄像头设备缺失
    CameraError,      // 摄像头启动失败
    SimulationMode,   // 启用模拟监考模式（仅留痕，不加作弊分）
    SimulationVerify, // 模拟人脸核验通过（仅留痕，不加作弊分）
    NetworkError,     // 网络异常但继续答题（仅留痕，不判作弊）
    Unknown
};

struct RiskResult
{
    int totalScore = 0;
    QString riskLevel = QStringLiteral("低风险");
    QString message;
};

class RiskScoreManager
{
public:
    explicit RiskScoreManager(Database *database = nullptr);

    void setDatabase(Database *database);
    QJsonObject addAbnormalEvent(int examId,
                                 int studentId,
                                 AbnormalType type,
                                 const QString &description,
                                 QString *error);

    static int getScoreByType(AbnormalType type);
    static QString getRiskLevel(int score);
    static QString abnormalTypeToString(AbnormalType type);
    static QString abnormalTypeName(AbnormalType type);
    static AbnormalType abnormalTypeFromString(const QString &type);

private:
    QString cooldownKey(int examId, int studentId, AbnormalType type) const;

    Database *database = nullptr;
    QHash<QString, QDateTime> lastEventTimes;
    int cooldownSeconds = 60;
};
