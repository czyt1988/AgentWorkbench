#ifndef AWBTEST_H
#define AWBTEST_H

#include <QList>

class QObject;

// Test registry: one executable per module (tst_core) runs several test
// classes. Each test file registers itself at static-init time through
// AWB_TEST(); main() constructs them after QCoreApplication exists and
// QTest::qExec's each in turn.
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

#endif // AWBTEST_H
