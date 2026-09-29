#include "core/OpResult.h"

namespace awb::core {

/**
 * @brief 构造一个成功结果
 *
 * @return { ok = true, error 为空 }
 */
OpResult OpResult::success()
{
    return {};
}

/**
 * @brief 构造一个失败结果
 *
 * @param error 可读的失败原因（英文），最终可能直接展示给用户
 * @return { ok = false, error = 传入原因 }
 */
OpResult OpResult::failure(const QString &error)
{
    OpResult r;
    r.ok = false;
    r.error = error;
    return r;
}

} // namespace awb::core
