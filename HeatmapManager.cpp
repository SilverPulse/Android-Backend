#include "HeatmapManager.h"
#include "MapMath.h"
#include "stb_image_write.h"
#include "stb_image.h"
#include <filesystem>
#include <cmath>
#include <iostream>

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

double HeatmapManager::GetDistanceMeters(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000.0;
    double dLat = (lat2 - lat1) * MapMath::RAD;
    double dLon = (lon2 - lon1) * MapMath::RAD;
    double a = std::sin(dLat/2) * std::sin(dLat/2) +
               std::cos(lat1 * MapMath::RAD) * std::cos(lat2 * MapMath::RAD) *
               std::sin(dLon/2) * std::sin(dLon/2);
    return R * 2 * std::atan2(std::sqrt(a), std::sqrt(1-a));
}

void HeatmapManager::GetColorForValue(double value, int type, unsigned char& r, unsigned char& g, unsigned char& b, unsigned char& a) {
    double minVal = -120.0, maxVal = -70.0; // По умолчанию (RSRP)

    if (type == 1)      { minVal = -20.0;  maxVal = -9.0;  }
    else if (type == 2) { minVal = -110.0; maxVal = -75.0; }
    else if (type == 3) { minVal = 50.0;   maxVal = 260.0; }

    if (value < minVal) value = minVal;
    if (value > maxVal) value = maxVal;

    double t = (value - minVal) / (maxVal - minVal);
    int colors[5][3] = { {0,0,255}, {0,255,255}, {0,255,0}, {255,255,0}, {255,0,0} };

    double scaled_t = t * 4.0;
    int idx = (int)scaled_t;
    if (idx >= 4) { r = colors[4][0]; g = colors[4][1]; b = colors[4][2]; return; }

    double frac = scaled_t - idx;
    r = (unsigned char)(colors[idx][0] + frac * (colors[idx+1][0] - colors[idx][0]));
    g = (unsigned char)(colors[idx][1] + frac * (colors[idx+1][1] - colors[idx][1]));
    b = (unsigned char)(colors[idx][2] + frac * (colors[idx+1][2] - colors[idx][2]));
}

GLuint HeatmapManager::GetHeatmapTile(int z, int x, int y, int type, AppState& state) {
    TileKey key = {z, x, y, type};

    if (_textures.find(key) != _textures.end()) return _textures[key];

    if (_pendingTasks.find(key) != _pendingTasks.end()) {
        if (_pendingTasks[key].wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            HeatmapImage img = _pendingTasks[key].get();
            _pendingTasks.erase(key);

            if (img.valid) {
                GLuint id = CreateGLTexture(img);
                _textures[key] = id;
                _textureQueue.push(key);
                if (_textures.size() > MAX_TEXTURES) {
                    TileKey old = _textureQueue.front(); _textureQueue.pop();
                    GLuint oldId = _textures[old];
                    glDeleteTextures(1, &oldId);
                    _textures.erase(old);
                }
                return id;
            }
        }
        return 0;
    }

    std::vector<GPSPoint> pointsCopy;
    {
        std::lock_guard<std::mutex> lock(state.points_mutex);
        pointsCopy = state.points;
    }

    if (!pointsCopy.empty()) {
        _pendingTasks[key] = std::async(std::launch::async, GenerateTileAsync, z, x, y, type, pointsCopy);
    }
    return 0;
}

HeatmapManager::HeatmapImage HeatmapManager::GenerateTileAsync(int z, int x, int y, int type, std::vector<GPSPoint> points) {
    HeatmapImage result;
    std::string dirPath = "heatmap/" + std::to_string(type) + "/" + std::to_string(z) + "/" + std::to_string(x);
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

    // --- МАГИЯ ЗУМА ---
    // 1. Считаем, сколько физических метров в ширине этого тайла
    double tile_width_m = (lon_max - lon_min) * lon_to_m;
    // 2. Узнаем, сколько метров в 1 пикселе (тайл всегда 256px)
    double meters_per_pixel = tile_width_m / 256.0;

    // 3. ДИНАМИЧЕСКИЙ РАДИУС:
    // Берем 40 метров. Но если 40 метров на этом зуме меньше 3.5 пикселей,
    // мы принудительно увеличиваем радиус, чтобы точку было видно!
    double max_radius_m = std::max(40.0, 3.5 * meters_per_pixel);
    double min_radius_m = std::max(10.0, 1.0 * meters_per_pixel);

    double max_dist_sq = max_radius_m * max_radius_m;
    double min_dist_sq = min_radius_m * min_radius_m;

    // 4. ДИНАМИЧЕСКИЙ ЗАХВАТ ТОЧЕК (Margin):
    // Раньше мы брали жестко +0.01 градуса вокруг. На Zoom 3 этого не хватит,
    // и огромные точки будут обрезаться по краям квадратов. Считаем отступ пропорционально радиусу!
    double margin_lat = (max_radius_m / lat_to_m) * 1.5;
    double margin_lon = (max_radius_m / lon_to_m) * 1.5;

    struct LocalPoint { double xm, ym, val; };
    std::vector<LocalPoint> localPoints;
    localPoints.reserve(1000);

    for (const auto& pt : points) {
        // Используем наши новые динамические отступы
        if (pt.lat >= lat_min - margin_lat && pt.lat <= lat_max + margin_lat &&
            pt.lon >= lon_min - margin_lon && pt.lon <= lon_max + margin_lon) {

            double val = 0;
            if (type == 0) val = pt.rsrp;
            else if (type == 1) val = pt.rsrq;
            else if (type == 2) val = pt.rssi;
            else {
                val = pt.altitude;
                if (val < 50.0 || val > 300.0) continue; // Защита от багов GPS
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
            double min_actual_dist_sq = max_dist_sq + 1.0;

            for (const auto& lpt : localPoints) {
                double dx = px_m - lpt.xm;
                double dy = py_m - lpt.ym;
                double dist_sq = dx*dx + dy*dy;

                // Сравниваем с динамическим максимальным квадратом
                if (dist_sq <= max_dist_sq) {
                    min_actual_dist_sq = std::min(min_actual_dist_sq, dist_sq);
                    // Ограничиваем динамическим минимумом
                    double d_sq = std::max(dist_sq, min_dist_sq);
                    double weight = 1.0 / d_sq;
                    sum_weights += weight;
                    sum_values += lpt.val * weight;
                }
            }

            if (sum_weights > 0) {
                int idx = (py * 256 + px) * 4;
                GetColorForValue(sum_values / sum_weights, type, result.pixels[idx], result.pixels[idx+1], result.pixels[idx+2], result.pixels[idx+3]);

                // Плавное затухание по краям, рассчитанное по новому динамическому радиусу
                double min_dist = std::sqrt(min_actual_dist_sq);
                double alpha_factor = 1.0 - (min_dist / max_radius_m);
                result.pixels[idx+3] = (unsigned char)(220.0 * alpha_factor * alpha_factor);

                hasData = true;
            }
        }
    }

    if (hasData) {
        fs::create_directories(dirPath);
        stbi_write_png(filepath.c_str(), 256, 256, 4, result.pixels.data(), 256 * 4);
        result.valid = true;
        printf("Saved heatmap tile: z=%d x=%d y=%d\n", z, x, y);
    }
    return result;
}

GLuint HeatmapManager::CreateGLTexture(const HeatmapImage& img) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
    return id;
}