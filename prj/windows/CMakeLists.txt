cmake_minimum_required (VERSION 3.22)

project(SecuritySocket)

# Security Socket Project

message("-- Security Socket --")


## COMMON VARIABLE

set(repository_dir "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(project_dir ${CMAKE_CURRENT_SOURCE_DIR})
message("repository_dir = ${repository_dir}")
message("project_dir = ${project_dir}")

set(source_dir "${repository_dir}/src")
set(library_dir "${repository_dir}/lib")
message("source_dir = ${source_dir}")
message("library_dir = ${library_dir}")

set(build_dir ${project_dir}/build)
message("build_dir = ${build_dir}")

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

message("-- Security Socket CMake Initialization --")

set(securitysocket_build_dir ${build_dir}/SecuritySocket)
set(securitysocket_include_dir ${securitysocket_build_dir}/include)
set(securitysocket_binary_dir ${securitysocket_build_dir}/lib)

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

configure_file(${source_dir}/SecuritySocket.hpp ${securitysocket_include_dir}/SecuritySocket.hpp)

if(MSVC)
    target_compile_options(securitysocket PRIVATE /W4)
    target_compile_options(securitysocket PRIVATE /W4)
elseif(APPLE)
    target_compile_options(securitysocket PRIVATE -Wall -Wextra -fPIC)
    target_compile_options(securitysocket PRIVATE -Wall -Wextra -fPIC)
elseif(UNIX AND NOT APPLE)
    target_compile_options(securitysocket PRIVATE -Wall -Wextra -fPIC)
    target_compile_options(securitysocket PRIVATE -Wall -Wextra -fPIC)
endif()


if (ANDROID)
set_target_properties(securitysocket PROPERTIES LIBRARY_OUTPUT_DIRECTORY
    ${securitysocket_binary_dir}/${ANDROID_ABI})
set_target_properties(securitysocket PROPERTIES ARCHIVE_OUTPUT_DIRECTORY
    ${securitysocket_binary_dir}/${ANDROID_ABI})
set_target_properties(securitysocket PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    ${securitysocket_binary_dir}/${ANDROID_ABI})
else()
set_target_properties(securitysocket PROPERTIES LIBRARY_OUTPUT_DIRECTORY
    ${securitysocket_binary_dir})
set_target_properties(securitysocket PROPERTIES ARCHIVE_OUTPUT_DIRECTORY
    ${securitysocket_binary_dir})
set_target_properties(securitysocket PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    ${securitysocket_binary_dir})
endif()

## Security Socket Test App

message("-- Security Socket Sample App CMake Initialization --")

add_executable(securitysocket_test main.cpp)
add_dependencies(securitysocket_test securitysocket)
target_link_libraries(securitysocket_test securitysocket)
target_include_directories(securitysocket_test PRIVATE ${securitysocket_include_dir})

