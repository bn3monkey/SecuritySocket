cmake_minimum_required (VERSION 3.16)

project(SecuritySocket)


# Security Socket Project

message("-- Security Socket --")

set(source_dir ${PROJECT_SOURCE_DIR}/src)
message("Security Socket Source Dir : ${source_dir}")

## OPTION

option(SECURITYSOCKET_USING_TLS "Apply TLS on security socket" ON)
option(BUILD_SECURITYSOCKET_SHARED "Build Security Socket Library as shared library" ON)
message("BUILD_SECURITYSOCKET_SHARED = ${BUILD_SECURITYSOCKET_SHARED}")
option(BUILD_SECURITYSOCKET_TEST "Build Tests of Security Socket Library" on)
message("BUILD_SECURITYSOCKET_TEST = ${BUILD_SECURITYSOCKET_TEST}")
option(BUILD_SECURITYSOCKETTEST_SHARED "Build Security Socket Test Library as shared library" ON)
message("BUILD_SECURITYSOCKETTEST_SHARED = ${BUILD_SECURITYSOCKETTEST_SHARED}")
option(SECURITYSOCKET_TEST_USE_CURL
    "Fetch libcurl and build HTTP/WebSocket regression test targets" OFF)
message("SECURITYSOCKET_TEST_USE_CURL = ${SECURITYSOCKET_TEST_USE_CURL}")

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message("Apply -fPIC in linux environment ")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

## OpenSSL

message("-- OpenSSL CMake Initialization --")
include(FetchContent)

if (SECURITYSOCKET_USING_TLS)
    # set(TEMP_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    # set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build OpenSSL Static Libraries" FORCE)
    FetchContent_Declare(OpenSSL
        GIT_REPOSITORY https://github.com/bn3monkey/openssl-cmake
        GIT_TAG proto)
    FetchContent_MakeAvailable(OpenSSL)
    # set(BUILD_SHARED_LIBS ${TEMP_BUILD_SHARED_LIBS} ON CACHE BOOL "Build OpenSSL Static Libraries" FORCE)

    # message("OpenSSL BINARY DIR is ${openssl_BINARY_DIR}")
    # set(openssl_include_dir "${openssl_BINARY_DIR}/include")
endif()

## Security Socket

message("-- Security Socket CMake Initialization -- ")

