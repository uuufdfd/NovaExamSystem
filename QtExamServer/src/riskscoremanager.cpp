#include "riskscoremanager.h"

RiskScoreManager::RiskScoreManager(Database *database)
    : database(database)
{
}

void RiskScoreManager::setDatabase(Database *database)
{
    this->database = database;
}

QJsonObject RiskScoreManager::addAbnormalEvent(int examId,
                                               int studentId,
                                               AbnormalType type,
                                               const QString &description,
                                               QString *error)
{
    QJsonObject result;
    if (!database) {
        if (error) {
            *error = QStringLiteral("风险评分模块未初始化。");
        }
        return result;
    }
    if (type == AbnormalType::Unknown) {
        if (error) {
            *error = QStringLiteral("未知异常类型。");
        }
        return result;
    }

    const QString key = cooldownKey(examId, studentId, type);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime lastTime = lastEventTimes.value(key);
    if (lastTime.isValid() && lastTime.secsTo(now) < cooldownSeconds) {
        result = database->currentRisk(examId, studentId, error);
        if (error && !error->isEmpty()) {
            return {};
        }
        result.insert(QStringLiteral("addedScore"), 0);
        result.insert(QStringLiteral("abnormalType"), abnormalTypeToString(type));
        result.insert(QStringLiteral("message"), QStringLiteral("已记录同类异常，冷却期内不重复加分。"));
        return result;
    }

    if (!database->addRiskEvent(examId,
                                studentId,
                                abnormalTypeToString(type),
                                getScoreByType(type),
                                description,
                                error,
                                &result)) {
        return {};
    }

    lastEventTimes.insert(key, now);
    result.insert(QStringLiteral("abnormalType"), abnormalTypeToString(type));
    result.insert(QStringLiteral("message"), QStringLiteral("已记录考试环境异常，请保持本人在摄像头前并专注答题。"));
    return result;
}

int RiskScoreManager::getScoreByType(AbnormalType type)
{
    // 分值规则（Stage 2 统一为渐进式累计模型，0-29 低 / 30-59 中 / 60+ 高）。
    // 模拟模式与网络异常只留痕、不加作弊分（返回 0）。
    switch (type) {
    case AbnormalType::NoFace:
        return 5;
    case AbnormalType::MultiFace:
        return 10;
    case AbnormalType::FaceMismatch:
        return 8;
    case AbnormalType::ScreenSwitch:   // 窗口失焦 / 切屏
        return 6;
    case AbnormalType::FullscreenExit: // 退出全屏
        return 6;
    case AbnormalType::Screenshot:     // 考试期间截屏：主动取证行为，权重高于一般切屏
        return 12;
    case AbnormalType::CameraMissing:  // 摄像头设备缺失
        return 10;
    case AbnormalType::CameraError:    // 摄像头启动失败
        return 10;
    case AbnormalType::TimeAbnormal:
        return 10;
    case AbnormalType::SimulationMode:    // 仅留痕
        return 0;
    case AbnormalType::SimulationVerify:  // 仅留痕
        return 0;
    case AbnormalType::NetworkError:      // 仅留痕
        return 0;
    default:
        return 0;
    }
}

QString RiskScoreManager::getRiskLevel(int score)
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

QString RiskScoreManager::abnormalTypeToString(AbnormalType type)
{
    switch (type) {
    case AbnormalType::NoFace:
        return QStringLiteral("NO_FACE");
    case AbnormalType::MultiFace:
        return QStringLiteral("MULTI_FACE");
    case AbnormalType::FaceMismatch:
        return QStringLiteral("FACE_MISMATCH");
    case AbnormalType::ScreenSwitch:
        return QStringLiteral("SCREEN_SWITCH");
    case AbnormalType::TimeAbnormal:
        return QStringLiteral("TIME_ABNORMAL");
    case AbnormalType::FullscreenExit:
        return QStringLiteral("FULLSCREEN_EXIT");
    case AbnormalType::Screenshot:
        return QStringLiteral("SCREENSHOT");
    case AbnormalType::CameraMissing:
        return QStringLiteral("CAMERA_MISSING");
    case AbnormalType::CameraError:
        return QStringLiteral("CAMERA_ERROR");
    case AbnormalType::SimulationMode:
        return QStringLiteral("SIMULATION_MODE");
    case AbnormalType::SimulationVerify:
        return QStringLiteral("SIMULATION_VERIFY");
    case AbnormalType::NetworkError:
        return QStringLiteral("NETWORK_ERROR");
    default:
        return QStringLiteral("UNKNOWN");
    }
}

