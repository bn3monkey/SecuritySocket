#include "securitysockettest.hpp"
#include "securitysockettest_tlshelper.hpp"
#include <gtest/gtest.h>

int startSecuritySocketTest(int argc, char** argv)
{
    // Ensure all TLS test certificates exist before any test runs.
    createCertificates();

    ::testing::InitGoogleTest(&argc, argv);
    ::testing::GTEST_FLAG(filter) = "TLSConnection*";

    return RUN_ALL_TESTS();
}
