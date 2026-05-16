cmake_minimum_required(VERSION 3.16)

project(JeriBot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE JERIBOT_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.rc"
)

add_executable(JeriBot WIN32
    ${JERIBOT_SOURCES}
)

target_include_directories(JeriBot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_options(JeriBot PRIVATE /utf-8)

target_compile_definitions(JeriBot PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(JeriBot PRIVATE
    user32
)

install(TARGETS JeriBot
    RUNTIME DESTINATION .
)
