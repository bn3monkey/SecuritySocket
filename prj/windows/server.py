import socket

def echo_server(host, port):
    # 소켓 생성
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind((host, port))  # 호스트와 포트에 바인딩
    server_socket.listen(1)  # 연결 요청 대기

    print(f"에코 서버가 {host}:{port}에서 실행 중입니다.")

    while True:
        client_socket, client_address = server_socket.accept()  # 클라이언트 연결 수락
        print(f"클라이언트가 연결되었습니다: {client_address[0]}:{client_address[1]}")

        while True:
            print("DATA 와라!")
            data = client_socket.recv(32768)  # 클라이언트로부터 데이터 수신
            print("DATA 왔따!")

            if not data:
                break  # 데이터가 없으면 연결 종료

            print(f"이게 데이터다! {data}")

            print("Data 보낸다!")
            client_socket.sendall(data)  # 수신한 데이터를 클라이언트에게 전송
            print("Data 보냈다!")

        print(f"클라이언트와의 연결이 종료되었습니다: {client_address[0]}:{client_address[1]}")
        client_socket.close()  # 클라이언트 소켓 닫기

    server_socket.close()  # 서버 소켓 닫기

# 에코 서버 실행
echo_server('127.0.0.1', 3000)