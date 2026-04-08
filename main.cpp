#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <cstdlib>
#include "implot.h"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <fstream>
#include <map>
#include <type_traits>
#include <libpq-fe.h>

using json = nlohmann::json;

struct GPSPoint {
    float lat, lon, alt;
    long long timestamp;
};

struct PciData {
    std::vector<double> time_data;
    std::vector<double> rsrp_data;
};

std::vector<GPSPoint> g_points;
std::mutex g_points_mutex;
bool g_running = true;
std::string g_status = "Waiting for connection...";

std::map<int, PciData> g_pci_data;

std::vector<double> g_pci_history_time;
std::vector<double> g_pci_history_value;

int g_current_pci = 0;
double g_current_rsrp = 0.0;

std::vector<double> g_log_lons;
std::vector<double> g_log_lats;
long long g_start_time = 0;

PGconn* g_db_conn = nullptr;
std::mutex g_db_mutex;

void SetupNeonTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    style.WindowRounding = 7.0f;
    style.FrameRounding = 5.0f;
    style.ItemSpacing = ImVec2(10, 8);
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.10f, 0.94f);
    colors[ImGuiCol_Button] = ImVec4(1.00f, 0.50f, 0.00f, 0.80f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
}

template <typename T>
std::string GetVal(const json& j, const std::string& key) {
    if (!j.contains(key) || j[key].is_null()) return "NULL";
    try {
        if constexpr (std::is_same_v<T, std::string>) {
            if (j[key].is_string()) return "'" + j[key].get<std::string>() + "'";
            if (j[key].is_number()) return "'" + std::to_string(j[key].get<int>()) + "'";
        } else {
            return std::to_string(j[key].get<T>());
        }
    } catch (...) {}
    return "NULL";
}

std::string BuildInsertSQL(const json& j) {
    if (!j.contains("location")) return "";

    auto& loc = j["location"];
    long long time = j.value("timestamp", 0LL);
    if (time == 0) return "";

    std::string sql = "INSERT INTO full_telemetry (ts, lat, lon, alt, accuracy, "
                      "lte_pci, lte_earfcn, lte_ci, lte_tac, lte_band, lte_rsrp, lte_rsrq, lte_rssi, lte_rssnr, lte_cqi, lte_asu_level, lte_timing_advance, "
                      "gsm_ci, gsm_bsic, gsm_arfcn, gsm_lac, gsm_dbm, gsm_rssi, gsm_timing_advance, total_bytes_device) VALUES (";

    sql += std::to_string(time) + ", " +
           std::to_string(loc.value("lat", 0.0)) + ", " +
           std::to_string(loc.value("lon", 0.0)) + ", " +
           GetVal<double>(loc, "alt") + ", " +
           GetVal<double>(loc, "accuracy") + ", ";

    if (j.contains("telephony") && j["telephony"].contains("lte")) {
        auto& lte = j["telephony"]["lte"];
        sql += GetVal<int>(lte, "pci") + ", " +
               GetVal<int>(lte, "earfcn") + ", " +
               GetVal<long long>(lte, "ci") + ", " +
               GetVal<int>(lte, "tac") + ", " +
               GetVal<std::string>(lte, "band") + ", " +
               GetVal<int>(lte, "rsrp") + ", " +
               GetVal<int>(lte, "rsrq") + ", " +
               GetVal<int>(lte, "rssi") + ", " +
               GetVal<long long>(lte, "rssnr") + ", " +
               GetVal<long long>(lte, "cqi") + ", " +
               GetVal<int>(lte, "asu_level") + ", " +
               GetVal<long long>(lte, "timing_advance") + ", ";
    } else {
        sql += "NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, ";
    }

    if (j.contains("telephony") && j["telephony"].contains("gsm")) {
        auto& gsm = j["telephony"]["gsm"];
        sql += GetVal<long long>(gsm, "ci") + ", " +
               GetVal<int>(gsm, "bsic") + ", " +
               GetVal<int>(gsm, "arfcn") + ", " +
               GetVal<long long>(gsm, "lac") + ", " +
               GetVal<int>(gsm, "dbm") + ", " +
               GetVal<int>(gsm, "rssi") + ", " +
               GetVal<long long>(gsm, "timing_advance") + ", ";
    } else {
        sql += "NULL, NULL, NULL, NULL, NULL, NULL, NULL, ";
    }

    if (j.contains("network_stats")) {
        sql += GetVal<long long>(j["network_stats"], "total_bytes_device");
    } else {
        sql += "NULL";
    }

    sql += ") ON CONFLICT (ts) DO NOTHING;";
    return sql;
}

