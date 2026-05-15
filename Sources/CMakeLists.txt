cmake_minimum_required(VERSION 3.16)

project(JeriBot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(JeriBot
    src/main.cpp
    src/app.rc
)

target_compile_options(JeriBot PRIVATE /utf-8)

target_compile_definitions(JeriBot PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(JeriBot PRIVATE
    ole32
    oleaut32
    uiautomationcore
)

install(TARGETS JeriBot
    RUNTIME DESTINATION .
)
