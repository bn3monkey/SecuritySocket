#if !defined(__BN3MONKEY_SOCKETRESULT__)
#define __BN3MONKEY_SOCKETRESULT__

#include "../SecuritySocket.hpp"

#if defined _WIN32
#include <windows.h>
#include <WinSock2.h>
#elif defined __linux__
#include <errno.h>
#endif // _WIN32

#include "TLSHelper.hpp"

using namespace Bn3Monkey;

inline SocketResult createResultFromSocketError(int error)
{
#ifdef _WIN32
    switch (error)
	{
		// Socket initialization error
	case WSAEACCES:
		return SocketResult(SocketCode::SOCKET_PERMISSION_DENIED);
	case WSAEAFNOSUPPORT:
		return SocketResult(SocketCode::SOCKET_ADDRESS_FAMILY_NOT_SUPPORTED);
	case WSAEINVAL:
		return SocketResult(SocketCode::SOCKET_INVALID_ARGUMENT);
	case WSAEMFILE:
		return SocketResult(SocketCode::SOCKET_CANNOT_CREATED);
	case WSAENOBUFS :
		return SocketResult(SocketCode::SOCKET_CANNOT_ALLOC);
	case WSAETIMEDOUT:
		return SocketResult(SocketCode::SOCKET_CONNECTION_NOT_RESPOND);
	case WSAEADDRINUSE:
		return SocketResult(SocketCode::SOCKET_CONNECTION_ADDRESS_IN_USE);
	case WSAEBADF :
		return SocketResult(SocketCode::SOCKET_CONNECTION_BAD_DESCRIPTOR);
	case WSAECONNREFUSED:
		return SocketResult(SocketCode::SOCKET_CONNECTION_REFUSED);
	case WSAEFAULT:
		return SocketResult(SocketCode::SOCKET_CONNECTION_BAD_ADDRESS);
	case WSAENETUNREACH:
		return SocketResult(SocketCode::SOCKET_CONNECTION_UNREACHED);
	case WSAEINTR:
		return SocketResult(SocketCode::SOCKET_CONNECTION_INTERRUPTED);
	case WSAEALREADY:
		return SocketResult(SocketCode::SOCKET_ALREADY_CONNECTED);
	case WSAEINPROGRESS:
		return SocketResult(SocketCode::SOCKET_CONNECTION_IN_PROGRESS);
	case WSAEWOULDBLOCK:
	case WSATRY_AGAIN:
		return SocketResult(SocketCode::SOCKET_CONNECTION_NEED_TO_BE_BLOCKED);
	}
#else
    switch (error)
    {
        // Socket initialization error
        case EACCES:
            return SocketResult(SocketCode::SOCKET_PERMISSION_DENIED);
        case EAFNOSUPPORT:
            return SocketResult(SocketCode::SOCKET_ADDRESS_FAMILY_NOT_SUPPORTED);
        case EINVAL:
            return SocketResult(SocketCode::SOCKET_INVALID_ARGUMENT);
        case EMFILE:
            return SocketResult(SocketCode::SOCKET_CANNOT_CREATED);
        case ENOBUFS:
            return SocketResult(SocketCode::SOCKET_CANNOT_ALLOC);
        case ETIMEDOUT:
            return SocketResult(SocketCode::SOCKET_CONNECTION_NOT_RESPOND);
        case EADDRINUSE:
            return SocketResult(SocketCode::SOCKET_CONNECTION_ADDRESS_IN_USE);
        case EBADF:
            return SocketResult(SocketCode::SOCKET_CONNECTION_BAD_DESCRIPTOR);
        case ECONNREFUSED:
            return SocketResult(SocketCode::SOCKET_CONNECTION_REFUSED);
        case EFAULT:
            return SocketResult(SocketCode::SOCKET_CONNECTION_BAD_ADDRESS);
        case ENETUNREACH:
            return SocketResult(SocketCode::SOCKET_CONNECTION_UNREACHED);
        case EINTR:
            return SocketResult(SocketCode::SOCKET_CONNECTION_INTERRUPTED);
        case EALREADY:
            return SocketResult(SocketCode::SOCKET_ALREADY_CONNECTED);
        case EINPROGRESS:
            return SocketResult(SocketCode::SOCKET_CONNECTION_IN_PROGRESS);
        case EWOULDBLOCK:
            return SocketResult(SocketCode::SOCKET_CONNECTION_NEED_TO_BE_BLOCKED);
    }
#endif
    return SocketResult(SocketCode::UNKNOWN_ERROR);
}

inline SocketResult createResult(int operation_return)
{
	if (operation_return > 0)
	{
		return SocketResult(SocketCode::SUCCESS, operation_return);
	}
#ifdef _WIN32
	int error = WSAGetLastError();
#else
	int error = errno;
#endif

	auto res = createResultFromSocketError(error);
    res = SocketResult(res.code(), operation_return);
    return res;
}


