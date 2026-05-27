// SecuritySocket WebSocket server regression tests (driven by libcurl's
// curl_ws_send / curl_ws_recv API).
//
// The WebSocket handshake and frame codec land in Phase 6 of the v3
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
#include <curl/websockets.h>

// Take the address of the WS API entry points so the symbols cannot be
// stripped by --gc-sections / /OPT:REF. If either is missing at link time,
// the test binary fails to link with a clear error instead of surfacing the
// problem in a later phase.
TEST(WebSocketServerSmoke, LibCurlWebSocketSymbolsAreLinked) {
    void* const send_ptr = reinterpret_cast<void*>(&curl_ws_send);
    void* const recv_ptr = reinterpret_cast<void*>(&curl_ws_recv);
    void* const meta_ptr = reinterpret_cast<void*>(&curl_ws_meta);
    ASSERT_NE(nullptr, send_ptr);
    ASSERT_NE(nullptr, recv_ptr);
    ASSERT_NE(nullptr, meta_ptr);
}

TEST(WebSocketServerSmoke, LoopbackWebSocketConnectFailsCleanly) {
    CURL* handle = curl_easy_init();
    ASSERT_NE(nullptr, handle);

    // ws:// loopback against a closed port — libcurl should route through
    // the WebSocket upgrade path and surface a connection-level CURLcode.
    curl_easy_setopt(handle, CURLOPT_URL, "ws://127.0.0.1:1/");
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,        2000L);
    curl_easy_setopt(handle, CURLOPT_CONNECT_ONLY,      2L);  // WS mode
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL,          1L);

    const CURLcode rc = curl_easy_perform(handle);
    EXPECT_NE(CURLE_OK, rc) << "expected ws://127.0.0.1:1 to fail, got: "
                            << curl_easy_strerror(rc);

    curl_easy_cleanup(handle);
}

#endif  // SECURITYSOCKET_TEST_USE_CURL
