#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// 一道题的导入中间表示：与题库模板列一一对应（题型/课程/题干/四选项/答案/分值/难度/解析）。
// 全部用 QString 承载（含分值）：导入阶段保留原文便于预览人工修正，入库前才做数值化校验。
struct QuestionRecord
{
    QString type;
    QString course;
    QString content;
    QString optionA;
    QString optionB;
    QString optionC;
    QString optionD;
    QString answer;
    QString score;
    QString difficulty;
    QString analysis;
};

// 一次导入的完整结果：success=false 时 error 给用户看；
// warnings 收集“能导入但可疑”的行级问题（如分值为空），在预览对话框里集中展示。
struct ImportResult
{
    bool success = false;
    QString error;
    QStringList warnings;
    QList<QuestionRecord> questions;
};

// 题库/试卷文件解析器：按扩展名分派到 CSV / XLSX / DOCX 三种解析实现（策略分派）。
// 纯静态无状态，不依赖 Qt 之外的库——xlsx/docx 直接按 ZIP+XML 手工解析，避免引第三方依赖。
class ExcelImporter
{
public:
    // 主入口：按扩展名分派；不支持的格式返回 success=false + 中文原因。
    static ImportResult importFile(const QString &filePath);
    // Word 整篇作为一道大题导入（试卷场景的特殊模式，与逐行解析互补）。
    static ImportResult importDocxAsSingleQuestion(const QString &filePath);

private:
    static ImportResult importCsv(const QString &filePath);
    static ImportResult importXlsx(const QString &filePath);
    static ImportResult importDocx(const QString &filePath);
    // CSV 解析：处理引号转义/逗号包含/跨行字段，输出规整的二维表。
    static QList<QStringList> readCsvRows(const QString &text);
    // 二维表 → 题目列表的公共收口：表头识别、宽松列匹配、行级校验与 warnings 归集。
    static ImportResult rowsToQuestions(const QList<QStringList> &rows);
};
