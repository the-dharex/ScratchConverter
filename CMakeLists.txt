cmake_minimum_required(VERSION 3.20...4.2)
project(ScratchConverter VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──────────────────────────────────────────────
find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG        3.0.2
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    nanosvg
    GIT_REPOSITORY https://github.com/memononen/nanosvg.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(json miniz nanosvg stb)

# ── Sources ───────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/app/App.cpp
    src/ui/MainFrame.cpp
    src/core/SB3Parser.cpp
    src/core/CodeGenerator.cpp
    src/core/Compiler.cpp
    src/core/ConversionPipeline.cpp
    src/core/SvgConverter.cpp
)

add_executable(ScratchConverter WIN32 ${SOURCES})

target_include_directories(ScratchConverter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${nanosvg_SOURCE_DIR}/src
    ${stb_SOURCE_DIR}
)

target_link_libraries(ScratchConverter PRIVATE
    ${wxWidgets_LIBRARIES}
    nlohmann_json::nlohmann_json
    miniz
)

# ── Copy runtime templates next to executable ─────────────────
add_custom_command(TARGET ScratchConverter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
        "$<TARGET_FILE_DIR:ScratchConverter>/runtime"
    COMMENT "Copying runtime templates..."
)

# ── Install ───────────────────────────────────────────────────
install(TARGETS ScratchConverter DESTINATION bin)
install(DIRECTORY runtime/ DESTINATION share/ScratchConverter/runtime)
