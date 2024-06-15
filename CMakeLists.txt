cmake_minimum_required (VERSION 3.22)

project(SecuritySocket)

# Security Socket Project

message("-- Security Socket --")

set(source_dir ${PROJECT_SOURCE_DIR}/src)
message("Security Socket Source Dir : ${source_dir}")

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

configure_file(
    ${source_dir}/SecuritySocket.hpp 
    ${PROJECT_BINARY_DIR}/include/SecuritySocket.hpp)

file(GLOB_RECURSE securitysocket_source_files 
    ${source_dir}/*.h
    ${source_dir}/*.hpp
    ${source_dir}/*.c
    ${source_dir}/*.cpp)
message("securitysocket_source_files = ${securitysocket_source_files}")

add_library(securitysocket STATIC ${securitysocket_source_files})
target_include_directories(
    securitysocket PRIVATE "${openssl_include_dir}"
)
add_dependencies(securitysocket OpenSSL::SSL OpenSSL::Crypto)
# target_link_libraries(securitysocket PUBLIC openssl_crypto openssl_ssl)
target_sources(securitysocket PRIVATE $<TARGET_OBJECTS:crypto> $<TARGET_OBJECTS:ssl>)
