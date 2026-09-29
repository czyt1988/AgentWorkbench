#include "core/ScriptRunner.h"

#include "core/ProcessRunner.h"

#include <QDir>
#include <QTimer>

namespace awb::core {

namespace {

/**
 * @brief 量出 buf 末尾不完整 UTF-8 序列的长度
 *
 * 这几个字节先扣留，等下一块补齐再解码——跨两次 readyRead() 到达的
 * 多字节字符因此能正确解码，而不是变成替换符。
 *
 * @param buf 本次到达的原始字节
 * @return 0..3；末尾是完整字符或序列非法时返回 0（按原样解码）
 */
int incompleteUtf8Tail(const QByteArray &buf)
{
    if (buf.isEmpty()) {
        return 0;
    }
    const int n = buf.size();
    int back = 0;
    while (back < 3 && (n - 1 - back) >= 0
           && (static_cast<uchar>(buf.at(n - 1 - back)) & 0xC0) == 0x80) {
        ++back;
    }
    if (n - 1 - back < 0) {
        return 0; // 只有续字节——非法序列，按原样解码
    }
    const uchar lead = static_cast<uchar>(buf.at(n - 1 - back));
    if (back == 3 && ((lead & 0xC0) == 0x80)) {
        return 0; // 4 个以上续字节——非法序列，按原样解码
    }
    int total;
    if ((lead & 0x80) == 0) {
        total = 1;
    }
    else if ((lead & 0xE0) == 0xC0) {
        total = 2;
    }
    else if ((lead & 0xF0) == 0xE0) {
        total = 3;
    }
    else if ((lead & 0xF8) == 0xF0) {
        total = 4;
    }
    else {
        return 0; // 不是首字节——非法序列，按原样解码
    }
    const int present = back + 1;
    return present < total ? present : 0;
}

} // namespace

/**
 * @brief 构造脚本执行器
 *
 * @param parent QObject 父项
 */
ScriptRunner::ScriptRunner(QObject *parent)
    : QObject(parent)
{
}

/**
 * @brief 判断 epoch 是否仍是该 key 的当前运行
 *
 * @param key 运行标识
 * @param epoch 回调捕获的 epoch 值
 * @param proc 可选；回调捕获的进程指针，还须与槽位持有的一致
 * @return 全部匹配时返回 true
 */
bool ScriptRunner::isCurrent(const QString &key, int epoch,
                             const QProcess *proc) const
{
    const auto it = m_slots.constFind(key);
    if (it == m_slots.cend() || it->epoch != epoch) {
        return false;
    }
    if (proc && it->proc != proc) {
        return false;
    }
    return true;
}

/**
 * @brief 查询该 key 是否有运行在进行
 *
 * @param key 运行标识
 * @return 槽位持有进程时返回 true
 */
bool ScriptRunner::isRunning(const QString &key) const
{
    const auto it = m_slots.constFind(key);
    return it != m_slots.cend() && it->proc != nullptr;
}

/**
 * @brief 推翻该 key 下正在运行的一切
 *
 * 杀掉进程、丢弃回调（陈旧回调比对的 epoch 已变），临时批处理文件一并清理。
 *
 * @param key 运行标识
 */
void ScriptRunner::invalidate(const QString &key)
{
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        return;
    }
    Slot &slot = it.value();
    if (slot.proc) {
        QProcess *old = slot.proc;
        slot.proc = nullptr;
        // 旧回调拿捕获的 proc 指针与 slot.proc 比对；再 disconnect 一道，
        // kill() 引起的任何信号都到不了这里。
        old->disconnect(this);
        if (old->state() != QProcess::NotRunning) {
            old->kill();
        }
        old->deleteLater();
    }
    if (slot.batchFile) {
        slot.batchFile->deleteLater();
        slot.batchFile = nullptr;
    }
}

/**
 * @brief 直接运行一条命令并流式回报输出
 *
 * 输出边到边发（UI 实时看到进度），同时累计在本函数里——读取即消耗，
 * finished() 要汇报完整文本。块尾停在字符中间的字节先扣留
 * （pendingOut/pendingErr），与下一块一起解码。超时经 singleShot 定时器
 * 杀进程，超时退出同样走 finished()。
 *
 * @param key 运行标识
 * @param program 程序路径
 * @param args 命令行参数
 * @param timeoutMs 超时毫秒数；<= 0 表示不限时
 * @param mergeChannels true 时 stderr 并入 stdout
 */
