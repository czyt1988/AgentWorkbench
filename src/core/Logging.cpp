#include "core/Logging.h"

#include "core/Paths.h"
#include "core/TextUtils.h"

#include <spdlog/async.h>
#include <spdlog/details/file_helper.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/periodic_worker.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>

#include <chrono>
#include <exception>
#include <memory>
#include <vector>

// 与 Logging.h 里的 Q_DECLARE_LOGGING_CATEGORY 同在全局作用域。
Q_LOGGING_CATEGORY(lcAwbEvent, "awb.event")
Q_LOGGING_CATEGORY(lcAwbPerf, "awb.perf")

namespace awb::core {

namespace {

// spdlog 异步后端的参数：8192 条 MPMC 队列 + 1 个工作线程。队列深度沿用
// 参考工程的取值——正常运行远填不满，只有磁盘长时间写不动时才会触底；
// 溢出策略选 overrun_oldest（丢最旧的一条），日志不能反过来阻塞调用方
// （UI 线程）。
constexpr std::size_t kQueueCapacity = 8192;
constexpr std::size_t kWorkerThreads = 1;
// 定期 flush 间隔：flush_on(warn) 只保警告及以上立即写盘，其余消息最迟
// 1 s 后提交；每次 flush 只是把 CRT 缓冲交给操作系统，不是 fsync。
constexpr std::chrono::seconds kFlushInterval{1};
const char *const kLoggerName = "agentworkbench";

// 级别名（install() 与 settings.json 的 logging.level 共用同一份取值
// 清单）→ spdlog 级别；未知名字返回 false，调用方按默认处理。
bool parseLevelName(const QString &name, spdlog::level::level_enum &out)
{
    if (name == QStringLiteral("debug"))
        out = spdlog::level::debug;
    else if (name == QStringLiteral("info"))
        out = spdlog::level::info;
    else if (name == QStringLiteral("warning"))
        out = spdlog::level::warn;
    else if (name == QStringLiteral("critical"))
        out = spdlog::level::critical;
    else if (name == QStringLiteral("off"))
        out = spdlog::level::off;
    else
        return false;
    return true;
}

// 文件名类型由 SPDLOG_WCHAR_FILENAMES 决定：Windows 下是 std::wstring
// （宽字符才打得开非 ASCII 路径），其它平台是 std::string。
spdlog::filename_t toFilename(const QString &path)
{
#ifdef Q_OS_WIN
    return path.toStdWString();
#else
    return path.toStdString();
#endif
}

/// 保留本仓库既有命名与语义的轮转文件 sink。
///
/// 不用 spdlog 自带 rotating_file_sink 的原因是备份名不兼容：它把下标插在
/// 扩展名前面（mylog.3.txt），而本仓库的文档、测试与存量用户文件都约定
/// agentworkbench.log.1。这里复用 spdlog 的 file_helper 做打开与写入
/// （Windows 下走宽字符 fopen），只把「到上限就滚动」换成与改造前逐条
/// 一致的 rename 链。
class RotatingFileSink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
    /// @param path        日志文件绝对路径（父目录已存在）
    /// @param maxFileSize 单文件字节上限，达到即滚动
    /// @param maxFiles    文件总数（含当前文件）；1 表示不滚动、到上限截断
    /// @throws spdlog::spdlog_ex 打不开文件时抛出，由调用方降级处理
    RotatingFileSink(const QString &path, qint64 maxFileSize, int maxFiles)
        : m_path(path)
        , m_maxFileSize(static_cast<std::size_t>(maxFileSize))
        , m_maxFiles(maxFiles)
    {
        m_file.open(toFilename(path)); // 追加打开，沿用上次会话的内容
        m_size = m_file.size();
    }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        m_file.write(formatted);
        m_size += formatted.size();
        rotateIfNeeded();
    }

    void flush_() override { m_file.flush(); }

private:
    QString backupPath(int index) const
    {
        return QStringLiteral("%1.%2").arg(m_path).arg(index);
    }

