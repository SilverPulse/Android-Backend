#pragma once
#include "AppState.h"
#include <GL/glew.h>
#include <tuple>
#include <map>
#include <future>
#include <vector>
#include <string>
#include <queue>

class HeatmapManager {
public:
    HeatmapManager();
    ~HeatmapManager();

    GLuint GetHeatmapTile(int z, int x, int y, int type, AppState& state);

    void ClearCache();

private:
    using TileKey = std::tuple<int, int, int, int, std::string>;

    std::map<TileKey, GLuint> _textures;
    std::queue<TileKey> _textureQueue;
    const size_t MAX_TEXTURES = 200;

    struct HeatmapImage {
        int width = 256, height = 256;
        std::vector<unsigned char> pixels;
        bool valid = false;
    };

    std::map<TileKey, std::future<HeatmapImage>> _pendingTasks;

    static HeatmapImage GenerateTileAsync(int z, int x, int y, int type, std::string pci_mode, std::vector<GPSPoint> points);
    static double GetDistanceMeters(double lat1, double lon1, double lat2, double lon2);
    static void GetColorForValue(double value, int type, unsigned char& r, unsigned char& g, unsigned char& b, unsigned char& a);

    GLuint CreateGLTexture(const HeatmapImage& img);
};