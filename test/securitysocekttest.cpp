#include "securitysockettest.hpp"
#include "securitysockettest_tlshelper.hpp"
#include <gtest/gtest.h>
#include <string>

// Global working directory for local cert files.
// Defaults to "." if startSecuritySocketTest() is called without a cwd.
static std::string g_cwd = ".";

const char* getCWD()
{
    return g_cwd.c_str();
}

int startSecuritySocketTest(int argc, char** argv, const char* cwd)
{
    // Store the local working directory (Android internal storage or current dir).
    g_cwd = (cwd && cwd[0] != '\0') ? cwd : ".";

    // Ensure all TLS test certificates exist before any test runs.
    createCertificates();

    ::testing::InitGoogleTest(&argc, argv);
    ::testing::GTEST_FLAG(filter) = "TLSConnection*";

    return RUN_ALL_TESTS();
}
