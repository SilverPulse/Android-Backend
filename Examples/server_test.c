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
    SOCKET server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    int client_len;
    char buffer[BUFFER_SIZE];
    const char *response = "Hello from Server!";

    printf("[SERVER] Initializing Winsock...\n");

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[SERVER] WSAStartup failed\n");
        return 1;
    }

    server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == INVALID_SOCKET) {
        printf("[SERVER] socket failed\n");
        WSACleanup();
        return 1;
    }

    ZeroMemory(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");

    printf("[SERVER] Binding to 127.0.0.1:%d\n", PORT);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("[SERVER] bind failed\n");
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    if (listen(server_sock, 1) == SOCKET_ERROR) {
        printf("[SERVER] listen failed\n");
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    printf("[SERVER] Listening on 127.0.0.1:%d...\n", PORT);
    printf("[SERVER] Waiting for client connection...\n\n");

    while (1) {
        client_len = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);

        if (client_sock == INVALID_SOCKET) {
            printf("[SERVER] accept failed\n");
            continue;
        }

        printf("[SERVER] Client connected from %s\n", inet_ntoa(client_addr.sin_addr));

        ZeroMemory(buffer, BUFFER_SIZE);
        int recv_len = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);

        if (recv_len > 0) {
            buffer[recv_len] = '\0';
            printf("[SERVER] Received: %s\n", buffer);

            send(client_sock, response, strlen(response), 0);
            printf("[SERVER] Sent: %s\n", response);
        }

        closesocket(client_sock);
        printf("[SERVER] Client disconnected\n\n");
    }

    closesocket(server_sock);
    WSACleanup();

    return 0;
}
