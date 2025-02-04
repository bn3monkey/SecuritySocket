
#if !defined(__BN3MONKEY_SECURITYSOCKET_TEST__)
#define __BN3MONKEY_SECURITYSOCKET_TEST__

#if defined(_WIN32) || defined(_WIN64) // Windows
    #ifdef SECURITYSOCKETTEST_EXPORTS
        #define SECURITYSOCKETTEST_API __declspec(dllexport)
    #else
        #define SECURITYSOCKETTEST_API __declspec(dllimport)
    #endif
#elif defined(__linux__) || defined(__unix__) || defined(__ANDROID__) // Linux / Android
    #ifdef SECURITYSOCKETTEST_EXPORTS
        #define SECURITYSOCKETTEST_API __attribute__((visibility("default")))
    #else
        #define SECURITYSOCKETTEST_API
    #endif
#else 
    #define SECURITYSOCKETTEST_API
    #pragma warning Unknown dynamic link import/export semantics.
#endif

#ifdef __cplusplus
extern "C" {
#endif

SECURITYSOCKETTEST_API int startSecuritySocketTest(int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif // __BN3MONKEY_SECURITYSOCKET_TEST__