    // 到上限就滚动：关文件 → 丢最旧备份 → 逐级上移（.1 → .2，…）→ 当前
    // 文件顶成 .1 → 重开。单文件模式没有滚动目标，直接截断。
    void rotateIfNeeded()
    {
        if (m_size < m_maxFileSize)
            return;

        m_file.close();
        if (m_maxFiles > 1) {
            QFile::remove(backupPath(m_maxFiles - 1));
            for (int i = m_maxFiles - 2; i >= 1; --i)
                QFile::rename(backupPath(i), backupPath(i + 1));
            QFile::rename(m_path, backupPath(1));
        }
        m_file.open(toFilename(m_path), /*truncate=*/m_maxFiles <= 1);
        m_size = 0;
    }

    QString m_path;
    std::size_t m_maxFileSize;
    int m_maxFiles;
    // 当前文件已写字节数，累积自算，避免每次写都 stat 文件。
    std::size_t m_size = 0;
    spdlog::details::file_helper m_file{spdlog::file_event_handlers{}};
};

// 后端状态。线程约束：install/uninstall 与日志生产者在同一个线程（当前即
// UI 线程），或经线程 join 建立 happens-before（测试先 join 生产线程再
// uninstall）；不要在别的线程还在写日志时拆建后端。
QString s_logPath;
qint64 s_maxFileSize = Logging::DEFAULT_MAX_FILE_SIZE;
int s_maxFiles = Logging::DEFAULT_MAX_FILES;
// 最低落盘级别与 stderr 镜像开关，由 install() 的参数驱动。
spdlog::level::level_enum s_minLevel = spdlog::level::debug;
bool s_mirrorToStderr = true;
std::shared_ptr<spdlog::logger> s_logger;
std::shared_ptr<spdlog::details::thread_pool> s_pool;
std::shared_ptr<RotatingFileSink> s_fileSink;
std::shared_ptr<spdlog::sinks::stderr_sink_mt> s_stderrSink;
std::unique_ptr<spdlog::details::periodic_worker> s_flusher;

/// Qt 消息类型到 spdlog 级别的映射。FATAL 归入 critical——它与普通
/// critical 的区别（FATAL 字样）在行文本里，由 messageHandler 负责。
spdlog::level::level_enum toSpdlogLevel(QtMsgType type)
{
    switch (type) {
    case QtInfoMsg:     return spdlog::level::info;
    case QtWarningMsg:  return spdlog::level::warn;
    case QtCriticalMsg:
    case QtFatalMsg:    return spdlog::level::critical;
    case QtDebugMsg:
    default:            return spdlog::level::debug;
    }
}

// 拆掉当前后端：先停定期 flush（它在自己的线程上投消息，reset 会 join），
// 再投一条 flush，最后靠 thread_pool 析构排空队列——析构给每个工作线程投
// 一条 block 策略的 terminate 并 join，join 返回时队列里的日志与 flush 都
// 已消费完，所以本函数返回即全部落盘。
void teardownBackend()
{
    s_flusher.reset();
    if (s_logger)
        s_logger->flush();
    s_logger.reset();
    s_fileSink.reset();
    s_stderrSink.reset();
    s_pool.reset();
}

