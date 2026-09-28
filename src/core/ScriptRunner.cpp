#include "core/ScriptRunner.h"

#include "core/ProcessRunner.h"

#include <QDir>
#include <QTimer>

namespace awb::core {

namespace {

// Length (0..3) of the incomplete UTF-8 sequence at the end of `buf` —
// those bytes are held back until the next chunk completes them, so a
// multi-byte character split across two readyRead() deliveries decodes
// correctly instead of turning into replacement characters.
int incompleteUtf8Tail(const QByteArray &buf)
{
    if (buf.isEmpty())
        return 0;
    const int n = buf.size();
    int back = 0;
    while (back < 3 && (n - 1 - back) >= 0
           && (static_cast<uchar>(buf.at(n - 1 - back)) & 0xC0) == 0x80)
        ++back;
    if (n - 1 - back < 0)
        return 0; // only continuation bytes — invalid, decode as-is
    const uchar lead = static_cast<uchar>(buf.at(n - 1 - back));
    if (back == 3 && ((lead & 0xC0) == 0x80))
        return 0; // 4+ continuation bytes — invalid, decode as-is
    int total;
    if ((lead & 0x80) == 0)
        total = 1;
    else if ((lead & 0xE0) == 0xC0)
        total = 2;
    else if ((lead & 0xF0) == 0xE0)
        total = 3;
    else if ((lead & 0xF8) == 0xF0)
        total = 4;
    else
        return 0; // not a lead byte — invalid, decode as-is
    const int present = back + 1;
    return present < total ? present : 0;
}

} // namespace

ScriptRunner::ScriptRunner(QObject *parent)
    : QObject(parent)
{
}

bool ScriptRunner::isCurrent(const QString &key, int epoch,
                             const QProcess *proc) const
{
    const auto it = m_slots.constFind(key);
    if (it == m_slots.cend() || it->epoch != epoch)
        return false;
    if (proc && it->proc != proc)
        return false;
    return true;
}

bool ScriptRunner::isRunning(const QString &key) const
{
    const auto it = m_slots.constFind(key);
    return it != m_slots.cend() && it->proc != nullptr;
}

void ScriptRunner::invalidate(const QString &key)
{
    auto it = m_slots.find(key);
    if (it == m_slots.end())
        return;
    Slot &slot = it.value();
    if (slot.proc) {
        QProcess *old = slot.proc;
        slot.proc = nullptr;
        // Old callbacks compare their captured proc pointer against
        // slot.proc; disconnect as well so a kill() cannot reach us at all.
        old->disconnect(this);
        if (old->state() != QProcess::NotRunning)
            old->kill();
        old->deleteLater();
    }
    if (slot.batchFile) {
        slot.batchFile->deleteLater();
        slot.batchFile = nullptr;
    }
}

void ScriptRunner::run(const QString &key, const QString &program,
                       const QStringList &args, int timeoutMs,
                       bool mergeChannels)
{
    invalidate(key);
    Slot &slot = m_slots[key];
    const int epoch = ++slot.epoch;

    auto *proc = new QProcess(this);
    if (mergeChannels)
        proc->setProcessChannelMode(QProcess::MergedChannels);
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

    // Stream output as it arrives, so the UI can show progress live. The
    // bytes are also accumulated here — reading consumes them, and
    // finished() has to report the complete text. A chunk ending mid
    // character is held back (pendingOut) and decoded with the next one.
    connect(proc, &QProcess::readyReadStandardOutput, this,
            [this, key, epoch, proc]() {
                if (!isCurrent(key, epoch, proc))
                    return;
                const QByteArray data = proc->readAllStandardOutput();
                Slot &slot = m_slots[key];
                slot.rawOut.append(data);
                slot.pendingOut.append(data);
                const int hold = incompleteUtf8Tail(slot.pendingOut);
                const int emitLen = slot.pendingOut.size() - hold;
                if (emitLen > 0) {
                    emit outputChunk(key, ProcessRunner::decodeOutput(
                                              slot.pendingOut.left(emitLen)));
                    slot.pendingOut.remove(0, emitLen);
                }
            });
    connect(proc, &QProcess::readyReadStandardError, this,
            [this, key, epoch, proc]() {
                if (!isCurrent(key, epoch, proc))
                    return;
                const QByteArray data = proc->readAllStandardError();
                Slot &slot = m_slots[key];
                slot.rawErr.append(data);
                slot.pendingErr.append(data);
                const int hold = incompleteUtf8Tail(slot.pendingErr);
                const int emitLen = slot.pendingErr.size() - hold;
                if (emitLen > 0) {
                    emit outputChunk(key, ProcessRunner::decodeOutput(
                                              slot.pendingErr.left(emitLen)));
                    slot.pendingErr.remove(0, emitLen);
                }
            });

    connect(proc, &QProcess::started, this, [this, key, epoch, proc]() {
        if (!isCurrent(key, epoch, proc))
            return;
        m_slots[key].started = true;
    });

    // Normal completion (including a timeout kill, which arrives here with
    // a non-zero exit code).
    // Qt 5.15 的 QProcess::finished 是重载信号（弃用的单参版仍在），
    // qOverload 按参数表消歧，两版通用。
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this, key, epoch, proc, timeoutMs](int exitCode,
                                                QProcess::ExitStatus) {
                if (!isCurrent(key, epoch, proc))
                    return;
                Slot &slot = m_slots[key];
                const QByteArray tailOut = proc->readAllStandardOutput();
                slot.rawOut.append(tailOut);
                slot.pendingOut.append(tailOut);
                if (!slot.merged) {
                    const QByteArray tailErr = proc->readAllStandardError();
                    slot.rawErr.append(tailErr);
                    slot.pendingErr.append(tailErr);
                }
                // Flush any bytes still held back for a split character —
                // the stream ended, so decode them as they are.
                if (!slot.pendingOut.isEmpty()) {
                    emit outputChunk(key,
                                     ProcessRunner::decodeOutput(slot.pendingOut));
                    slot.pendingOut.clear();
                }
                if (!slot.pendingErr.isEmpty()) {
                    emit outputChunk(key,
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
                emit finished(key, !timedOut && exitCode == 0, exitCode,
                              out, err, error);
            });

    // The process never started. Anything that dies after `started` is
    // reported through finished() instead.
    connect(proc, &QProcess::errorOccurred, this,
            [this, key, epoch, proc](QProcess::ProcessError) {
                if (!isCurrent(key, epoch, proc))
                    return;
                Slot &slot = m_slots[key];
                if (slot.started || slot.timedOut)
                    return;
                const QString detail = proc->errorString();
                slot.proc = nullptr;
                proc->deleteLater();
                if (slot.batchFile) {
                    slot.batchFile->deleteLater();
                    slot.batchFile = nullptr;
                }
                emit finished(key, false, -1, QString(), QString(),
                              QStringLiteral("failed to start: %1").arg(detail));
            });

    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, this, [this, key, epoch, proc]() {
            if (!isCurrent(key, epoch, proc))
                return;
            Slot &slot = m_slots[key];
            if (!slot.started || slot.proc->state() == QProcess::NotRunning)
                return;
            slot.timedOut = true;
            slot.proc->kill(); // finished() reports the timeout
        });
    }

    proc->start();
}

