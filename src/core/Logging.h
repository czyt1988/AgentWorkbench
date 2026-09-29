#ifndef AWB_CORE_LOGGING_H
#define AWB_CORE_LOGGING_H

#include <QDebug>
#include <QLoggingCategory>
#include <QString>
#include <QStringList>
// QtLogging 汇总头是 Qt 6.5 引入的；QLoggingCategory 已足够。
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QtLogging>
#endif

/// 应用级事件日志的 category（"awb.event"）：由下方的 AWB_* 宏绑定。
/// 声明在全局作用域——宏展开处按未限定名查找，放进命名空间就解析不到。
Q_DECLARE_LOGGING_CATEGORY(lcAwbEvent)

/// 性能埋点日志的 category（"awb.perf"）：由 AWB_PERF 宏绑定。
/// 同样声明在全局作用域（原因同上）。
Q_DECLARE_LOGGING_CATEGORY(lcAwbPerf)

/// 应用级事件日志的分级宏——级别取宏名，分类固定 awb.event：行前缀会带
/// 该分类（供按事件过滤），将来的 UI 日志通道也按它分流。模块内部的一般
/// 日志仍用 qInfo()/qWarning() 加 [module] 前缀。
#define AWB_DEBUG     qCDebug(lcAwbEvent)
#define AWB_INFO      qCInfo(lcAwbEvent)
#define AWB_WARNING   qCWarning(lcAwbEvent)
#define AWB_CRITICAL  qCCritical(lcAwbEvent)

/// 性能埋点专用宏：记录耗时观测（缓存加载、目录扫描、模型重建等），
/// 只在排查性能问题时打开。开关分两层：
///  - 编译期：CMake 给 awb_core 及其下游仅在 Debug 配置定义
///    AWB_PERF_ENABLED——release 构建里本宏展开为空语句，任何日志规则
///    都打不开，参数表达式也不求值；
///  - 运行期（Debug 构建）：分类默认关闭，用
///    QT_LOGGING_RULES "awb.perf.debug=true" 开启，输出经 Logging 的
///    消息处理器落盘。
#ifdef AWB_PERF_ENABLED
#define AWB_PERF qCDebug(lcAwbPerf)
#else
#define AWB_PERF while (false) QNoDebug()
#endif

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
    /// @param directory       日志目录；空串用 Paths::logsDir()
    /// @param maxFileSize     单文件字节上限；非正数取默认值
    /// @param maxFiles        文件总数（含当前文件）；小于 1 按 1 处理
    /// @param level           最低落盘级别（debug/info/warning/critical/off）；
    ///                        非法值告警后按 debug 处理
    /// @param mirrorToStderr  是否同时镜像到 stderr
    static void install(const QString &directory = QString(),
                        qint64 maxFileSize = DEFAULT_MAX_FILE_SIZE,
                        int maxFiles = DEFAULT_MAX_FILES,
                        const QString &level = QStringLiteral("debug"),
                        bool mirrorToStderr = true);

    /// 排空队列、停掉后台线程、恢复默认消息处理器。
    /// 退出路径必须调用：不调则队列里还没写盘的尾部日志会丢。
    static void uninstall();

    /// 当前日志文件的绝对路径；install() 之前是空串。
    static QString logFilePath();

    /// 级别名是否是 install()/settings.json 接受的取值之一
    /// （debug、info、warning、critical、off）。Settings 用它校验
    /// logging.level，避免两处各持一份取值清单。
    static bool isValidLevelName(const QString &name);

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
