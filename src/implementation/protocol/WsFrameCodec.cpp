#include "WsFrameCodec.hpp"

#include <cstring>

namespace Bn3Monkey
{
    WsFrameCodec::DecodeResult WsFrameCodec::decode(char* in, size_t in_len,
                                                    WsFrameView& view)
    {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(in);

        // Minimal header is 2 bytes (FIN/opcode + MASK/len7).
        if (in_len < 2) return DecodeResult::NEED_MORE;

        const uint8_t b0 = p[0];
        const uint8_t b1 = p[1];

        const bool    fin     = (b0 & 0x80) != 0;
        const uint8_t rsv     = static_cast<uint8_t>((b0 >> 4) & 0x07);
        const uint8_t opcode  = static_cast<uint8_t>(b0 & 0x0F);
        const bool    masked  = (b1 & 0x80) != 0;
        uint64_t      payload_len = static_cast<uint64_t>(b1 & 0x7F);

        // No extensions negotiated -> all RSV bits must be clear.
        if (rsv != 0) return DecodeResult::INVALID;

        // Validate opcode (reserved opcodes are a protocol error).
        switch (opcode) {
            case 0x0: case 0x1: case 0x2:
            case 0x8: case 0x9: case 0xA:
                break;
            default:
                return DecodeResult::INVALID;
        }

        // Control frames (0x8-0xF) must not be fragmented and cap at 125 bytes.
        const bool is_control = (opcode & 0x08) != 0;
        if (is_control) {
            if (!fin)               return DecodeResult::INVALID;
            if (payload_len > 125)  return DecodeResult::INVALID;
        }

        // RFC 6455 §5.1: client->server frames MUST be masked.
        if (!masked) return DecodeResult::INVALID;

        size_t offset = 2;

        if (payload_len == 126) {
            if (in_len < offset + 2) return DecodeResult::NEED_MORE;
            payload_len = (static_cast<uint64_t>(p[2]) << 8) |
                           static_cast<uint64_t>(p[3]);
            offset += 2;
        } else if (payload_len == 127) {
            if (in_len < offset + 8) return DecodeResult::NEED_MORE;
            payload_len = 0;
            for (size_t i = 0; i < 8; ++i)
                payload_len = (payload_len << 8) | static_cast<uint64_t>(p[offset + i]);
            offset += 8;
            // High bit must be 0 (RFC 6455 §5.2).
            if (payload_len & (static_cast<uint64_t>(1) << 63))
                return DecodeResult::INVALID;
        }

        // 4-byte masking key.
        if (in_len < offset + 4) return DecodeResult::NEED_MORE;
        const unsigned char mask_key[4] = {
            p[offset], p[offset + 1], p[offset + 2], p[offset + 3]
        };
        offset += 4;

        // Whole payload present?
        if (in_len < offset + payload_len) return DecodeResult::NEED_MORE;

        // Unmask in place.
        char* payload = in + offset;
        for (uint64_t i = 0; i < payload_len; ++i) {
            payload[i] = static_cast<char>(
                static_cast<unsigned char>(payload[i]) ^ mask_key[i & 3]);
        }

        view.fin         = fin;
        view.opcode      = static_cast<WsOpcode>(opcode);
        view.payload     = payload;
        view.payload_len = static_cast<size_t>(payload_len);
        view.frame_len   = offset + static_cast<size_t>(payload_len);
        return DecodeResult::OK;
    }

    size_t WsFrameCodec::encode(WsOpcode opcode, const char* payload, size_t len,
                                bool fin, char* out, size_t out_capacity)
    {
        // Header: 2 bytes + extended length (0 / 2 / 8). Never masked.
        size_t header = 2;
        if (len > 0xFFFF)    header += 8;
        else if (len > 125)  header += 2;

        if (out_capacity < header + len) return 0;

        unsigned char* o = reinterpret_cast<unsigned char*>(out);
        o[0] = static_cast<unsigned char>(
            (fin ? 0x80 : 0x00) | (static_cast<uint8_t>(opcode) & 0x0F));

        if (len > 0xFFFF) {
            o[1] = 127;
            const uint64_t n = static_cast<uint64_t>(len);
            for (size_t i = 0; i < 8; ++i)
                o[2 + i] = static_cast<unsigned char>((n >> (56 - 8 * i)) & 0xFF);
        } else if (len > 125) {
            o[1] = 126;
            o[2] = static_cast<unsigned char>((len >> 8) & 0xFF);
            o[3] = static_cast<unsigned char>(len & 0xFF);
        } else {
            o[1] = static_cast<unsigned char>(len);
        }

        if (payload && len) std::memcpy(out + header, payload, len);
        return header + len;
    }

    size_t WsFrameCodec::encodeClose(uint16_t status_code,
                                     char* out, size_t out_capacity)
    {
        char body[2];
        body[0] = static_cast<char>((status_code >> 8) & 0xFF);
        body[1] = static_cast<char>(status_code & 0xFF);
        return encode(WsOpcode::CLOSE, body, sizeof(body), true, out, out_capacity);
    }

    size_t WsFrameCodec::encodeCloseEcho(const char* close_payload, size_t len,
                                         char* out, size_t out_capacity)
    {
        if (len > MAX_CONTROL_PAYLOAD) len = MAX_CONTROL_PAYLOAD;
        // < 2 bytes => peer sent no status code; reply with a bare Close.
        if (len < 2)
            return encode(WsOpcode::CLOSE, nullptr, 0, true, out, out_capacity);
        return encode(WsOpcode::CLOSE, close_payload, len, true, out, out_capacity);
    }

    size_t WsFrameCodec::encodePong(const char* ping_payload, size_t len,
                                    char* out, size_t out_capacity)
    {
        if (len > MAX_CONTROL_PAYLOAD) len = MAX_CONTROL_PAYLOAD;
        return encode(WsOpcode::PONG, ping_payload, len, true, out, out_capacity);
    }
}
