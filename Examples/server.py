import zmq
import os
from datetime import datetime

PORT = "5555"
LOG_FILE = "server_log.txt"

def print_saved_data():
    if os.path.exists(LOG_FILE):
        print("\n--- Сохранённые данные ---")
        with open(LOG_FILE, 'r', encoding='utf-8') as f:
            print(f.read())
        print("------------------------------\n")
    else:
        print("Файл с данными пока пуст.")

def run_server():
    context = zmq.Context()
    socket = context.socket(zmq.REP)

    socket.bind(f"tcp://*:{PORT}")

    print(f"Сервер запущен. Ожидание данных на порту {PORT}...")
    packet_count = 0

    try:
        while True:
            message = socket.recv_string()
            packet_count += 1

            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            log_entry = f"[{timestamp}] Packet #{packet_count}: {message}\n"

            with open(LOG_FILE, "a", encoding='utf-8') as f:
                f.write(log_entry)

            print(f"Получено: {message} | Всего пакетов: {packet_count}")
            socket.send_string("Hello from Server!")

    except KeyboardInterrupt:
        print("\nСервер остановлен.")
        print_saved_data()
    finally:
        socket.close()
        context.term()

if __name__ == "__main__":
    run_server()
