#include "core/OpResult.h"

namespace awb::core {

OpResult OpResult::success()
{
    return {};
}

OpResult OpResult::failure(const QString &error)
{
    OpResult r;
    r.ok = false;
    r.error = error;
    return r;
}

} // namespace awb::core
