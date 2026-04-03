#include "core/ConversionPipeline.hpp"
#include "core/SB3Parser.hpp"
#include "core/CodeGenerator.hpp"
#include "core/Compiler.hpp"

#include <exception>
#include <format>

namespace sc {

ConversionPipeline::ConversionPipeline(ConversionConfig config,
                                       std::filesystem::path runtimeDir,
                                       StatusCallback status)
    : config_(std::move(config))
    , runtimeDir_(std::move(runtimeDir))
    , status_(std::move(status))
{}

void ConversionPipeline::Run(std::stop_token stop)
{
    try {
        // ── Step 1: Parse .sb3 ───────────────────────────────
        if (stop.stop_requested()) return;
        status_("Iniciando conversión...", 2);

        SB3Parser parser(status_);
        auto project = parser.Parse(config_.sb3Path);

        status_(std::format("Proyecto: {} targets, {} assets",
                            project.targets.size(),
                            project.assets.size()), 30);

        // ── Step 2: Generate code ────────────────────────────
        if (stop.stop_requested()) return;

        CodeGenConfig genCfg;
        genCfg.windowTitle = config_.windowTitle;
        genCfg.width       = config_.width;
        genCfg.height      = config_.height;
        genCfg.fullscreen  = config_.fullscreen;

        CodeGenerator generator(status_);
        generator.Generate(project, genCfg, config_.exportPath, runtimeDir_);

        // ── Step 3: Compile ──────────────────────────────────
        if (stop.stop_requested()) return;

        Compiler compiler(status_);
        compiler.Build(config_.exportPath, config_.targetOS);

        status_("[OK] Conversión completada exitosamente.", 100);

    } catch (const std::exception& ex) {
        status_(std::format("[ERROR] {}", ex.what()), -1);
    }
}

} // namespace sc
