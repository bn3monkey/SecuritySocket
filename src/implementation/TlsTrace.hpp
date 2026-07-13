#ifndef __BN3MONKEY_TLS_TRACE__
#define __BN3MONKEY_TLS_TRACE__

#include "TlsHelper.hpp"

#include <cstdio>

namespace Bn3Monkey
{
    // Both TlsClientConfiguration::TlsEventCallback and
    // TlsServerConfiguration::TlsEventCallback are void(*)(const char*). They are
    // distinct nested typedefs, so name the shared shape once here.
    using TlsEventCallbackFn = void (*)(const char*);

    // Index into the SSL object's ex_data where the user's TlsEventCallback is
    // stashed. The info callback is registered on the SSL_CTX (shared by every
    // connection), so it can only recover the per-session callback this way.
    static constexpr int kTlsEventCallbackExDataIndex = 0;

    // OpenSSL info callback: renders handshake progress / alerts into a human
    // readable line and forwards it to the user's TlsEventCallback, if any.
    //
    // Registered with SSL_CTX_set_info_callback. Used by BOTH sides: the `where`
    // bits carry SSL_ST_CONNECT on a client and SSL_ST_ACCEPT on a server, which
    // is the only difference in the rendered text.
    inline void trackTLSInfo(const SSL* ssl, int where, int ret)
    {
        char buffer[2048]{ 0 };

        int w = where & ~SSL_ST_MASK;

        if (where & SSL_CB_LOOP)
        {
            const char* header = "";
            if (w & SSL_ST_CONNECT) header = "SSL_connect";
            else if (w & SSL_ST_ACCEPT) header = "SSL_accept";
            snprintf(buffer, sizeof(buffer), "[%s] %s", header, SSL_state_string_long(ssl));
        }
        else if (where & SSL_CB_ALERT)
        {
            snprintf(buffer, sizeof(buffer), "[ALERT] : %s :%s", SSL_alert_type_string_long(ret), SSL_alert_desc_string_long(ret));
        }
        else if (where & SSL_CB_EXIT)
        {
            if (ret <= 0) {
                snprintf(buffer, sizeof(buffer), "%s", ret == 0 ? "Handshake failed" : "Handshake error");
            }
        }
        else if (where & SSL_CB_HANDSHAKE_START) {
            snprintf(buffer, sizeof(buffer), "Handshake start");
        }
        else if (where & SSL_CB_HANDSHAKE_DONE) {
            snprintf(buffer, sizeof(buffer), "Handshake done");
        }

        auto* onTLSEvent = reinterpret_cast<TlsEventCallbackFn>(
            SSL_get_ex_data(ssl, kTlsEventCallbackExDataIndex));
        if (onTLSEvent) {
            onTLSEvent(buffer);
        }
    }
}

#endif // __BN3MONKEY_TLS_TRACE__
