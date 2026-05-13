#include "Network.h"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>

using json = nlohmann::json;

void Network::RunServer(AppState& state, Database& db) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    try {
        socket.bind("tcp://*:5555");
    } catch (const zmq::error_t& e) {
        state.SetStatus("Error: " + std::string(e.what()));
        return;
    }

    std::ofstream log_file("gps_track_log.json", std::ios::app);

    while (state.running) {
        zmq::message_t request;
        auto res = socket.recv(request, zmq::recv_flags::dontwait);

        if (!res) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::string msg_str(static_cast<char*>(request.data()), request.size());
        if (log_file.is_open()) {
            log_file << msg_str << std::endl;
            log_file.flush();
        }

        try {
            auto j = json::parse(msg_str);

            if (j.contains("type") && j["type"] == "telemetry_update") {
                float lat = j["location"].value("lat", 0.0f);
                float lon = j["location"].value("lon", 0.0f);
                float alt = j["location"].value("alt", 0.0f);
                long long time = j.value("timestamp", 0LL);

                int pci = 0, rsrp = -140, rsrq = -30, rssi = -120;
                if (j.contains("telephony") && j["telephony"].contains("lte")) {
                    pci = j["telephony"]["lte"].value("pci", 0);
                    rsrp = j["telephony"]["lte"].value("rsrp", -140);
                    rsrq = j["telephony"]["lte"].value("rsrq", -30);
                    rssi = j["telephony"]["lte"].value("rssi", -120);
                }

                {
                    std::lock_guard<std::mutex> lock(state.points_mutex);

                    GPSPoint newPoint;
                    newPoint.lat = lat;
                    newPoint.lon = lon;
                    newPoint.rsrp = (double)rsrp;
                    newPoint.rsrq = (double)rsrq;
                    newPoint.rssi = (double)rssi;
                    newPoint.altitude = (double)alt;
                    state.points.push_back(newPoint);

                    if (pci != 0) {
                        if (state.start_time == 0) state.start_time = time;
                        double time_sec = (time - state.start_time) / 1000.0;

                        state.pci_data[pci].time_data.push_back(time_sec);
                        state.pci_data[pci].rsrp_data.push_back(rsrp);

                        state.pci_history_time.push_back(time_sec);
                        state.pci_history_value.push_back((double)pci);

                        state.current_pci = pci;
                        state.current_rsrp = rsrp;
                    }
                }

                db.InsertTelemetry(j);
                state.SetStatus("Receiving full telemetry... Saved ALL to DB!");
            }
        } catch (...) {}

        socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);
    }

    if (log_file.is_open()) log_file.close();
}