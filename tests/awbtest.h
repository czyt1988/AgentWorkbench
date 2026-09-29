#ifndef AWBTEST_H
#define AWBTEST_H

#include <QList>
#include <QMetaMethod>
#include <QMetaType>
#include <QStringList>

class QObject;

// 各模块套件共享的测试注册表：一个模块一个可执行文件、内含多个测试类。
// 每个测试文件经 AWB_TEST() 在静态初始化期把自己登记进来；
// awbRunRegisteredTests()（见 tests/awbtest_runner.cpp）在 QCoreApplication
// 就绪后逐个构造并交给 QTest::qExec 执行。
using AwbTestFactory = QObject *(*)();
QList<AwbTestFactory> &awbTestRegistry();

struct AwbTestRegistrar
{
    explicit AwbTestRegistrar(AwbTestFactory factory)
    {
        awbTestRegistry().append(factory);
    }
};

#define AWB_TEST(Class)                                    \
    static QObject *awbMake##Class() { return new Class; } \
    static AwbTestRegistrar awbReg##Class(&awbMake##Class);

// QML 调用 Q_INVOKABLE 时，返回类型按 moc 记录的类型名字符串查 QMetaType
// 注册表。Gadget 的自动注册名是类全名（如 awb::core::OpResult），而 moc
// 按头文件书写形式记录——声明处写了短名（core::OpResult）就查不到，
// QML 里抛 "Unknown method return type"，按钮点击静默无响应。Qt 6 走
// 模板类型解析不受书写形式影响，所以这类缺陷只在 Qt 5 构建里爆，且
// 构建与既有测试全绿。QMetaMethod::returnType()/parameterType() 与 QML
// 调用端走同一条名字解析，此守卫逐个方法断言解析得到已注册类型。
// gadget 需先按 app/main.cpp 的注册序调 qRegisterMetaType。
inline QStringList awbUnresolvedQmlCallTypes(const QObject *object)
{
    QStringList failures;
    const QMetaObject *mo = object->metaObject();
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        const QMetaMethod method = mo->method(i);
        // 只查 QML 会调的方法（Q_INVOKABLE/槽）；构造器没有返回类型，
        // 信号的参数类型与本缺陷无关。
        if (method.methodType() != QMetaMethod::Method
            && method.methodType() != QMetaMethod::Slot) {
            continue;
        }

        const QString name = QString::fromLatin1(mo->className())
            + QStringLiteral("::") + QString::fromLatin1(method.name());
        if (method.returnType() == QMetaType::UnknownType) {
            failures << name + QStringLiteral("() return type '")
                + QString::fromLatin1(method.typeName())
                + QStringLiteral("' is not registered — QML calls throw "
                                "\"Unknown method return type\"");
        }
        for (int p = 0; p < method.parameterCount(); ++p) {
            if (method.parameterType(p) == QMetaType::UnknownType) {
                failures << name + QStringLiteral("() parameter #")
                    + QString::number(p) + QStringLiteral(" type '")
                    + QString::fromLatin1(method.parameterTypes().at(p))
                    + QStringLiteral("' is not registered — QML calls throw "
                                    "\"Unknown method parameter type\"");
            }
        }
    }
    return failures;
}

// 依次运行所有已注册的测试类，并处理 -functions 与按类过滤的用例参数
int awbRunRegisteredTests(int argc, char *argv[]);

#endif // AWBTEST_H
