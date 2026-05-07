#include <gtest/gtest.h>
#include <rte_eal.h>
#include <rte_errno.h>

class DpdkEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        static char arg0[] = "test";
        static char arg1[] = "--no-huge";
        static char arg2[] = "-m";
        static char arg3[] = "64";
        static char arg4[] = "-l";
        static char arg5[] = "0";
        char* argv[] = { arg0, arg1, arg2, arg3, arg4, arg5 };
        int argc = 6;

        int ret = rte_eal_init(argc, argv);
        if (ret < 0) {
            // EAL may already be initialized if tests run in same process
            // In that case rte_errno is set to EALREADY or similar.
            // For our purposes, we just need EAL to be up.
            if (rte_errno != 0) {
                // Try to continue; some DPDK versions return -1 if already init
            }
        }
    }

    void TearDown() override {
        // rte_eal_cleanup() is available in newer DPDK versions.
        // We skip it to avoid issues with multi-test runs.
    }
};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new DpdkEnvironment());
    return RUN_ALL_TESTS();
}
