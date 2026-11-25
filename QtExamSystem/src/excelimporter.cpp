#include "excelimporter.h"

#include <QDebug>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QPair>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtGlobal>

namespace {

// 表头归一化：去首尾空白 → 小写 → 删除空格(含全角)/下划线/连字符/中英文冒号。
// 使“选项A”“选项 A”“option_a”“A：”等写法落到同一个 key，实现大小写与符号不敏感匹配。
QString normalizeHeader(QString value)
{
    value = value.trimmed().toLower();
    value.remove(' ');
    value.remove(QChar(0x3000));   // 全角空格
    value.remove('_');
    value.remove('-');
    value.remove(':');
    value.remove(QChar(0xFF1A));   // 中文全角冒号
    return value;
}

QString valueAt(const QStringList &row, int index)
{
    if (index < 0 || index >= row.size()) {
        return {};
    }
    return row.at(index).trimmed();
}

int columnIndexFromCellRef(const QString &cellRef)
{
    int result = 0;
    for (const QChar ch : cellRef) {
        if (!ch.isLetter()) {
            break;
        }
        result = result * 26 + ch.toUpper().unicode() - 'A' + 1;
    }
    return result - 1;
}

QString cellRef(const QDomElement &cell)
{
    return cell.attribute(QStringLiteral("r"));
}

QString joinedText(const QDomElement &element)
{
    QString text;
    for (const QString &tagName : {QStringLiteral("t"), QStringLiteral("w:t")}) {
        const QDomNodeList textNodes = element.elementsByTagName(tagName);
        for (int i = 0; i < textNodes.size(); ++i) {
            text += textNodes.at(i).toElement().text();
        }
    }
    return text;
}

QString cellText(const QDomElement &cell, const QStringList &sharedStrings)
{
    const QString type = cell.attribute(QStringLiteral("t"));
    if (type == QStringLiteral("inlineStr")) {
        const QDomElement inlineString = cell.firstChildElement(QStringLiteral("is"));
        return joinedText(inlineString).trimmed();
    }

    const QString value = cell.firstChildElement(QStringLiteral("v")).text();
    if (type == QStringLiteral("s")) {
        bool ok = false;
        const int index = value.toInt(&ok);
        if (ok && index >= 0 && index < sharedStrings.size()) {
            return sharedStrings.at(index).trimmed();
        }
    }

    if (type == QStringLiteral("b")) {
        return value == QStringLiteral("1") ? QStringLiteral("是") : QStringLiteral("否");
    }

    return value.trimmed();
}

bool loadXml(const QString &filePath, QDomDocument *document, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("无法打开文件：%1").arg(filePath);
        }
        return false;
    }

    QString parseError;
    int line = 0;
    int column = 0;
    if (!document->setContent(&file, &parseError, &line, &column)) {
        if (error) {
            *error = QStringLiteral("解析 XML 失败：%1，第 %2 行，第 %3 列").arg(parseError).arg(line).arg(column);
        }
        return false;
    }

    return true;
}

QStringList readSharedStrings(const QString &xlsxRoot)
{
    QDomDocument document;
    QString error;
    const QString path = QDir(xlsxRoot).filePath(QStringLiteral("xl/sharedStrings.xml"));
    if (!QFile::exists(path) || !loadXml(path, &document, &error)) {
        return {};
    }

    QStringList values;
    const QDomNodeList items = document.elementsByTagName(QStringLiteral("si"));
    for (int i = 0; i < items.size(); ++i) {
        values.append(joinedText(items.at(i).toElement()));
    }
    return values;
}

QString firstWorksheetPath(const QString &xlsxRoot)
{
    const QDir worksheetDir(QDir(xlsxRoot).filePath(QStringLiteral("xl/worksheets")));
    QStringList sheets = worksheetDir.entryList({QStringLiteral("sheet*.xml")}, QDir::Files, QDir::Name);
    if (sheets.isEmpty()) {
        return {};
    }
    return worksheetDir.filePath(sheets.first());
}

QList<QStringList> readWorksheetRows(const QString &sheetPath, const QStringList &sharedStrings, QString *error)
{
    QDomDocument document;
    if (!loadXml(sheetPath, &document, error)) {
        return {};
    }

    QList<QStringList> rows;
    const QDomNodeList rowNodes = document.elementsByTagName(QStringLiteral("row"));
    for (int i = 0; i < rowNodes.size(); ++i) {
        const QDomElement rowElement = rowNodes.at(i).toElement();
        QStringList row;

        QDomElement cell = rowElement.firstChildElement(QStringLiteral("c"));
        while (!cell.isNull()) {
            const int index = columnIndexFromCellRef(cellRef(cell));
            while (row.size() <= index) {
                row.append(QString());
            }
            row[index] = cellText(cell, sharedStrings);
            cell = cell.nextSiblingElement(QStringLiteral("c"));
        }

        rows.append(row);
    }
    return rows;
}

QString powerShellLiteral(QString value)
{
    value.replace('\'', "''");
    return value;
}