// 建新后端：轮转文件 sink + stderr 镜像 sink + 异步队列。
//
// 任何一步失败都不向上抛（core 不跨边界抛异常），而是降级：文件打不开时
// 只写 stderr 并记一条警告（与旧实现一致），后端整体起不来时 handler 退回
// 直写 stderr，消息不吞。
void buildBackend()
{
    std::vector<spdlog::sink_ptr> sinks;

    try {
        s_fileSink = std::make_shared<RotatingFileSink>(
            Logging::logFilePath(), s_maxFileSize, s_maxFiles);
        sinks.push_back(s_fileSink);
    } catch (const spdlog::spdlog_ex &e) {
        s_fileSink.reset();
        qWarning().noquote() << QStringLiteral("AgentWorkbench: cannot write the log "
                                               "file %1: %2")
                                    .arg(Logging::logFilePath(),
                                         QString::fromUtf8(e.what()));
    }

    // stderr 镜像：旧实现无条件 fprintf(stderr)，这里等价保留，只是改由
    // 后台线程写；logging.mirrorToStderr=false 时不挂这个 sink。
    if (s_mirrorToStderr) {
        s_stderrSink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
        sinks.push_back(s_stderrSink);
    }

    // 一个 sink 都没有（文件打不开且不镜像 stderr）：不建 logger，让
    // handler 的 stderr 直写分支兜底——错误状况下的可见性优先于镜像开关。
    if (sinks.empty())
        return;

    try {
        s_pool = std::make_shared<spdlog::details::thread_pool>(kQueueCapacity,
                                                                kWorkerThreads);
        s_logger = std::make_shared<spdlog::async_logger>(
            kLoggerName, sinks.begin(), sinks.end(), s_pool,
            spdlog::async_overflow_policy::overrun_oldest);
        // 整行由 messageHandler 拼好，pattern 只负责原样输出并补行尾
        // （Windows 下 "\r\n"，与旧实现 QFile 的 Text 模式逐字节一致）；
        // 时间戳、级别、分类、位置都不走 spdlog 的 pattern。
        s_logger->set_pattern("%v");
        // handler 已按 s_minLevel 过滤过一轮，这里再设一次是双保险。
        s_logger->set_level(s_minLevel);
        s_logger->flush_on(spdlog::level::warn);
        s_flusher = std::make_unique<spdlog::details::periodic_worker>(
            [] {
                if (s_logger)
                    s_logger->flush();
            },
            kFlushInterval);
    } catch (const std::exception &e) {
        // 线程池或 logger 构造失败（线程耗尽等）：整体退回 handler 的
        // stderr 直写路径，文件 sink 一并拆掉，避免留下没人消费的句柄。
        teardownBackend();
        qWarning().noquote() << QStringLiteral(
                                 "AgentWorkbench: cannot start the log backend: %1")
                                 .arg(QString::fromUtf8(e.what()));
    }
}

// 绕过队列把一行同步写进所有 sink 并 flush。给 QtFatalMsg 用：fatal 之后
// 进程可能立刻终止，投进队列来不及消费。
//
// @return true = 已写盘；false = 后端不可用或写失败（调用方退回入队路径）
bool writeFatalNow(spdlog::string_view_t payload)
{
    if (!s_fileSink && !s_stderrSink)
        return false;
    try {
        const spdlog::details::log_msg msg(kLoggerName, spdlog::level::critical,
                                           payload);
        if (s_fileSink) {
            s_fileSink->log(msg);
            s_fileSink->flush();
        }
        if (s_stderrSink) {
            s_stderrSink->log(msg);
            s_stderrSink->flush();
        }
        return true;
    } catch (const std::exception &) {
        // 同步写失败（盘满等）：交给入队路径，后台的 flush_on 还能兜底。
        return false;
    }
}

} // namespace

void Logging::install(const QString &directory, qint64 maxFileSize, int maxFiles,
                      const QString &level, bool mirrorToStderr)
{
    s_maxFileSize = maxFileSize > 0 ? maxFileSize : DEFAULT_MAX_FILE_SIZE;
    s_maxFiles = qMax(1, maxFiles);
    s_mirrorToStderr = mirrorToStderr;

    // 日志目录：<dataRoot>/log/，调用方可覆盖（测试传临时目录）。
    s_logPath = directory.isEmpty() ? Paths::logsDir() : directory;
    QDir().mkpath(s_logPath);

    // 可重复调用（main.cpp 读到非默认轮转参数时会二次 install）：先排空
    // 旧后端再建新的，保证任一时刻只有一个 worker 在写文件。
    teardownBackend();

    // 先装 handler 再建后端：这期间到达的消息走 handler 的 stderr 直写
    // 分支，不会打到还不存在的队列上。
    qInstallMessageHandler(Logging::messageHandler);

    // 级别解析放在 handler 之后：非法值的告警要进得了日志。未知值按 debug
    // 处理（settings 侧已校验过，这里防的是直接调用 install()）。
    bool hasBadLevel = false;
    if (!parseLevelName(level, s_minLevel)) {
        s_minLevel = spdlog::level::debug;
        hasBadLevel = true;
    }

    buildBackend();

    if (hasBadLevel)
        qWarning().noquote() << QStringLiteral(
            "AgentWorkbench: unknown log level \"%1\"; using debug").arg(level);

    qInfo().noquote() << QStringLiteral(
                             "AgentWorkbench: logging started → %1 "
                             "(rotating at %2 KB, keeping %3 files)")
                             .arg(logFilePath())
                             .arg(s_maxFileSize / 1024)
                             .arg(s_maxFiles);
}