void ScriptRunner::run(const QString &key, const QString &program,
                       const QStringList &args, int timeoutMs,
                       bool mergeChannels)
{
    invalidate(key);
    Slot &slot = m_slots[key];
    const int epoch = ++slot.epoch;

    auto *proc = new QProcess(this);
    if (mergeChannels) {
        proc->setProcessChannelMode(QProcess::MergedChannels);
    }
    proc->setProgram(program);
    proc->setArguments(args);
    slot.proc = proc;
    slot.timedOut = false;
    slot.started = false;
    slot.merged = mergeChannels;
    slot.rawOut.clear();
    slot.rawErr.clear();
    slot.pendingOut.clear();
    slot.pendingErr.clear();

    connect(proc, &QProcess::readyReadStandardOutput, this,
            [this, key, epoch, proc]() {
                if (!isCurrent(key, epoch, proc)) {
                    return;
                }
                const QByteArray data = proc->readAllStandardOutput();
                Slot &slot = m_slots[key];
                slot.rawOut.append(data);
                slot.pendingOut.append(data);
                const int hold = incompleteUtf8Tail(slot.pendingOut);
                const int emitLen = slot.pendingOut.size() - hold;
                if (emitLen > 0) {
                    Q_EMIT outputChunk(key, ProcessRunner::decodeOutput(
                                              slot.pendingOut.left(emitLen)));
                    slot.pendingOut.remove(0, emitLen);
                }
            });
    connect(proc, &QProcess::readyReadStandardError, this,
            [this, key, epoch, proc]() {
                if (!isCurrent(key, epoch, proc)) {
                    return;
                }
                const QByteArray data = proc->readAllStandardError();
                Slot &slot = m_slots[key];
                slot.rawErr.append(data);
                slot.pendingErr.append(data);
                const int hold = incompleteUtf8Tail(slot.pendingErr);
                const int emitLen = slot.pendingErr.size() - hold;
                if (emitLen > 0) {
                    Q_EMIT outputChunk(key, ProcessRunner::decodeOutput(
                                              slot.pendingErr.left(emitLen)));
                    slot.pendingErr.remove(0, emitLen);
                }
            });

    connect(proc, &QProcess::started, this, [this, key, epoch, proc]() {
        if (!isCurrent(key, epoch, proc)) {
            return;
        }
        m_slots[key].started = true;
    });

    // 正常完成（含超时被杀——它也走这里，带非零退出码）。
    // Qt 5.15 的 QProcess::finished 是重载信号（弃用的单参版仍在），
    // qOverload 按参数表消歧，两版通用。
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this, key, epoch, proc, timeoutMs](int exitCode,
                                                QProcess::ExitStatus) {
                if (!isCurrent(key, epoch, proc)) {
                    return;
                }
                Slot &slot = m_slots[key];
                const QByteArray tailOut = proc->readAllStandardOutput();
                slot.rawOut.append(tailOut);
                slot.pendingOut.append(tailOut);
                if (!slot.merged) {
                    const QByteArray tailErr = proc->readAllStandardError();
                    slot.rawErr.append(tailErr);
                    slot.pendingErr.append(tailErr);
                }
                // 流已结束，为劈开字符扣留的字节按原样冲出去。
                if (!slot.pendingOut.isEmpty()) {
                    Q_EMIT outputChunk(key,
                                     ProcessRunner::decodeOutput(slot.pendingOut));
                    slot.pendingOut.clear();
                }
                if (!slot.pendingErr.isEmpty()) {
                    Q_EMIT outputChunk(key,
                                     ProcessRunner::decodeOutput(slot.pendingErr));
                    slot.pendingErr.clear();
                }
                const QString out = ProcessRunner::decodeOutput(slot.rawOut);
                const QString err = ProcessRunner::decodeOutput(slot.rawErr);
                const bool timedOut = slot.timedOut;

                slot.proc = nullptr;
                proc->deleteLater();
                if (slot.batchFile) {
                    slot.batchFile->deleteLater();
                    slot.batchFile = nullptr;
                }

                const QString error =
                    timedOut
                        ? QStringLiteral("timed out after %1 ms").arg(timeoutMs)
                        : QString();
                Q_EMIT finished(key, !timedOut && exitCode == 0, exitCode,
                              out, err, error);
            });

    // 进程从未起来。起来之后再死掉的（含超时）都经 finished() 汇报。
    connect(proc, &QProcess::errorOccurred, this,
            [this, key, epoch, proc](QProcess::ProcessError) {
                if (!isCurrent(key, epoch, proc)) {
                    return;
                }
                Slot &slot = m_slots[key];
                if (slot.started || slot.timedOut) {
                    return;
                }
                const QString detail = proc->errorString();
                slot.proc = nullptr;
                proc->deleteLater();
                if (slot.batchFile) {
                    slot.batchFile->deleteLater();
                    slot.batchFile = nullptr;
                }
                Q_EMIT finished(key, false, -1, QString(), QString(),
                              QStringLiteral("failed to start: %1").arg(detail));
            });

    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, this, [this, key, epoch, proc]() {
            if (!isCurrent(key, epoch, proc)) {
                return;
            }
            Slot &slot = m_slots[key];
            if (!slot.started || slot.proc->state() == QProcess::NotRunning) {
                return;
            }
            slot.timedOut = true;
            slot.proc->kill(); // 超时退出经 finished() 汇报
        });
    }

    proc->start();
}