inline SocketResult createTLSResult(SSL* ssl, int operation_return)
{
	if (operation_return >= 1)
		return SocketResult(SocketCode::SUCCESS, operation_return);

	int code = SSL_get_error(ssl, operation_return);
	switch (code)
	{
	case SSL_ERROR_SSL: {
		// [Step 1] Check the X.509 chain/hostname verification result, but ONLY
		// when the client actually enabled peer verification (SSL_VERIFY_PEER).
		// With SSL_VERIFY_NONE, OpenSSL may still internally populate the verify
		// result (e.g., 18 = X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT for a
		// self-signed server cert) even though the client chose to ignore it.
		// Blindly trusting that value would mask real client-cert rejection alerts.
		if (SSL_get_verify_mode(ssl) & SSL_VERIFY_PEER) {
			long verify_result = SSL_get_verify_result(ssl);

			// X509_V_ERR_HOSTNAME_MISMATCH (62) : CN / DNS SAN does not match the
			//   expected hostname passed to SSL_set1_host().
			// X509_V_ERR_IP_ADDRESS_MISMATCH (64) : IP SAN does not match the IP
			//   address passed to SSL_set1_host().  OpenSSL uses a separate code
			//   when the host string is an IP literal, so both must be caught here.
			if (verify_result == X509_V_ERR_HOSTNAME_MISMATCH
			 || verify_result == X509_V_ERR_IP_ADDRESS_MISMATCH)
				return SocketResult(SocketCode::TLS_HOSTNAME_MISMATCH, operation_return);

			// Any other non-OK verify result means the server certificate itself is
			// invalid (expired, untrusted issuer, revoked, etc.).
			if (verify_result != X509_V_OK)
				return SocketResult(SocketCode::TLS_SERVER_CERT_INVALID, operation_return);
		}

		// [Step 2] Distinguish protocol-level failures via the OpenSSL error queue.
		int reason = ERR_GET_REASON(ERR_peek_error());

		// TLS version negotiation failure.
		if (reason == SSL_R_UNSUPPORTED_PROTOCOL
		 || reason == SSL_R_NO_PROTOCOLS_AVAILABLE
		 || reason == SSL_R_TLSV1_ALERT_PROTOCOL_VERSION)
			return SocketResult(SocketCode::TLS_VERSION_NOT_SUPPORTED, operation_return);

		// Client-side: the SSL context itself has no usable cipher configured.
		// These are set before any network exchange happens.
		if (reason == SSL_R_NO_CIPHERS_AVAILABLE
		 || reason == SSL_R_NO_SHARED_CIPHER)
			return SocketResult(SocketCode::TLS_CIPHER_SUITE_MISMATCH, operation_return);

		// Unambiguous client-certificate rejection alerts from the server:
		//   certificate_required (TLS 1.3) and unknown_ca are only sent when
		//   the server is processing a client certificate, so they cannot be
		//   confused with a cipher mismatch.
		if (reason == SSL_R_TLSV13_ALERT_CERTIFICATE_REQUIRED
		 || reason == SSL_R_TLSV1_ALERT_UNKNOWN_CA)
			return SocketResult(SocketCode::TLS_CLIENT_CERT_REJECTED, operation_return);

		// SSL_R_SSLV3_ALERT_HANDSHAKE_FAILURE (1040) is ambiguous: the server
		// sends this generic alert both for cipher-suite mismatches and for
		// missing/rejected client certificates (TLS 1.2).
		// Disambiguate by checking whether cipher negotiation already completed:
		//   - Cipher mismatch: ServerHello was never received, so no cipher was
		//     negotiated → SSL_get_current_cipher() == nullptr.
		//   - Cert rejection (no cert / untrusted cert): the handshake advanced
		//     past ServerHello and cipher negotiation succeeded before the server
		//     later rejected the Certificate message → SSL_get_current_cipher() != nullptr.
		if (reason == SSL_R_SSLV3_ALERT_HANDSHAKE_FAILURE) {
			if (SSL_get_current_cipher(ssl) != nullptr)
				return SocketResult(SocketCode::TLS_CLIENT_CERT_REJECTED, operation_return);
			else
				return SocketResult(SocketCode::TLS_CIPHER_SUITE_MISMATCH, operation_return);
		}

		return SocketResult(SocketCode::SSL_PROTOCOL_ERROR, operation_return);
	}
	case SSL_ERROR_WANT_READ:
	case SSL_ERROR_WANT_WRITE:
		return SocketResult(SocketCode::SOCKET_CONNECTION_NEED_TO_BE_BLOCKED, operation_return);
	case SSL_ERROR_SYSCALL:
		// System Error / TCP Error
		return createResult(operation_return);
	case SSL_ERROR_ZERO_RETURN:
		return SocketResult(SocketCode::SSL_ERROR_CLOSED_BY_PEER, operation_return);
	}
	return SocketResult(SocketCode::UNKNOWN_ERROR, operation_return);

}

