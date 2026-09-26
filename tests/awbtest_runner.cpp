#include "awbtest.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QMetaMethod>
#include <QMetaObject>
#include <QSet>
#include <QStringList>
#include <QTest>

#include <cstdio>

namespace {

// Test functions of a class: its own slots except the setup/teardown
// special-cases and anything inherited from QObject.
QStringList testFunctionsOf(const QObject *test)
{
    const QMetaObject *meta = test->metaObject();
    QStringList functions;
    for (int i = 0; i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.methodType() != QMetaMethod::Method
            && method.methodType() != QMetaMethod::Slot)
            continue;
        if (method.enclosingMetaObject() != meta)
            continue; // inherited (e.g. QObject::deleteLater)
        const QString name = QString::fromLatin1(method.methodSignature())
                                 .section(QLatin1Char('('), 0, 0);
        if (name == QLatin1String("init") || name == QLatin1String("cleanup")
            || name == QLatin1String("initTestCase")
            || name == QLatin1String("cleanupTestCase"))
            continue;
        functions.append(name);
    }
    return functions;
}

} // namespace

int awbRunRegisteredTests(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QStringList functionFilters;
    QStringList optionArgs;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith(QLatin1Char('-')))
            optionArgs.append(arg);
        else
            functionFilters.append(arg.contains(QStringLiteral("::"))
                                       ? arg.section(QStringLiteral("::"), 1)
                                       : arg);
    }

    // QTest::qExec exits the process when asked for -functions, which would
    // stop after the first class — list the union here instead.
    if (optionArgs.contains(QStringLiteral("-functions"))) {
        QSet<QString> printed;
        for (const AwbTestFactory factory : awbTestRegistry()) {
            QObject *test = factory();
            for (const QString &name : testFunctionsOf(test))
                printed.insert(name);
            delete test;
        }
        QStringList sorted = printed.values();
        sorted.sort(Qt::CaseInsensitive);
        for (const QString &name : sorted)
            fprintf(stdout, "%s()\n", qPrintable(name));
        return 0;
    }

    int status = 0;
    for (const AwbTestFactory factory : awbTestRegistry()) {
        QObject *test = factory();

        QStringList matched = functionFilters;
        if (!functionFilters.isEmpty()) {
            matched.removeIf([test](const QString &name) {
                return test->metaObject()->indexOfMethod(
                           qPrintable(name + QStringLiteral("()"))) < 0;
            });
            if (matched.isEmpty()) {
                delete test;
                continue;
            }
        }

        // Rebuild argv: program + options + only the functions this class
        // provides.
        QList<QByteArray> storage;
        storage.append(QCoreApplication::applicationFilePath().toLocal8Bit());
        for (const QString &arg : optionArgs)
            storage.append(arg.toLocal8Bit());
        for (const QString &arg : matched)
            storage.append(arg.toLocal8Bit());

        QList<char *> args;
        args.reserve(storage.size());
        for (QByteArray &bytes : storage)
            args.append(bytes.data());

        status |= QTest::qExec(test, args.size(), args.data());
        delete test;
    }
    return status;
}

QList<AwbTestFactory> &awbTestRegistry()
{
    static QList<AwbTestFactory> registry;
    return registry;
}
