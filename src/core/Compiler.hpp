#pragma once

#include "core/ConversionConfig.hpp"
#include "core/SB3Parser.hpp"  // StatusCallback

#include <filesystem>
#include <string>

namespace sc {

class Compiler {
public:
    explicit Compiler(StatusCallback status);

    /// Configure and build the generated SFML project.
    /// Uses CMake + system compiler.
    void Build(const std::filesystem::path& projectDir,
               ConversionConfig::TargetOS targetOS);

private:
    struct ProcessResult {
        std::string output;
        int         exitCode = -1;
    };

    ProcessResult RunProcess(const std::string& command,
                             const std::filesystem::path& workDir);
    StatusCallback status_;
};

} // namespace sc
