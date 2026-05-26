#include "HeatmapManager.h"
#include "MapMath.h"
#include "stb_image_write.h"
#include "stb_image.h"
#include <filesystem>
#include <cmath>
#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;

HeatmapManager::HeatmapManager() {}

HeatmapManager::~HeatmapManager() {
    ClearCache();
}

void HeatmapManager::ClearCache() {
    for (auto const& [key, id] : _textures) {
        glDeleteTextures(1, &id);
    }
    _textures.clear();
    while(!_textureQueue.empty()) _textureQueue.pop();
}

GLuint HeatmapManager::GetHeatmapTile(int z, int x, int y, int type, AppState& state) {
    std::string pci_mode = "all";
    {
        std::lock_guard<std::mutex> lock(state.points_mutex);
        if (state.selected_pcis.size() == 1) {
            pci_mode = std::to_string(*state.selected_pcis.begin());
        }
    }

    TileKey key = {z, x, y, type, pci_mode};

    if (_textures.find(key) != _textures.end()) {
        return _textures[key];
    }

    if (_pendingTasks.find(key) != _pendingTasks.end()) {
        auto& fut = _pendingTasks[key];
        if (fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            HeatmapImage img = fut.get();
            _pendingTasks.erase(key);
            if (img.valid) {
                GLuint id = CreateGLTexture(img);
                _textures[key] = id;
                _textureQueue.push(key);
                if (_textureQueue.size() > MAX_TEXTURES) {
                    TileKey oldKey = _textureQueue.front();
                    _textureQueue.pop();
                    glDeleteTextures(1, &_textures[oldKey]);
                    _textures.erase(oldKey);
                }
                return id;
            }
        }
        return 0;
    }

    std::vector<GPSPoint> pointsCopy;
    {
        std::lock_guard<std::mutex> lock(state.points_mutex);
        for (const auto& pt : state.points) {
            if (state.selected_pcis.empty() || state.selected_pcis.count(pt.pci)) {
                pointsCopy.push_back(pt);
            }
        }
    }

    if (pointsCopy.empty()) return 0;

    _pendingTasks[key] = std::async(std::launch::async, [this, z, x, y, type, pci_mode, pointsCopy]() {
        return GenerateTileAsync(z, x, y, type, pci_mode, pointsCopy);
    });

    return 0;
}

void HeatmapManager::GetColorForValue(double value, int type, unsigned char& r, unsigned char& g, unsigned char& b, unsigned char& a) {
    double minVal = -120.0, maxVal = -70.0;

    if (type == 1)      { minVal = -20.0;  maxVal = -9.0;  }
    else if (type == 2) { minVal = -110.0; maxVal = -75.0; }
    else if (type == 3) { minVal = 50.0;   maxVal = 260.0; }

    if (value < minVal) value = minVal;
    if (value > maxVal) value = maxVal;

    double t = (value - minVal) / (maxVal - minVal);
    int colors[5][3] = { {0,0,255}, {0,255,255}, {0,255,0}, {255,255,0}, {255,0,0} };

    double scaled_t = t * 4.0;
    int idx = (int)scaled_t;
    if (idx >= 4) { r = colors[4][0]; g = colors[4][1]; b = colors[4][2]; a = 255; return; }

    double frac = scaled_t - idx;
    r = (unsigned char)(colors[idx][0] + frac * (colors[idx+1][0] - colors[idx][0]));
    g = (unsigned char)(colors[idx][1] + frac * (colors[idx+1][1] - colors[idx][1]));
    b = (unsigned char)(colors[idx][2] + frac * (colors[idx+1][2] - colors[idx][2]));
    a = 255;
}

