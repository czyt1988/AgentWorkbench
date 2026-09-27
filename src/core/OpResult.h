#ifndef AWB_CORE_OPRESULT_H
#define AWB_CORE_OPRESULT_H

#include <QMetaObject>
#include <QString>

namespace awb::core {

// Result of a fallible synchronous operation. Crossing a module boundary
// never throws — a failure travels as { ok = false, error = <readable
// reason> }. Q_GADGET so QML can read `.ok` and
// `.error` off Q_INVOKABLE results (e.g. UiServices::copyText).
struct OpResult
{
    Q_GADGET
    Q_PROPERTY(bool ok MEMBER ok)
    Q_PROPERTY(QString error MEMBER error)

public:
    bool ok = true;
    QString error;

    static OpResult success();
    static OpResult failure(const QString &error);

    explicit operator bool() const { return ok; }
};

} // namespace awb::core

#endif // AWB_CORE_OPRESULT_H
