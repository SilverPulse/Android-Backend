import socket

HOST = '127.0.0.1'
PORT = 5555
BACKLOG = 1
BUFFER_SIZE = 1024

def start_server():
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    try:
        server_socket.bind((HOST, PORT))
        print(f"[SERVER] Binding to {HOST}:{PORT}")

        server_socket.listen(BACKLOG)
        print(f"[SERVER] Listening on {HOST}:{PORT}...")
        print("[SERVER] Waiting for client connection...\n")

        while True:
            client_socket, client_address = server_socket.accept()
            print(f"[SERVER] Client connected from {client_address}")

            try:
                received_data = client_socket.recv(BUFFER_SIZE)

                if received_data:
                    message = received_data.decode('utf-8')
                    print(f"[SERVER] Received: {message}")

                    response = "Hello from Server!"

                    client_socket.sendall(response.encode('utf-8'))
                    print(f"[SERVER] Sent: {response}")

                else:
                    print("[SERVER] No data received")

            except Exception as e:
                print(f"[SERVER] Error processing client: {e}")

            finally:
                client_socket.close()
                print(f"[SERVER] Client {client_address} disconnected\n")

    except KeyboardInterrupt:
        print("\n[SERVER] Interrupt received, shutting down...")

    except OSError as e:
        print(f"[SERVER] Socket error: {e}")
        print(f"[SERVER] Make sure port {PORT} is not already in use")

    except Exception as e:
        print(f"[SERVER] Unexpected error: {e}")

    finally:
        server_socket.close()
        print("[SERVER] Server stopped\n")

if __name__ == "__main__":
    start_server()
