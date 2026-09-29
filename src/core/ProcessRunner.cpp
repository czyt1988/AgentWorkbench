#include "core/ProcessRunner.h"

#include <QProcess>
#include <QStandardPaths>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringDecoder>
#else
// QTextCodec 在 Qt 5 属 QtCore、在 Qt 6 被移去 Core5Compat，decodeOutput
// 的两个版本各用各的解码器。
#include <QTextCodec>
#endif

namespace awb::core {

/**
 * @brief 经 PATH 解析裸程序名
 *
 * @param program 程序名，可带相对/绝对路径
 * @return 解析到的可执行文件路径；找不到或入参为空时返回空串
 */
QString ProcessRunner::findExecutable(const QString &program)
{
    if (program.isEmpty()) {
        return QString();
    }
    // QStandardPaths::findExecutable 会搜 PATH，且在 Windows 上补全
    // PATHEXT 后缀（.exe/.cmd/.bat/…），恰好能解析 npm 风格的垫片
    // "qwen" -> "qwen.cmd"。
    return QStandardPaths::findExecutable(program);
}

/**
 * @brief 分离启动外部命令并按需重定向输出
 *
 * 成员版 startDetached() 尊重通道设置（静态重载不尊重）：outputFile
 * 非空时把 stderr 并入 stdout 一起重定向，token URL 的提取因此只需扫
 * 一个文件。
 *
 * @param program 已解析的绝对程序路径（先用 findExecutable() 解析）
 * @param args 命令行参数
 * @param pid 可选；成功时收到子进程 PID
 * @param error 可选；失败时收到可读原因
 * @param workingDirectory 工作目录；空串表示继承当前目录
 * @param env 进程环境；空环境表示继承当前环境
 * @param outputFile 输出重定向文件；空串表示不重定向
 * @return 启动成功返回 true
 */
bool ProcessRunner::startDetached(const QString &program, const QStringList &args,
                                  qint64 *pid, QString *error,
                                  const QString &workingDirectory,
                                  const QProcessEnvironment &env,
                                  const QString &outputFile)
{
    if (program.isEmpty()) {
        if (error) {
            *error = QStringLiteral("program is empty");
        }
        return false;
    }

    // 裸程序名先经 PATH（Windows 上含 PATHEXT）解析，npm 风格的
    // .cmd/.bat 垫片（如 "qwen" -> "qwen.cmd"）才能被找到；
    // CreateProcess 自己不会尝试这些后缀。
    const QString resolved = findExecutable(program);
    if (resolved.isEmpty()) {
        if (error) {
            *error = QStringLiteral("'%1' is not on PATH").arg(program);
        }
        return false;
    }

    QProcess proc;
    proc.setProgram(resolved);
    proc.setArguments(args);
    if (!workingDirectory.isEmpty()) {
        proc.setWorkingDirectory(workingDirectory);
    }
    if (!env.isEmpty()) {
        proc.setProcessEnvironment(env);
    }
    if (!outputFile.isEmpty()) {
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.setStandardOutputFile(outputFile, QIODevice::Truncate);
    }

    qint64 outPid = 0;
    const bool ok = proc.startDetached(&outPid);
    if (ok && pid) {
        *pid = outPid;
    }
    if (!ok && error) {
        *error = proc.errorString();
    }
    return ok;
}

/**
 * @brief 同步运行命令到结束
 *
 * @param program 程序路径
 * @param args 命令行参数
 * @param timeoutMs 超时毫秒数；<= 0 表示无限等待
 * @return 两个通道的解码文本与退出码；未启动或超时时 error 非空
 */
ProcessResult ProcessRunner::run(const QString &program, const QStringList &args,
                                 int timeoutMs)
{
    ProcessResult result;
    if (program.isEmpty()) {
        result.error = QStringLiteral("program is empty");
        return result;
    }

    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(timeoutMs > 0 ? timeoutMs : 30000)) {
        result.error = QStringLiteral("failed to start: %1").arg(proc.errorString());
        return result;
    }
    result.started = true;

