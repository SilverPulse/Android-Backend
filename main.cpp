#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include "implot.h"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <fstream>

using json = nlohmann::json;

struct GPSPoint {
    float lat, lon, alt;
    long long timestamp;
};

std::vector<GPSPoint> g_points;
std::mutex g_points_mutex;
bool g_running = true;
std::string g_status = "Waiting for connection...";

std::vector<double> g_time_data;
std::vector<double> g_rsrp_data;

std::vector<double> g_log_lons;
std::vector<double> g_log_lats;
long long g_start_time = 0;

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

                if (j.contains("type")) {
                    if (j["type"] == "location_update") {
                        float lat = j["data"]["lat"];
                        float lon = j["data"]["lon"];
                        float alt = j["data"].value("alt", 0.0f);
                        long long time = j["data"].value("timestamp", 0LL);

                        std::lock_guard<std::mutex> lock(g_points_mutex);
                        g_points.push_back({lat, lon, alt, time});
                        g_status = "Receiving basic location...";
                    }
                    else if (j["type"] == "telemetry_update") {
                        float lat = j["location"]["lat"];
                        float lon = j["location"]["lon"];
                        float alt = j["location"].value("alt", 0.0f);
                        long long time = j["location"].value("time", 0LL);

                        double rsrp = 0.0;
                        bool has_signal = false;
                        if (j.contains("telephony") && j["telephony"].contains("lte")) {
                            rsrp = j["telephony"]["lte"].value("rsrp", 0.0);
                            has_signal = true;
                        }

                        std::lock_guard<std::mutex> lock(g_points_mutex);
                        g_points.push_back({lat, lon, alt, time});

                        if (has_signal) {
                            if (g_time_data.empty()) {
                                g_start_time = time;
                            }
                            double time_sec = (time - g_start_time) / 1000.0;
                            g_time_data.push_back(time_sec);
                            g_rsrp_data.push_back(rsrp);
                        }

                        g_status = "Receiving full telemetry...";
                    }
                }
            } catch (...) {
            }
            socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);
        }
    }
    if (log_file.is_open()) log_file.close();
}

void LoadTrackFromJson(const std::string& filename, int step) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        g_status = "Error: Cannot open " + filename;
        return;
    }

    g_log_lons.clear();
    g_log_lats.clear();

    std::string line;
    int count = 0;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        try {
            auto j = json::parse(line);
            if (j.contains("location")) {
                if (count % step == 0) {
                    g_log_lats.push_back(j["location"]["lat"].get<double>());
                    g_log_lons.push_back(j["location"]["lon"].get<double>());
                }
                count++;
            }
        } catch (...) {
        }
    }
    g_status = "Loaded " + std::to_string(g_log_lons.size()) + " points from JSON";
}

void run_gui_loop(GLFWwindow* window) {
    float scale = 150000.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- ОКНО УПРАВЛЕНИЯ ---
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 390), ImGuiCond_FirstUseEver);
        ImGui::Begin("Settings & Status");
        ImGui::TextColored(ImVec4(0, 1, 1, 1), "SYSTEM STATUS");
        ImGui::Separator();
        ImGui::Text("Status: %s", g_status.c_str());
        ImGui::Text("Points: %zu", g_points.size());

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
                ImGui::TableSetColumnIndex(0); ImGui::Text("Altitude:");
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.1f m", last.alt);

                ImGui::EndTable();
            }
        } else {
            ImGui::TextDisabled("Waiting for first point...");
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::SliderFloat("Zoom", &scale, 1000.0f, 2000000.0f);
        if (ImGui::Button("CLEAR ALL", ImVec2(-1, 40))) {
            std::lock_guard<std::mutex> lock(g_points_mutex);
            g_points.clear();
            g_time_data.clear();
            g_rsrp_data.clear();
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        if (ImGui::Button("LOAD TRACK FROM JSON\n(Every 10th point)", ImVec2(-1, 40))) {
            LoadTrackFromJson("gps_track_log.json", 10);
        }
        ImGui::End();

        // --- ОКНО КАРТЫ ---
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
                        draw_list->AddCircle(p_current, 15.0f, IM_COL32(255, 255, 255, 100), 12, 2.0f);
                    } else if (i == 0) {
                        draw_list->AddCircleFilled(p_current, 7.0f, IM_COL32(0, 200, 255, 255));
                    }
                }
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(10, 410), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
        ImGui::Begin("Signal Strength (LTE RSRP)");

        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.0f, 0.02f, 0.0f, 1.0f));
        ImPlot::PushColormap("Hacker");

        if (ImPlot::BeginPlot("Cell Signal", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Time (seconds)", "RSRP (dBm)");

            std::lock_guard<std::mutex> lock(g_points_mutex);
            if (!g_time_data.empty() && !g_rsrp_data.empty()) {
                ImPlot::PlotLine("LTE RSRP", g_time_data.data(), g_rsrp_data.data(), (int)g_time_data.size());
            }
            ImPlot::EndPlot();
        }
        ImPlot::PopColormap();
        ImPlot::PopStyleColor(2);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(320, 370), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(600, 340), ImGuiCond_FirstUseEver);
        ImGui::Begin("JSON Saved Trajectory (Lat vs Lon)");

        if (ImPlot::BeginPlot("Trajectory Plot", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Longitude (X)", "Latitude (Y)", 0, 0);

            if (!g_log_lats.empty() && !g_log_lons.empty()) {
                ImPlot::PlotLine("Path", g_log_lons.data(), g_log_lats.data(), (int)g_log_lons.size());
                ImPlot::PlotScatter("Points", g_log_lons.data(), g_log_lats.data(), (int)g_log_lons.size());
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
    if (!glfwInit()) return 1;
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Cyber GPS Monitor", NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImVec4 hacker_colors[2] = {
            ImVec4(0.0f, 1.0f, 0.2f, 1.0f),
            ImVec4(0.0f, 1.0f, 0.2f, 1.0f)
    };
    ImPlot::AddColormap("Hacker", hacker_colors, 2);

    SetupNeonTheme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    std::thread net_thread(network_thread_func);

    run_gui_loop(window);

    g_running = false;
    if (net_thread.joinable()) net_thread.join();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
