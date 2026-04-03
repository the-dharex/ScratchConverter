#pragma once

#include "core/ProjectModel.hpp"
#include "core/SB3Parser.hpp"   // StatusCallback

#include <filesystem>
#include <string>

namespace sc {

struct CodeGenConfig {
    std::string windowTitle{"Scratch Game"};
    int         width{960};
    int         height{720};
    bool        fullscreen{false};
};

class CodeGenerator {
public:
    explicit CodeGenerator(StatusCallback status);

    /// Generate a complete SFML project from a parsed Scratch project.
    /// Writes files into outputDir.
    void Generate(const ScratchProject& project,
                  const CodeGenConfig& config,
                  const std::filesystem::path& outputDir,
                  const std::filesystem::path& runtimeDir);

private:
    // Code-generation helpers
    std::string GenerateMainCpp(const ScratchProject& project,
                                const CodeGenConfig& config);
    std::string GenerateSpritesHpp(const ScratchProject& project);
    std::string GenerateScriptsHpp(const ScratchProject& project);

    // Block → C++ expression
    std::string EmitExpression(const ScratchBlock& block,
                               const ScratchTarget& target,
                               const std::string& currentProc = "");
    std::string EmitInputExpr(const BlockInput& input,
                              const ScratchTarget& target,
                              const std::string& currentProc = "");

    // Block chain → state-machine cases
    struct StateEntry {
        int         stateId;
        std::string code;
        int         nextState;   // -1 = derived at emit time
        bool        yields;      // true = return true after this state
    };
    std::vector<StateEntry> EmitBlockChain(const std::string& startBlockId,
                                           const ScratchTarget& target,
                                           int& stateCounter,
                                           const std::string& currentProc = "",
                                           bool warp = false);
    std::vector<StateEntry> EmitBlock(const ScratchBlock& block,
                                      const ScratchTarget& target,
                                      int& stateCounter,
                                      const std::string& currentProc = "",
                                      bool warp = false);

    // Pre-generate procedure bodies for a target (enables recursion)
    struct ProcInfo {
        int startState;
        int endState;   // return (pop call stack) state
        bool warp;
    };
    std::unordered_map<std::string, ProcInfo> procStates_; // proccode → info

    // Repeat loop nesting depth → unique counter per nesting level
    int repeatDepth_ = 0;
    int maxRepeatDepth_ = 0;

    void WriteAssets(const ScratchProject& project,
                     const std::filesystem::path& assetsDir);

    // Track SVG output dimensions (for rotation center scaling)
    struct SvgOutputSize { int width; int height; };
    std::unordered_map<std::string, SvgOutputSize> svgScales_;

    StatusCallback status_;
};

} // namespace sc
