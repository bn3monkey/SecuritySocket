cmake_minimum_required (VERSION 3.22)

project(SecuritySocket)

# Security Socket Project

message("-- Security Socket --")
include(FetchContent)
FetchContent_Declare(SecuritySocket
    GIT_REPOSITORY https://github.com/bn3monkey/securitysocket
    GIT_TAG main)
FetchContent_MakeAvailable(SecuritySocket)
add_library(SecuritySocket ALIAS securitysocket)

## Security Socket Test App

message("-- Security Socket Sample App CMake Initialization --")

add_executable(securitysocket_test main.cpp)
add_dependencies(securitysocket_test securitysocket)
target_link_libraries(securitysocket_test securitysocket)
target_include_directories(securitysocket_test PRIVATE ${securitysocket_include_dir})

