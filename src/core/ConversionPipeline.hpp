#pragma once

#include "core/ConversionConfig.hpp"
#include "core/SB3Parser.hpp"  // StatusCallback

#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>

namespace sc {

class ConversionPipeline {
public:
    ConversionPipeline(ConversionConfig config,
                       std::filesystem::path runtimeDir,
                       StatusCallback status);

    /// Execute the full conversion pipeline. Can be cancelled via stop_token.
    void Run(std::stop_token stop);

private:
    ConversionConfig          config_;
    std::filesystem::path     runtimeDir_;
    StatusCallback            status_;
};

} // namespace sc
