#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 5555
#define BUFFER_SIZE 1024

int main() {
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    const char *message = "Hello World!";

    printf("[CLIENT] Initializing Winsock...\n");

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[CLIENT] WSAStartup failed\n");
        return 1;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("[CLIENT] socket failed\n");
        WSACleanup();
        return 1;
    }

    ZeroMemory(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");

    printf("[CLIENT] Connecting to 127.0.0.1:%d...\n", PORT);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("[CLIENT] connect failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("[CLIENT] Successfully connected to server!\n\n");

    send(sock, message, strlen(message), 0);
    printf("[CLIENT] Sent: %s\n", message);

    ZeroMemory(buffer, BUFFER_SIZE);
    int recv_len = recv(sock, buffer, BUFFER_SIZE - 1, 0);

    if (recv_len > 0) {
        buffer[recv_len] = '\0';
        printf("[CLIENT] Received: %s\n\n", buffer);
    }

    closesocket(sock);
    WSACleanup();
    printf("[CLIENT] Disconnected from server\n");

    return 0;
}
