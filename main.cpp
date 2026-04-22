#include <iostream>
#include <thread>
#include "AppState.h"
#include "Database.h"
#include "Network.h"
#include "Gui.h"

int main() {
    AppState state;

    Database db("host=127.0.0.1 port=5444 dbname=telemetry_db user=postgres password=qwerty");
    if (!db.IsConnected()) {
        std::cerr << "DB Connection failed: " << db.GetErrorMessage() << std::endl;
    } else {
        std::cout << "Connected to PostgreSQL successfully!\n";
    }

    std::thread net_thread([&state, &db]() {
        Network::RunServer(state, db);
    });

    Gui appGui;
    if (appGui.Init(1280, 720, "Cyber GPS Monitor")) {
        appGui.RunLoop(state, db);
    } else {
        std::cerr << "Failed to initialize GUI!" << std::endl;
    }

    state.running = false;
    if (net_thread.joinable()) {
        net_thread.join();
    }

    return 0;
}