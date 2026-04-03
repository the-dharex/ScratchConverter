#pragma once

#include "core/ProjectModel.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace sc {

using StatusCallback = std::function<void(const std::string&, int)>;

class SB3Parser {
public:
    explicit SB3Parser(StatusCallback status);

    /// Parse an .sb3 file and return the full project model.
    ScratchProject Parse(const std::filesystem::path& sb3Path);

private:
    StatusCallback status_;
};

} // namespace sc