inline const char* getMessage(const SocketCode& code)
{
    switch (code)
    {
        case SocketCode::SUCCESS: return "Success";
        case SocketCode::ADDRESS_NOT_AVAILABLE: return "Address not available";

        case SocketCode::WINDOWS_SOCKET_INITIALIZATION_FAIL: return "Windows socket initialization failed";
        case SocketCode::TLS_CONTEXT_INITIALIZATION_FAIL: return "TLS context initialization failed";
        case SocketCode::TLS_INITIALIZATION_FAIL: return "TLS initialization failed";

        // - SOCKET INITIALIZATION
        case SocketCode::SOCKET_PERMISSION_DENIED: return "The process does not have permission to create the socket.";
        case SocketCode::SOCKET_ADDRESS_FAMILY_NOT_SUPPORTED: return "Address Faimily is not supported in host system";
        case SocketCode::SOCKET_INVALID_ARGUMENT: return "An invalid argument was used when creating the socket.";
        case SocketCode::SOCKET_CANNOT_CREATED: return "No more sockets can be created because the file descriptor limit has been reached.";
        case SocketCode::SOCKET_CANNOT_ALLOC: return "No more sockets can be created due to insufficient memory.";
        case SocketCode::SOCKET_OPTION_ERROR: return "Socket option error occurred";

        // - SOCKET CONNECTION
        case SocketCode::SOCKET_CONNECTION_NOT_RESPOND: return "Connected host is not responding to requests.";
        case SocketCode::SOCKET_CONNECTION_ADDRESS_IN_USE: return "Socket connection address already in use";
        case SocketCode::SOCKET_CONNECTION_BAD_DESCRIPTOR: return "Invalid socket descriptor";
        case SocketCode::SOCKET_CONNECTION_REFUSED: return "The connection was refused, or the server did not accept the connection request.";
        case SocketCode::SOCKET_CONNECTION_BAD_ADDRESS: return "Bad address for socket connection";
        case SocketCode::SOCKET_CONNECTION_UNREACHED: return "Socket connection unreachable";
        case SocketCode::SOCKET_CONNECTION_INTERRUPTED: return "Socket connection interrupted";

        case SocketCode::SOCKET_ALREADY_CONNECTED: return "Socket is already connected";
        case SocketCode::SOCKET_CONNECTION_IN_PROGRESS: return "Socket connection in progress";
        case SocketCode::SOCKET_CONNECTION_NEED_TO_BE_BLOCKED: return "Socket has no data. Call select or poll function for waiting data";

        case SocketCode::SOCKET_BIND_FAILED: return "Socket binding failed";
        case SocketCode::SOCKET_LISTEN_FAILED: return "Socket listen failed";

        case SocketCode::TLS_SETFD_ERROR: return "Failed to set file descriptor for TLS";

        case SocketCode::TLS_VERSION_NOT_SUPPORTED: return "Server does not support the configured TLS version";
        case SocketCode::TLS_CIPHER_SUITE_MISMATCH: return "No common cipher suite between client and server";
        case SocketCode::TLS_SERVER_CERT_INVALID: return "Server certificate verification failed";
        case SocketCode::TLS_CLIENT_CERT_REJECTED: return "Server rejected the client certificate";
        case SocketCode::TLS_HOSTNAME_MISMATCH: return "Server certificate does not match the expected hostname";

        case SocketCode::SSL_PROTOCOL_ERROR: return "SSL protocol error occurred";
        case SocketCode::SSL_ERROR_CLOSED_BY_PEER: return "SSL connection closed by peer";

        case SocketCode::SOCKET_TIMEOUT: return "Socket operation timed out";
        case SocketCode::SOCKET_CLOSED: return "Socket is closed";

        case SocketCode::SOCKET_EVENT_ERROR: return "Socket event error occurred";
        case SocketCode::SOCKET_EVENT_OBJECT_NOT_CREATED: return "Socket event object could not be created";
        case SocketCode::SOCKET_EVENT_CANNOT_ADDED: return "Failed to add socket event";

        case SocketCode::SOCKET_SERVER_ALREADY_RUNNING: return "Socket server is already running";

        case SocketCode::UNKNOWN_ERROR: return "Unknown socket error occurred";

        case SocketCode::LENGTH: return "Invalid socket code (LENGTH should not be used)";

        default: return "Unhandled socket error code";
    }
}


#endif // __BN3MONKEY_SOCKETRESULT__