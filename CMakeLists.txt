cmake_minimum_required(VERSION 3.10)
project(AndroidMapVisualizer)

set(CMAKE_CXX_STANDARD 17)

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)

include_directories(external/imgui)
include_directories(external/imgui/backends)
include_directories(external/glfw/include)
include_directories(external/implot)

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

add_subdirectory(external/glfw)

add_executable(MapApp main.cpp ${IMGUI_SOURCES} ${IMPLOT_SOURCES})

target_link_libraries(MapApp
        glfw
        ${OPENGL_LIBRARIES}
        zmq
        Threads::Threads
        dl
)