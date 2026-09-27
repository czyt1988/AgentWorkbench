#include "awbtest.h"

// Entry point of the core test suite: every registered
// class runs in sequence under the single `tst_core` target.
int main(int argc, char *argv[])
{
    return awbRunRegisteredTests(argc, argv);
}
