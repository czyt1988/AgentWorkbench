#ifndef AWB_CORE_SCRIPTRUNNER_H
#define AWB_CORE_SCRIPTRUNNER_H

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>

namespace awb::core {

/// 一次性命令（install / update / version / setup）的流式执行器。
///
/// 一次运行以 key（通常是 agent id）标识。同一 key 发起新运行会使旧运行
/// 的一切回调失效——进程被杀、信号被丢弃——陈旧的完成逻辑因此永远碰不到
/// 新运行的状态（"epoch" 机制）。命令运行期间输出经 outputChunk() 流式
/// 送出，调用方日志里再经 TextUtils::clampOutput() 截断。
class ScriptRunner : public QObject
{
    Q_OBJECT

public:
    explicit ScriptRunner(QObject *parent = nullptr);

    // 直接运行 program args（不经 shell 解释）。
    // timeoutMs <= 0 表示不限时。
    void run(const QString &key, const QString &program,
             const QStringList &args, int timeoutMs = 0,
             bool mergeChannels = true);

    // 把原始命令串经 cmd /c 运行（install/update/version 命令的 Windows shell 形态）
    void runShell(const QString &key, const QString &command,
                  int timeoutMs = 0, bool mergeChannels = true);

    // Windows 引号规避：QProcess 把内嵌 " 转义成 \" 而 cmd.exe 读错，
    // 因此把命令写进临时 .cmd 文件再运行该文件
    void runBatch(const QString &key, const QString &command, int timeoutMs = 0);

    // 该 key 的运行是否仍在进行
    bool isRunning(const QString &key) const;

Q_SIGNALS:
    /**
     * @brief 输出到达时发射，供卡片实时显示
     * @param key 运行标识
     * @param text 本次到达的解码文本（跨多字节字符的块会被扣留拼齐）
     */
    void outputChunk(const QString &key, const QString &text);

    /**
     * @brief 每个被接受的运行恰好在结束时发射一次
     * @param key 运行标识
     * @param ok 仅在干净退出（退出码 0、未超时、确实启动过）时为 true
     * @param exitCode 进程退出码；启动失败时为 -1
     * @param stdOut stdout 累计文本（合并通道的运行全部记在这里）
     * @param stdErr stderr 累计文本
     * @param error 启动失败或超时的原因；干净运行为空串
     */
    void finished(const QString &key, bool ok, int exitCode,
                  const QString &stdOut, const QString &stdErr,
                  const QString &error);

private:
    /// 每个 key 一个执行槽。
    struct Slot
    {
        int epoch = 0;            ///< 每次运行自增；陈旧回调靠它比对失效
        QProcess *proc = nullptr; ///< 当前进程，空闲时为空
        bool started = false;     ///< 当前进程是否已触发 started
        bool timedOut = false;    ///< 当前运行是否被超时杀掉
        bool merged = false;      ///< 当前运行的通道模式
        QTemporaryFile *batchFile = nullptr; ///< 运行结束后清理的批处理临时文件
        // 按通道累计：readyRead 会消耗 QProcess 里的字节，
        // finished() 必须汇报见过的全部内容加上最后的尾巴。
        QByteArray rawOut;
        QByteArray rawErr;
        // 尚未流经 outputChunk 的字节：块尾停在多字节 UTF-8 序列中间时先扣留，
        // 与下一块一起解码，避免把被劈开的字符解码成乱码。
        QByteArray pendingOut;
        QByteArray pendingErr;
    };

    // epoch 是否仍是该 key 的当前运行（给了 proc 时还要求该进程仍持有槽位）
    bool isCurrent(const QString &key, int epoch,
                   const QProcess *proc = nullptr) const;

    // 推翻该 key 下正在跑的一切（杀进程 + 丢弃回调）
    void invalidate(const QString &key);

    QHash<QString, Slot> m_slots;  ///< key -> 执行槽
};

} // namespace awb::core

#endif // AWB_CORE_SCRIPTRUNNER_H
