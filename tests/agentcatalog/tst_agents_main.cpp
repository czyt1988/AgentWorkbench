#include "awbtest.h"

// Entry point of the agent catalog test suite: every registered
// class runs in sequence under the single `tst_agentcatalog` target.
int main(int argc, char *argv[])
{
    return awbRunRegisteredTests(argc, argv);
}
