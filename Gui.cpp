#include "Gui.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <thread>
#include "MapMath.h"
#include <cmath>
#include <algorithm>

Gui::Gui() {}

Gui::~Gui() {
    if (window) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
    }
}

bool Gui::Init(int width, int height, const char* title) {
    if (!glfwInit()) return false;
    window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window) return false;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    SetupNeonTheme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    return true;
}

void Gui::SetupNeonTheme() {
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

void Gui::RunLoop(AppState& state, Database& db) {
    while (!glfwWindowShouldClose(window) && state.running) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        RenderUI(state, db);

        ImGui::Render();
        int dw, dh; glfwGetFramebufferSize(window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }
}

void Gui::RenderUI(AppState& state, Database& db) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin("Settings & Status");

    ImGui::TextColored(ImVec4(0, 1, 1, 1), "SYSTEM STATUS");
    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", state.GetStatus().c_str());

    if (db.IsConnected()) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "DB: CONNECTED");
    } else {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "DB: DISCONNECTED");
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "LATEST DATA");
    ImGui::Separator();

    std::unique_lock<std::mutex> lock(state.points_mutex);
    if (!state.points.empty()) {
        auto& last = state.points.back();
        if (ImGui::BeginTable("gps_table", 2, ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("Latitude:");
            ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(0, 1, 0, 1), "%.6f", last.lat);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("Longitude:");
            ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(0, 1, 0, 1), "%.6f", last.lon);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("PCI:");
            ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1, 1, 0, 1), "%d", state.current_pci);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("RSRP:");
            ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.1f dBm", state.current_rsrp);
            ImGui::EndTable();
        }
    }
    lock.unlock();

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::SliderFloat("Zoom", &scale, 1000.0f, 2000000.0f);

    if (ImGui::Button("CLEAR ALL", ImVec2(-1, 30))) {
        std::lock_guard<std::mutex> lock(state.points_mutex);
        state.points.clear();
        state.pci_data.clear();
        state.pci_history_time.clear();
        state.pci_history_value.clear();
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    if (ImGui::Button("LOAD TRACK FROM DATABASE", ImVec2(-1, 40))) {
        db.LoadTrackFromDB(1, state);
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    if (ImGui::Button("IMPORT JSON TO DATABASE", ImVec2(-1, 40))) {
        std::thread([&]() { db.ImportJsonToDB("gps_track_log.json", state); }).detach();
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(320, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600, 350), ImGuiCond_FirstUseEver);
    ImGui::Begin("Live Trajectory");
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetCursorScreenPos();
    ImVec2 win_size = ImGui::GetContentRegionAvail();
    draw_list->AddRectFilled(win_pos, ImVec2(win_pos.x + win_size.x, win_pos.y + win_size.y), IM_COL32(5, 5, 15, 255));

    lock.lock();
    if (!state.points.empty()) {
        float ref_lat = state.points[0].lat;
        float ref_lon = state.points[0].lon;
        ImVec2 center = { win_pos.x + win_size.x / 2, win_pos.y + win_size.y / 2 };
        for (size_t i = 0; i < state.points.size(); i++) {
            ImVec2 p_current = { center.x + (state.points[i].lon - ref_lon) * scale, center.y - (state.points[i].lat - ref_lat) * scale };
            if (i > 0) {
                ImVec2 p_prev = { center.x + (state.points[i-1].lon - ref_lon) * scale, center.y - (state.points[i-1].lat - ref_lat) * scale };
                draw_list->AddLine(p_prev, p_current, IM_COL32(0, 255, 100, 200), 3.0f);
            }
            if (i == state.points.size() - 1) {
                draw_list->AddCircleFilled(p_current, 10.0f, IM_COL32(255, 0, 80, 255));
            }
        }
    }
    lock.unlock();
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(10, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 210), ImGuiCond_FirstUseEver);
    ImGui::Begin("Signal Strength (LTE RSRP)");
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.0f, 0.02f, 0.0f, 1.0f));
    if (ImPlot::BeginPlot("Cell Signal", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Time (seconds)", "RSRP (dBm)");
        lock.lock();
        for (auto const& [pci, data] : state.pci_data) {
            if (!data.time_data.empty()) {
                std::string label = "PCI " + std::to_string(pci);
                ImPlot::PlotLine(label.c_str(), data.time_data.data(), data.rsrp_data.data(), (int)data.time_data.size());
            }
        }
        lock.unlock();
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
        lock.lock();
        if (!state.pci_history_value.empty()) {
            ImPlot::PlotStairs("Tower PCI", state.pci_history_time.data(), state.pci_history_value.data(), (int)state.pci_history_value.size());
        }
        lock.unlock();
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleColor(2);
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(320, 370), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600, 130), ImGuiCond_FirstUseEver);
    ImGui::Begin("Database Saved Trajectory (Lat vs Lon)");
    if (ImPlot::BeginPlot("Trajectory Plot", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Latitude (X)", "Longitude (Y)", 0, 0);
        lock.lock();
        if (!state.log_lats.empty() && !state.log_lons.empty()) {
            ImPlot::PlotLine("Path (from DB)", state.log_lats.data(), state.log_lons.data(), (int)state.log_lats.size());
            ImPlot::PlotScatter("Points", state.log_lats.data(), state.log_lons.data(), (int)state.log_lats.size());
        }
        lock.unlock();
        ImPlot::EndPlot();
    }
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("OpenStreetMap Live");

    double targetLat = 55.0131;
    double targetLon = 82.9506;

    double startX = MapMath::lon2x(targetLon, 0);
    double startY = MapMath::lat2y(targetLat, 0);

    double initSize = 1.0 / (1 << 16);

    ImPlot::SetNextAxesLimits(startX - initSize/2, startX + initSize/2,
                              startY - initSize/2, startY + initSize/2, ImGuiCond_FirstUseEver);

    static const ImPlotAxisFlags axFlags = ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickMarks;

    if (ImPlot::BeginPlot("##MapPlot", ImVec2(-1, -1), ImPlotFlags_Equal)) {
        ImPlot::SetupAxes("X", "Y", axFlags, axFlags | ImPlotAxisFlags_Invert);

        ImPlotRect lims = ImPlot::GetPlotLimits();

        double viewWidth = lims.X.Max - lims.X.Min;

        int currentZoom = 0;
        if (viewWidth > 0) {
            currentZoom = (int)std::floor(-std::log2(viewWidth));

            currentZoom += 1;
        }

        currentZoom = std::max(0, std::min(currentZoom, 18));

        double tilesCount = (double)(1 << currentZoom);

        int minTileX = std::max(0, (int)std::floor(lims.X.Min * tilesCount));
        int maxTileX = std::min((1 << currentZoom) - 1, (int)std::floor(lims.X.Max * tilesCount));

        int minTileY = std::max(0, (int)std::floor(lims.Y.Min * tilesCount));
        int maxTileY = std::min((1 << currentZoom) - 1, (int)std::floor(lims.Y.Max * tilesCount));

        if ((maxTileX - minTileX + 1) * (maxTileY - minTileY + 1) < 256) {
            for (int x = minTileX; x <= maxTileX; ++x) {
                for (int y = minTileY; y <= maxTileY; ++y) {
                    GLuint tex_id = mapTiles.GetTileTexture(currentZoom, x, y);

                    if (tex_id != 0) {
                        ImVec2 bmin{ (float)(x / tilesCount), (float)(y / tilesCount) };
                        ImVec2 bmax{ (float)((x + 1) / tilesCount), (float)((y + 1) / tilesCount) };

                        ImVec2 uv0{0, 1};
                        ImVec2 uv1{1, 0};

                        ImPlot::PlotImage("##tile", (void*)(intptr_t)tex_id, bmin, bmax, uv0, uv1);
                    }
                }
            }
        } else {
            ImPlot::PlotText("Loading / Zoom in...", (lims.X.Min + lims.X.Max)/2, (lims.Y.Min + lims.Y.Max)/2);
        }

        ImPlot::EndPlot();
    }
    ImGui::End();
}