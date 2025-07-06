cmake_minimum_required(VERSION 3.16)
project(SecuritySocketMinGWTest)

set(SECURITYSOCKET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(SECURITYSOCKET_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/library")
message(STATUS "SECURITYSOCKET_SOURCE_DIR = ${SECURITYSOCKET_SOURCE_DIR}")
message(STATUS "SECURITYSOCKET_BINARY_DIR = ${SECURITYSOCKET_BINARY_DIR}")

add_subdirectory("${SECURITYSOCKET_SOURCE_DIR}" "${SECURITYSOCKET_BINARY_DIR}")

set(SECURITY_SOCKET_TEST_DIR "${SECURITYSOCKET_SOURCE_DIR}/test")


file(TO_CMAKE_PATH ${CMAKE_PREFIX_PATH} CMAKE_PREFIX_PATH)
message(STATUS "CMAKE_PREFIX_PATH = ${CMAKE_PREFIX_PATH}")

set(MINGW_LIB_PATH "$ENV{QTPath}/Tools/mingw810_64/x86_64-w64-mingw32/lib")
file(TO_CMAKE_PATH ${MINGW_LIB_PATH} MINGW_LIB_PATH)

list (FIND CMAKE_PREFIX_PATH ${MINGW_LIB_PATH} PATH_INDEX)
if (PATH_INDEX EQUAL -1)
    list(APPEND CMAKE_PREFIX_PATH ${MINGW_LIB_PATH})
    message(STATUS "NEW CMAKE_PREFIX_PATH = ${CMAKE_PREFIX_PATH}")
else()
    message(STATUS "${MINGW_LIB_PATH} is already in CMAKE_PREFIX_PATH : ${CMAKE_PREFIX_PATH}")
endif()


add_executable(
	SecuritySocketMinGWTest
	main.cpp
)

target_include_directories(
	SecuritySocketMinGWTest
	PUBLIC
	${SECURITY_SOCKET_TEST_DIR}
)

target_link_libraries(
	SecuritySocketMinGWTest
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
