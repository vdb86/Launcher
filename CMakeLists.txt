cmake_minimum_required(VERSION 3.20)
project(Launcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static-link the MSVC runtime so launcher.exe runs on a clean Windows 10/11 machine WITHOUT the
# Visual C++ redistributable installed (portable xcopy deploy). Requires CMP0091 NEW (default >= 3.15).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# WIN32 -> uses wWinMain, no console window. app.rc embeds the application icon (launcher.ico);
# the Visual Studio / MSVC toolchain compiles the .rc automatically.
add_executable(launcher WIN32
    src/main.cpp
    src/app.rc
)

target_include_directories(launcher PRIVATE src)

# Libraries are also referenced via #pragma comment(lib, ...) in main.cpp,
# but list them here too for non-MSVC generators / clarity.
target_link_libraries(launcher PRIVATE
    d3d11
    dxgi
    d3dcompiler
    xinput9_1_0
)

if (MSVC)
    target_compile_options(launcher PRIVATE /W3 /permissive-)
endif()
