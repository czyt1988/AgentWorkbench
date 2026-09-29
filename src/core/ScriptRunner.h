#ifndef AWB_CORE_SCRIPTRUNNER_H
#define AWB_CORE_SCRIPTRUNNER_H

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>

namespace awb::core {

// Streaming execution of one-shot commands (install / update / version /
// setup.
//
// A run is identified by `key` (usually the agent id). Starting a new run
// with the same key invalidates every pending callback of the previous run
// — its process is killed and its signals are dropped — so stale completion
// logic can never clear the state of a newer run (the "epoch" mechanism).
// Output is streamed through outputChunk() while the command runs and
// clamped in the caller's log via TextUtils::clampOutput().
class ScriptRunner : public QObject
{
    Q_OBJECT

public:
    explicit ScriptRunner(QObject *parent = nullptr);

    // Run `program args` directly (no shell interpretation).
    // timeoutMs <= 0 means no timeout.
    void run(const QString &key, const QString &program,
             const QStringList &args, int timeoutMs = 0,
             bool mergeChannels = true);

    // Run a raw command string through `cmd /c` (Windows shell form used by
    // install/update/version commands).
    void runShell(const QString &key, const QString &command,
                  int timeoutMs = 0, bool mergeChannels = true);

    // Windows quoting workaround: QProcess escapes an embedded " as \" which
    // cmd.exe misreads, so write the command into a temp .cmd file and run
    // that instead.
    void runBatch(const QString &key, const QString &command, int timeoutMs = 0);

    // True while a run with this key is in flight.
    bool isRunning(const QString &key) const;

Q_SIGNALS:
    // Emitted as output arrives, for live display on a card.
    void outputChunk(const QString &key, const QString &text);

    // Emitted exactly once per accepted run. `ok` is true only for a clean
    // exit (code 0, no timeout, actually started). stdOut/stdErr are
    // channel-separated — merged runs report everything on stdOut.
    // `error` explains a start failure or timeout.
    void finished(const QString &key, bool ok, int exitCode,
                  const QString &stdOut, const QString &stdErr,
                  const QString &error);

private:
    struct Slot
    {
        int epoch = 0;            // bumped per run; stale callbacks compare
        QProcess *proc = nullptr; // active process, null when idle
        bool started = false;     // `started` fired for the active process
        bool timedOut = false;    // active run was killed by its timeout
        bool merged = false;      // channel mode of the active run
        QTemporaryFile *batchFile = nullptr; // cleanup after the run
        // Accumulated per channel: readyRead consumes from QProcess, so
        // finished() must report everything seen so far plus the final tail.
        QByteArray rawOut;
        QByteArray rawErr;
        // Not yet streamed through outputChunk: a chunk ending inside a
        // multi-byte UTF-8 sequence is held back and decoded together with
        // the next chunk, instead of garbling the split character.
        QByteArray pendingOut;
        QByteArray pendingErr;
    };

    // True while `epoch` is still the current run for `key` (and, when a
    // process pointer is given, while that process owns the slot).
    bool isCurrent(const QString &key, int epoch,
                   const QProcess *proc = nullptr) const;

    // Tear down whatever is running under `key` (kill + drop callbacks).
    void invalidate(const QString &key);

    QHash<QString, Slot> m_slots;
};

} // namespace awb::core

#endif // AWB_CORE_SCRIPTRUNNER_H