    if (timeoutMs <= 0) {
        proc.waitForFinished(-1);
    } else if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(3000);
        result.error = QStringLiteral("timed out after %1 ms").arg(timeoutMs);
        result.stdOut = decodeOutput(proc.readAllStandardOutput());
        result.stdErr = decodeOutput(proc.readAllStandardError());
        return result;
    }

    result.exitCode = proc.exitCode();
    result.stdOut = decodeOutput(proc.readAllStandardOutput());
    result.stdErr = decodeOutput(proc.readAllStandardError());
    return result;
}

/**
 * @brief 返回 killTree() 使用的杀进程命令名
 *
 * @return Windows 上是 "taskkill"，其它平台是 "kill"
 */
QString ProcessRunner::killProgram()
{
#ifdef Q_OS_WIN
    return QStringLiteral("taskkill");
#else
    return QStringLiteral("kill");
#endif
}

/**
 * @brief 返回杀指定进程树的参数
 *
 * @param pid 目标进程 ID
 * @return Windows 上为 /F /T /PID <pid>，其它平台为 -9 <pid>
 */
QStringList ProcessRunner::killProgramArgs(qint64 pid)
{
#ifdef Q_OS_WIN
    // /F 强杀，/T 连整棵树一起杀（cmd -> qwen.cmd -> node）。
    return {QStringLiteral("/F"), QStringLiteral("/T"),
            QStringLiteral("/PID"), QString::number(pid)};
#else
    return {QStringLiteral("-9"), QString::number(pid)};
#endif
}

/**
 * @brief 杀掉整棵进程树
 *
 * @param pid 根进程 ID
 * @return 杀进程命令本身启动失败时返回 false；不等待其结果（即发即忘）
 */
bool ProcessRunner::killTree(qint64 pid)
{
    qint64 outPid = 0;
    return startDetached(killProgram(), killProgramArgs(pid), &outPid);
}

/**
 * @brief 把命令行字符串拆成程序 + 参数
 *
 * @param command 原始命令行
 * @return 拆分出的 token 列表；引号数量为奇数等非法输入返回空表
 */
QStringList ProcessRunner::splitCommand(const QString &command)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QProcess::splitCommand(command);
#else
    // 与 QProcess::splitCommand 同一套规则：空白分词、双引号成组、
    // 反斜杠仅转义引号自身、成对的引号字面量（"" -> "）。奇数引号这类
    // 非法输入 Qt 6 返回空表，这里保持一致。
    QStringList tokens;
    QString current;
    bool inQuote = false;
    bool tokenStarted = false;
    bool escaped = false;
    int quoteCount = 0;
    const int size = command.size();
    for (int i = 0; i < size; ++i) {
        const QChar c = command.at(i);
        if (escaped) {
            current += c;
            escaped = false;
        } else if (c == QLatin1Char('\\') && inQuote
                   && i + 1 < size
                   && command.at(i + 1) == QLatin1Char('"')) {
            escaped = true;
        } else if (c == QLatin1Char('"')) {
            ++quoteCount;
            inQuote = !inQuote;
            tokenStarted = true;
        } else if (!inQuote && c.isSpace()) {
            if (tokenStarted) {
                tokens.append(current);
                current.clear();
                tokenStarted = false;
            }
        } else {
            current += c;
            tokenStarted = true;
        }
    }
    if (quoteCount % 2 != 0) {
        return {};
    }
    if (tokenStarted) {
        tokens.append(current);
    }
    return tokens;
#endif
}

/**
 * @brief 解码子进程输出
 *
 * @param data 捕获到的原始字节
 * @return UTF-8 解码成功时为 UTF-8 文本；含非法序列时回退系统 locale
 *         （zh-CN Windows 上是 GBK）解码；入参为空返回空串
 */
QString ProcessRunner::decodeOutput(const QByteArray &data)
{
    if (data.isEmpty()) {
        return {};
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString result = decoder.decode(data);
    if (!decoder.hasError()) {
        return result;
    }
#else
    // QTextDecoder::toUnicode 带 ConverterState，invalidChars 记录非法序列数，
    // 与 QStringDecoder::hasError 的判据等价。
    QTextCodec::ConverterState state;
    const QString result = QTextCodec::codecForName("UTF-8")
                               ->toUnicode(data.constData(), data.size(),
                                           &state);
    if (state.invalidChars == 0) {
        return result;
    }
#endif
    return QString::fromLocal8Bit(data);
}

} // namespace awb::core
