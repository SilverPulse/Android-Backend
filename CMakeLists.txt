cmake_minimum_required(VERSION 3.10)
project(AndroidMapVisualizer)

set(CMAKE_CXX_STANDARD 20)

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(GLEW REQUIRED)
find_package(CURL REQUIRED)

include_directories(
        external/imgui
        external/imgui/backends
        external/glfw/include
        external/implot
        /usr/include/stb
        ${PostgreSQL_INCLUDE_DIRS}
        ${GLEW_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
        .
)

set(IMGUI_SOURCES
        external/imgui/imgui.cpp
        external/imgui/imgui_demo.cpp
        external/imgui/imgui_draw.cpp
        external/imgui/imgui_tables.cpp
        external/imgui/imgui_widgets.cpp
        external/imgui/backends/imgui_impl_glfw.cpp
        external/imgui/backends/imgui_impl_opengl3.cpp
)

set(IMPLOT_SOURCES
        external/implot/implot.cpp
        external/implot/implot_items.cpp
        external/implot/implot_demo.cpp
)

set(APP_SOURCES
        Database.cpp
        Network.cpp
        Gui.cpp
        TileManager.cpp
        main.cpp
)

add_subdirectory(external/glfw)
add_executable(MapApp ${APP_SOURCES} ${IMGUI_SOURCES} ${IMPLOT_SOURCES})

target_link_libraries(MapApp
        glfw
        ${OPENGL_LIBRARIES}
        ${GLEW_LIBRARIES}
        ${CURL_LIBRARIES}
        ${PostgreSQL_LIBRARIES}
        zmq
        Threads::Threads
)

if(NOT WIN32)
    target_link_libraries(MapApp dl)
endif()