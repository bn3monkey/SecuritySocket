#include "SniffPhase.hpp"

#include "../protocol/ProtocolSniffer.hpp"

namespace Bn3Monkey
{
    ConnectionState SniffPhase::onReadable(PhaseHost& host,
                                           SocketMultiEventListener& listener)
    {
        (void)listener;   // sniff never dispatches SLOW
        // Do NOT drain: the HTTP/Custom phase re-reads these same bytes as the
        // start of its first message (single-buffer carry-over, no copy).
        StagingBuffer& in = host.input();
        switch (ProtocolSniffer::detect(static_cast<const char*>(in.head()), in.pending())) {
        case Protocol::HTTP:
            return ConnectionState::ReceivingHttpRequest;
        case Protocol::CUSTOM:
            return ConnectionState::ReceivingCustomMessage;
        case Protocol::NEED_MORE:
        default:
            return ConnectionState::Sniffing;   // stay; host waits for more bytes
        }
    }

    ConnectionState SniffPhase::onSendComplete(PhaseHost& host)
    {
        (void)host;
        // Sniffing has no write-state, so the host never flushes output here.
        // Returning Closed is a defensive no-op for an unreachable call.
        return ConnectionState::Closed;
    }
}
