#include "core/ScriptRunner.h"

#include "core/ProcessRunner.h"

#include <QDir>
#include <QTimer>

namespace awb::core {

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

    // Stream output as it arrives, so the UI can show progress live. The
    // bytes are also accumulated here — reading consumes them, and
    // finished() has to report the complete text.
    connect(proc, &QProcess::readyReadStandardOutput, this,
            [this, key, epoch, proc]() {
                if (!isCurrent(key, epoch, proc))
                    return;
                const QByteArray data = proc->readAllStandardOutput();
                m_slots[key].rawOut.append(data);
                emit outputChunk(key, ProcessRunner::decodeOutput(data));
            });
    connect(proc, &QProcess::readyReadStandardError, this,
            [this, key, epoch, proc]() {
                if (!isCurrent(key, epoch, proc))
                    return;
                const QByteArray data = proc->readAllStandardError();
                m_slots[key].rawErr.append(data);
                emit outputChunk(key, ProcessRunner::decodeOutput(data));
            });

    connect(proc, &QProcess::started, this, [this, key, epoch, proc]() {
        if (!isCurrent(key, epoch, proc))
            return;
        m_slots[key].started = true;
    });

    // Normal completion (including a timeout kill, which arrives here with
    // a non-zero exit code).
    connect(proc, &QProcess::finished, this,
            [this, key, epoch, proc, timeoutMs](int exitCode,
                                                QProcess::ExitStatus) {
                if (!isCurrent(key, epoch, proc))
                    return;
                Slot &slot = m_slots[key];
                slot.rawOut.append(proc->readAllStandardOutput());
                if (!slot.merged)
                    slot.rawErr.append(proc->readAllStandardError());
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