void network_thread_func() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    try { socket.bind("tcp://*:5555"); }
    catch (const zmq::error_t& e) { g_status = "Error: " + std::string(e.what()); return; }

    std::ofstream log_file("gps_track_log.json", std::ios::app);

    while (g_running) {
        zmq::message_t request;
        auto res = socket.recv(request, zmq::recv_flags::none);
        if (res) {
            std::string msg_str(static_cast<char*>(request.data()), request.size());
            if (log_file.is_open()) { log_file << msg_str << std::endl; log_file.flush(); }

            try {
                auto j = json::parse(msg_str);

                if (j.contains("type") && j["type"] == "telemetry_update") {
                    float lat = j["location"].value("lat", 0.0f);
                    float lon = j["location"].value("lon", 0.0f);
                    float alt = j["location"].value("alt", 0.0f);
                    long long time = j.value("timestamp", 0LL);

                    {
                        std::lock_guard<std::mutex> lock(g_points_mutex);
                        g_points.push_back({lat, lon, alt, time});

                        if (j.contains("telephony") && j["telephony"].contains("lte")) {
                            int pci = j["telephony"]["lte"].value("pci", 0);
                            int rsrp = j["telephony"]["lte"].value("rsrp", 0);

                            if (g_start_time == 0) g_start_time = time;
                            double time_sec = (time - g_start_time) / 1000.0;

                            g_pci_data[pci].time_data.push_back(time_sec);
                            g_pci_data[pci].rsrp_data.push_back(rsrp);

                            g_pci_history_time.push_back(time_sec);
                            g_pci_history_value.push_back((double)pci);

                            g_current_pci = pci;
                            g_current_rsrp = rsrp;
                        }
                    }

                    if (g_db_conn && PQstatus(g_db_conn) == CONNECTION_OK) {
                        std::string query = BuildInsertSQL(j);
                        if (!query.empty()) {
                            std::lock_guard<std::mutex> db_lock(g_db_mutex);
                            PGresult* res_db = PQexec(g_db_conn, query.c_str());
                            PQclear(res_db);
                        }
                    }
                    g_status = "Receiving full telemetry... Saved ALL to DB!";
                }
            } catch (...) {}
            socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);
        }
    }
    if (log_file.is_open()) log_file.close();
}

void ImportJsonToDB(const std::string& filename) {
    if (!g_db_conn || PQstatus(g_db_conn) != CONNECTION_OK) {
        g_status = "Cannot import: No DB connection";
        return;
    }

    std::ifstream file(filename);
    if (!file.is_open()) return;

    std::string line;
    int imported_count = 0;

    g_status = "Importing ALL data to DB... Please wait.";

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        try {
            auto j = json::parse(line);
            if (j.contains("type") && j["type"] == "telemetry_update") {

                std::string query = BuildInsertSQL(j);
                if (query.empty()) continue;

                std::lock_guard<std::mutex> db_lock(g_db_mutex);
                PGresult* res_db = PQexec(g_db_conn, query.c_str());

                if (atof(PQcmdTuples(res_db)) > 0) {
                    imported_count++;
                }
                PQclear(res_db);
            }
        } catch (...) {}
    }
    g_status = "Import finished! Inserted " + std::to_string(imported_count) + " FULL points.";
}

