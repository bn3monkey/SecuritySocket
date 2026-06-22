#if !defined(__SECURITY_SOCKET_TEST_FILE_PROTOCOL__)
#define __SECURITY_SOCKET_TEST_FILE_PROTOCOL__

// Shared "file service" Custom Protocol fixture (milestone_write_stream.md §5).
//
// Driven over BOTH transports by the same handler:
//   - raw TCP Custom Protocol  (securitysockettest_tcp_request_file.cpp)
//   - WebSocket-tunnelled Custom (securitysockettest_websocket_server.cpp)
// proving the handler behaves identically regardless of how the message
// arrives. The struct layouts are shared between the in-process "client" and
// the server, and FILE* handles round-trip as raw bytes — valid only because
// both ends live in the same address space (true for these unit tests).
//
// Op model (continuous streaming, no per-message response for the upload path):
//   CREATE_FILE (FAST)         : open "wb", respond with the FILE*.
//   OPEN_FILE   (FAST)         : open "rb", respond with the FILE* + total size.
//   CLOSE_FILE  (FAST)         : fclose, respond.
//   WRITE_BEGIN (WRITE_STREAM) : enter the upload stream (onWriteStreamBegin).
//   WRITE_CHUNK (stream data)  : appended via onWriteStreamData; header.last=1
//                                ends the stream (no response per chunk).
//   READ_BEGIN  (READ_STREAM)  : server streams raw file bytes back, one chunk
//                                per onReadStreamData, until `total` consumed.
//
// Constructing FileRequestHandler with a WebSocketConfiguration flips its
// supportWebSocket() on so it can be combined with an HttpRequestHandler (for
// the Upgrade) to serve the protocol over ws://.

#include <SecuritySocket.hpp>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <unordered_map>

#include "securitysockettest_helper.hpp"   // printConcurrent


enum class FileRequestType : int32_t {
    CREATE_FILE,   // FAST         : open "wb" -> FileOpenResponse{fp, 0}
    OPEN_FILE,     // FAST         : open "rb" -> FileOpenResponse{fp, total}
    CLOSE_FILE,    // FAST         : fclose    -> FileCloseResponse
    WRITE_BEGIN,   // WRITE_STREAM : stream entry (no payload of interest)
    WRITE_CHUNK,   // stream data  : FileWriteRequestPayload; header.last=1 ends
    READ_BEGIN,    // READ_STREAM  : ReadBeginPayload{fp, chunk_size, total}
};

struct FileRequestHeader
{
    FileRequestType request_type{ FileRequestType::CREATE_FILE };
    int32_t request_no{ 0 };
    size_t  payload_size{ 0 };
    int32_t client_no{ 0 };
    int32_t last{ 0 };          // WRITE_CHUNK: 1 = final chunk

    FileRequestHeader(FileRequestType request_type, int32_t request_no,
                      size_t payload_size, int32_t client_no, int32_t last = 0)
        : request_type(request_type), request_no(request_no),
          payload_size(payload_size), client_no(client_no), last(last) {}
};

struct FileOpenRequestPayload { char filename[256]{ 0 }; };
struct WriteBeginPayload      { FILE* fp{ nullptr }; };
struct FileWriteRequestPayload
{
    FILE*  fp{ nullptr };
    size_t length{ 0 };
    char   data[4096]{ 0 };
};
struct ReadBeginPayload
{
    FILE*  fp{ nullptr };
    size_t chunk_size{ 0 };
    size_t total{ 0 };
};
struct FileCloseRequestPayload { FILE* fp{ nullptr }; };

struct FileResponseHeader
{
    FileRequestType request_type{ FileRequestType::CREATE_FILE };
    int32_t response_no{ 0 };
    size_t  payload_size{ 0 };
};
struct FileOpenResponse  { FileResponseHeader header; FILE* fp{ nullptr }; size_t total_size{ 0 }; };
struct FileCloseResponse { FileResponseHeader header; };

struct FileRequestHandler : public Bn3Monkey::CustomProtocolRequestHandler
{
    using Progress = Bn3Monkey::CustomProtocolRequestHandler::StreamProgress;

    FileRequestHandler() = default;
    explicit FileRequestHandler(const Bn3Monkey::WebSocketConfiguration& ws)
        : Bn3Monkey::CustomProtocolRequestHandler(ws) {}

    size_t headerSize() override { return sizeof(FileRequestHeader); }
    size_t payloadSize(const void* buffer) override {
        return reinterpret_cast<const FileRequestHeader*>(buffer)->payload_size;
    }

    Bn3Monkey::RequestProcessingMode classifyMode(const void* header) override {
        switch (reinterpret_cast<const FileRequestHeader*>(header)->request_type) {
        case FileRequestType::WRITE_BEGIN: return Bn3Monkey::RequestProcessingMode::WRITE_STREAM;
        case FileRequestType::READ_BEGIN:  return Bn3Monkey::RequestProcessingMode::READ_STREAM;
        default:                           return Bn3Monkey::RequestProcessingMode::FAST;
        }
    }

