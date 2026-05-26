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
    ImGui::SetNextWindowSize(ImVec2(300, 700), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 700), ImGuiCond_FirstUseEver);
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

    ImGui::Dummy(ImVec2(0.0f, 15.0f));
    ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "PCI DISPLAY MODE");
    ImGui::Separator();

    bool is_all_selected = (!state.all_available_pcis.empty() && state.selected_pcis.size() == state.all_available_pcis.size());
    const char* btn_text = is_all_selected ? "SHOW ONLY BEST PCI" : "SHOW ALL PCI";

    if (ImGui::Button(btn_text, ImVec2(-1, 40))) {
        std::lock_guard<std::mutex> lock(state.points_mutex);
        state.selected_pcis.clear();

        if (is_all_selected) {
            if (state.top_pci != -1) state.selected_pcis.insert(state.top_pci);
        } else {
            state.selected_pcis = state.all_available_pcis;
        }
        heatTiles.ClearCache();
    }

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

    static int heatmap_criterion = 0;
    const char* criteria_names[] = { "RSRP", "RSRQ", "RSSI", "Altitude" };

    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo("Heatmap Metric", &heatmap_criterion, criteria_names, IM_ARRAYSIZE(criteria_names))) {
        heatTiles.ClearCache();
    }

    double targetLat = 55.0131;
    double targetLon = 82.9506;

    double startX = MapMath::lon2x(targetLon, 0);
    double startY = MapMath::lat2y(targetLat, 0);

    double initSize = 1.0 / (1 << 16);

    ImPlot::SetNextAxesLimits(startX - initSize/2, startX + initSize/2,
                              startY - initSize/2, startY + initSize/2, ImGuiCond_FirstUseEver);

    static const ImPlotAxisFlags axFlags = ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickMarks;

    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    if (ImPlot::BeginPlot("##MapPlot", ImVec2(-1, -1), ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes("X", "Y", axFlags, axFlags | ImPlotAxisFlags_Invert);

        ImPlotRect lims = ImPlot::GetPlotLimits();
        double viewWidth = lims.X.Max - lims.X.Min;

        int currentZoom = (viewWidth > 0) ? std::max(0, std::min((int)std::floor(-std::log2(viewWidth)) + 1, 18)) : 0;
        double tilesCount = (double)(1 << currentZoom);

        int minTileX = std::max(0, (int)std::floor(lims.X.Min * tilesCount));
        int maxTileX = std::min((1 << currentZoom) - 1, (int)std::floor(lims.X.Max * tilesCount));
        int minTileY = std::max(0, (int)std::floor(lims.Y.Min * tilesCount));
        int maxTileY = std::min((1 << currentZoom) - 1, (int)std::floor(lims.Y.Max * tilesCount));

        if ((maxTileX - minTileX + 1) * (maxTileY - minTileY + 1) < 256) {
            for (int x = minTileX; x <= maxTileX; ++x) {
                for (int y = minTileY; y <= maxTileY; ++y) {

                    ImGui::PushID(x * 100000 + y);

                    ImVec2 bmin{ (float)(x / tilesCount), (float)(y / tilesCount) };
                    ImVec2 bmax{ (float)((x + 1) / tilesCount), (float)((y + 1) / tilesCount) };
                    ImVec2 uv0{0, 1}; ImVec2 uv1{1, 0};

                    GLuint tex_id = mapTiles.GetTileTexture(currentZoom, x, y);
                    if (tex_id != 0) {
                        ImPlot::PlotImage("OSM", (void*)(intptr_t)tex_id, bmin, bmax, uv0, uv1);
                    }

                    GLuint heat_tex = heatTiles.GetHeatmapTile(currentZoom, x, y, heatmap_criterion, state);
                    if (heat_tex != 0) {
                        ImPlot::PlotImage("Heatmap", (void*)(intptr_t)heat_tex, bmin, bmax, uv0, uv1);
                    }

                    ImGui::PopID();
                }
            }
        } else {
            ImPlot::PlotText("Loading / Zoom in...", (lims.X.Min + lims.X.Max)/2, (lims.Y.Min + lims.Y.Max)/2);
        }

        ImPlot::EndPlot();
    }

    ImPlot::PopStyleColor(2);

    ImGui::SetCursorPos(ImVec2(15, 60));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.7f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);

    if (ImGui::BeginChild("SignalLegend", ImVec2(220, 85), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs)) {
        const char* title = "";
        const char* minL = ""; const char* midL = ""; const char* maxL = "";

        if (heatmap_criterion == 0)      { title = "RSRP (dBm)";   minL = "-120"; midL = "-95"; maxL = "-70"; }
        else if (heatmap_criterion == 1) { title = "RSRQ (dB)";    minL = "-20";  midL = "-15"; maxL = "-9";  }
        else if (heatmap_criterion == 2) { title = "RSSI (dBm)";   minL = "-110"; midL = "-92"; maxL = "-75"; }
        else if (heatmap_criterion == 3) { title = "Altitude (m)"; minL = "50";   midL = "155"; maxL = "260"; }

        ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", title);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 3));

        ImVec2 p = ImGui::GetCursorScreenPos();
        float width = 190.0f;
        float height = 15.0f;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilledMultiColor(ImVec2(p.x, p.y), ImVec2(p.x + width*0.25f, p.y + height), IM_COL32(0,0,255,255), IM_COL32(0,255,255,255), IM_COL32(0,255,255,255), IM_COL32(0,0,255,255));
        draw_list->AddRectFilledMultiColor(ImVec2(p.x + width*0.25f, p.y), ImVec2(p.x + width*0.5f, p.y + height), IM_COL32(0,255,255,255), IM_COL32(0,255,0,255), IM_COL32(0,255,0,255), IM_COL32(0,255,255,255));
        draw_list->AddRectFilledMultiColor(ImVec2(p.x + width*0.5f, p.y), ImVec2(p.x + width*0.75f, p.y + height), IM_COL32(0,255,0,255), IM_COL32(255,255,0,255), IM_COL32(255,255,0,255), IM_COL32(0,255,0,255));
        draw_list->AddRectFilledMultiColor(ImVec2(p.x + width*0.75f, p.y), ImVec2(p.x + width, p.y + height), IM_COL32(255,255,0,255), IM_COL32(255,0,0,255), IM_COL32(255,0,0,255), IM_COL32(255,255,0,255));

        ImGui::Dummy(ImVec2(width, height + 2));

        ImGui::Text("%s", minL);
        ImGui::SameLine(width / 2 - ImGui::CalcTextSize(midL).x / 2); ImGui::Text("%s", midL);
        ImGui::SameLine(width - ImGui::CalcTextSize(maxL).x + 5);     ImGui::Text("%s", maxL);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::End();
}