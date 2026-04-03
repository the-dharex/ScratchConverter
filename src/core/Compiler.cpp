#include "core/Compiler.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace sc {

// Return the last N lines of a string (for trimming huge build logs)
static std::string TailLines(const std::string& text, int n = 25)
{
    std::vector<std::string> lines;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line))
        lines.push_back(line);

    if (static_cast<int>(lines.size()) <= n)
        return text;

    std::string out = "... (salida truncada, mostrando últimas " + std::to_string(n) + " líneas) ...\n";
    for (int i = static_cast<int>(lines.size()) - n; i < static_cast<int>(lines.size()); ++i)
        out += lines[i] + "\n";
    return out;
}

Compiler::Compiler(StatusCallback status)
    : status_(std::move(status))
{}

void Compiler::Build(const fs::path& projectDir,
                     ConversionConfig::TargetOS targetOS)
{
    auto buildDir = projectDir / "build";
    fs::create_directories(buildDir);

    // ── CMake configure ──────────────────────────────────────
    status_("Ejecutando CMake configure...", 75);

    std::string generator;
    switch (targetOS) {
        case ConversionConfig::TargetOS::Windows:
            generator = "-G \"Visual Studio 18 2026\"";
            break;
        case ConversionConfig::TargetOS::Linux:
            generator = "-G \"Unix Makefiles\"";
            break;
    }

    std::string configCmd = std::format(
        "cmake {} -DCMAKE_POLICY_VERSION_MINIMUM=3.5"
        " -S \"{}\" -B \"{}\"",
        generator, projectDir.string(), buildDir.string());

    auto [configOutput, configExit] = RunProcess(configCmd, projectDir);

    if (configExit != 0) {
        throw std::runtime_error(
            "CMake configure falló (código " + std::to_string(configExit) + "):\n"
            + TailLines(configOutput));
    }

    // ── CMake build ──────────────────────────────────────────
    status_("Compilando proyecto SFML...", 82);

    std::string buildCmd = std::format(
        "cmake --build \"{}\" --config Release --parallel",
        buildDir.string());

    auto [buildOutput, buildExit] = RunProcess(buildCmd, projectDir);

    if (buildExit != 0) {
        throw std::runtime_error(
            "Compilación falló (código " + std::to_string(buildExit) + "):\n"
            + TailLines(buildOutput));
    }

    // ── Locate the executable ────────────────────────────────
    status_("Buscando ejecutable...", 93);

    // Find the project name from CMakeLists.txt
    std::string exeName;
    {
        auto cmakePath = projectDir / "CMakeLists.txt";
        if (fs::exists(cmakePath)) {
            std::ifstream in(cmakePath);
            std::string line;
            while (std::getline(in, line)) {
                auto pos = line.find("project(");
                if (pos != std::string::npos) {
                    auto start = pos + 8;
                    auto end = line.find_first_of(" )", start);
                    if (end != std::string::npos)
                        exeName = line.substr(start, end - start);
                    break;
                }
            }
        }
    }

#ifdef _WIN32
    std::string exeFile = exeName + ".exe";
#else
    std::string exeFile = exeName;
#endif

    // Look in common output locations
    fs::path exePath;
    std::vector<fs::path> searchPaths = {
        buildDir / "Release" / exeFile,           // Visual Studio multi-config
        buildDir / exeFile,                        // Makefiles / Ninja
        buildDir / "Debug" / exeFile,
        buildDir / "RelWithDebInfo" / exeFile,
    };

    for (auto& p : searchPaths) {
        if (fs::exists(p)) {
            exePath = p;
            break;
        }
    }

    if (exePath.empty() || !fs::exists(exePath)) {
        // Recursive search as a fallback
        for (auto& entry : fs::recursive_directory_iterator(buildDir)) {
            if (entry.is_regular_file() && entry.path().filename().string() == exeFile) {
                exePath = entry.path();
                break;
            }
        }
    }

    if (exePath.empty() || !fs::exists(exePath)) {
        throw std::runtime_error(
            "No se encontró el ejecutable: " + exeFile
            + "\nÚltimas líneas del build:\n" + TailLines(buildOutput, 15));
    }

    status_(std::format("[EXEC] {}", exePath.string()), 95);
    status_("Compilación completada.", 95);
}

Compiler::ProcessResult Compiler::RunProcess(const std::string& command,
                                             const fs::path& workDir)
{
    auto oldCwd = fs::current_path();
    if (fs::exists(workDir))
        fs::current_path(workDir);

    std::string result;
    int exitCode = -1;

    std::string fullCmd = command + " 2>&1";

#ifdef _WIN32
    FILE* raw = _popen(fullCmd.c_str(), "r");
#else
    FILE* raw = popen(fullCmd.c_str(), "r");
#endif

    if (!raw) {
        fs::current_path(oldCwd);
        throw std::runtime_error("No se pudo ejecutar: " + command);
    }

    std::array<char, 256> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), raw))
        result += buffer.data();

#ifdef _WIN32
    exitCode = _pclose(raw);
#else
    exitCode = pclose(raw);
#endif

    fs::current_path(oldCwd);
    return { result, exitCode };
}

} // namespace sc
