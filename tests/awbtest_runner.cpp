#include "awbtest.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QMetaMethod>
#include <QMetaObject>
#include <QSet>
#include <QStringList>
#include <QTest>

#include <cstdio>
#include <utility>

namespace {

/**
 * @brief 取一个测试类自有的全部用例名
 *
 * 只收该类自身声明的方法，排除从 QObject 继承来的（如 deleteLater），
 * 并跳过 init / cleanup / initTestCase / cleanupTestCase 这几个 QTest
 * 框架钩子——它们不是用例，混进 -functions 输出会误导按名字跑用例的调用方。
 *
 * @param test 已构造的测试对象
 * @return 用例名列表（仅方法名，不含参数与括号）
 */
QStringList testFunctionsOf(const QObject *test)
{
    const QMetaObject *meta = test->metaObject();
    QStringList functions;
    for (int i = 0; i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.methodType() != QMetaMethod::Method
            && method.methodType() != QMetaMethod::Slot) {
            continue;
        }
        if (method.enclosingMetaObject() != meta) {
            continue; // 继承来的方法（如 QObject::deleteLater），不是本类用例
        }
        const QString name = QString::fromLatin1(method.methodSignature())
                                 .section(QLatin1Char('('), 0, 0);
        if (name == QStringLiteral("init") || name == QStringLiteral("cleanup")
            || name == QStringLiteral("initTestCase")
            || name == QStringLiteral("cleanupTestCase")) {
            continue;
        }
        functions.append(name);
    }
    return functions;
}

} // namespace

/**
 * @brief 依次运行所有已注册的测试类
 *
 * QTest::qExec 遇到 -functions 会直接退出进程，多类套件若逐类 qExec 会在
 * 第一个类后中断——这种情况下改为自行汇总所有类的用例名并集打印。位置
 * 参数按「类::用例」或裸用例名过滤后只传给拥有该用例的类，让
 * `tst_x <case>` 只跑一个用例，其余类不会因 "function not found" 报失败。
 *
 * @param argc 参数个数，含义同 main
 * @param argv 参数数组，含义同 main
 * @return 各类的 QTest 退出状态按位或；-functions 模式下恒为 0
 */
int awbRunRegisteredTests(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QStringList functionFilters;
    QStringList optionArgs;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith(QLatin1Char('-'))) {
            optionArgs.append(arg);
        }
        else {
            functionFilters.append(arg.contains(QStringLiteral("::"))
                                       ? arg.section(QStringLiteral("::"), 1)
                                       : arg);
        }
    }

    // QTest::qExec 遇到 -functions 会退出进程，多类套件会在第一个类后
    // 中断——改为在这里汇总所有类的用例名并集打印。
    if (optionArgs.contains(QStringLiteral("-functions"))) {
        QSet<QString> printed;
        for (const AwbTestFactory factory : awbTestRegistry()) {
            QObject *test = factory();
            for (const QString &name : testFunctionsOf(test)) {
                printed.insert(name);
            }
            delete test;
        }
        QStringList sorted = printed.values();
        sorted.sort(Qt::CaseInsensitive);
        for (const QString &name : std::as_const(sorted)) {
            fprintf(stdout, "%s()\n", qPrintable(name));
        }
        return 0;
    }

    int status = 0;
    for (const AwbTestFactory factory : awbTestRegistry()) {
        QObject *test = factory();

        QStringList matched = functionFilters;
        if (!functionFilters.isEmpty()) {
            // QStringList::removeIf 是 Qt 6.1+ 的 API；这里用迭代删除保持
            // Qt 5 兼容。
            for (int i = matched.size() - 1; i >= 0; --i) {
                if (test->metaObject()->indexOfMethod(
                        qPrintable(matched.at(i) + QStringLiteral("()"))) < 0) {
                    matched.removeAt(i);
                }
            }
            if (matched.isEmpty()) {
                delete test;
                continue;
            }
        }

        // 重组 argv：程序名 + 选项 + 仅该类提供的用例名。
        QList<QByteArray> storage;
        storage.append(QCoreApplication::applicationFilePath().toLocal8Bit());
        for (const QString &arg : std::as_const(optionArgs)) {
            storage.append(arg.toLocal8Bit());
        }
        for (const QString &arg : std::as_const(matched)) {
            storage.append(arg.toLocal8Bit());
        }

        QList<char *> args;
        args.reserve(storage.size());
        for (QByteArray &bytes : storage) {
            args.append(bytes.data());
        }

        // QList::data() 是 Qt 6 的 API；Qt 5 用取首元素地址的老写法
        // （storage 至少含 program 一项，列表不为空）。
        status |= QTest::qExec(test, args.size(), &args.first());
        delete test;
    }
    return status;
}

/**
 * @brief 返回测试工厂注册表（首次调用时惰性创建）
 */
QList<AwbTestFactory> &awbTestRegistry()
{
    static QList<AwbTestFactory> registry;
    return registry;
}
