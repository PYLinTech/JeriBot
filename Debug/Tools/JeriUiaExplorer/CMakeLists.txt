cmake_minimum_required(VERSION 3.16)

project(JeriUiaExplorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(JeriUiaExplorer WIN32
    src/main.cpp
    src/app.rc
)

target_compile_options(JeriUiaExplorer PRIVATE /utf-8)

target_compile_definitions(JeriUiaExplorer PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(JeriUiaExplorer PRIVATE
    ole32
    oleaut32
    uiautomationcore
    comctl32
    comdlg32
    user32
    gdi32
    shell32
)

install(TARGETS JeriUiaExplorer
    RUNTIME DESTINATION .
)