QString RiskScoreManager::abnormalTypeName(AbnormalType type)
{
    switch (type) {
    case AbnormalType::NoFace:
        return QStringLiteral("无人脸超过 10 秒");
    case AbnormalType::MultiFace:
        return QStringLiteral("出现多人脸");
    case AbnormalType::FaceMismatch:
        return QStringLiteral("人脸不匹配");
    case AbnormalType::ScreenSwitch:
        return QStringLiteral("切屏 / 窗口失焦");
    case AbnormalType::TimeAbnormal:
        return QStringLiteral("考试时间异常");
    case AbnormalType::FullscreenExit:
        return QStringLiteral("退出全屏");
    case AbnormalType::Screenshot:
        return QStringLiteral("考试期间截屏");
    case AbnormalType::CameraMissing:
        return QStringLiteral("摄像头设备缺失");
    case AbnormalType::CameraError:
        return QStringLiteral("摄像头启动失败");
    case AbnormalType::SimulationMode:
        return QStringLiteral("启用模拟监考模式（演示）");
    case AbnormalType::SimulationVerify:
        return QStringLiteral("模拟人脸核验通过（演示）");
    case AbnormalType::NetworkError:
        return QStringLiteral("网络异常但继续答题");
    default:
        return QStringLiteral("未知异常");
    }
}

AbnormalType RiskScoreManager::abnormalTypeFromString(const QString &type)
{
    const QString normalized = type.trimmed().toUpper();
    if (normalized == QStringLiteral("NO_FACE")) {
        return AbnormalType::NoFace;
    }
    if (normalized == QStringLiteral("MULTI_FACE")) {
        return AbnormalType::MultiFace;
    }
    if (normalized == QStringLiteral("FACE_MISMATCH")) {
        return AbnormalType::FaceMismatch;
    }
    if (normalized == QStringLiteral("SCREEN_SWITCH") || normalized == QStringLiteral("FOCUS_LOST")) {
        return AbnormalType::ScreenSwitch;
    }
    if (normalized == QStringLiteral("TIME_ABNORMAL")) {
        return AbnormalType::TimeAbnormal;
    }
    if (normalized == QStringLiteral("FULLSCREEN_EXIT")) {
        return AbnormalType::FullscreenExit;
    }
    if (normalized == QStringLiteral("SCREENSHOT")) {
        return AbnormalType::Screenshot;
    }
    if (normalized == QStringLiteral("CAMERA_MISSING")) {
        return AbnormalType::CameraMissing;
    }
    if (normalized == QStringLiteral("CAMERA_ERROR")) {
        return AbnormalType::CameraError;
    }
    if (normalized == QStringLiteral("SIMULATION_MODE")) {
        return AbnormalType::SimulationMode;
    }
    if (normalized == QStringLiteral("SIMULATION_VERIFY")) {
        return AbnormalType::SimulationVerify;
    }
    if (normalized == QStringLiteral("NETWORK_ERROR")) {
        return AbnormalType::NetworkError;
    }
    return AbnormalType::Unknown;
}

QString RiskScoreManager::cooldownKey(int examId, int studentId, AbnormalType type) const
{
    return QStringLiteral("%1:%2:%3")
        .arg(examId)
        .arg(studentId)
        .arg(abnormalTypeToString(type));
}
