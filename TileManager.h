#pragma once
#include <GL/glew.h>
#include <tuple>
#include <map>
#include <future>
#include <vector>
#include <string>
#include <queue>

struct TileImage {
    int width = 0, height = 0, channels = 0;
    std::vector<unsigned char> pixels;
    bool valid = false;
};

class TileManager {
public:
    TileManager();
    ~TileManager();

    GLuint GetTileTexture(int z, int x, int y);

private:
    using TileKey = std::tuple<int, int, int>;

    std::map<TileKey, GLuint> _textures;

    std::queue<TileKey> _textureQueue;
    const size_t MAX_TEXTURES = 400;

    std::map<TileKey, std::future<TileImage>> _pendingDownloads;

    static TileImage LoadTileAsync(int z, int x, int y);
    static bool DownloadWithCurl(const std::string& url, std::vector<unsigned char>& buffer);
    static void SaveToDisk(const std::string& filepath, const std::vector<unsigned char>& buffer);
    static bool LoadFromDisk(const std::string& filepath, std::vector<unsigned char>& buffer);

    GLuint CreateGLTexture(const TileImage& img);
};