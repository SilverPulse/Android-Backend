cmake_minimum_required(VERSION 3.10)
project(AndroidMapVisualizer)

set(CMAKE_CXX_STANDARD 17)

find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)

include_directories(external/imgui)
include_directories(external/imgui/backends)
include_directories(external/glfw/include)

set(IMGUI_SOURCES
        external/imgui/imgui.cpp
        external/imgui/imgui_demo.cpp
        external/imgui/imgui_draw.cpp
        external/imgui/imgui_tables.cpp
        external/imgui/imgui_widgets.cpp
        external/imgui/backends/imgui_impl_glfw.cpp
        external/imgui/backends/imgui_impl_opengl3.cpp
)

add_subdirectory(external/glfw)
add_executable(MapApp main.cpp ${IMGUI_SOURCES})

target_link_libraries(MapApp
        glfw
        ${OPENGL_LIBRARIES}
        zmq
        Threads::Threads
        dl
)