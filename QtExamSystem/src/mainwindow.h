#pragma once

#include "accountstore.h"
#include "excelimporter.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QDateTime>
#include <QMap>
#include <QPair>
#include <QVector>

#include <functional>

class AiProctorMonitor;
class QCloseEvent;
class QComboBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class QEvent;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QTabBar;
class QTableWidget;
class QTextEdit;
class QTimer;
class QUrl;
class QVBoxLayout;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const UserAccount &account, QWidget *parent = nullptr);
    ~MainWindow() override;

    // main.cpp 据此判断窗口关闭后是回到登录页（切换账号）还是退出程序。
    bool isLogoutRequested() const { return m_logoutRequested; }

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createSidebar();
    QWidget *createDashboardPage();
    QWidget *createExamPage();
    QWidget *createGradingPage();
    QWidget *createQuestionPage();
    void refreshQuestionCategoryCounts();
    QWidget *createPublishPage();
    QWidget *createStatsPage();
    QWidget *createUserManagementPage();
    // 管理员端：学生管理 / 教师管理（按角色过滤的用户管理页，复用 list_users + 用户CRUD协议）。
    QWidget *createUserAdminPage(const QString &roleCode, const QString &pageTag,
                                 const QString &title, const QString &subtitle,
                                 QTableWidget **tableOut);
    void adminPopulateRoleUsers(QTableWidget *table, const QString &roleCode);
    void adminAddUser(const QString &roleCode, const QString &pageTag);
    void adminEditUser(QTableWidget *table, const QString &pageTag);
    void adminDeleteUser(QTableWidget *table, const QString &pageTag);
    void adminResetPassword(QTableWidget *table, const QString &pageTag);
    void refreshAdminUserTables();
    // 管理员端：考试监控（list_exams 只读）+ 系统日志（list_risk_records 只读）。
    QWidget *createAdminExamMonitorPage();
    void refreshAdminExamMonitor();
    QWidget *createAdminLogsPage();
    void refreshAdminLogs();

    void createMenus();
    void switchContentPage(QWidget *targetPage, const QString &label);
    void updateNavHighlight(QPushButton *activeButton);
    void triggerNav(QPushButton *button, QWidget *page, const QString &label, const QString &source);
    // 按 navButtons/navPages 下标直接切页（eventFilter 命中 navButton 时调用，不依赖 clicked 信号）。
    void triggerNav(int index);
    void bindTeacherButton(QPushButton *button,
                           const QString &objectName,
                           const QString &page,
                           const QString &action,
                           std::function<void()> handler);
    void discoverTeacherButtons(QWidget *page, const QString &pageName) const;
    // 阶段2A：普通按钮动作注册表（最小验证版）。
    void registerButtonAction(QPushButton *button, const QString &page,
                              const QString &name, std::function<void()> handler);
    // 临时对话框按钮专用：关窗后立即移除注册，避免 QHash 残留已析构按钮指针。
    void unregisterButtonAction(QPushButton *button);
    bool dispatchRegisteredButtonAction(QPushButton *button);
    // P1 步骤3：把任意 QTableWidget 当前可见数据导出为 CSV（UTF-8 带 BOM，Excel/WPS 中文不乱码）。
    bool simpleHttpPostJsonViaSocket(const QUrl &url,
                                     const QByteArray &jsonBody,
                                     QByteArray *responseBody,
                                     QString *errorMessage);
    QByteArray tableToCsvBytes(QTableWidget *table) const;
    bool exportCsvViaWindowsHostPicker(const QByteArray &csvBytes,
                                       const QString &defaultFileName,
                                       QString *savedPath,
                                       QString *errorMessage,
                                       bool *cancelled);
    bool exportTableToCsv(QTableWidget *table, const QString &defaultFileName, const QString &title);
    void requestLogout();
    void refreshInitialData();

    void refreshQuestionBank();
    void refreshQuestionBankPage();
    void fillQuestionTables();
    void fillQuestionManageTable();
    void updateQuestionPageControls();
    void goQuestionPage(int delta);
    void addQuestionRow(const QuestionRecord &question);
    void importQuestions();
    bool importQuestionsViaHostPicker();
    void importQuestionsLocal();
    bool importQuestionFile(const QString &filePath);
    void importQuestionsFromWindowsProxy();
    void importExamFromFile();
    // 通用 Windows 宿主机文件选择：mode=question_bank/full_exam，文件经 base64 拉回 /tmp/qtexam_imports。
    // env-empty / request-failed 不弹框（由调用方决定兜底策略），其余失败在函数内提示。
    bool pickFileFromWindowsHost(const QString &mode, QString *localPath,
                                 QString *failReason = nullptr, QString *failDetail = nullptr);
    // 整套试卷解析/确认/上传/建卷流程（importExamFromFile 与本机备用入口共用）。
    void importExamFile(const QString &filePath);
    // 显式备用入口（仅文件菜单）：从麒麟本机/共享目录选整套试卷。
    void importExamFromFileLocal();
    void createQuestion();
    void editSelectedQuestion();
    void deleteSelectedQuestion();
    void detectDuplicateQuestions();
    bool fetchAllQuestionsForDuplicateCheck(QJsonArray *questions, QString *error);
    bool filterImportedDuplicateQuestions(const QList<QuestionRecord> &confirmed,
                                          QList<QuestionRecord> *filtered);
    bool confirmImportedQuestions(const QList<QuestionRecord> &imported,
                                  const QStringList &warnings,
                                  QList<QuestionRecord> *confirmed);
    void uploadQuestions(const QList<QuestionRecord> &items);
    bool uploadQuestions(const QList<QuestionRecord> &items,
                         QJsonArray *questionIds,
                         QString *error);
    void openQuestionTemplate();
    void refreshQuestionCount();
    bool questionMatchesTypeCategory(const QString &rawType, const QString &selectedCategory) const;
    QJsonArray filteredQuestionObjects(bool includeTypeFilter = true) const;
    void filterQuestions();
    void resetQuestionFilters();
    QJsonArray selectedPublishQuestionIds() const;
    void createExamDraft();
    void publishSelectedExam();
    void publishExamById(int examId, const QString &publishStatus);
    void updatePublishExamButton();
    void refreshExamManagementList();
    void closeSelectedExam();
    void deleteSelectedExam();

    void refreshStudentExams();
    void loadSelectedExam();
    void renderExamQuestions(const QJsonArray &questions);
    QJsonArray collectStudentAnswers() const;
    void applyAnswersToUi(const QJsonArray &answers);
    void updateAnswerNavStates();
    void updateExamCountdown();
    // 阶段A：倒计时自减驱动（麒麟 PMF 失效兜底——新式 connect 失败/静默则切 singleShot 自调度链）。
    void startExamCountdown(int seconds);
    void stopExamCountdown();
    void onExamTick();
    void scheduleExamTickSingleShot();
    void setCurrentStudentQuestion(int index);
    // 阶段A：答题卡统一跳题入口（保存当前答案→存草稿→滚动到目标题→刷新答题卡状态）。
    void jumpToQuestion(int index);
    void moveStudentQuestion(int offset);
    void saveDraft();
    void submitExam();
    void submitExamInternal(bool autoSubmit);

    // Stage 4 本地草稿缓存（断网保护）。隐藏答案框始终是答案唯一真相源。
    QString localDraftDir() const;
    QString localDraftFilePath(int examId, int studentId) const;
    void saveLocalDraft();
    void scheduleLocalDraftSave();
    void maybeRestoreLocalDraft(int examId);
    void clearLocalDraft(int examId, int studentId);
    void setDraftStatus(const QString &text, const QString &state = QStringLiteral("normal"));
    void refreshMySubmissions();
    void handleApplicationStateChanged(Qt::ApplicationState state);
    bool ensureStudentFaceProfile();
    bool enrollFaceProfile();
    bool verifyFaceProfile(const QString &faceTemplate);
    void recordAbnormalEvent(const QString &abnormalType, const QString &description);
    void updateRiskHint(const QJsonObject &risk);
    void onAiFaceResult(bool hasFace, int faceCount, bool faceMatched);
    void startAiProctor();
    void stopAiProctor();
    void setAiProctorStatus(const QString &text, const QString &state = QStringLiteral("normal"));
    QString aiBaselinePath() const;

    void populateSubmissionList();
    void setSubmission(int index);
    void loadSubmissionDetail(int submissionId);
    void renderGradingAnswers(bool lockedByMe);
    void lockCurrentSubmission();
    void releaseCurrentSubmission();
    void submitCurrentGrade();

    void populateAccountTable();
    void populateAccountEditorFromSelection();
    void clearAccountEditor();
    void createUser();
    void applyAccountChanges();
    void deleteSelectedUser();

