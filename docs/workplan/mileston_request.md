자. 그럼 docs/workplan에 아래 의 마일스톤을 넣어보자

1. 경량 Http Parser를 Build System에 편입하기
2. SocketClientConnection class 제공
   
- const char* ip() const;
- const uint32_t port() const; 
- SocketClientConnectionImpl은 HttpRequest, HttpResponse, SocketUserProtocolRequest, SocketUserProtocolResponse를 모두 상속하여 적절한 인터페이스를 제공한다.

3. SocketRequstHandler refactoring

- SocketRequestHandler의 Base interface 제공.
- virtual void onConnected(const SocketClientConnection& connection) = 0;
- virtual void onDisconnected(const SocketClientConnection& connection) = 0;
- virtual bool supportHttp();
- virtual bool supportUserProtocol();
- virtual bool supportWebSocket();

4. SocketHttpRequestHandler refactoring

- class SocketHttpRequestHandler : public SocketRequestHandler
- virtual void registerRoutes(SocketHttpRouter& router) = 0; 
- class SocketHttpRouter {
    void get(const char* pattern, [](SocketClientConnection& connection, HttpRequest& req, HttpResponse& res) { });
    void post(const char* pattern, [](SocketClientConnection& connection, HttpRequest& req, HttpResponse& res) { });
    void put(const char* pattern, [](SocketClientConnection& connection, HttpRequest& req, HttpResponse& res) { });
    void del(const char* pattern, [](SocketClientConnection& connection, HttpRequest& req, HttpResponse& res) { });
    void patch(const char* pattern, [](SocketClientConnection& connection, HttpRequest& req, HttpResponse& res) { });
    void head(const char* pattern, [](SocketClientConnection& connection, HttpRequest& req, HttpResponse& res) { });
    void options(const char* pattern, [](SocketClientConnection& connection, HttpRequest& req, HttpResponse& res) { });
}

5. SocketUserProtocolRequestHandler refactoring

- class SocketUserProtocolRequestHandler : public SocketRequestHandler

- SocketCusotmRequestHandler(); // WebSocket OFF
- WebSocketConfiguration { const char* pattern; };
- SocketCustomProtocolRequestHandler(const WebSocketConfiguration& config); //WEB SOCKET ON 
- virtual size_t headerSize(const SocketClientConnection& connection) = 0;
- virtual size_t payloadSize(const SocketClientConnection& connection) = 0;
- virtual SocketRequestMode classifyMode(const SocketClientConnection& connection) = 0;
- class SocketUserProtocolRequest {
    const void* data() = 0; // header + payload인지, payload만인지 기억 안남.
    size_t length = 0;
    } // 내부에서 Impl을 구현해서 멤버변수 초기화함/
- class SocketUserProtocolResponse {
    void* data() = 0; //header + payload 넣는 거였을 듯
    size_t& length() = 0;
    } // 내부에서 Impl을 구현해서 멤버변수 초기화함/

- virtual process(const SocketClientConnection& connection, 
    const SocketUserProtocolRequest& req,
    SocketUserProtocolResponse& res
    ) = 0;
- virtual processWithoutResponse(const SocketClientConnection& connection,
    const SocketUserProtocolRequest& req) = 0;

6. SocketRequestHandler의 실적용

```
// HTTP, Custom Protocol
class UserHandler : public SocketHttpRequestHandler, SocketCustomRequestHandler
{
}
// Only HTTP
class UserHandler : public SocketHttpRequestHandler
{
}
// Only CUSTOM
class UserHandler : public SocketCustomRequestHandler
{
}

```

7. SocketRequestServer 내부 흐름


```
if (handler.supportHttp()) {
    handler 형변환
    start only http routine(httpHandler)
}
else if (handler.supportUserProtocol()) {
    start only user protocol routine(user_protocol_handler)
}
else if (handler.supportWebSocket()) {
    start user protocol with websocket routine(user_protocol_handler)
}
else if (handler.supportHttp() && handler.supportCustomProtocol()) {
    
    start only http routine & user protocol(httpHandler, user_protocol_handler)
}
else if (handler.supportHttp() && handler.supportWebSocket()) {
        start only http routine & user protocol(httpHandler, user_protocol_handler)
}

// HTTP, WebSocket, User Protocol 전부 제공시에만 예시. 나머지 routine은 아래의 routine에서 적절하게 빼서 제공

receive header to check if http request
if (checkIfHttpRequest)
{
    if (existsHttpRouteInHttpHandler) {
        receivePayload
        handleHttpRequest
    } else if (isWebSocketUpgradeRequest) {
        receivePaylaod
        handleUpgrade
    } else if {
        fallback
    }
}
else if (!isWebSocketUpgraded) {
    if (user_defined_header_size > http header) {
        append more header
    }
    else (user_defined_header_size < http header) {
        move surplused header to payload buffer
    }
    classifyMode
    receivePayload
    handlePayload
else {
    check if web socket payload with already received header to check http request
    
    receive user protocol header
    classifyMode
    receivePayload
    handlePayload
}


```

8. SocketHttpClient 추가 

SocketHttpClient : public SocketClient

HttpResponse get(const char* pattern, const HttpRequest& req);
HttpResponse post(const char* pattern, const HttpRequest& req);
HttpResponse put(const char* pattern, const HttpRequest& req);
HttpResponse del(const char* pattern, const HttpRequest& req);
HttpResponse patch(const char* pattern, const HttpRequest& req);
HttpResponse head(const char* pattern, const HttpRequest& req);
HttpResponse options(const char* pattern, const HttpRequest& req);

9. SocketWebSocketClient 추가
// 아이디어 없음.. 아이디어 좀 줘