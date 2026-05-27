// SecuritySocket HTTP server regression tests (driven by libcurl).
//
// The HTTP server implementation lands in Phase 4 / Phase 6 of the v3
// milestone (see docs/workplan/mileston_http.md). This file is the
// libcurl-backed test client that exercises it.
//
// Everything in this translation unit is gated by SECURITYSOCKET_TEST_USE_CURL.
// The macro is defined by the root CMakeLists.txt only when the build option
// of the same name is ON. With the option OFF (default) this file compiles
// to an empty TU — no libcurl headers are pulled in and no test cases are
// registered.

#if defined(SECURITYSOCKET_TEST_USE_CURL)

#include <gtest/gtest.h>

#include <curl/curl.h>

namespace {

// Phase 1 smoke check: prove that the test binary picked up libcurl through
// the SECURITYSOCKET_TEST_USE_CURL build option. Replaced by real server
// regression tests in Phase 4/6.
class HttpServerCurlEnv : public ::testing::Environment {
public:
    void SetUp() override {
        ASSERT_EQ(CURLE_OK, curl_global_init(CURL_GLOBAL_DEFAULT));
    }
    void TearDown() override {
        curl_global_cleanup();
    }
};

::testing::Environment* const g_http_server_curl_env =
    ::testing::AddGlobalTestEnvironment(new HttpServerCurlEnv);

size_t discardWrite(char*, size_t size, size_t nmemb, void*) {
    return size * nmemb;
}

}  // namespace

TEST(HttpServerSmoke, LibCurlVersionMeetsMinimum) {
    curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
    ASSERT_NE(nullptr, info);
    // libcurl ≥ 8.11.0 is the contract Phase 1 enforces (see root CMakeLists).
    ASSERT_GE(info->version_num, 0x080B00) << "libcurl version: " << info->version;
}

TEST(HttpServerSmoke, LoopbackConnectFailsCleanly) {
    CURL* handle = curl_easy_init();
    ASSERT_NE(nullptr, handle);

    // Port 1 is reserved/closed — connect must fail with a well-formed
    // CURLcode (no crash, no UB). This proves libcurl's transport stack
    // is actually linked into the test binary.
    curl_easy_setopt(handle, CURLOPT_URL, "http://127.0.0.1:1/");
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,        2000L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &discardWrite);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

    const CURLcode rc = curl_easy_perform(handle);
    EXPECT_NE(CURLE_OK, rc) << "expected loopback:1 to fail, got: "
                            << curl_easy_strerror(rc);

    curl_easy_cleanup(handle);
}

#endif  // SECURITYSOCKET_TEST_USE_CURL