void Logging::uninstall()
{
    qInstallMessageHandler(nullptr);
    teardownBackend();
}

QString Logging::logFilePath()
{
    if (s_logPath.isEmpty())
        return {};
    return s_logPath + QLatin1Char('/') + QStringLiteral("agentworkbench.log");
}

bool Logging::isValidLevelName(const QString &name)
{
    spdlog::level::level_enum parsed = spdlog::level::debug;
    return parseLevelName(name, parsed);
}

QString Logging::formatCommandLine(const QString &program, const QStringList &args)
{
    return TextUtils::formatCommandLine(program, args);
}

QString Logging::clampOutput(const QString &text, int limit)
{
    return TextUtils::clampOutput(text, limit);
}

// 把一条 Qt 消息拼成整行后交给后台线程。处理器里不做任何文件 IO——
// 时间戳、分类、位置在调用线程上拼装，写盘、轮转、stderr 镜像全部经队列
// 由后台线程执行。
//
// 行格式是既有契约（tst_agentscripts 按它断言，用户也照它排障），保持
// 逐字节不变：
// [yyyy-MM-dd hh:mm:ss.zzz] [LEVEL][category][file:line] msg
void Logging::messageHandler(QtMsgType type,
                             const QMessageLogContext &context,
                             const QString &msg)
{
    // 级别过滤放在最前：被过滤的消息连行都不用拼。spdlog 的级别序是数值
    // 越大越严重，低于阈值的直接丢弃（off = 全部丢弃）。
    if (static_cast<int>(toSpdlogLevel(type)) < static_cast<int>(s_minLevel))
        return;

    const char *level = "DEBUG";
    switch (type) {
    case QtInfoMsg:     level = "INFO";    break;
    case QtWarningMsg:  level = "WARNING"; break;
    case QtCriticalMsg: level = "CRITICAL"; break;
    case QtFatalMsg:    level = "FATAL";  break;
    case QtDebugMsg:
    default:            level = "DEBUG";   break;
    }

    const QString timestamp = QDateTime::currentDateTime()
                                  .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));

    // 分类前缀（awb.agents、awb.theme…）用于按模块过滤；Qt 默认分类不写。
    QString category;
    if (context.category && qstrcmp(context.category, "default") != 0)
        category = QStringLiteral(" [") + QString::fromLatin1(context.category)
                   + QLatin1Char(']');

    // 源码位置（file:line）；QT_MESSAGELOGCONTEXT 已在构建期全局开启，
    // release 下同样有值。
    QString location;
    if (context.file)
        location = QStringLiteral(" [%1:%2]").arg(context.file).arg(context.line);

    const QString line = QStringLiteral("[%1] [%2]%3%4 %5")
                             .arg(timestamp)
                             .arg(QString::fromLatin1(level))
                             .arg(category)
                             .arg(location)
                             .arg(msg);

    const QByteArray utf8 = line.toUtf8();
    const spdlog::string_view_t payload(utf8.constData(), utf8.size());

    // QtFatalMsg 同步直写：fatal 之后 Qt 会终止进程，队列里的这条来不及
    // 消费。代价是这行可能排到已入队日志的前面（sink 带互斥，不会撕行），
    // 换来最后一行不丢。
    if (type == QtFatalMsg && writeFatalNow(payload))
        return;

    if (s_logger) {
        // 队列侧的 log_msg_buffer 会把 payload 拷进自己的缓冲，utf8 在
        // 本函数返回后析构是安全的。
        s_logger->log(toSpdlogLevel(type), payload);
        return;
    }

    // 后端未就绪或构建失败：退回直写 stderr，消息不吞。
    fprintf(stderr, "%s\n", utf8.constData());
    fflush(stderr);
}

} // namespace awb::core
