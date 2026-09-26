#ifndef AWBTEST_H
#define AWBTEST_H

#include <QList>

class QObject;

// Test registry shared by every module suite: one executable per module
// runs several test classes. Each test file registers itself at static-init
// time through AWB_TEST(); awbRunRegisteredTests() (tests/awbtest_runner.cpp)
// constructs them after QCoreApplication exists and QTest::qExec's each in
// turn.
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

// Runs every registered class; handles -functions (QTest's qExec exits the
// process for it, which would stop after the first class) and per-class
// function filters so `tst_x <case>` runs one case without the other
// classes failing with "function not found".
int awbRunRegisteredTests(int argc, char *argv[]);

#endif // AWBTEST_H