void LoadTrackFromDB(int step) {
    if (!g_db_conn || PQstatus(g_db_conn) != CONNECTION_OK) {
        g_status = "Error: No DB connection";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_points_mutex);
        g_points.clear();
        g_pci_data.clear();
        g_pci_history_time.clear();
        g_pci_history_value.clear();
        g_log_lats.clear();
        g_log_lons.clear();
        g_start_time = 0;
    }

    std::lock_guard<std::mutex> db_lock(g_db_mutex);
    PGresult* res = PQexec(g_db_conn, "SELECT ts, lat, lon, lte_pci, lte_rsrp FROM full_telemetry ORDER BY ts ASC;");

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        g_status = "DB Select Error: " + std::string(PQerrorMessage(g_db_conn));
        PQclear(res);
        return;
    }

    int rows = PQntuples(res);
    for (int i = 0; i < rows; i += step) {
        long long ts = atoll(PQgetvalue(res, i, 0));
        double lat = atof(PQgetvalue(res, i, 1));
        double lon = atof(PQgetvalue(res, i, 2));
        int pci = atoi(PQgetvalue(res, i, 3));
        int rsrp = atoi(PQgetvalue(res, i, 4));

        if (g_start_time == 0) g_start_time = ts;
        double time_sec = (ts - g_start_time) / 1000.0;

        std::lock_guard<std::mutex> lock(g_points_mutex);

        g_points.push_back({(float)lat, (float)lon, 0.0f, ts});
        g_log_lats.push_back(lat);
        g_log_lons.push_back(lon);

        if (pci != 0) {
            g_pci_data[pci].time_data.push_back(time_sec);
            g_pci_data[pci].rsrp_data.push_back((double)rsrp);

            g_pci_history_time.push_back(time_sec);
            g_pci_history_value.push_back((double)pci);

            g_current_pci = pci;
            g_current_rsrp = (double)rsrp;
        }
    }

    PQclear(res);
    g_status = "Loaded " + std::to_string(rows) + " points with Telemetry from DB!";
}

