cmake_minimum_required(VERSION 3.16)
project(SecuritySocketAndroidTest)

set(SECURITYSOCKET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../..")
set(SECURITYSOCKET_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/library")
message(STATUS "SECURITYSOCKET_SOURCE_DIR = ${SECURITYSOCKET_SOURCE_DIR}")
message(STATUS "SECURITYSOCKET_BINARY_DIR = ${SECURITYSOCKET_BINARY_DIR}")

add_subdirectory("${SECURITYSOCKET_SOURCE_DIR}" "${SECURITYSOCKET_BINARY_DIR}")

set(SECURITY_SOCKET_TEST_DIR "${SECURITYSOCKET_SOURCE_DIR}/test")
message(STATUS "SECURITY_SOCKET_TEST_DIR = ${SECURITY_SOCKET_TEST_DIR}")

add_library(
        SecuritySocketAndroidTest
        SHARED
        main.cpp
)

target_include_directories(
        SecuritySocketAndroidTest
        PUBLIC
        ${SECURITY_SOCKET_TEST_DIR}
)

target_link_libraries(
        SecuritySocketAndroidTest
        securitysocket
        securitysockettest
)

set_target_properties(securitysocket PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}   # 정적 라이브러리 경로
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}   # 동적 라이브러리 경로
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}   # 실행 파일 경로
)

set_target_properties(securitysockettest PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}   # 정적 라이브러리 경로
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}   # 동적 라이브러리 경로
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}   # 실행 파일 경로
)