    void onConnected(const Bn3Monkey::ClientConnection& conn) override {
        printConcurrent("Client connected (ip : %s port : %u)\n", conn.ip(), conn.port());
    }
    void onDisconnected(const Bn3Monkey::ClientConnection& conn) override {
        printConcurrent("Client disconnected (ip : %s port : %u)\n", conn.ip(), conn.port());
    }

    // ── FAST: CREATE_FILE / OPEN_FILE / CLOSE_FILE ──
    void process(const Bn3Monkey::ClientConnection&,
                 const Bn3Monkey::CustomProtocolRequest& req,
                 Bn3Monkey::CustomProtocolResponse& res) override
    {
        auto* h  = reinterpret_cast<const FileRequestHeader*>(req.header());
        auto* in = reinterpret_cast<const char*>(req.payload());
        char* out = reinterpret_cast<char*>(res.data());

        switch (h->request_type) {
        case FileRequestType::CREATE_FILE: {
            auto* op = reinterpret_cast<const FileOpenRequestPayload*>(in);
            FILE* fp = fopen(op->filename, "wb");
            new (out) FileOpenResponse{ { h->request_type, h->request_no, sizeof(FileOpenResponse) }, fp, 0 };
            res.setLength(sizeof(FileOpenResponse));
            break;
        }
        case FileRequestType::OPEN_FILE: {
            auto* op = reinterpret_cast<const FileOpenRequestPayload*>(in);
            FILE* fp = fopen(op->filename, "rb");
            size_t total = 0;
            if (fp) { fseek(fp, 0, SEEK_END); total = static_cast<size_t>(ftell(fp)); fseek(fp, 0, SEEK_SET); }
            new (out) FileOpenResponse{ { h->request_type, h->request_no, sizeof(FileOpenResponse) }, fp, total };
            res.setLength(sizeof(FileOpenResponse));
            break;
        }
        case FileRequestType::CLOSE_FILE: {
            auto* cp = reinterpret_cast<const FileCloseRequestPayload*>(in);
            if (cp->fp) fclose(cp->fp);
            new (out) FileCloseResponse{ { h->request_type, h->request_no, sizeof(FileCloseResponse) } };
            res.setLength(sizeof(FileCloseResponse));
            break;
        }
        default:
            break;
        }
    }

    // ── WRITE_STREAM (upload) ──
    void onWriteStreamBegin(const Bn3Monkey::ClientConnection&,
                            const Bn3Monkey::CustomProtocolRequest&) override
    {
        // The FILE* travels in each WRITE_CHUNK payload, so nothing to set up.
    }
    Progress onWriteStreamData(const Bn3Monkey::ClientConnection&,
                               const Bn3Monkey::CustomProtocolRequest& req) override
    {
        auto* h  = reinterpret_cast<const FileRequestHeader*>(req.header());
        auto* wp = reinterpret_cast<const FileWriteRequestPayload*>(req.payload());
        if (wp->fp) fwrite(wp->data, 1, wp->length, wp->fp);
        return h->last ? Progress::COMPLETE : Progress::CONTINUE;
    }

    // ── READ_STREAM (download) ──
    void onReadStreamBegin(const Bn3Monkey::ClientConnection& conn,
                           const Bn3Monkey::CustomProtocolRequest& req) override
    {
        auto* rp = reinterpret_cast<const ReadBeginPayload*>(req.payload());
        std::lock_guard<std::mutex> lock(_mtx);
        _reads[&conn] = ReadState{ rp->fp, rp->chunk_size, rp->total };
    }
    Progress onReadStreamData(const Bn3Monkey::ClientConnection& conn,
                              Bn3Monkey::CustomProtocolResponse& res) override
    {
        ReadState st;
        {
            std::lock_guard<std::mutex> lock(_mtx);
            auto it = _reads.find(&conn);
            if (it == _reads.end()) { res.setLength(0); return Progress::COMPLETE; }
            st = it->second;
        }

        const size_t want = min_of(min_of(st.chunk_size, st.remaining), res.capacity());
        const size_t n = (want && st.fp) ? fread(res.data(), 1, want, st.fp) : 0;
        res.setLength(n);
        st.remaining -= n;

        const bool done = (st.remaining == 0) || (n == 0);
        {
            std::lock_guard<std::mutex> lock(_mtx);
            if (done) _reads.erase(&conn);
            else      _reads[&conn] = st;
        }
        return done ? Progress::COMPLETE : Progress::CONTINUE;
    }

private:
    struct ReadState { FILE* fp{ nullptr }; size_t chunk_size{ 0 }; size_t remaining{ 0 }; };
    std::mutex _mtx;
    std::unordered_map<const Bn3Monkey::ClientConnection*, ReadState> _reads;
};

#endif // __SECURITY_SOCKET_TEST_FILE_PROTOCOL__
