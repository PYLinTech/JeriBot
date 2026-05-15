cmake_minimum_required(VERSION 3.16)

project(JeriVisionExplorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(JeriVisionExplorer WIN32
    src/main.cpp
    src/app.rc
)

target_compile_options(JeriVisionExplorer PRIVATE /utf-8)

target_compile_definitions(JeriVisionExplorer PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(JeriVisionExplorer PRIVATE
    comctl32
    user32
    gdi32
    msimg32
    shell32
)

install(TARGETS JeriVisionExplorer
    RUNTIME DESTINATION .
)
