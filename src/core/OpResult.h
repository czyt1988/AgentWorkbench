#ifndef AWB_CORE_OPRESULT_H
#define AWB_CORE_OPRESULT_H

#include <QString>

namespace awb::core {

// Result of a fallible synchronous operation. Crossing a module boundary
// never throws — a failure travels as { ok = false, error = <readable
// reason> } (01-architecture.md §6).
struct OpResult
{
    bool ok = true;
    QString error;

    static OpResult success();
    static OpResult failure(const QString &error);

    explicit operator bool() const { return ok; }
};

} // namespace awb::core

#endif // AWB_CORE_OPRESULT_H
