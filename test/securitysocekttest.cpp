#include "securitysockettest.hpp"
#include <gtest/gtest.h>

int startSecuritySocketTest(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::GTEST_FLAG(filter) = "TLSConnection*";

    return RUN_ALL_TESTS();
}
