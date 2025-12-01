#!/usr/bin/env python3

import socket
import sys
import time

HOST = '127.0.0.1'
PORT = 5555
BUFFER_SIZE = 1024
TIMEOUT = 5

def connect_to_server():
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.settimeout(TIMEOUT)

    try:
        print(f"[CLIENT] Connecting to {HOST}:{PORT}...")
        client_socket.connect((HOST, PORT))
        print("[CLIENT] Successfully connected to server!\n")

        message = "Hello World!"

        client_socket.sendall(message.encode('utf-8'))
        print(f"[CLIENT] Sent: {message}")

        response_data = client_socket.recv(BUFFER_SIZE)

        if response_data:
            response = response_data.decode('utf-8')
            print(f"[CLIENT] Received: {response}\n")

        else:
            print("[CLIENT] No response from server\n")

    except socket.timeout:
        print(f"[CLIENT] ERROR: Connection timeout after {TIMEOUT} seconds")
        print("[CLIENT] Make sure server is running!\n")

    except ConnectionRefusedError:
        print(f"[CLIENT] ERROR: Connection refused")
        print(f"[CLIENT] Server is not running on {HOST}:{PORT}")
        print("[CLIENT] Start server first: python server.py\n")

    except ConnectionResetError:
        print("[CLIENT] ERROR: Connection reset by server")

    except OSError as e:
        print(f"[CLIENT] Socket error: {e}\n")

    except Exception as e:
        print(f"[CLIENT] Unexpected error: {e}\n")

    finally:
        client_socket.close()
        print("[CLIENT] Disconnected from server")

if __name__ == "__main__":
    connect_to_server()