# Recursive glob — pulls in `src/implementation/thirdparty/**` (e.g. the
# vendored picohttpparser .c/.h) the same way it pulls in our own sources.
# The same list is reused for the test library below so tests can exercise
# vendored code directly without having to re-export symbols through the
# public DLL surface.
file(GLOB_RECURSE securitysocket_source_files
    ${source_dir}/*.h
    ${source_dir}/*.hpp
    ${source_dir}/*.c
    ${source_dir}/*.cpp)
message("securitysocket_source_files = ${securitysocket_source_files}")

if (BUILD_SECURITYSOCKET_SHARED)
    add_library(securitysocket SHARED ${securitysocket_source_files})
else()
    add_library(securitysocket STATIC ${securitysocket_source_files})
endif()

target_compile_definitions(securitysocket PRIVATE SECURITYSOCKET_EXPORTS)
if (MINGW)
    target_compile_definitions(securitysocket PRIVATE -D_WIN32_WINNT=0x600)
endif()

if (SECURITYSOCKET_USING_TLS)
    target_compile_definitions(securitysocket PRIVATE SECURITYSOCKET_TLS)
endif()

target_include_directories(
    securitysocket PRIVATE
        "${OPENSSL_INCLUDE_DIR}"
        "${source_dir}/implementation/thirdparty/picohttpparser"
)


if (SECURITYSOCKET_USING_TLS)
    add_dependencies(securitysocket OpenSSL::Crypto OpenSSL::SSL)
    target_link_libraries(securitysocket OpenSSL::Crypto OpenSSL::SSL) 
endif()

if (WIN32)
    target_link_libraries(securitysocket ws2_32)
endif()

# PROPERTIES 

set_target_properties(securitysocket PROPERTIES PUBLIC_HEADER "${source_dir}/SecuritySocket.hpp")

if(MSVC)
    target_compile_options(securitysocket PRIVATE /W4)
    target_compile_options(securitysocket PRIVATE /W4)
elseif(APPLE)
    target_compile_options(securitysocket PRIVATE -Wall -Wextra)
    target_compile_options(securitysocket PRIVATE -Wall -Wextra)
elseif(UNIX AND NOT APPLE)
    target_compile_options(securitysocket PRIVATE -Wall -Wextra)
    target_compile_options(securitysocket PRIVATE -Wall -Wextra)
endif()


# INSTALL

install(TARGETS securitysocket
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include
)

if (BUILD_SECURITYSOCKET_TEST)
    set(TEMP_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build Google Test Static Libraries" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )
    FetchContent_MakeAvailable(googletest)
    set(BUILD_SHARED_LIBS ${TEMP_BUILD_SHARED_LIBS} CACHE BOOL "Restore Google Test Static Libraries" FORCE)

    FetchContent_Declare(
        remote_command
        GIT_REPOSITORY https://github.com/bn3monkey/remote-command.git
        GIT_TAG 1.2.4
    )
    FETCHCONTENT_MAKEAVAILABLE(remote_command)

    set(SECURITYSOCKET_TEST_DIR "${PROJECT_SOURCE_DIR}/test")
    message(STATUS "SECURITYSOCKET_TEST_DIR = ${SECURITYSOCKET_TEST_DIR}")
    file(GLOB SECURITYSOCKET_TEST_FILES
        "${SECURITYSOCKET_TEST_DIR}/*.h"
        "${SECURITYSOCKET_TEST_DIR}/*.hpp"
        "${SECURITYSOCKET_TEST_DIR}/*.c"
        "${SECURITYSOCKET_TEST_DIR}/*.cpp")
    message(STATUS "SECURITYSOCKET_TEST_FILES = ${SECURITYSOCKET_TEST_FILES}")

    # Vendored third-party sources (e.g. picohttpparser) are compiled
    # directly into securitysockettest as well, so tests can call into them
    # without having to re-export their symbols through the securitysocket
    # DLL surface.
    set(SECURITYSOCKET_THIRDPARTY_FILES ${securitysocket_source_files})
    list(FILTER SECURITYSOCKET_THIRDPARTY_FILES INCLUDE REGEX "/thirdparty/")
    message(STATUS "SECURITYSOCKET_THIRDPARTY_FILES = ${SECURITYSOCKET_THIRDPARTY_FILES}")

    # ConnectionPhase implementations (SniffPhase / CustomPhase / HttpPhase /
    # WebSocketPhase) and the CustomProtocol*Impl view/builders they construct are
    # internal — NOT exported through the DLL surface — so the phase unit tests
    # (securitysockettest_*_phase.cpp) compile them directly into the test target,
    # the same way thirdparty sources are pulled in above. Their *exported*
    # dependencies (HttpParser, WsFrameCodec, HttpRouterImpl, Http*Impl, Url, ...)
    # still resolve through securitysocket.dll, so only the non-exported sources
    # are listed here (adding an exported one would double-define its symbols).
    set(SECURITYSOCKET_CONNECTION_FILES ${securitysocket_source_files})
    list(FILTER SECURITYSOCKET_CONNECTION_FILES INCLUDE REGEX "/(connection|custom)/.*\\.cpp$")
    message(STATUS "SECURITYSOCKET_CONNECTION_FILES = ${SECURITYSOCKET_CONNECTION_FILES}")

    if (BUILD_SECURITYSOCKETTEST_SHARED)
        add_library(securitysockettest SHARED
            ${SECURITYSOCKET_TEST_FILES}
            ${SECURITYSOCKET_THIRDPARTY_FILES}
            ${SECURITYSOCKET_CONNECTION_FILES}
            ${SECURITYSOCKET_UTILS_FILES})
        target_compile_definitions(securitysockettest PRIVATE SECURITYSOCKETTEST_EXPORTS)


    else()
        add_library(securitysockettest
            STATIC
            ${SECURITYSOCKET_TEST_FILES}
            ${SECURITYSOCKET_THIRDPARTY_FILES}
            ${SECURITYSOCKET_CONNECTION_FILES}
            ${SECURITYSOCKET_UTILS_FILES})
        target_compile_definitions(securitysockettest PUBLIC SECURITYSOCKETTEST_STATIC)

    endif()


    message(STATUS "GTEST_SOURCE_DIR = ${gtest_SOURCE_DIR}")
    message(STATUS "GMOCK_SOURCE_DIR = ${gmock_SOURCE_DIR}")

    target_include_directories(securitysockettest
        PRIVATE
        ${source_dir}
        "${source_dir}/implementation/thirdparty/picohttpparser"
        "${gtest_SOURCE_DIR}/include"
        "${gmock_SOURCE_DIR}/include")

    set(SECURITYSOCKETTEST_PLATFORMDEPENDENT_LIBRARY)
    if(CMAKE_SYSTEM_NAME STREQUAL "Android")
        list(APPEND SECURITYSOCKETTEST_PLATFORMDEPENDENT_LIBRARY
            log
            android)
    endif()

    add_dependencies(securitysockettest securitysocket)

    target_link_libraries(securitysockettest
        securitysocket
        gtest
        gtest_main
        remote_command_client
        ${SECURITYSOCKETTEST_PLATFORMDEPENDENT_LIBRARY})

    ## libcurl — test-only dependency for HTTP/WebSocket regression tests
    ##
    ## Gated entirely by SECURITYSOCKET_TEST_USE_CURL. When OFF, libcurl is
    ## never fetched, configured, or linked. When ON, libcurl is linked into
    ## securitysockettest and the SECURITYSOCKET_TEST_USE_CURL macro is
    ## defined so the HTTP/WebSocket server test sources compile in their
    ## libcurl-dependent content (otherwise those files compile to nothing).
    if (SECURITYSOCKET_TEST_USE_CURL)
        message(STATUS "-- libcurl CMake Initialization (test-only) --")

        # Save / override BUILD_SHARED_LIBS so libcurl is statically linked
        # without affecting other dependencies.
        set(_securitysocket_saved_build_shared_libs ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build libcurl statically" FORCE)

        # libcurl configure-time knobs — minimise the surface to HTTP + WS,
        # no extra protocols, no command-line tool, no curl test suite.
        set(BUILD_CURL_EXE       OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING        OFF CACHE BOOL "" FORCE)
        set(BUILD_LIBCURL_DOCS   OFF CACHE BOOL "" FORCE)
        set(BUILD_MISC_DOCS      OFF CACHE BOOL "" FORCE)
        set(ENABLE_CURL_MANUAL   OFF CACHE BOOL "" FORCE)
        set(HTTP_ONLY            ON  CACHE BOOL "" FORCE)
        set(CURL_ENABLE_WEBSOCKETS ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_INSTALL ON  CACHE BOOL "" FORCE)
        set(CURL_USE_LIBSSH2     OFF CACHE BOOL "" FORCE)
        set(CURL_USE_LIBPSL      OFF CACHE BOOL "" FORCE)
        # Phase 1 smoke tests use HTTP only; TLS for HTTPS/WSS comes in later
        # phases (alongside the HTTP/WS server impl).
        set(CURL_ENABLE_SSL      OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
            curl
            GIT_REPOSITORY https://github.com/curl/curl.git
            GIT_TAG        curl-8_11_0
        )
        FetchContent_MakeAvailable(curl)

        # Restore the caller's BUILD_SHARED_LIBS.
        set(BUILD_SHARED_LIBS ${_securitysocket_saved_build_shared_libs}
            CACHE BOOL "Restore BUILD_SHARED_LIBS" FORCE)
        unset(_securitysocket_saved_build_shared_libs)

        # Resolve the libcurl target. Modern libcurl exposes the alias
        # CURL::libcurl; fall back to the bare 'libcurl' target if not.
        if (TARGET CURL::libcurl)
            set(_securitysocket_curl_target CURL::libcurl)
        elseif (TARGET libcurl_static)
            set(_securitysocket_curl_target libcurl_static)
        elseif (TARGET libcurl)
            set(_securitysocket_curl_target libcurl)
        else()
            message(FATAL_ERROR
                "SECURITYSOCKET_TEST_USE_CURL=ON but no libcurl target was produced. "
                "Check the FetchContent block above and your libcurl version.")
        endif()
        message(STATUS "SecuritySocket: libcurl target = ${_securitysocket_curl_target}")

        # Compile-time guard: fail fast if the libcurl we just built does not
        # actually expose the WebSocket API. This is the contract Phase 1 has
        # to enforce so later phases can rely on curl_ws_send / curl_ws_recv.
        #
        # We force the try_compile target type to STATIC_LIBRARY so the probe
        # only needs to compile (not link) — otherwise MSVC's import-stub
        # __declspec(dllimport) reference to curl_ws_send would fail at link
        # time without an explicit libcurl on the link line.
        include(CheckSymbolExists)
        set(_securitysocket_saved_req_incs    ${CMAKE_REQUIRED_INCLUDES})
        set(_securitysocket_saved_try_target  ${CMAKE_TRY_COMPILE_TARGET_TYPE})
        set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
        get_target_property(_securitysocket_curl_incs
            ${_securitysocket_curl_target} INTERFACE_INCLUDE_DIRECTORIES)
        if (_securitysocket_curl_incs)
            list(APPEND CMAKE_REQUIRED_INCLUDES ${_securitysocket_curl_incs})
        endif()
        # websockets.h relies on CURL_EXTERN / CURLcode / curl_off_t from
        # curl.h, so both headers must be included for the symbol probe to
        # parse successfully.
        check_symbol_exists(curl_ws_send "curl/curl.h;curl/websockets.h"
            SECURITYSOCKET_CURL_HAS_WS_SEND)
        check_symbol_exists(curl_ws_recv "curl/curl.h;curl/websockets.h"
            SECURITYSOCKET_CURL_HAS_WS_RECV)
        set(CMAKE_REQUIRED_INCLUDES       ${_securitysocket_saved_req_incs})
        set(CMAKE_TRY_COMPILE_TARGET_TYPE ${_securitysocket_saved_try_target})
        unset(_securitysocket_saved_req_incs)
        unset(_securitysocket_saved_try_target)
        unset(_securitysocket_curl_incs)

        if (NOT SECURITYSOCKET_CURL_HAS_WS_SEND OR NOT SECURITYSOCKET_CURL_HAS_WS_RECV)
            message(FATAL_ERROR
                "SECURITYSOCKET_TEST_USE_CURL=ON but the fetched libcurl does not "
                "expose curl_ws_send / curl_ws_recv. Rebuild with libcurl >= 8.11.0 "
                "configured with CURL_ENABLE_WEBSOCKETS=ON.")
        endif()

        # Inject libcurl into the existing test library. The HTTP/WebSocket
        # server test sources gate all their content with
        # #if defined(SECURITYSOCKET_TEST_USE_CURL), so they compile to
        # empty translation units when this option is OFF and to real test
        # cases when it is ON.
        target_compile_definitions(securitysockettest
            PRIVATE SECURITYSOCKET_TEST_USE_CURL)
        target_link_libraries(securitysockettest
            ${_securitysocket_curl_target})
        unset(_securitysocket_curl_target)
    endif()

endif()