// 跨平台解压 docx/xlsx（本质都是 zip）。
// 关键修复：原实现只用 Windows PowerShell Expand-Archive，麒麟(Linux)无 powershell，
// 会导致 docx/xlsx 导入在部署平台上必然失败。现按平台给出候选解压器并逐个尝试：
//   Windows → PowerShell Expand-Archive；
//   Linux   → unzip（麒麟桌面通常自带）→ 回退 bsdtar → 回退 tar（libarchive 版可解 zip）。
// 任一成功即返回；全部失败时给出“已尝试哪些工具 + 最后错误”的具体原因（含安装提示）。
bool extractArchive(const QString &archiveFile, const QString &destinationDir, QString *error)
{
    QDir().mkpath(destinationDir);

    // 复制成 .zip 后缀的副本：部分解压器按扩展名判定格式，docx/xlsx 直接喂可能被拒。
    const QString zipFile = QDir(QFileInfo(destinationDir).absolutePath()).filePath(QStringLiteral("source.zip"));
    QFile::remove(zipFile);
    if (!QFile::copy(archiveFile, zipFile)) {
        if (error) {
            *error = QStringLiteral("无法复制文件到临时目录。");
        }
        return false;
    }

    // 候选解压命令（按优先级），逐个尝试直到有一个成功。
    QList<QPair<QString, QStringList>> candidates;
#ifdef Q_OS_WIN
    candidates.append({QStringLiteral("powershell"),
                       {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                        QStringLiteral("-Command"),
                        QStringLiteral("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                            .arg(powerShellLiteral(zipFile), powerShellLiteral(destinationDir))}});
#else
    candidates.append({QStringLiteral("unzip"), {QStringLiteral("-o"), QStringLiteral("-q"), zipFile, QStringLiteral("-d"), destinationDir}});
    candidates.append({QStringLiteral("bsdtar"), {QStringLiteral("-xf"), zipFile, QStringLiteral("-C"), destinationDir}});
    candidates.append({QStringLiteral("tar"), {QStringLiteral("-xf"), zipFile, QStringLiteral("-C"), destinationDir}});
#endif

    QStringList triedPrograms;
    QString lastError;
    for (const auto &candidate : candidates) {
        triedPrograms.append(candidate.first);
        QProcess process;
        process.start(candidate.first, candidate.second);
        if (!process.waitForStarted(5000)) {
            lastError = QStringLiteral("%1 不可用").arg(candidate.first);  // 程序缺失，换下一个候选
            continue;
        }
        if (!process.waitForFinished(30000)) {
            process.kill();
            process.waitForFinished(2000);
            lastError = QStringLiteral("%1 解压超时").arg(candidate.first);
            continue;
        }
        if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
            return true;  // 解压成功
        }
        lastError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (lastError.isEmpty()) {
            lastError = QStringLiteral("%1 退出码 %2").arg(candidate.first).arg(process.exitCode());
        }
    }

    if (error) {
#ifdef Q_OS_WIN
        *error = QStringLiteral("解压文件失败（已尝试：%1）：%2").arg(triedPrograms.join(QStringLiteral(", ")), lastError);
#else
        *error = QStringLiteral("解压文件失败（已尝试：%1）：%2\n如在麒麟/Linux 上，请先安装解压工具：sudo apt install unzip")
                     .arg(triedPrograms.join(QStringLiteral(", ")), lastError);
#endif
    }
    return false;
}

QString stripQuestionPrefix(QString line)
{
    line = line.trimmed();
    line.remove(QRegularExpression(QStringLiteral("^\\s*第?\\d+\\s*[题\\.、．\\)]\\s*")));
    line.remove(QRegularExpression(QStringLiteral("^\\s*\\d{1,3}\\s*[\\.、．\\)]\\s*")));
    line.remove(QRegularExpression(QStringLiteral("^\\s*[一二三四五六七八九十]+\\s*[、\\.．]\\s*")));
    return line.trimmed();
}

QString extractScore(const QString &line)
{
    const QRegularExpression scoreRegex(QStringLiteral("(?:每题|每空)?\\s*(\\d+(?:\\.\\d+)?)\\s*分"));
    const QRegularExpressionMatch match = scoreRegex.match(line);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString removeScoreText(QString line)
{
    line.remove(QRegularExpression(QStringLiteral("[（(]\\s*\\d+(?:\\.\\d+)?\\s*分\\s*[）)]")));
    return line.trimmed();
}

QString inferQuestionType(const QString &line, const QString &sectionType)
{
    const QString text = line + sectionType;
    if (text.contains(QStringLiteral("多选"))) {
        return QStringLiteral("多选题");
    }
    if (text.contains(QStringLiteral("单选")) || text.contains(QStringLiteral("单项选择"))) {
        return QStringLiteral("单选题");
    }
    if (text.contains(QStringLiteral("判断"))) {
        return QStringLiteral("判断题");
    }
    if (text.contains(QStringLiteral("填空"))) {
        return QStringLiteral("填空题");
    }
    if (text.contains(QStringLiteral("综合")) || text.contains(QStringLiteral("应用题")) || text.contains(QStringLiteral("案例"))) {
        return QStringLiteral("综合应用题");
    }
    if (text.contains(QStringLiteral("简答"))) {
        return QStringLiteral("简答题");
    }
    if (text.contains(QStringLiteral("编程")) || text.contains(QStringLiteral("程序")) || text.contains(QStringLiteral("代码"))
        || text.contains(QStringLiteral("算法")) || text.contains(QStringLiteral("数组")) || text.contains(QStringLiteral("函数")) || text.contains(QStringLiteral("Linux"))
        || text.contains(QStringLiteral("类")) || text.contains(QStringLiteral("实现"))) {
        return QStringLiteral("程序题");
    }
    if (!sectionType.isEmpty()) {
        return sectionType;
    }
    return QStringLiteral("简答题");
}

bool isSectionHeading(const QString &line)
{
    if (line.size() > 48) {
        return false;
    }
    const bool startsLikeSection = QRegularExpression(QStringLiteral("^\\s*[一二三四五六七八九十]+\\s*[、\\.．]")).match(line).hasMatch();
    const bool hasSectionKeyword = line.contains(QStringLiteral("单选")) || line.contains(QStringLiteral("单项选择"))
        || line.contains(QStringLiteral("多选")) || line.contains(QStringLiteral("判断"))
        || line.contains(QStringLiteral("填空")) || line.contains(QStringLiteral("简答"))
        || line.contains(QStringLiteral("综合")) || line.contains(QStringLiteral("应用题"))
        || line.contains(QStringLiteral("编程")) || line.contains(QStringLiteral("程序"));
    return startsLikeSection && hasSectionKeyword;
}

bool isCompositeSection(const QString &sectionType)
{
    return sectionType.contains(QStringLiteral("综合"))
        || sectionType.contains(QStringLiteral("案例"))
        || sectionType.contains(QStringLiteral("应用题"));
}

bool isQuestionStart(const QString &line, bool hasCurrentQuestion, const QString &sectionType)
{
    if (QRegularExpression(QStringLiteral("^\\s*第\\s*\\d+\\s*题")).match(line).hasMatch()) {
        return true;
    }

    if (QRegularExpression(QStringLiteral("^\\s*(\\d{1,3}\\s*[\\.、．\\)]|[一二三四五六七八九十]+\\s*[、\\.．])")).match(line).hasMatch()) {
        if (hasCurrentQuestion && isCompositeSection(sectionType)) {
            return false;
        }
        return true;
    }

    return !hasCurrentQuestion && !extractScore(line).isEmpty();
}

bool hasInlineOptions(const QString &line)
{
    return QRegularExpression(QStringLiteral("[A-Da-d]\\s*[\\.、．\\)]")).match(line).hasMatch();
}

bool isChoiceSection(const QString &type)
{
    return type.contains(QStringLiteral("单选")) || type.contains(QStringLiteral("多选")) || type.contains(QStringLiteral("选择"));
}

bool isJudgmentType(const QString &type)
{
    return type.contains(QStringLiteral("判断"));
}

bool isFillBlankType(const QString &type)
{
    return type.contains(QStringLiteral("填空"));
}

bool hasEmptyParentheses(const QString &line)
{
    return QRegularExpression(QStringLiteral("[（(]\\s*[）)]")).match(line).hasMatch();
}

bool hasFillBlankMarker(const QString &line)
{
    return hasEmptyParentheses(line)
        || QRegularExpression(QStringLiteral("_{2,}|＿{2,}|\\s{4,}")).match(line).hasMatch();
}

QString stripJudgmentMarker(QString line)
{
    line.remove(QRegularExpression(QStringLiteral("\\s*[（(]\\s*[）)]\\s*$")));
    return line.trimmed();
}

void normalizeImportedQuestion(QuestionRecord *question)
{
    if (!question) {
        return;
    }

    question->type = question->type.trimmed();
    question->content = question->content.trimmed();
    if (isJudgmentType(question->type)) {
        question->content = stripJudgmentMarker(question->content);
        if (question->optionA.trimmed().isEmpty()) {
            question->optionA = QStringLiteral("正确");
        }
        if (question->optionB.trimmed().isEmpty()) {
            question->optionB = QStringLiteral("错误");
        }
    }
}

bool shouldSplitStandaloneLine(const QString &line, const QString &sectionType, bool hasCurrentQuestion)
{
    if (!hasCurrentQuestion) {
        return false;
    }
    const QString content = removeScoreText(stripQuestionPrefix(line));
    if (content.isEmpty()) {
        return false;
    }
    if (isJudgmentType(sectionType)) {
        return hasEmptyParentheses(content);
    }
    if (isFillBlankType(sectionType)) {
        return hasFillBlankMarker(content);
    }
    return false;
}

bool isOneParagraphQuestionSection(const QString &type)
{
    return isChoiceSection(type)
        || type.contains(QStringLiteral("判断"))
        || type.contains(QStringLiteral("填空"))
        || type.contains(QStringLiteral("简答"));
}

bool shouldStartQuestionFromSection(const QString &line, const QString &sectionType, bool hasCurrentQuestion)
{
    if (sectionType.isEmpty() || isCompositeSection(sectionType)) {
        return false;
    }
    if (isChoiceSection(sectionType)) {
        return hasInlineOptions(line);
    }
    if (hasCurrentQuestion) {
        return false;
    }
    return isOneParagraphQuestionSection(sectionType);
}

QString defaultScoreForQuestion(const QuestionRecord &question, const QString &sectionScore)
{
    if (sectionScore.isEmpty()) {
        return QStringLiteral("10");
    }

    if (!question.type.contains(QStringLiteral("填空"))) {
        return sectionScore;
    }

    bool ok = false;
    const double scorePerBlank = sectionScore.toDouble(&ok);
    if (!ok) {
        return sectionScore;
    }

    int blankCount = 0;
    QRegularExpressionMatchIterator iterator = QRegularExpression(QStringLiteral("_{3,}")).globalMatch(question.content);
    while (iterator.hasNext()) {
        iterator.next();
        ++blankCount;
    }
    if (blankCount <= 1) {
        return sectionScore;
    }
    return QString::number(scorePerBlank * blankCount, 'g', 12);
}

bool applyOptionLine(QuestionRecord *question, const QString &line)
{
    const QRegularExpression optionRegex(QStringLiteral("^\\s*([A-Da-d])\\s*[\\.、．\\)]\\s*(.+)$"));
    const QRegularExpressionMatch match = optionRegex.match(line);
    if (!match.hasMatch()) {
        return false;
    }

    const QString key = match.captured(1).toUpper();
    const QString value = match.captured(2).trimmed();
    if (key == QStringLiteral("A")) {
        question->optionA = value;
    } else if (key == QStringLiteral("B")) {
        question->optionB = value;
    } else if (key == QStringLiteral("C")) {
        question->optionC = value;
    } else if (key == QStringLiteral("D")) {
        question->optionD = value;
    }
    return true;
}

bool applyAnswerLine(QuestionRecord *question, const QString &line)
{
    const QRegularExpression answerRegex(QStringLiteral("^\\s*(参考答案|答案|正确答案)\\s*[:：]?\\s*(.+)$"));
    const QRegularExpressionMatch match = answerRegex.match(line);
    if (!match.hasMatch()) {
        return false;
    }

    question->answer = match.captured(2).trimmed();
    return true;
}

void extractInlineOptions(QuestionRecord *question)
{
    if (!question->optionA.isEmpty() || !question->content.contains(QRegularExpression(QStringLiteral("[A-Da-d]\\s*[\\.、．\\)]")))) {
        return;
    }

    const QRegularExpression optionRegex(QStringLiteral("([A-Da-d])\\s*[\\.、．\\)]"));
    QRegularExpressionMatchIterator iterator = optionRegex.globalMatch(question->content);
    QList<QRegularExpressionMatch> matches;
    while (iterator.hasNext()) {
        matches.append(iterator.next());
    }
    if (matches.size() < 2) {
        return;
    }

    const QString original = question->content;
    question->content = original.left(matches.first().capturedStart()).trimmed();
    for (int i = 0; i < matches.size(); ++i) {
        const QString key = matches.at(i).captured(1).toUpper();
        const int start = matches.at(i).capturedEnd();
        const int end = (i + 1 < matches.size()) ? matches.at(i + 1).capturedStart() : original.size();
        const QString value = original.mid(start, end - start).trimmed();
        if (key == QStringLiteral("A")) {
            question->optionA = value;
        } else if (key == QStringLiteral("B")) {
            question->optionB = value;
        } else if (key == QStringLiteral("C")) {
            question->optionC = value;
        } else if (key == QStringLiteral("D")) {
            question->optionD = value;
        }
    }
}

QStringList readDocxParagraphs(const QString &docxRoot, QString *error)
{
    QDomDocument document;
    const QString documentPath = QDir(docxRoot).filePath(QStringLiteral("word/document.xml"));
    if (!loadXml(documentPath, &document, error)) {
        return {};
    }

    QStringList paragraphs;
    for (const QString &tagName : {QStringLiteral("p"), QStringLiteral("w:p")}) {
        const QDomNodeList paragraphNodes = document.elementsByTagName(tagName);
        for (int i = 0; i < paragraphNodes.size(); ++i) {
            const QString text = joinedText(paragraphNodes.at(i).toElement()).trimmed();
            if (!text.isEmpty()) {
                paragraphs.append(text);
            }
        }
        if (!paragraphs.isEmpty()) {
            break;
        }
    }
    return paragraphs;
}

// 把题型映射成答案区回填用的类别键（choice/judge/fill/subjective）。
QString answerCategoryOf(const QString &type)
{
    if (isChoiceSection(type)) {
        return QStringLiteral("choice");
    }
    if (type.contains(QStringLiteral("判断"))) {
        return QStringLiteral("judge");
    }
    if (type.contains(QStringLiteral("填空"))) {
        return QStringLiteral("fill");
    }
    return QStringLiteral("subjective");
}

// 判断题/选择题答案标记归一化：√对T→正确，×✗错F→错误，字母选项保持原样（去空格转大写）。
QString normalizeAnswerToken(QString token)
{
    token = token.trimmed();
    if (token == QStringLiteral("√") || token == QStringLiteral("对") || token == QStringLiteral("正确")
        || token.compare(QStringLiteral("T"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("正确");
    }
    if (token == QStringLiteral("×") || token == QStringLiteral("✗") || token == QStringLiteral("错")
        || token == QStringLiteral("错误") || token.compare(QStringLiteral("F"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("错误");
    }
    return token;
}

// 是否为答案区起始段落：行首是“参考答案/答案/评分标准”等（独立成段或后跟答案列表）。
// 只匹配行首，避免把题目中内联的“答案：X”误判为整段答案区。
bool isAnswerRegionMarker(const QString &line)
{
    return QRegularExpression(QStringLiteral("^\\s*(参考答案及评分标准|参考答案|评分标准|答案)\\s*[:：]?\\s*"))
        .match(line).hasMatch();
}

// 把段落切成 题目区 / 答案区 两部分（以第一处答案区标记为界）。无标记时答案区为空。
void splitAnswerRegion(const QStringList &paragraphs, QStringList *body, QStringList *answers)
{
    int markerIndex = -1;
    for (int i = 0; i < paragraphs.size(); ++i) {
        if (isAnswerRegionMarker(paragraphs.at(i))) {
            markerIndex = i;
            break;
        }
    }
    if (markerIndex < 0) {
        *body = paragraphs;
        return;
    }
    *body = paragraphs.mid(0, markerIndex);
    *answers = paragraphs.mid(markerIndex);
    // 去掉答案区首行的“参考答案：”前缀，保留其后可能跟着的“1.C 2.B”。
    if (!answers->isEmpty()) {
        QString first = answers->first();
        first.remove(QRegularExpression(QStringLiteral("^\\s*(参考答案及评分标准|参考答案|评分标准|答案)\\s*[:：]?\\s*")));
        if (first.trimmed().isEmpty()) {
            answers->removeFirst();
        } else {
            (*answers)[0] = first.trimmed();
        }
    }
}

// 解析答案区为「类别 → 按题号顺序排列的答案列表」。
// 支持一行多题（“1.C 2.B 3.C”）与主观题整段答案（“1. 硬件 软件”）。
QHash<QString, QStringList> parseAnswerRegion(const QStringList &answers)
{
    QHash<QString, QStringList> byCategory;
    QString currentCategory = QStringLiteral("choice");  // 答案区常以选择题打头，给个默认
    const QRegularExpression numberMarker(QStringLiteral("(\\d{1,3})\\s*[\\.、．)）]\\s*"));

    for (const QString &rawLine : answers) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (isSectionHeading(line)) {
            currentCategory = answerCategoryOf(inferQuestionType(line, QString()));
            continue;
        }
        // 用题号作为分隔，取相邻题号之间的文本作为该题答案。
        QList<QRegularExpressionMatch> marks;
        QRegularExpressionMatchIterator it = numberMarker.globalMatch(line);
        while (it.hasNext()) {
            marks.append(it.next());
        }
        if (marks.isEmpty()) {
            continue;
        }
        for (int i = 0; i < marks.size(); ++i) {
            const int start = marks.at(i).capturedEnd();
            const int end = (i + 1 < marks.size()) ? marks.at(i + 1).capturedStart() : line.size();
            const QString token = normalizeAnswerToken(line.mid(start, end - start));
            if (!token.isEmpty()) {
                byCategory[currentCategory].append(token);
            }
        }
    }
    return byCategory;
}

ImportResult paragraphsToQuestions(const QStringList &paragraphs)
{
    ImportResult result;
    QuestionRecord current;
    QString sectionType;
    QString sectionScore;

    const auto flush = [&]() {
        current.content = current.content.trimmed();
        if (current.content.isEmpty()) {
            return;
        }
        extractInlineOptions(&current);
        normalizeImportedQuestion(&current);
        if (current.score.isEmpty()) {
            current.score = defaultScoreForQuestion(current, sectionScore);
            if (sectionScore.isEmpty()) {
                result.warnings.append(QStringLiteral("题目“%1”未识别到分值，已默认设为 10 分。").arg(current.content.left(24)));
            }
        }
        if (current.difficulty.isEmpty()) {
            current.difficulty = QStringLiteral("中等");
        }
        result.questions.append(current);
        current = QuestionRecord();
    };

    for (const QString &rawLine : paragraphs) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        if (isSectionHeading(line)) {
            flush();
            sectionType = inferQuestionType(line, QString());
            sectionScore = extractScore(line);
            continue;
        }

        if (sectionType.isEmpty()) {
            continue;
        }

        if (!current.content.isEmpty() && applyAnswerLine(&current, line)) {
            continue;
        }
        if (!current.content.isEmpty() && applyOptionLine(&current, line)) {
            continue;
        }

        const bool hasCurrentQuestion = !current.content.isEmpty();
        if (shouldSplitStandaloneLine(line, sectionType, hasCurrentQuestion)
            || isQuestionStart(line, hasCurrentQuestion, sectionType)
            || shouldStartQuestionFromSection(line, sectionType, hasCurrentQuestion)) {
            flush();
            current.type = inferQuestionType(line, sectionType);
            current.score = extractScore(line);
            current.content = removeScoreText(stripQuestionPrefix(line));
            continue;
        }

        if (current.content.isEmpty()) {
            current.type = inferQuestionType(line, sectionType);
            current.score = extractScore(line);
            current.content = removeScoreText(stripQuestionPrefix(line));
        } else {
            const QString extraScore = extractScore(line);
            if (current.score.isEmpty() && !extraScore.isEmpty()) {
                current.score = extraScore;
            }
            const QString inferredType = inferQuestionType(line, sectionType);
            if (current.type == QStringLiteral("简答题") && inferredType != QStringLiteral("简答题")) {
                current.type = inferredType;
            }
            current.content += QStringLiteral("\n") + removeScoreText(line);
        }
    }

    flush();
    if (result.questions.isEmpty()) {
        result.error = QStringLiteral("没有从 Word 文档中识别到题目。");
        return result;
    }

    result.success = true;
    result.warnings.prepend(QStringLiteral("Word 导入采用宽松识别，建议导入后检查题干、分值和题型。"));
    return result;
}

} // namespace

ImportResult ExcelImporter::importFile(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("csv")) {
        return importCsv(filePath);
    }
    if (suffix == QStringLiteral("xlsx")) {
        return importXlsx(filePath);
    }
    if (suffix == QStringLiteral("docx")) {
        return importDocx(filePath);
    }

    ImportResult result;
    result.error = QStringLiteral("暂时只支持 .docx、.xlsx 和 .csv 题库文件。");
    return result;
}

ImportResult ExcelImporter::importCsv(const QString &filePath)
{
    QFile file(filePath);
    ImportResult result;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = QStringLiteral("无法打开文件：%1").arg(filePath);
        return result;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QString text = stream.readAll();
    return rowsToQuestions(readCsvRows(text));
}

ImportResult ExcelImporter::importXlsx(const QString &filePath)
{
    ImportResult result;
    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) {
        result.error = QStringLiteral("无法创建临时目录。");
        return result;
    }

    const QString extractedDir = QDir(temporaryDir.path()).filePath(QStringLiteral("xlsx"));
    QString error;
    if (!extractArchive(filePath, extractedDir, &error)) {
        result.error = error;
        return result;
    }

    const QString sheetPath = firstWorksheetPath(extractedDir);
    if (sheetPath.isEmpty()) {
        result.error = QStringLiteral("Excel 文件中没有找到工作表。");
        return result;
    }

    const QStringList sharedStrings = readSharedStrings(extractedDir);
    const QList<QStringList> rows = readWorksheetRows(sheetPath, sharedStrings, &error);
    if (!error.isEmpty()) {
        result.error = error;
        return result;
    }

    return rowsToQuestions(rows);
}

ImportResult ExcelImporter::importDocx(const QString &filePath)
{
    ImportResult result;
    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) {
        result.error = QStringLiteral("无法创建临时目录。");
        return result;
    }

    const QString extractedDir = QDir(temporaryDir.path()).filePath(QStringLiteral("docx"));
    QString error;
    if (!extractArchive(filePath, extractedDir, &error)) {
        result.error = error;
        return result;
    }

    const QStringList paragraphs = readDocxParagraphs(extractedDir, &error);
    if (!error.isEmpty()) {
        result.error = error;
        return result;
    }
    if (paragraphs.isEmpty()) {
        result.error = QStringLiteral("Word 文档中没有读取到正文内容。");
        return result;
    }

    qInfo("[IMPORT_PARSE_MODE] mode=docx_school_exam paragraphs=%lld", static_cast<long long>(paragraphs.size()));

    // 先把题目区与文末答案区切开：题目区按学校试卷启发式拆题，答案区单独解析后回填。
    QStringList body;
    QStringList answerLines;
    splitAnswerRegion(paragraphs, &body, &answerLines);

    result = paragraphsToQuestions(body);
    if (!result.success) {
        return result;  // 一道题都没识别到才算失败（错误信息已在内部设置）
    }

    // 回填答案区：按「类别 → 题号顺序」把答案补进“答案为空”的题（选择/判断主要受益）。
    if (!answerLines.isEmpty()) {
        const QHash<QString, QStringList> answersByCategory = parseAnswerRegion(answerLines);
        QHash<QString, int> cursor;
        int filled = 0;
        for (QuestionRecord &question : result.questions) {
            if (!question.answer.trimmed().isEmpty()) {
                continue;
            }
            const QString category = answerCategoryOf(question.type);
            const QStringList &list = answersByCategory.value(category);
            int &index = cursor[category];
            if (index < list.size()) {
                question.answer = list.at(index);
                ++index;
                ++filled;
            }
        }
        if (filled > 0) {
            qInfo("[IMPORT_PARSE] docx-answer-region backfilled=%d", filled);
            result.warnings.append(QStringLiteral("已从文末答案区自动回填 %1 道题的答案，请在预览中核对。").arg(filled));
        }
    }

    // 回填后仍缺答案的选择/判断题：记 warning 但不失败（允许“待补答案”导入）。
    int missingAnswer = 0;
    int displayIndex = 0;
    for (const QuestionRecord &question : result.questions) {
        ++displayIndex;
        const QString category = answerCategoryOf(question.type);
        if ((category == QStringLiteral("choice") || category == QStringLiteral("judge"))
            && question.answer.trimmed().isEmpty()) {
            qInfo("[IMPORT_PARSE_WARNING] question=%d reason=missing-answer", displayIndex);
            ++missingAnswer;
        }
    }
    if (missingAnswer > 0) {
        result.warnings.append(QStringLiteral("共有 %1 道选择/判断题未识别到答案，已按“待补答案”导入，可在预览中补填或导入后人工批阅。")
                                   .arg(missingAnswer));
    }
    return result;
}

ImportResult ExcelImporter::importDocxAsSingleQuestion(const QString &filePath)
{
    ImportResult result;
    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) {
        result.error = QStringLiteral("无法创建临时目录。");
        return result;
    }

    const QString extractedDir = QDir(temporaryDir.path()).filePath(QStringLiteral("docx"));
    QString error;
    if (!extractArchive(filePath, extractedDir, &error)) {
        result.error = error;
        return result;
    }

    const QStringList paragraphs = readDocxParagraphs(extractedDir, &error);
    if (!error.isEmpty()) {
        result.error = error;
        return result;
    }
    if (paragraphs.isEmpty()) {
        result.error = QStringLiteral("Word 文档中没有读取到正文内容。");
        return result;
    }

    const QString content = paragraphs.join(QStringLiteral("\n")).trimmed();
    QuestionRecord question;
    question.type = inferQuestionType(content.left(240), QStringLiteral("简答题"));
    question.content = removeScoreText(content);
    question.score = extractScore(content);
    question.difficulty = QStringLiteral("中等");
    if (question.score.isEmpty()) {
        question.score = QStringLiteral("10");
        result.warnings.append(QStringLiteral("未识别到分值，已默认设为 10 分。"));
    }

    result.questions.append(question);
    result.success = true;
    result.warnings.prepend(QStringLiteral("Word 文档已按整篇作为一道题导入，题号、小问和步骤会保留在题干中。"));
    return result;
}

QList<QStringList> ExcelImporter::readCsvRows(const QString &text)
{
    QList<QStringList> rows;
    QStringList row;
    QString cell;
    bool inQuotes = false;

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == '"') {
            if (inQuotes && i + 1 < text.size() && text.at(i + 1) == '"') {
                cell.append('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
            continue;
        }

        if (!inQuotes && ch == ',') {
            row.append(cell.trimmed());
            cell.clear();
            continue;
        }

        if (!inQuotes && (ch == '\n' || ch == '\r')) {
            if (ch == '\r' && i + 1 < text.size() && text.at(i + 1) == '\n') {
                ++i;
            }
            row.append(cell.trimmed());
            cell.clear();
            rows.append(row);
            row.clear();
            continue;
        }

        cell.append(ch);
    }

    if (!cell.isEmpty() || !row.isEmpty()) {
        row.append(cell.trimmed());
        rows.append(row);
    }
    return rows;
}

namespace {

// 题型归类：选择题（含单选/多选/选择）。
bool isChoiceCategory(const QString &type)
{
    return type.contains(QStringLiteral("单选")) || type.contains(QStringLiteral("多选"))
        || type.contains(QStringLiteral("选择"));
}

// 选择题/判断题缺答案时记 warning（仍允许导入，便于学校无答案试卷先入库再人工补）。
// 主观题（填空/简答/综合）答案为空属正常，不告警。返回 true 表示记录了缺答案告警。
bool warnIfMissingAnswer(const QuestionRecord &question, int displayIndex, ImportResult *result)
{
    const bool needsAnswer = isChoiceCategory(question.type) || question.type.contains(QStringLiteral("判断"));
    if (needsAnswer && question.answer.trimmed().isEmpty()) {
        qInfo("[IMPORT_PARSE_WARNING] question=%d reason=missing-answer", displayIndex);
        result->warnings.append(QStringLiteral("第 %1 题（%2）缺少答案，已按“待补答案”导入，可在预览中补填或导入后人工批阅。")
                                    .arg(displayIndex).arg(question.type));
        return true;
    }
    return false;
}

// 单元格清洗：全角空格转半角后去首尾空白。
QString cleanCell(QString value)
{
    value.replace(QChar(0x3000), QLatin1Char(' '));
    return value.trimmed();
}

// 无显式题型列时，按选项/括号推断题型：四选项齐→单/多选；以（）结尾→判断；否则简答。
QString inferTableType(const QuestionRecord &question)
{
    if (!question.type.trimmed().isEmpty()) {
        return question.type.trimmed();
    }
    const bool hasOptions = !question.optionA.trimmed().isEmpty() && !question.optionB.trimmed().isEmpty();
    if (hasOptions) {
        // 答案含 2 个及以上字母（如 ABD）判为多选，否则单选。
        if (QRegularExpression(QStringLiteral("[A-Da-d]\\s*[,，、]?\\s*[A-Da-d]")).match(question.answer).hasMatch()) {
            return QStringLiteral("多选题");
        }
        return QStringLiteral("单选题");
    }
    if (QRegularExpression(QStringLiteral("[（(]\\s*[）)]\\s*$")).match(question.content).hasMatch()) {
        return QStringLiteral("判断题");
    }
    return QStringLiteral("简答题");
}

// 该行是否像表头：任一单元格归一化后命中已知别名关键字。
bool looksLikeHeaderRow(const QStringList &row)
{
    static const QStringList keys = {
        QStringLiteral("题型"), QStringLiteral("类型"), QStringLiteral("type"),
        QStringLiteral("题干"), QStringLiteral("题目"), QStringLiteral("试题"), QStringLiteral("question"), QStringLiteral("content"),
        QStringLiteral("答案"), QStringLiteral("answer"), QStringLiteral("分值"), QStringLiteral("score")
    };
    for (const QString &cell : row) {
        const QString norm = normalizeHeader(cell);
        for (const QString &key : keys) {
            if (norm == normalizeHeader(key)) {
                return true;
            }
        }
    }
    return false;
}

// 无表头表格按列数定位解析。支持三种常见顺序：
//   9 列：题型,题干,A,B,C,D,答案,分值,解析            → no_header_9cols
//   6~8 列：题干,A,B,C,D,答案[,解析]                 → no_header_7cols
//   2~4 列：题干,答案[,分值][,解析]                  → no_header_subjective
ImportResult parsePositionalRows(const QList<QStringList> &rows)
{
    ImportResult result;

    // 以前几行的最大列数判定格式（容忍个别短行）。
    int columns = 0;
    for (int i = 0; i < rows.size() && i < 5; ++i) {
        columns = qMax(columns, rows.at(i).size());
    }

    QString mode;
    if (columns >= 9) {
        mode = QStringLiteral("no_header_9cols");
    } else if (columns >= 5) {
        mode = QStringLiteral("no_header_7cols");
    } else if (columns >= 2) {
        mode = QStringLiteral("no_header_subjective");
    } else {
        qWarning("[IMPORT_PARSE_ERROR] reason=unrecognized-columns columns=%d", columns);
        result.error = QStringLiteral("无法识别无表头表格：检测到 %1 列。\n"
                                      "请使用以下任一顺序：\n"
                                      "9 列：题型,题干,A,B,C,D,答案,分值,解析\n"
                                      "7 列：题干,A,B,C,D,答案,解析\n"
                                      "4 列：题干,答案,分值,解析\n"
                                      "或在第一行加上表头。").arg(columns);
        return result;
    }
    qInfo("[IMPORT_PARSE_MODE] mode=%s columns=%d", qPrintable(mode), columns);

    int displayIndex = 0;
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const QStringList &row = rows.at(rowIndex);
        // 跳过空行与误当数据的表头行（首行若是表头，no-header 模式也要忽略）。
        if (row.isEmpty() || looksLikeHeaderRow(row)) {
            continue;
        }

        QuestionRecord question;
        if (mode == QStringLiteral("no_header_9cols")) {
            question.type = cleanCell(valueAt(row, 0));
            question.content = cleanCell(valueAt(row, 1));
            question.optionA = cleanCell(valueAt(row, 2));
            question.optionB = cleanCell(valueAt(row, 3));
            question.optionC = cleanCell(valueAt(row, 4));
            question.optionD = cleanCell(valueAt(row, 5));
            question.answer = cleanCell(valueAt(row, 6));
            question.score = cleanCell(valueAt(row, 7));
            question.analysis = cleanCell(valueAt(row, 8));
        } else if (mode == QStringLiteral("no_header_7cols")) {
            question.content = cleanCell(valueAt(row, 0));
            question.optionA = cleanCell(valueAt(row, 1));
            question.optionB = cleanCell(valueAt(row, 2));
            question.optionC = cleanCell(valueAt(row, 3));
            question.optionD = cleanCell(valueAt(row, 4));
            question.answer = cleanCell(valueAt(row, 5));
            question.analysis = cleanCell(valueAt(row, 6));
        } else {  // no_header_subjective
            question.content = cleanCell(valueAt(row, 0));
            question.answer = cleanCell(valueAt(row, 1));
            question.score = cleanCell(valueAt(row, 2));
            question.analysis = cleanCell(valueAt(row, 3));
        }

        if (question.content.isEmpty()) {
            continue;  // 题干为空的行直接跳过，不报错（容忍尾部空行）
        }

        question.type = inferTableType(question);
        if (question.difficulty.trimmed().isEmpty()) {
            question.difficulty = QStringLiteral("中等");
        }
        normalizeImportedQuestion(&question);
        ++displayIndex;
        warnIfMissingAnswer(question, displayIndex, &result);
        result.questions.append(question);
    }

    if (result.questions.isEmpty()) {
        result.error = QStringLiteral("没有读取到有效题目（无表头模式）：请检查每行是否至少有题干。");
        return result;
    }
    result.success = true;
    return result;
}

} // namespace

