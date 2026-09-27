#ifndef AWB_CORE_LOGGING_H
#define AWB_CORE_LOGGING_H

#include <QString>
#include <QStringList>
#include <QtLogging>

namespace awb::core {

/// 异步滚动文件日志：install() 装上 Qt 消息处理器，把 qDebug/qInfo/
/// qWarning/qCritical 写到 <dataRoot>/log/agentworkbench.log。
///
/// 写盘、轮转与 stderr 镜像全部由 spdlog 的后台线程完成——调用线程只负责
/// 拼日志行和入队，日志量再大也不会占用调用方（通常是 UI 线程）的 IO 时间。
/// 文件到上限后滚动：agentworkbench.log 顶成 .log.1，.1 顶成 .2，最旧的备份
/// 删除，任意时刻最多 maxFiles 个文件（当前 + 备份）。
///
/// 模块可以声明 Qt logging category（Q_LOGGING_CATEGORY，命名 "awb.<module>"）
/// 经分类输出，分类名会作为前缀写进日志行，便于按模块过滤。
class Logging
{
public:
    /// 轮转策略默认值：单文件 5 MB、共 3 个文件（当前 + 2 备份）→ 15 MB。
    static constexpr qint64 DEFAULT_MAX_FILE_SIZE = 5 * 1024 * 1024;
    static constexpr int DEFAULT_MAX_FILES = 3;

    /// 单条命令输出写进日志的逐字上限。
    static constexpr int DEFAULT_MAX_OUTPUT = 16 * 1024;

    /// 建日志目录、装消息处理器、启动后台写盘线程。启动期最先调用，之后的
    /// 任何失败都要落盘。可重复调用：旧后端先排空拆除，再建新的。
    ///
    /// @param directory   日志目录；空串用 Paths::logsDir()
    /// @param maxFileSize 单文件字节上限；非正数取默认值
    /// @param maxFiles    文件总数（含当前文件）；小于 1 按 1 处理
    static void install(const QString &directory = QString(),
                        qint64 maxFileSize = DEFAULT_MAX_FILE_SIZE,
                        int maxFiles = DEFAULT_MAX_FILES);

    /// 排空队列、停掉后台线程、恢复默认消息处理器。
    /// 退出路径必须调用：不调则队列里还没写盘的尾部日志会丢。
    static void uninstall();

    /// 当前日志文件的绝对路径；install() 之前是空串。
    static QString logFilePath();

    /// 命令行的展示形式，转发到 TextUtils 的规范实现。
    static QString formatCommandLine(const QString &program,
                                     const QStringList &args = QStringList());

    /// 输出截断，转发到 TextUtils 的规范实现。
    static QString clampOutput(const QString &text,
                               int limit = DEFAULT_MAX_OUTPUT);

private:
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext &context,
                               const QString &msg);
};

} // namespace awb::core

#endif // AWB_CORE_LOGGING_H
