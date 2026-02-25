import zmq
import json
import os
from datetime import datetime

PORT = "5555"
LOG_FILE = "server_log.txt"

def run_server():
    context = zmq.Context()
    socket = context.socket(zmq.REP)
    socket.bind(f"tcp://*:{PORT}")

    print(f"Сервер запущен. Ожидание данных на порту {PORT}...")

    try:
        while True:
            # Получаем сообщение от телефона
            message = socket.recv_string()

            try:
                # Пытаемся распарсить JSON
                data = json.loads(message)

                # Проверяем тип сообщения
                if data.get("type") == "location_update":
                    gps = data["data"]
                    print(f"📍 КООРДИНАТЫ: Широта {gps['lat']}, Долгота {gps['lon']}, Высота {gps['alt']}")

                elif data.get("type") == "system_event":
                    print(f"⚙️ СИСТЕМА: {data['message']}")

                else:
                    print(f"📩 Получено: {message}")

                # Сохраняем в лог файл
                with open(LOG_FILE, "a", encoding='utf-8') as f:
                    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                    f.write(f"[{timestamp}] {message}\n")

            except json.JSONDecodeError:
                # Если пришел не JSON (например, старый "Hello World")
                print(f"❓ Неизвестный формат: {message}")

            # Отправляем ответ телефону, чтобы ZMQ-сокет REQ-REP не заблокировался
            socket.send_string("Данные приняты")

    except KeyboardInterrupt:
        print("\nСервер остановлен.")
    finally:
        socket.close()
        context.term()

if __name__ == "__main__":
    run_server()