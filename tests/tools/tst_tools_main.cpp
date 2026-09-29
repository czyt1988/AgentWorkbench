#include "awbtest.h"

/**
 * @brief tools 测试套件入口：在单个 tst_tools 目标里依次运行所有已注册的测试类
 */
int main(int argc, char *argv[])
{
    return awbRunRegisteredTests(argc, argv);
}