/**
 * @brief 把命令串经 cmd /c 运行
 *
 * @param key 运行标识
 * @param command 原始命令行字符串
 * @param timeoutMs 超时毫秒数；<= 0 表示不限时
 * @param mergeChannels true 时 stderr 并入 stdout
 * @sa run
 */
void ScriptRunner::runShell(const QString &key, const QString &command,
                            int timeoutMs, bool mergeChannels)
{
    run(key, QStringLiteral("cmd"), {QStringLiteral("/c"), command},
        timeoutMs, mergeChannels);
}

/**
 * @brief 把命令写进临时 .cmd 文件再运行
 *
 * QProcess 在 Windows 上把内嵌 " 转义成 \"（C 语言惯例），而 cmd.exe 把
 * 反斜杠当字面字符，路径会被弄坏（"invalid filename syntax"）。跑批处理
 * 文件完全绕开 argv 引号问题。
 *
 * @param key 运行标识
 * @param command 原始命令行字符串
 * @param timeoutMs 超时毫秒数；<= 0 表示不限时
 * @sa run
 */
void ScriptRunner::runBatch(const QString &key, const QString &command,
                            int timeoutMs)
{
    auto *batchFile = new QTemporaryFile(
        QDir::tempPath() + QStringLiteral("/agentworkbench_XXXXXX.cmd"), this);
    if (!batchFile->open()) {
        const QString detail = batchFile->errorString();
        batchFile->deleteLater();
        invalidate(key);
        Slot &slot = m_slots[key];
        const int epoch = ++slot.epoch;
        // 排队投递：runBatch() 之后才 connect 的调用方也能收到；
        // 若该 key 先来了更新的运行则被丢弃。
        QTimer::singleShot(0, this, [this, key, epoch, detail]() {
            if (!isCurrent(key, epoch)) {
                return;
            }
            Q_EMIT finished(key, false, -1, QString(), QString(),
                          QStringLiteral("cannot create the temporary batch "
                                         "file: %1").arg(detail));
        });
        return;
    }
    batchFile->write(QStringLiteral("@echo off\r\n").toLocal8Bit());
    batchFile->write(command.toLocal8Bit());
    batchFile->write("\r\n");
    batchFile->close();

    run(key, QStringLiteral("cmd"), {QStringLiteral("/c"),
                                     batchFile->fileName()},
        timeoutMs, true);
    // 文件由槽位持有直到运行结束（cmd.exe 还在读它）。
    if (m_slots.contains(key)) {
        m_slots[key].batchFile = batchFile;
    }
}

} // namespace awb::core