void run_gui_loop(GLFWwindow* window) {
    float scale = 150000.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 480), ImGuiCond_FirstUseEver);
        ImGui::Begin("Settings & Status");

        ImGui::TextColored(ImVec4(0, 1, 1, 1), "SYSTEM STATUS");
        ImGui::Separator();
        ImGui::TextWrapped("Status: %s", g_status.c_str());

        if (g_db_conn && PQstatus(g_db_conn) == CONNECTION_OK) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "DB: CONNECTED");
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "DB: DISCONNECTED");
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "LATEST DATA");
        ImGui::Separator();

        if (!g_points.empty()) {
            auto& last = g_points.back();
            if (ImGui::BeginTable("gps_table", 2, ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("Latitude:");
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(0, 1, 0, 1), "%.6f", last.lat);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("Longitude:");
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(0, 1, 0, 1), "%.6f", last.lon);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("PCI:");
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1, 1, 0, 1), "%d", g_current_pci);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("RSRP:");
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.1f dBm", g_current_rsrp);
                ImGui::EndTable();
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::SliderFloat("Zoom", &scale, 1000.0f, 2000000.0f);

        if (ImGui::Button("CLEAR ALL", ImVec2(-1, 30))) {
            std::lock_guard<std::mutex> lock(g_points_mutex);
            g_points.clear();
            g_pci_data.clear();
            g_pci_history_time.clear();
            g_pci_history_value.clear();
        }

        ImGui::Dummy(ImVec2(0.0f, 5.0f));

        if (ImGui::Button("LOAD TRACK FROM DATABASE", ImVec2(-1, 40))) {
            LoadTrackFromDB(1);
        }

        ImGui::Dummy(ImVec2(0.0f, 5.0f));

        if (ImGui::Button("IMPORT JSON TO DATABASE", ImVec2(-1, 40))) {
            std::thread([]() { ImportJsonToDB("gps_track_log.json"); }).detach();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(320, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(600, 350), ImGuiCond_FirstUseEver);
        ImGui::Begin("Live Trajectory");
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 win_pos = ImGui::GetCursorScreenPos();
        ImVec2 win_size = ImGui::GetContentRegionAvail();
        draw_list->AddRectFilled(win_pos, ImVec2(win_pos.x + win_size.x, win_pos.y + win_size.y), IM_COL32(5, 5, 15, 255));

        {
            std::lock_guard<std::mutex> lock(g_points_mutex);
            if (!g_points.empty()) {
                float ref_lat = g_points[0].lat;
                float ref_lon = g_points[0].lon;
                ImVec2 center = { win_pos.x + win_size.x / 2, win_pos.y + win_size.y / 2 };
                for (size_t i = 0; i < g_points.size(); i++) {
                    ImVec2 p_current = { center.x + (g_points[i].lon - ref_lon) * scale, center.y - (g_points[i].lat - ref_lat) * scale };
                    if (i > 0) {
                        ImVec2 p_prev = { center.x + (g_points[i-1].lon - ref_lon) * scale, center.y - (g_points[i-1].lat - ref_lat) * scale };
                        draw_list->AddLine(p_prev, p_current, IM_COL32(0, 255, 100, 200), 3.0f);
                    }
                    if (i == g_points.size() - 1) {
                        draw_list->AddCircleFilled(p_current, 10.0f, IM_COL32(255, 0, 80, 255));
                    }
                }
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(10, 500), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 210), ImGuiCond_FirstUseEver);
        ImGui::Begin("Signal Strength (LTE RSRP)");
        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.0f, 0.02f, 0.0f, 1.0f));
        if (ImPlot::BeginPlot("Cell Signal", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Time (seconds)", "RSRP (dBm)");
            std::lock_guard<std::mutex> lock(g_points_mutex);
            for (auto const& [pci, data] : g_pci_data) {
                if (!data.time_data.empty()) {
                    std::string label = "PCI " + std::to_string(pci);
                    ImPlot::PlotLine(label.c_str(), data.time_data.data(), data.rsrp_data.data(), (int)data.time_data.size());
                }
            }
            ImPlot::EndPlot();
        }
        ImPlot::PopStyleColor(2);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(320, 500), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(600, 210), ImGuiCond_FirstUseEver);
        ImGui::Begin("PCI Handover Timeline");
        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.0f, 0.02f, 0.0f, 1.0f));
        if (ImPlot::BeginPlot("PCI Change", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Time (s)", "PCI ID");
            std::lock_guard<std::mutex> lock(g_points_mutex);
            if (!g_pci_history_value.empty()) {
                ImPlot::PlotStairs("Tower PCI", g_pci_history_time.data(), g_pci_history_value.data(), (int)g_pci_history_value.size());
            }
            ImPlot::EndPlot();
        }
        ImPlot::PopStyleColor(2);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(320, 370), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(600, 130), ImGuiCond_FirstUseEver); // Уменьшил высоту, чтобы всё влезло
        ImGui::Begin("Database Saved Trajectory (Lat vs Lon)");
        if (ImPlot::BeginPlot("Trajectory Plot", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Latitude (X)", "Longitude (Y)", 0, 0);
            if (!g_log_lats.empty() && !g_log_lons.empty()) {
                ImPlot::PlotLine("Path (from DB)", g_log_lats.data(), g_log_lons.data(), (int)g_log_lats.size());
                ImPlot::PlotScatter("Points", g_log_lats.data(), g_log_lons.data(), (int)g_log_lats.size());
            }
            ImPlot::EndPlot();
        }
        ImGui::End();

        ImGui::Render();
        int dw, dh; glfwGetFramebufferSize(window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.02f, 0.02f, 0.04f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
}

int main(int, char**) {
    g_db_conn = PQconnectdb("host=127.0.0.1 port=5444 dbname=telemetry_db user=postgres password=qwerty");
    if (PQstatus(g_db_conn) != CONNECTION_OK) {
        printf("DB Connection failed: %s\n", PQerrorMessage(g_db_conn));
    } else {
        printf("Connected to PostgreSQL successfully!\n");
    }

    if (!glfwInit()) return 1;
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Cyber GPS Monitor", NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    SetupNeonTheme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    std::thread net_thread(network_thread_func);

    run_gui_loop(window);

    g_running = false;
    if (net_thread.joinable()) net_thread.join();

    if (g_db_conn) PQfinish(g_db_conn);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}