#pragma once
#include "AppState.h"
#include "Database.h"
#include "TileManager.h"
#include <GLFW/glfw3.h>

class Gui {
public:
    Gui();
    ~Gui();

    bool Init(int width, int height, const char* title);
    void RunLoop(AppState& state, Database& db);

private:
    void SetupNeonTheme();
    void RenderUI(AppState& state, Database& db);

    GLFWwindow* window = nullptr;
    float scale = 150000.0f;

    TileManager mapTiles;
};