#pragma once

#include <filesystem>
#include <string>

namespace sc {

struct ConversionConfig {
    std::filesystem::path sb3Path;
    std::filesystem::path exportPath;
    std::string           windowTitle{"Scratch Game"};
    int                   width{960};
    int                   height{720};
    bool                  fullscreen{false};

    enum class TargetOS { Windows, Linux };
    TargetOS targetOS{TargetOS::Windows};
};

} // namespace sc