HeatmapManager::HeatmapImage HeatmapManager::GenerateTileAsync(int z, int x, int y, int type, std::string pci_mode, std::vector<GPSPoint> points) {
    HeatmapImage result;

    std::string dirPath = "heatmap/pci_" + pci_mode + "/type_" + std::to_string(type) + "/" + std::to_string(z) + "/" + std::to_string(x);
    std::string filepath = dirPath + "/" + std::to_string(y) + ".png";

    if (fs::exists(filepath)) {
        int w, h, c;
        unsigned char* data = stbi_load(filepath.c_str(), &w, &h, &c, 4);
        if (data) {
            result.pixels.assign(data, data + (w * h * 4));
            stbi_image_free(data);
            result.valid = true;
            return result;
        }
    }

    double lon_min = MapMath::x2lon(x, z);
    double lon_max = MapMath::x2lon(x + 1, z);
    double lat_max = MapMath::y2lat(y, z);
    double lat_min = MapMath::y2lat(y + 1, z);

    double center_lat = (lat_min + lat_max) / 2.0;
    double lat_to_m = 111132.92;
    double lon_to_m = 111132.92 * std::cos(center_lat * MapMath::RAD);

    double tile_width_m = (lon_max - lon_min) * lon_to_m;
    double meters_per_pixel = tile_width_m / 256.0;

    double max_radius_m = std::max(40.0, 3.5 * meters_per_pixel);
    double min_radius_m = std::max(10.0, 1.0 * meters_per_pixel);
    double max_dist_sq = max_radius_m * max_radius_m;
    double min_dist_sq = min_radius_m * min_radius_m;

    double margin_lat = (max_radius_m / lat_to_m) * 1.2;
    double margin_lon = (max_radius_m / lon_to_m) * 1.2;

    struct LocalPoint { double xm, ym, val; };
    std::vector<LocalPoint> localPoints;

    for (const auto& pt : points) {
        if (pt.lat >= lat_min - margin_lat && pt.lat <= lat_max + margin_lat &&
            pt.lon >= lon_min - margin_lon && pt.lon <= lon_max + margin_lon) {

            double val = 0;
            if (type == 0) val = pt.rsrp;
            else if (type == 1) val = pt.rsrq;
            else if (type == 2) val = pt.rssi;
            else {
                val = pt.altitude;
                if (val < 40.0 || val > 400.0) continue;
            }
            double pt_xm = (pt.lon - lon_min) * lon_to_m;
            double pt_ym = (lat_max - pt.lat) * lat_to_m;
            localPoints.push_back({pt_xm, pt_ym, val});
        }
    }

    if (localPoints.empty()) return result;

    result.pixels.resize(256 * 256 * 4, 0);
    bool hasData = false;

    for (int py = 0; py < 256; ++py) {
        double global_y = y + (double)py / 256.0;
        double pixel_lat = MapMath::y2lat(global_y, z);
        double py_m = (lat_max - pixel_lat) * lat_to_m;

        for (int px = 0; px < 256; ++px) {
            double global_x = x + (double)px / 256.0;
            double pixel_lon = MapMath::x2lon(global_x, z);
            double px_m = (pixel_lon - lon_min) * lon_to_m;

            double sum_weights = 0.0, sum_values = 0.0;
            double nearest_dist_sq = max_dist_sq + 1.0;

            for (const auto& lpt : localPoints) {
                double dx = px_m - lpt.xm;
                double dy = py_m - lpt.ym;
                double d2 = dx*dx + dy*dy;

                if (d2 <= max_dist_sq) {
                    nearest_dist_sq = std::min(nearest_dist_sq, d2);
                    double weight = 1.0 / std::max(d2, min_dist_sq);
                    sum_weights += weight;
                    sum_values += lpt.val * weight;
                }
            }

            if (sum_weights > 0) {
                int idx = (py * 256 + px) * 4;
                GetColorForValue(sum_values / sum_weights, type, result.pixels[idx], result.pixels[idx+1], result.pixels[idx+2], result.pixels[idx+3]);

                double d = std::sqrt(nearest_dist_sq);
                double alpha = 1.0 - (d / max_radius_m);
                result.pixels[idx+3] = (unsigned char)(210.0 * alpha * alpha);
                hasData = true;
            }
        }
    }

    if (hasData) {
        fs::create_directories(dirPath);
        stbi_write_png(filepath.c_str(), 256, 256, 4, result.pixels.data(), 256 * 4);
        result.valid = true;
    }
    return result;
}

GLuint HeatmapManager::CreateGLTexture(const HeatmapImage& img) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return id;
}