void ScriptRunner::runShell(const QString &key, const QString &command,
                            int timeoutMs, bool mergeChannels)
{
    run(key, QStringLiteral("cmd"), {QStringLiteral("/c"), command},
        timeoutMs, mergeChannels);
}

void ScriptRunner::runBatch(const QString &key, const QString &command,
                            int timeoutMs)
{
    // Write the command into a temp .cmd file: QProcess on Windows escapes
    // an embedded " as \" (the C convention), but cmd.exe treats \ as a
    // literal, corrupting paths ("invalid filename syntax"). Running a batch
    // file sidesteps argv quoting entirely.
    auto *batchFile = new QTemporaryFile(
        QDir::tempPath() + QStringLiteral("/agentworkbench_XXXXXX.cmd"), this);
    if (!batchFile->open()) {
        const QString detail = batchFile->errorString();
        batchFile->deleteLater();
        invalidate(key);
        Slot &slot = m_slots[key];
        const int epoch = ++slot.epoch;
        // Queued so callers that connect after calling runBatch() still see
        // it; dropped if a newer run for this key starts first.
        QTimer::singleShot(0, this, [this, key, epoch, detail]() {
            if (!isCurrent(key, epoch))
                return;
            emit finished(key, false, -1, QString(), QString(),
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
    // Owned by the slot until the run finishes (cmd.exe still reads it).
    if (m_slots.contains(key))
        m_slots[key].batchFile = batchFile;
}

} // namespace awb::core