ImportResult ExcelImporter::rowsToQuestions(const QList<QStringList> &rows)
{
    ImportResult result;
    if (rows.isEmpty()) {
        result.error = QStringLiteral("表格内容为空，没有可导入的题目。");
        return result;
    }

    QMap<QString, int> headerIndex;
    const QStringList headers = rows.first();
    for (int i = 0; i < headers.size(); ++i) {
        const QString header = normalizeHeader(headers.at(i));
        if (!header.isEmpty()) {
            headerIndex.insert(header, i);
        }
    }

    // 别名匹配：大小写/符号不敏感（normalizeHeader 已统一），命中第一个别名即返回列号。
    const auto indexOf = [&](std::initializer_list<const char *> aliases) {
        for (const char *alias : aliases) {
            const QString key = normalizeHeader(QString::fromUtf8(alias));
            if (headerIndex.contains(key)) {
                return headerIndex.value(key);
            }
        }
        return -1;
    };

    const int typeIndex = indexOf({"题型", "类型", "questiontype", "type"});
    const int courseIndex = indexOf({"课程", "科目", "章节", "知识点", "course", "subject", "chapter", "topic"});
    const int contentIndex = indexOf({"题干", "题目", "试题", "试题内容", "内容", "question", "stem", "content"});
    const int optionAIndex = indexOf({"选项A", "A选项", "A", "optiona", "option_a"});
    const int optionBIndex = indexOf({"选项B", "B选项", "B", "optionb", "option_b"});
    const int optionCIndex = indexOf({"选项C", "C选项", "C", "optionc", "option_c"});
    const int optionDIndex = indexOf({"选项D", "D选项", "D", "optiond", "option_d"});
    const int answerIndex = indexOf({"参考答案", "正确答案", "标准答案", "答案", "answer", "correctanswer"});
    const int scoreIndex = indexOf({"分值", "分数", "score", "points"});
    const int difficultyIndex = indexOf({"难度", "difficulty"});
    const int analysisIndex = indexOf({"解析", "答案解析", "说明", "explanation", "analysis"});

    // 没识别到“题干”列：判定为无表头表格，转按列数定位解析（向后兼容旧严格模板：
    // 旧模板有表头会走下面的表头分支，不受影响）。
    if (contentIndex < 0) {
        qInfo("[IMPORT_PARSE_MODE] mode=no_header (header not recognized, fallback to positional)");
        return parsePositionalRows(rows);
    }

    qInfo("[IMPORT_PARSE_MODE] mode=table_header columns=%lld", static_cast<long long>(headers.size()));

    int displayIndex = 0;
    for (int rowIndex = 1; rowIndex < rows.size(); ++rowIndex) {
        const QStringList row = rows.at(rowIndex);
        const QString type = cleanCell(valueAt(row, typeIndex));
        const QString content = cleanCell(valueAt(row, contentIndex));

        if (type.isEmpty() && content.isEmpty()) {
            continue;  // 整行空，跳过
        }
        if (content.isEmpty()) {
            qWarning("[IMPORT_PARSE_WARNING] row=%d reason=empty-content", rowIndex + 1);
            result.warnings.append(QStringLiteral("第 %1 行题干为空，已跳过。").arg(rowIndex + 1));
            continue;
        }

        QuestionRecord question;
        question.type = type;
        question.course = cleanCell(valueAt(row, courseIndex));
        question.content = content;
        question.optionA = cleanCell(valueAt(row, optionAIndex));
        question.optionB = cleanCell(valueAt(row, optionBIndex));
        question.optionC = cleanCell(valueAt(row, optionCIndex));
        question.optionD = cleanCell(valueAt(row, optionDIndex));
        question.answer = cleanCell(valueAt(row, answerIndex));
        question.score = cleanCell(valueAt(row, scoreIndex));
        question.difficulty = cleanCell(valueAt(row, difficultyIndex));
        question.analysis = cleanCell(valueAt(row, analysisIndex));

        // 缺题型时按选项/括号自动推断（不再因缺题型整行丢弃）。
        question.type = inferTableType(question);
        if (question.difficulty.isEmpty()) {
            question.difficulty = QStringLiteral("中等");
        }
        normalizeImportedQuestion(&question);
        ++displayIndex;

        // 选择题选项不足提醒（不致命，仍导入）。
        if (isChoiceCategory(question.type)) {
            const int optionCount = (question.optionA.isEmpty() ? 0 : 1) + (question.optionB.isEmpty() ? 0 : 1)
                                  + (question.optionC.isEmpty() ? 0 : 1) + (question.optionD.isEmpty() ? 0 : 1);
            if (optionCount < 2) {
                qWarning("[IMPORT_PARSE_WARNING] question=%d reason=too-few-options count=%d", displayIndex, optionCount);
                result.warnings.append(QStringLiteral("第 %1 题为选择题但只识别到 %2 个选项，请检查。").arg(displayIndex).arg(optionCount));
            }
        }
        warnIfMissingAnswer(question, displayIndex, &result);
        result.questions.append(question);
    }

    if (result.questions.isEmpty()) {
        result.error = QStringLiteral("识别到表头但没有读取到有效题目：请检查“题干”列是否填写。");
        return result;
    }

    result.success = true;
    return result;
}
