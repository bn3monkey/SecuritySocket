#if !defined(__BN3MONKEY_SECURITYSOCKET_TEST__)
#define __BN3MONKEY_SECURITYSOCKET_TEST__

/*
    Build Configuration:

    - Define SECURITYSOCKETTEST_STATIC for static builds
    - Define SECURITYSOCKETTEST_EXPORTS when building the shared library
*/

#if defined(SECURITYSOCKETTEST_STATIC)

/* Static build: no import/export needed */
#define SECURITYSOCKETTEST_API

#else

#if defined(_WIN32) || defined(_WIN64)

#if defined(SECURITYSOCKETTEST_EXPORTS)
#define SECURITYSOCKETTEST_API __declspec(dllexport)
#else
#define SECURITYSOCKETTEST_API __declspec(dllimport)
#endif

#elif defined(__linux__) || defined(__unix__) || defined(__ANDROID__)

#if defined(SECURITYSOCKETTEST_EXPORTS)
#define SECURITYSOCKETTEST_API __attribute__((visibility("default")))
#else
#define SECURITYSOCKETTEST_API
#endif

#else
#define SECURITYSOCKETTEST_API
#endif

#endif  // SECURITYSOCKETTEST_STATIC


#ifdef __cplusplus
extern "C" {
#endif

    SECURITYSOCKETTEST_API int startSecuritySocketTest(int argc, char** argv, const char* cwd);

#ifdef __cplusplus
}
#endif

#endif // __BN3MONKEY_SECURITYSOCKET_TEST__