private slots:
    // 账号权限页“硬触发”槽：三层保险（clicked / 注册表 / legacy SIGNAL-SLOT）统一汇入，槽内自带去重。
    void onAdminAccountApplyClicked();
    void onAdminAccountDisableClicked();

private:
    void changeOwnPassword();

    void loadStats();
    void refreshRiskRecords();
    void refreshRiskExamFilter();
    void updateRiskMetricCards(int abnormalStudents, int totalEvents, int midCount, int highCount, int severeCount);
    void populateEventTypeStats(const QMap<QString, QPair<int, int>> &typeStats);
    void showRiskLogsForRow(int row, int column);
    // P1 步骤3：成绩页「查看选中详情」按钮入口——读取风险表当前选中行并复用 showRiskLogsForRow，
    // 不依赖麒麟上不稳定的 QTableWidget 双击信号。
    void showSelectedRiskRecordDetail();
    void showError(const QString &title, const QString &error);
    void showToast(const QString &message, bool success = true);
    void setStyle();

    UserAccount currentUser;
    QLabel *titleLabel = nullptr;
    QLabel *questionCountLabel = nullptr;
    QLabel *submissionTitleLabel = nullptr;
    QLabel *submissionStateLabel = nullptr;
    QLabel *studentExamTitleLabel = nullptr;
    QLabel *studentExamStateLabel = nullptr;
    QLabel *studentRiskStatusLabel = nullptr;
    QLabel *studentRiskLevelLabel = nullptr;
    QLabel *aiProctorStatusLabel = nullptr;
    QLabel *accountCountLabel = nullptr;
    QLabel *dashboardLabel = nullptr;
    QLabel *statsLabel = nullptr;
    QVector<QLabel *> dashboardMetricLabels;
    // 右侧唯一内容容器：单容器动态替换页面。
    QWidget *contentHost = nullptr;
    QVBoxLayout *contentLayout = nullptr;
    QWidget *currentContentPage = nullptr;
    QList<QPushButton *> navButtons;
    QList<QWidget *> navPages;
    // 直连页面指针：导航按钮绑定这些 QWidget*，点击时加入 contentLayout 显示。
    QWidget *pageDashboard = nullptr;
    QWidget *pageExam = nullptr;
    QWidget *pageGrading = nullptr;
    QWidget *pageQuestion = nullptr;
    QWidget *pagePublish = nullptr;
    QWidget *pageStats = nullptr;
    QWidget *pageUsers = nullptr;
    // 管理员端新增页面（仅 admin 角色创建）。
    QWidget *pageAdminStudents = nullptr;
    QWidget *pageAdminTeachers = nullptr;
    QWidget *pageAdminExamMonitor = nullptr;
    QWidget *pageAdminLogs = nullptr;
    QTableWidget *studentMgmtTable = nullptr;
    QTableWidget *teacherMgmtTable = nullptr;
    QTableWidget *adminExamMonitorTable = nullptr;
    QTableWidget *adminLogsTable = nullptr;
    QPushButton *currentNavButton = nullptr;
    // 阶段2A：普通按钮动作注册表（按钮指针 → 业务）。
    // 防重入用“同按钮在飞”集合而非全局布尔：全局布尔会把注册表 handler 内打开的
    // 模态框里的注册按钮（如导入预览“确认上传”）一并拦成 reentrant 吞掉。
    struct ButtonAction {
        QString page;
        QString name;
        std::function<void()> handler;
    };
    QHash<QPushButton *, ButtonAction> m_buttonActions;
    QSet<QPushButton *> m_activeButtonActions;
    bool m_logoutRequested = false;       // 点击“退出登录”置位，main.cpp 据此回登录页
    bool m_previewLogged = false;         // 每场监考只打一次 [CAMERA_PREVIEW] 首帧日志
    bool m_proctorRunning = false;        // 监考是否正在运行（真实或模拟），避免预览文字覆盖画面
    QLabel *studentProctorPreview = nullptr;  // 学生考试界面摄像头预览框

    QTableWidget *questionTable = nullptr;
    QTableWidget *publishQuestionTable = nullptr;
    QLabel *questionPageLabel = nullptr;
    QPushButton *questionPrevPageButton = nullptr;
    QPushButton *questionNextPageButton = nullptr;
    QTableWidget *examManagementTable = nullptr;
    QLineEdit *questionSearchEdit = nullptr;
    QTabBar *questionTypeTabs = nullptr;
    QComboBox *questionDifficultyFilter = nullptr;
    QLineEdit *publishNameEdit = nullptr;
    QLineEdit *publishCourseEdit = nullptr;
    QDateTimeEdit *publishStartEdit = nullptr;
    QDateTimeEdit *publishEndEdit = nullptr;
    QSpinBox *publishDurationSpin = nullptr;
    QSpinBox *publishMaxSubmissionsSpin = nullptr;
    QPushButton *publishSelectedExamButton = nullptr;

    QListWidget *studentExamList = nullptr;
    QScrollArea *studentQuestionScroll = nullptr;
    QVBoxLayout *studentQuestionLayout = nullptr;
    QGridLayout *studentQuestionNavGrid = nullptr;
    QLabel *studentCountdownLabel = nullptr;
    QLabel *studentActionHintLabel = nullptr;
    QLabel *draftStatusLabel = nullptr;
    QTimer *localDraftDebounceTimer = nullptr;
    QTimer *localDraftAutosaveTimer = nullptr;
    QTableWidget *mySubmissionTable = nullptr;
    QTableWidget *riskRecordTable = nullptr;
    QTableWidget *eventTypeStatTable = nullptr;
    QComboBox *riskExamFilter = nullptr;
    QVector<QLabel *> riskMetricLabels;
    QLabel *riskBoardHintLabel = nullptr;
    QTimer *riskRefreshTimer = nullptr;
    QVector<QTextEdit *> studentAnswerEdits;
    QVector<QPushButton *> studentQuestionButtons;
    QVector<QWidget *> studentQuestionWidgets;
    QJsonArray currentExamQuestions;
    QDateTime currentExamStartTime;
    QDateTime currentExamEndTime;
    QDateTime noFaceStartTime;
    QDateTime faceMismatchStartTime;
    bool fiveMinuteAlerted = false;
    QPushButton *previousQuestionButton = nullptr;
    QPushButton *nextQuestionButton = nullptr;
    QPushButton *startAiProctorButton = nullptr;
    QPushButton *stopAiProctorButton = nullptr;
    QPushButton *simulateProctorButton = nullptr;
    AiProctorMonitor *aiProctorMonitor = nullptr;
    QString currentStudentFaceTemplate;
    int currentExamId = 0;
    int currentQuestionIndex = -1;
    // 阶段A：倒计时自减模型 + 麒麟可靠 tick 驱动状态。
    int remainingSeconds = 0;                 // 剩余秒数，每秒自减，落进本地草稿
    bool examCountdownActive = false;         // 倒计时是否在运行
    bool examCountdownViaSingleShot = false;  // true=singleShot 自调度链驱动（兜底路径）
    int examTickGeneration = 0;               // 自增以失效旧的 singleShot 链，杜绝一秒多跳
    qint64 examTickObserved = 0;              // 已观察到的 tick 数（看门狗判断 timer 是否真在跳）
    int examAutosaveCounter = 0;              // tick 计数，每 5 次自动落一次草稿
    int draftDebounceGeneration = 0;          // 答案改动防抖保存的合并代号
    int screenSwitchCount = 0;

    QListWidget *submissionList = nullptr;
    QComboBox *submissionStatusFilter = nullptr;
    QTableWidget *gradingAnswerTable = nullptr;
    QScrollArea *gradingQuestionScroll = nullptr;
    QVBoxLayout *gradingQuestionLayout = nullptr;
    QTextEdit *teacherCommentEdit = nullptr;
    QDoubleSpinBox *scoreSpinBox = nullptr;
    QPushButton *lockSubmissionButton = nullptr;
    QPushButton *releaseSubmissionButton = nullptr;
    QPushButton *submitGradeButton = nullptr;
    QVector<QLineEdit *> gradingScoreEdits;
    QVector<QTextEdit *> gradingCommentEdits;
    int currentSubmissionId = 0;

    QTableWidget *accountTable = nullptr;
    QComboBox *accountRoleFilter = nullptr;
    QLineEdit *accountSearchEdit = nullptr;
    QLabel *accountSelectedLabel = nullptr;
    QLineEdit *accountDisplayNameEdit = nullptr;
    QLineEdit *accountNoteEdit = nullptr;
    QLineEdit *newPasswordEdit = nullptr;
    QLineEdit *confirmPasswordEdit = nullptr;
    int selectedAccountUserId = 0;
    QString selectedAccountUsername;
    QString selectedAccountRole;

    QJsonArray questionObjects;
    QJsonArray questionManagePageObjects;
    int questionBankPage = 1;
    int questionBankPageSize = 20;
    int questionBankTotal = 0;
    int questionBankTotalPages = 1;
    QJsonArray studentExamObjects;
    QJsonArray currentSubmissionAnswers;
    QTimer *examTimer = nullptr;
};
