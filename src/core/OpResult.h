#ifndef AWB_CORE_OPRESULT_H
#define AWB_CORE_OPRESULT_H

#include <QMetaObject>
#include <QString>

namespace awb::core {

/// 可失败同步操作的结果：跨模块边界不抛异常，失败以 { ok = false, error = ... } 传递。
///
/// Q_GADGET 让 QML 能直接读 Q_INVOKABLE 返回值的 .ok 与 .error（如 UiServices::copyText）。
struct OpResult
{
    Q_GADGET
    Q_PROPERTY(bool ok MEMBER ok)
    Q_PROPERTY(QString error MEMBER error)

public:
    bool ok = true;    ///< 操作是否成功
    QString error;     ///< 失败原因（可直接展示给用户，英文 tr() 源串）

    // 构造一个成功结果
    static OpResult success();

    // 构造一个带失败原因的失败结果
    static OpResult failure(const QString &error);

    // 等同于 ok，允许 `if (result)` 式判断
    explicit operator bool() const { return ok; }
};

} // namespace awb::core

#endif // AWB_CORE_OPRESULT_H
