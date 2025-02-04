cmake_minimum_required (VERSION 3.22)

project(SecuritySocket)

# Security Socket Project

message("-- Security Socket --")

set(source_dir ${PROJECT_SOURCE_DIR}/src)
message("Security Socket Source Dir : ${source_dir}")

## OPTION

option(BUILD_SECURITYSOCKET_SHARED "Build Security Socket Library as shared library" ON)
message("BUILD_SECURITYSOCKET_SHARED = ${BUILD_SECURITYSOCKET_SHARED}")
option(BUILD_SECURITYSOCKET_TEST "Build Tests of Security Socket Library" on)
message("BUILD_SECURITYSOCKET_SHARED = ${BUILD_SECURITYSOCKET_SHARED}")

## OpenSSL

message("-- OpenSSL CMake Initialization --")
include(FetchContent)
FetchContent_Declare(OpenSSL
    GIT_REPOSITORY https://github.com/janbar/openssl-cmake
    GIT_TAG 1.1.1w-20231130)
FetchContent_MakeAvailable(OpenSSL)

add_library(OpenSSL::SSL ALIAS ssl)
add_library(OpenSSL::Crypto ALIAS crypto)
message("OpenSSL BINARY DIR is ${openssl_BINARY_DIR}")
set(openssl_include_dir "${openssl_BINARY_DIR}/include")

## Security Socket

message("-- Security Socket CMake Initialization -- ")

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

target_include_directories(
    securitysocket PRIVATE "${openssl_include_dir}"
)
add_dependencies(securitysocket OpenSSL::SSL OpenSSL::Crypto)
target_sources(securitysocket PRIVATE $<TARGET_OBJECTS:crypto> $<TARGET_OBJECTS:ssl>)

# PROPERTIES 

set_target_properties(securitysocket PROPERTIES PUBLIC_HEADER "${source_dir}/SecuritySocket.hpp")

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# INSTALL

install(TARGETS securitysocket
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include
)

if (BUILD_SECURITYSOCKET_TEST)

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build Google Test Static Libraries" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )
    FetchContent_MakeAvailable(googletest)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "Restore Google Test Static Libraries" FORCE)

    set(SECURITY_TEST_DIR "${PROJECT_SOURCE_DIR}/test")
    message(STATUS "SECURITY_TEST_DIR = ${SECURITY_TEST_DIR}")
    file(GLOB SECURITYSOCKET_TEST_FILES
        "${SECURITYSOCKET_TEST_DIR}/*.h"
        "${SECURITYSOCKET_TEST_DIR}/*.hpp"
        "${SECURITYSOCKET_TEST_DIR}/*.c"
        "${SECURITYSOCKET_TEST_DIR}/*.cpp")
    message(STATUS "SECURITYSOCKET_TEST_FILES = ${SECURITYSOCKET_TEST_FILES}")

    add_library(securitysockettest
        ${source_dir}
        ${SECURITYSOCKET_TEST_FILES}
        ${SECURITYSOCKET_UTILS_FILES})

    target_compile_definitions(securitysockettest PRIVATE SECURITYSOCKET_EXPORTS)

    message(STATUS "GTEST_SOURCE_DIR = ${gtest_SOURCE_DIR}")
    message(STATUS "GMOCK_SOURCE_DIR = ${gmock_SOURCE_DIR}")

    target_include_directories(securitysockettest
        PRIVATE
        ${HSNULTRASOUND_INCLUDE_DIR}
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
        ${SECURITYSOCKETTEST_PLATFORMDEPENDENT_LIBRARY})

endif()

