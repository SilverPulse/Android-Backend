#include "TileManager.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>


namespace fs = std::filesystem;

static size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* mem = static_cast<std::vector<unsigned char>*>(userp);
    mem->insert(mem->end(), static_cast<unsigned char*>(contents), static_cast<unsigned char*>(contents) + realsize);
    return realsize;
}

TileManager::TileManager() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

TileManager::~TileManager() {
    for (auto const& [key, id] : _textures) {
        glDeleteTextures(1, &id);
    }
    curl_global_cleanup();
}

GLuint TileManager::GetTileTexture(int z, int x, int y) {
    TileKey key = {z, x, y};

    if (_textures.find(key) != _textures.end()) {
        return _textures[key];
    }

    if (_pendingDownloads.find(key) != _pendingDownloads.end()) {
        if (_pendingDownloads[key].wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            TileImage img = _pendingDownloads[key].get();
            _pendingDownloads.erase(key);

            if (img.valid) {
                GLuint id = CreateGLTexture(img);
                _textures[key] = id;

                _textureQueue.push(key);

                if (_textures.size() > MAX_TEXTURES) {
                    TileKey oldestKey = _textureQueue.front();
                    _textureQueue.pop();

                    GLuint oldId = _textures[oldestKey];
                    glDeleteTextures(1, &oldId);

                    _textures.erase(oldestKey);
                }

                return id;
            } else {
                _textures[key] = 0;
                return 0;
            }
        }
        return 0;
    }

    _pendingDownloads[key] = std::async(std::launch::async, LoadTileAsync, z, x, y);
    return 0;
}

TileImage TileManager::LoadTileAsync(int z, int x, int y) {
    TileImage result;
    std::vector<unsigned char> fileBuffer;

    std::string dirPath = "Tiles/" + std::to_string(z) + "/" + std::to_string(x);
    std::string filepath = dirPath + "/" + std::to_string(y) + ".png";

    if (!LoadFromDisk(filepath, fileBuffer)) {
        std::ostringstream url;
        url << "https://a.tile.openstreetmap.org/" << z << "/" << x << "/" << y << ".png";

        if (DownloadWithCurl(url.str(), fileBuffer)) {
            fs::create_directories(dirPath);
            SaveToDisk(filepath, fileBuffer);
        } else {
            return result;
        }
    }

    unsigned char* data = stbi_load_from_memory(fileBuffer.data(), fileBuffer.size(),
                                                &result.width, &result.height, &result.channels, STBI_rgb_alpha);
    if (data) {
        result.pixels.assign(data, data + (result.width * result.height * 4));
        stbi_image_free(data);
        result.valid = true;
    }
    return result;
}

bool TileManager::DownloadWithCurl(const std::string& url, std::vector<unsigned char>& buffer) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "CyberGPS-Monitor/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK);
}

void TileManager::SaveToDisk(const std::string& filepath, const std::vector<unsigned char>& buffer) {
    std::ofstream file(filepath, std::ios::binary);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    }
}

bool TileManager::LoadFromDisk(const std::string& filepath, std::vector<unsigned char>& buffer) {
    // Флаг std::ios::ate (At The End) ставит "курсор" чтения в самый конец файла.
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    //tellg выдаёт точный размер файла в байтах
    std::streamsize size = file.tellg();

    file.seekg(0, std::ios::beg); // Возвращаем курсор в начало файла (std::ios::beg - beginning)
    buffer.resize(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) return true;
    return false;
}

GLuint TileManager::CreateGLTexture(const TileImage& img) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
    return id;
}