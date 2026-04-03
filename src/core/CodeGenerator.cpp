#include "core/CodeGenerator.hpp"
#include "core/SvgConverter.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace sc {

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────
namespace {

std::string SanitizeName(std::string_view name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)))
            out += c;
        else
            out += '_';
    }
    if (!out.empty() && std::isdigit(static_cast<unsigned char>(out[0])))
        out.insert(out.begin(), '_');
    return out;
}

std::string LiteralToString(const ScratchLiteral& lit)
{
    if (auto* d = std::get_if<double>(&lit))
        return std::format("{}", *d);
    return std::format("\"{}\"", std::get<std::string>(lit));
}

std::string LiteralToDouble(const ScratchLiteral& lit)
{
    if (auto* d = std::get_if<double>(&lit))
        return std::format("{}", *d);
    return std::format("scratch::toNum(\"{}\")", std::get<std::string>(lit));
}

std::string EscapeStr(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

// ── IMA ADPCM → PCM WAV conversion ──────────────────────────
// Decodes IMA ADPCM (WAV format tag 17) into 16-bit PCM (tag 1).
// Returns empty vector on failure.
std::vector<uint8_t> ConvertAdpcmToPcm(const std::vector<uint8_t>& wav)
{
    // Minimal RIFF WAV parser
    auto rd16 = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>(wav[off]) |
               (static_cast<uint16_t>(wav[off + 1]) << 8);
    };
    auto rd32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(wav[off])       |
               (static_cast<uint32_t>(wav[off + 1]) << 8)  |
               (static_cast<uint32_t>(wav[off + 2]) << 16) |
               (static_cast<uint32_t>(wav[off + 3]) << 24);
    };

    if (wav.size() < 44) return {};
    if (wav[0] != 'R' || wav[1] != 'I' || wav[2] != 'F' || wav[3] != 'F') return {};
    if (wav[8] != 'W' || wav[9] != 'A' || wav[10] != 'V' || wav[11] != 'E') return {};

    // Walk chunks to find fmt and data
    size_t pos = 12;
    uint16_t channels = 0, bitsPerSample = 0, blockAlign = 0, samplesPerBlock = 0;
    uint32_t sampleRate = 0;
    const uint8_t* adpcmData = nullptr;
    uint32_t adpcmDataSize = 0;

    while (pos + 8 <= wav.size()) {
        std::string chunkId(reinterpret_cast<const char*>(&wav[pos]), 4);
        uint32_t chunkSize = rd32(pos + 4);
        size_t chunkStart = pos + 8;

        if (chunkId == "fmt ") {
            if (chunkStart + 20 > wav.size()) return {};
            uint16_t fmt = rd16(chunkStart);
            if (fmt != 17) return {}; // not IMA ADPCM
            channels      = rd16(chunkStart + 2);
            sampleRate    = rd32(chunkStart + 4);
            blockAlign    = rd16(chunkStart + 12);
            bitsPerSample = rd16(chunkStart + 14);
            // Extra data: samplesPerBlock
            if (chunkStart + 20 <= wav.size())
                samplesPerBlock = rd16(chunkStart + 18);
        } else if (chunkId == "data") {
            adpcmData = &wav[chunkStart];
            adpcmDataSize = chunkSize;
        } else if (chunkId == "fact") {
            // skip
        }

        pos = chunkStart + chunkSize;
        if (pos & 1) pos++; // chunks are word-aligned
    }

    if (!adpcmData || channels == 0 || blockAlign == 0) return {};
    if (channels > 2) return {};

    // IMA step table & index table
    static const int16_t stepTable[89] = {
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
        34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
        143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
        494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
        1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
        4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
        11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
        27086, 29794, 32767
    };
    static const int indexTable[16] = {
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    };

    auto clampIndex = [](int idx) -> int {
        return std::max(0, std::min(88, idx));
    };
    auto clampSample = [](int s) -> int16_t {
        return static_cast<int16_t>(std::max(-32768, std::min(32767, s)));
    };

    // Decode all blocks
    std::vector<int16_t> pcmSamples;

    // Estimate total samples for reservation
    if (samplesPerBlock == 0)
        samplesPerBlock = static_cast<uint16_t>(
            (blockAlign - 4 * channels) * 2 / channels + 1);

    uint32_t numBlocks = adpcmDataSize / blockAlign;
    pcmSamples.reserve(static_cast<size_t>(numBlocks) * samplesPerBlock * channels);

    for (uint32_t b = 0; b < numBlocks; b++) {
        const uint8_t* block = adpcmData + static_cast<size_t>(b) * blockAlign;
        size_t blockEnd = static_cast<size_t>(b) * blockAlign + blockAlign;
        if (blockEnd > adpcmDataSize) break;

        // Per-channel state: read preamble (4 bytes per channel)
        struct ChState { int predictor; int stepIndex; };
        std::vector<ChState> chState(channels);

        size_t offset = 0;
        for (uint16_t ch = 0; ch < channels; ch++) {
            int16_t pred = static_cast<int16_t>(
                static_cast<uint16_t>(block[offset]) |
                (static_cast<uint16_t>(block[offset + 1]) << 8));
            int idx = block[offset + 2];
            // block[offset + 3] is reserved
            chState[ch] = {pred, clampIndex(idx)};
            offset += 4;
        }

        // First sample(s): the predictor values
        if (channels == 1) {
            pcmSamples.push_back(clampSample(chState[0].predictor));
        } else {
            pcmSamples.push_back(clampSample(chState[0].predictor));
            pcmSamples.push_back(clampSample(chState[1].predictor));
        }

        // Decode nibbles
        uint32_t samplesDecoded = 1; // already got the predictor
        while (offset < blockAlign && samplesDecoded < samplesPerBlock) {
            for (uint16_t ch = 0; ch < channels; ch++) {
                // For mono: nibbles are sequential
                // For stereo: 4 bytes (8 nibbles) per channel, interleaved in 8-byte groups
                size_t nibblesToDecode = (channels == 1) ? 2 : 8;
                size_t bytesToRead = nibblesToDecode / 2;

                for (size_t i = 0; i < bytesToRead && offset < blockAlign; i++) {
                    uint8_t byte = block[offset++];

                    for (int nib = 0; nib < 2; nib++) {
                        uint8_t nibble = (nib == 0) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);

                        int step = stepTable[chState[ch].stepIndex];
                        int diff = step >> 3;
                        if (nibble & 1) diff += step >> 2;
                        if (nibble & 2) diff += step >> 1;
                        if (nibble & 4) diff += step;
                        if (nibble & 8) diff = -diff;

                        chState[ch].predictor += diff;
                        chState[ch].stepIndex = clampIndex(
                            chState[ch].stepIndex + indexTable[nibble]);

                        pcmSamples.push_back(clampSample(chState[ch].predictor));
                        samplesDecoded++;
                    }
                }
            }
        }
    }

    // Build PCM WAV
    uint32_t pcmDataSize = static_cast<uint32_t>(pcmSamples.size() * 2);
    uint32_t fileSize = 36 + pcmDataSize;
    std::vector<uint8_t> out(8 + fileSize);

    auto wr16 = [&](size_t off, uint16_t v) {
        out[off]     = static_cast<uint8_t>(v & 0xFF);
        out[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    };
    auto wr32 = [&](size_t off, uint32_t v) {
        out[off]     = static_cast<uint8_t>(v & 0xFF);
        out[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        out[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        out[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    };

    // RIFF header
    out[0]='R'; out[1]='I'; out[2]='F'; out[3]='F';
    wr32(4, fileSize);
    out[8]='W'; out[9]='A'; out[10]='V'; out[11]='E';

    // fmt chunk
    out[12]='f'; out[13]='m'; out[14]='t'; out[15]=' ';
    wr32(16, 16);           // chunk size
    wr16(20, 1);            // PCM format
    wr16(22, channels);
    wr32(24, sampleRate);
    uint16_t pcmBlockAlign = channels * 2;
    wr32(28, sampleRate * pcmBlockAlign); // byte rate
    wr16(32, pcmBlockAlign);
    wr16(34, 16);           // bits per sample

    // data chunk
    out[36]='d'; out[37]='a'; out[38]='t'; out[39]='a';
    wr32(40, pcmDataSize);

    // Copy PCM samples (already little-endian on x86)
    std::memcpy(&out[44], pcmSamples.data(), pcmDataSize);

    return out;
}

void WriteFile(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("No se pudo escribir: " + path.string());
    out << content;
}

/// Find a procedure definition block by proccode in a target
const ScratchBlock* FindProcedureDefinition(const std::string& proccode,
                                            const ScratchTarget& target)
{
    for (auto& [id, block] : target.blocks) {
        if (block.opcode == "procedures_definition") {
            // The custom_block input points to a procedures_prototype
            auto custIt = block.inputs.find("custom_block");
            if (custIt != block.inputs.end() && custIt->second.isBlock) {
                auto protoIt = target.blocks.find(custIt->second.blockId);
                if (protoIt != target.blocks.end() &&
                    protoIt->second.proccode == proccode) {
                    return &block;
                }
            }
        }
    }
    return nullptr;
}

/// Produce a scoped key for a variable — stage variables are global,
/// sprite-local variables are prefixed with "targetName::".
std::string ScopedVarName(const std::string& varName,
                          const std::string& varId,
                          const ScratchTarget& target)
{
    if (!target.isStage && target.variables.count(varId))
        return target.name + "::" + varName;
    return varName;
}

bool IsLocalVar(const std::string& varId, const ScratchTarget& target) {
    return !target.isStage && target.variables.count(varId);
}

bool IsLocalList(const std::string& listId, const ScratchTarget& target) {
    return !target.isStage && target.lists.count(listId);
}

/// Same for lists.
std::string ScopedListName(const std::string& listName,
                           const std::string& listId,
                           const ScratchTarget& target)
{
    if (!target.isStage && target.lists.count(listId))
        return target.name + "::" + listName;
    return listName;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────
CodeGenerator::CodeGenerator(StatusCallback status)
    : status_(std::move(status))
{}

// ─────────────────────────────────────────────────────────────
// Public Generate
// ─────────────────────────────────────────────────────────────
void CodeGenerator::Generate(const ScratchProject& project,
                             const CodeGenConfig& config,
                             const fs::path& outputDir,
                             const fs::path& runtimeDir)
{
    status_("Generando código SFML...", 35);

    fs::create_directories(outputDir / "src");
    fs::create_directories(outputDir / "assets" / "costumes");
    fs::create_directories(outputDir / "assets" / "sounds");

    // Copy runtime header files
    status_("Copiando runtime engine...", 38);
    for (auto& entry : fs::directory_iterator(runtimeDir)) {
        if (entry.is_regular_file() &&
            (entry.path().extension() == ".hpp" ||
             entry.path().extension() == ".h" ||
             entry.path().extension() == ".cpp")) {
            auto dest = outputDir / "src" / entry.path().filename();
            fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
        }
    }

    // Generate source files
    status_("Generando main.cpp...", 42);
    WriteFile(outputDir / "src" / "main.cpp",
              GenerateMainCpp(project, config));

    status_("Generando sprites...", 48);
    WriteFile(outputDir / "src" / "Sprites.hpp",
              GenerateSpritesHpp(project));

    status_("Generando scripts...", 55);
    WriteFile(outputDir / "src" / "Scripts.hpp",
              GenerateScriptsHpp(project));

    // Write assets
    status_("Escribiendo assets...", 62);
    WriteAssets(project, outputDir / "assets");

    // Generate CMakeLists.txt
    status_("Generando CMakeLists.txt del proyecto...", 68);
    auto cmakeTemplate = runtimeDir / "CMakeLists.txt.in";
    if (fs::exists(cmakeTemplate)) {
        std::ifstream in(cmakeTemplate);
        std::string tpl((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        auto replace = [&](const std::string& key, const std::string& val) {
            std::string::size_type pos;
            while ((pos = tpl.find(key)) != std::string::npos)
                tpl.replace(pos, key.size(), val);
        };
        replace("@PROJECT_NAME@", SanitizeName(config.windowTitle));
        WriteFile(outputDir / "CMakeLists.txt", tpl);
    }

    status_("Código generado correctamente.", 70);
}

// ─────────────────────────────────────────────────────────────
// main.cpp generation
// ─────────────────────────────────────────────────────────────
std::string CodeGenerator::GenerateMainCpp(const ScratchProject& project,
                                           const CodeGenConfig& config)
{
    std::ostringstream o;
    o << R"(// Auto-generated by ScratchConverter
#include "ScratchRuntime.hpp"
#include "Sprites.hpp"
#include "Scripts.hpp"

int main()
{
    scratch::RuntimeConfig cfg;
    cfg.title      = ")" << config.windowTitle << R"(";
    cfg.width      = )" << config.width << R"(;
    cfg.height     = )" << config.height << R"(;
    cfg.fullscreen = )" << (config.fullscreen ? "true" : "false") << R"(;

    scratch::Runtime runtime(cfg);

)";

    // Register sprites & stage
    // Helper: get the costume filename (SVGs are converted to .png)
    auto costumeFile = [](const ScratchCostume& c) -> std::string {
        if (c.dataFormat == "svg") {
            std::string f = c.md5ext;
            auto dot = f.rfind('.');
            if (dot != std::string::npos) f = f.substr(0, dot) + ".png";
            return f;
        }
        return c.md5ext;
    };
    // SVGs are rasterized at 2x scale, so rotation centers must be scaled
    auto rotCenterX = [](const ScratchCostume& c) -> double {
        return c.dataFormat == "svg" ? c.rotationCenterX * 2.0 : c.rotationCenterX;
    };
    auto rotCenterY = [](const ScratchCostume& c) -> double {
        return c.dataFormat == "svg" ? c.rotationCenterY * 2.0 : c.rotationCenterY;
    };
    // SVGs rasterized at 2x need bitmapResolution=2 to render at correct logical size
    auto effectiveBitmapRes = [](const ScratchCostume& c) -> int {
        return c.dataFormat == "svg" ? 2 : c.bitmapResolution;
    };

    for (auto& target : project.targets) {
        std::string safeName = SanitizeName(target.name);
        if (target.isStage) {
            o << "    // Stage\n";
            o << "    auto& stage = runtime.stage();\n";
            for (auto& c : target.costumes) {
                if (c.dataFormat == "png" || c.dataFormat == "jpg" || c.dataFormat == "svg") {
                    o << std::format("    stage.addCostume(\"{}\", \"assets/costumes/{}\", {}, {}, {});\n",
                                     c.name, costumeFile(c), rotCenterX(c), rotCenterY(c), effectiveBitmapRes(c));
                }
            }
            if (!target.costumes.empty())
                o << std::format("    stage.setCostume({});\n", target.currentCostume);
        } else {
            o << std::format("    // Sprite: {}\n", target.name);
            o << std::format("    auto& {} = runtime.addSprite(\"{}\");\n",
                             safeName, target.name);
            o << std::format("    {}.setPosition({}, {});\n",
                             safeName, target.x, target.y);
            o << std::format("    {}.setDirection({});\n", safeName, target.direction);
            o << std::format("    {}.setSize({});\n", safeName, target.size);
            o << std::format("    {}.setVisible({});\n",
                             safeName, target.visible ? "true" : "false");
            o << std::format("    {}.setLayerOrder({});\n", safeName, target.layerOrder);
            if (target.rotationStyle != "all around")
                o << std::format("    {}.setRotationStyle(\"{}\");\n", safeName, target.rotationStyle);

            for (auto& c : target.costumes) {
                if (c.dataFormat == "png" || c.dataFormat == "jpg" || c.dataFormat == "svg") {
                    o << std::format("    {}.addCostume(\"{}\", \"assets/costumes/{}\", {}, {}, {});\n",
                                     safeName, c.name, costumeFile(c),
                                     rotCenterX(c), rotCenterY(c), effectiveBitmapRes(c));
                }
            }
            if (!target.costumes.empty())
                o << std::format("    {}.setCostume({});\n", safeName, target.currentCostume);
        }

        // Register sounds (map display name → hash filename)
        for (auto& sound : target.sounds) {
            o << std::format("    runtime.registerSound(\"{}\", \"assets/sounds/{}\");\n",
                             EscapeStr(sound.name), sound.md5ext);
        }

        // Initialize variables (scoped: sprite-local vars live on the sprite)
        for (auto& [vid, var] : target.variables) {
            bool local = IsLocalVar(vid, target);
            if (local) {
                std::string sprObj = target.isStage ? "stage" : safeName;
                if (auto* d = std::get_if<double>(&var.value))
                    o << std::format("    {}.setLocalVarStr(\"{}\", \"{}\");\n",
                                     sprObj, EscapeStr(var.name), *d);
                else
                    o << std::format("    {}.setLocalVarStr(\"{}\", \"{}\");\n",
                                     sprObj, EscapeStr(var.name), EscapeStr(std::get<std::string>(var.value)));
            } else {
                std::string scoped = ScopedVarName(var.name, vid, target);
                if (auto* d = std::get_if<double>(&var.value))
                    o << std::format("    runtime.setVarStr(\"{}\", \"{}\");\n",
                                     EscapeStr(scoped), *d);
                else
                    o << std::format("    runtime.setVarStr(\"{}\", \"{}\");\n",
                                     EscapeStr(scoped), EscapeStr(std::get<std::string>(var.value)));
            }
        }

        // Initialize lists (scoped: sprite-local lists live on the sprite)
        for (auto& [lid, lst] : target.lists) {
            bool local = IsLocalList(lid, target);
            o << std::format("    // List: {}\n", lst.name);
            if (local) {
                std::string sprObj = target.isStage ? "stage" : safeName;
                for (auto& item : lst.items) {
                    if (auto* d = std::get_if<double>(&item))
                        o << std::format("    {}.getLocalList(\"{}\").add({});\n",
                                         sprObj, EscapeStr(lst.name), *d);
                    else
                        o << std::format("    {}.getLocalList(\"{}\").addStr(\"{}\");\n",
                                         sprObj, EscapeStr(lst.name), EscapeStr(std::get<std::string>(item)));
                }
            } else {
                std::string scopedName = ScopedListName(lst.name, lid, target);
                for (auto& item : lst.items) {
                    if (auto* d = std::get_if<double>(&item))
                        o << std::format("    runtime.addToList(\"{}\", {});\n", EscapeStr(scopedName), *d);
                    else
                        o << std::format("    runtime.getList(\"{}\").addStr(\"{}\");\n",
                                         EscapeStr(scopedName), EscapeStr(std::get<std::string>(item)));
                }
            }
        }

        o << "\n";
    }

    // Register scripts
    o << "    // Register scripts\n";
    auto emitTargetScripts = [&](const ScratchTarget& target) {
        std::string safeName = SanitizeName(target.name);
        std::string sprRef = target.isStage ? "stage" : safeName;
        int scriptIdx = 0;

        for (auto& [blockId, block] : target.blocks) {
            if (!block.topLevel) continue;
            if (!block.opcode.starts_with("event_") &&
                block.opcode != "control_start_as_clone")
                continue;

            std::string scriptClass = std::format("Script_{}_{}", safeName, scriptIdx);

            if (block.opcode == "event_whenflagclicked") {
                o << std::format(
                    "    runtime.onGreenFlag(std::make_unique<scripts::{}>("
                    "runtime, {}));\n", scriptClass, sprRef);
            }
            else if (block.opcode == "event_whenkeypressed") {
                auto keyIt = block.fields.find("KEY_OPTION");
                std::string key = keyIt != block.fields.end() ? keyIt->second.value : "space";
                o << std::format(
                    "    runtime.onKeyPressed(\"{}\", "
                    "std::make_unique<scripts::{}>(runtime, {}));\n",
                    key, scriptClass, sprRef);
            }
            else if (block.opcode == "event_whenthisspriteclicked") {
                o << std::format(
                    "    runtime.onSpriteClicked(\"{}\", "
                    "std::make_unique<scripts::{}>(runtime, {}));\n",
                    target.name, scriptClass, sprRef);
            }
            else if (block.opcode == "event_whenbroadcastreceived") {
                auto msgIt = block.fields.find("BROADCAST_OPTION");
                std::string msg = msgIt != block.fields.end() ? msgIt->second.value : "";
                o << std::format(
                    "    runtime.onBroadcast(\"{}\", "
                    "std::make_unique<scripts::{}>(runtime, {}));\n",
                    EscapeStr(msg), scriptClass, sprRef);
            }
            else if (block.opcode == "event_whenbackdropswitchesto") {
                auto bdIt = block.fields.find("BACKDROP");
                std::string bd = bdIt != block.fields.end() ? bdIt->second.value : "";
                o << std::format(
                    "    runtime.onBackdropSwitch(\"{}\", "
                    "std::make_unique<scripts::{}>(runtime, {}));\n",
                    EscapeStr(bd), scriptClass, sprRef);
            }
            else if (block.opcode == "event_whenstageclicked") {
                o << std::format(
                    "    runtime.onStageClicked("
                    "std::make_unique<scripts::{}>(runtime, {}));\n",
                    scriptClass, sprRef);
            }
            else if (block.opcode == "event_whengreaterthan") {
                auto nameIt = block.fields.find("WHENGREATERTHANMENU");
                std::string sensorName = nameIt != block.fields.end() ? nameIt->second.value : "TIMER";
                auto valInput = block.inputs.find("VALUE");
                std::string valExpr = "10";
                if (valInput != block.inputs.end())
                    valExpr = EmitInputExpr(valInput->second, target);
                o << std::format(
                    "    runtime.onGreaterThan(\"{}\", {}, "
                    "std::make_unique<scripts::{}>(runtime, {}));\n",
                    sensorName, valExpr, scriptClass, sprRef);
            }
            else if (block.opcode == "control_start_as_clone") {
                o << std::format(
                    "    runtime.onCloneStart(\"{}\", "
                    "[](scratch::Runtime& rt, scratch::Sprite& sp) -> std::unique_ptr<scratch::Script> {{\n"
                    "        return std::make_unique<scripts::{}>(rt, sp);\n"
                    "    }});\n",
                    target.name, scriptClass);
            }

            ++scriptIdx;
        }
    };
    // Sprites first
    for (auto& target : project.targets) {
        if (!target.isStage) emitTargetScripts(target);
    }
    // Stage last (matches Scratch execution order)
    for (auto& target : project.targets) {
        if (target.isStage) emitTargetScripts(target);
    }

    o << R"(
    runtime.run();
    return 0;
}
)";
    return o.str();
}

// ─────────────────────────────────────────────────────────────
// Sprites.hpp generation
// ─────────────────────────────────────────────────────────────
std::string CodeGenerator::GenerateSpritesHpp(const ScratchProject& project)
{
    std::ostringstream o;
    o << "// Auto-generated by ScratchConverter\n";
    o << "#pragma once\n";
    o << "// Sprite declarations are handled inline in main.cpp\n\n";
    o << "namespace sprites {\n";
    for (auto& t : project.targets) {
        o << std::format("    // {} -> layer {}\n", t.name, t.layerOrder);
    }
    o << "} // namespace sprites\n";
    return o.str();
}

// ─────────────────────────────────────────────────────────────
// Scripts.hpp generation — the core of the converter
// ─────────────────────────────────────────────────────────────
std::string CodeGenerator::GenerateScriptsHpp(const ScratchProject& project)
{
    std::ostringstream o;
    o << R"(// Auto-generated by ScratchConverter
#pragma once
#include "ScratchRuntime.hpp"
#include <cmath>
#include <string>
#include <stack>
#include <unordered_map>
#include <vector>

namespace scripts {

using scratch::Runtime;
using scratch::Sprite;
using scratch::Script;
using scratch::ScratchVal;

// Call-stack frame for recursive custom block calls
struct CallFrame {
    int returnState;
    std::unordered_map<std::string, ScratchVal> savedArgs;
};

)";

    for (auto& target : project.targets) {
        std::string safeName = SanitizeName(target.name);
        int scriptIdx = 0;

        for (auto& [blockId, block] : target.blocks) {
            if (!block.topLevel) continue;
            if (!block.opcode.starts_with("event_") &&
                block.opcode != "control_start_as_clone")
                continue;

            std::string className = std::format("Script_{}_{}", safeName, scriptIdx);
            std::string firstBlock = block.next;

            o << std::format("class {} : public Script {{\n", className);
            o << "public:\n";
            o << std::format("    {}(Runtime& rt, Sprite& sp) : rt_(rt), sp_(sp) {{ setOwner(&sp); }}\n\n",
                             className);

            // Clone support
            o << "    std::unique_ptr<Script> clone(Runtime& rt, Sprite& sp) const override {\n";
            o << std::format("        return std::make_unique<{}>(rt, sp);\n", className);
            o << "    }\n\n";

            // Step function
            o << "    bool step(float dt) override {\n";
            o << "        for(;;) {\n";
            o << "        switch (state_) {\n";

            int mainStartState = 0;

            if (!firstBlock.empty()) {
                int stateCounter = 0;
                repeatDepth_ = 0;
                maxRepeatDepth_ = 0;

                // ── Phase 1: Discover all procedures & populate procStates_ ──
                // This pass runs EmitBlockChain for each procedure body to
                // calculate correct state ranges, but discards the generated
                // code.  After this loop procStates_ has entries for every
                // procedure so that cross-procedure calls can be resolved.
                procStates_.clear();
                for (auto& [pid, pblock] : target.blocks) {
                    if (pblock.opcode != "procedures_definition") continue;
                    auto custIt = pblock.inputs.find("custom_block");
                    if (custIt == pblock.inputs.end() || !custIt->second.isBlock) continue;
                    auto protoIt = target.blocks.find(custIt->second.blockId);
                    if (protoIt == target.blocks.end()) continue;
                    auto& proto = protoIt->second;
                    if (pblock.next.empty()) continue;

                    ProcInfo info;
                    info.startState = stateCounter;
                    info.warp = proto.warp;
                    EmitBlockChain(pblock.next, target, stateCounter,
                                   proto.proccode, proto.warp);
                    int returnState = stateCounter++;
                    info.endState = returnState;
                    procStates_[proto.proccode] = info;
                }

                // ── Phase 2: Re-generate procedure bodies & emit code ──
                // Now every procedure is in procStates_, so all procedure
                // calls (including cross-references) resolve correctly.
                stateCounter = 0;
                for (auto& [pid, pblock] : target.blocks) {
                    if (pblock.opcode != "procedures_definition") continue;
                    auto custIt = pblock.inputs.find("custom_block");
                    if (custIt == pblock.inputs.end() || !custIt->second.isBlock) continue;
                    auto protoIt = target.blocks.find(custIt->second.blockId);
                    if (protoIt == target.blocks.end()) continue;
                    auto& proto = protoIt->second;
                    if (pblock.next.empty()) continue;

                    auto bodyStates = EmitBlockChain(pblock.next, target, stateCounter,
                                                     proto.proccode, proto.warp);
                    int returnState = stateCounter++;

                    for (auto& s : bodyStates) {
                        o << std::format("        case {}:\n", s.stateId);
                        std::istringstream cs(s.code);
                        std::string ln;
                        while (std::getline(cs, ln)) {
                            if (!ln.empty()) o << "            " << ln << "\n";
                        }
                        if (s.nextState >= 0)
                            o << std::format("            state_ = {};\n", s.nextState);
                        if (s.yields)
                            o << "            return true;\n";
                        else
                            o << "            break;\n";
                    }

                    o << std::format("        case {}:\n", returnState);
                    o << "            if (!call_stack_.empty()) {\n";
                    o << "                auto& frame = call_stack_.back();\n";
                    o << "                state_ = frame.returnState;\n";
                    o << "                proc_args_ = std::move(frame.savedArgs);\n";
                    o << "                call_stack_.pop_back();\n";
                    o << "                break;\n";
                    o << "            }\n";
                    o << "            return false;\n";
                }

                // ── Now generate the main script body ──
                mainStartState = stateCounter;
                auto states = EmitBlockChain(firstBlock, target, stateCounter);

                for (auto& s : states) {
                    o << std::format("        case {}:\n", s.stateId);
                    std::istringstream codeStream(s.code);
                    std::string line;
                    while (std::getline(codeStream, line)) {
                        if (!line.empty())
                            o << "            " << line << "\n";
                    }
                    if (s.nextState >= 0)
                        o << std::format("            state_ = {};\n", s.nextState);
                    if (s.yields)
                        o << "            return true;\n";
                    else
                        o << "            break;\n";
                }

                // Terminal state
                o << std::format("        case {}:\n", stateCounter);
                o << "            return false;\n";
            } else {
                o << "        case 0:\n";
                o << "            return false;\n";
            }

            o << "        } // switch\n";
            o << "        } // for(;;)\n";
            o << "        return false;\n";
            o << "    }\n\n";

            o << std::format("    void reset() override {{ if (soundId_ >= 0) {{ rt_.stopSound(soundId_); soundId_ = -1; }} state_ = {}; wait_ = 0; call_stack_.clear(); proc_args_.clear(); }}\n", mainStartState);
            o << "    int activeSoundId() const override { return soundId_; }\n\n";

            o << "private:\n";
            o << "    Runtime& rt_;\n";
            o << "    Sprite& sp_;\n";
            o << std::format("    int state_ = {};\n", mainStartState);
            o << "    float wait_ = 0;\n";
            o << "    int repeat_counter_ = 0;\n";
            o << "    int soundId_ = -1;\n";
            o << "    int broadcastWaitId_ = 0;\n";
            for (int d = 1; d < maxRepeatDepth_; ++d)
                o << std::format("    int rc{}_ = 0;\n", d);
            o << "    std::unordered_map<std::string, ScratchVal> proc_args_;\n";
            o << "    std::vector<CallFrame> call_stack_;\n";
            o << "};\n\n";

            ++scriptIdx;
        }
    }

    o << "} // namespace scripts\n";
    return o.str();
}

// ─────────────────────────────────────────────────────────────
// Block chain → state entries
// ─────────────────────────────────────────────────────────────
std::vector<CodeGenerator::StateEntry>
CodeGenerator::EmitBlockChain(const std::string& startBlockId,
                              const ScratchTarget& target,
                              int& stateCounter,
                              const std::string& currentProc,
                              bool warp)
{
    std::vector<StateEntry> allStates;
    std::string currentId = startBlockId;

    while (!currentId.empty()) {
        auto it = target.blocks.find(currentId);
        if (it == target.blocks.end()) break;

        auto blockStates = EmitBlock(it->second, target, stateCounter, currentProc, warp);
        allStates.insert(allStates.end(), blockStates.begin(), blockStates.end());

        currentId = it->second.next;
    }

    // Link sequential states
    for (std::size_t i = 0; i < allStates.size(); ++i) {
        if (allStates[i].nextState < 0 && !allStates[i].yields) {
            int next = (i + 1 < allStates.size())
                           ? allStates[i + 1].stateId
                           : stateCounter;
            allStates[i].nextState = next;
        }
    }

    return allStates;
}

std::vector<CodeGenerator::StateEntry>
CodeGenerator::EmitBlock(const ScratchBlock& block,
                         const ScratchTarget& target,
                         int& stateCounter,
                         const std::string& currentProc,
                         bool warp)
{
    std::vector<StateEntry> states;
    const auto& op = block.opcode;
    auto sid = [&]() { return stateCounter++; };

    auto getInput = [&](const std::string& key) -> std::string {
        auto it = block.inputs.find(key);
        if (it == block.inputs.end()) return "0";
        return EmitInputExpr(it->second, target, currentProc);
    };

    auto getInputStr = [&](const std::string& key) -> std::string {
        auto it = block.inputs.find(key);
        if (it == block.inputs.end()) return "\"\"";
        if (it->second.inputType == 12) {
            if (IsLocalVar(it->second.refId, target))
                return std::format("sp_.getLocalVarStr(\"{}\")", EscapeStr(it->second.refName));
            std::string scoped = ScopedVarName(it->second.refName, it->second.refId, target);
            return std::format("rt_.getVarStr(\"{}\")", EscapeStr(scoped));
        }
        if (it->second.inputType == 13) {
            if (IsLocalList(it->second.refId, target))
                return std::format("sp_.getLocalList(\"{}\").joinedDisplay()", EscapeStr(it->second.refName));
            std::string scoped = ScopedListName(it->second.refName, it->second.refId, target);
            return std::format("rt_.listContents(\"{}\")", EscapeStr(scoped));
        }
        if (!it->second.isBlock) {
            if (auto* s = std::get_if<std::string>(&it->second.literal))
                return std::format("\"{}\"", EscapeStr(*s));
            return std::format("scratch::toStr({})", LiteralToDouble(it->second.literal));
        }
        auto bIt = target.blocks.find(it->second.blockId);
        if (bIt == target.blocks.end()) return "\"\"";
        // If it's a menu block, get the field value as string
        auto& subBlock = bIt->second;
        if (subBlock.opcode.find("_menu") != std::string::npos ||
            subBlock.opcode.ends_with("_menu_")) {
            for (auto& [fk, fv] : subBlock.fields)
                return std::format("\"{}\"", EscapeStr(fv.value));
        }
        // data_itemoflist: use string-returning version to preserve string list items
        if (subBlock.opcode == "data_itemoflist") {
            auto lf = subBlock.fields.find("LIST");
            std::string ln = lf != subBlock.fields.end() ? lf->second.value : "";
            std::string li = lf != subBlock.fields.end() ? lf->second.id : "";
            auto idxIt = subBlock.inputs.find("INDEX");
            std::string idx = idxIt != subBlock.inputs.end()
                ? EmitInputExpr(idxIt->second, target, currentProc) : "scratch::toNum(1)";
            if (IsLocalList(li, target))
                return std::format("sp_.getLocalList(\"{}\").itemStrAt(static_cast<int>({}))",
                                   EscapeStr(ln), idx);
            std::string sc = ScopedListName(ln, li, target);
            return std::format("rt_.itemStrOfList(\"{}\", static_cast<int>({}))",
                               EscapeStr(sc), idx);
        }
        return std::format("scratch::toStr({})", EmitExpression(subBlock, target, currentProc));
    };

    auto getField = [&](const std::string& key) -> std::string {
        auto it = block.fields.find(key);
        if (it == block.fields.end()) return "";
        return it->second.value;
    };

    // ═══════════════════════════════════════════════════════
    // MOTION
    // ═══════════════════════════════════════════════════════
    if (op == "motion_movesteps") {
        states.push_back({sid(),
            std::format("sp_.moveSteps({});", getInput("STEPS")),
            -1, false});
    }
    else if (op == "motion_turnright") {
        states.push_back({sid(),
            std::format("sp_.turnRight({});", getInput("DEGREES")),
            -1, false});
    }
    else if (op == "motion_turnleft") {
        states.push_back({sid(),
            std::format("sp_.turnLeft({});", getInput("DEGREES")),
            -1, false});
    }
    else if (op == "motion_gotoxy") {
        states.push_back({sid(),
            std::format("sp_.gotoXY({}, {});", getInput("X"), getInput("Y")),
            -1, false});
    }
    else if (op == "motion_goto") {
        states.push_back({sid(),
            std::format("sp_.gotoTarget({});", getInputStr("TO")),
            -1, false});
    }
    else if (op == "motion_changexby") {
        states.push_back({sid(),
            std::format("sp_.changeX({});", getInput("DX")),
            -1, false});
    }
    else if (op == "motion_changeyby") {
        states.push_back({sid(),
            std::format("sp_.changeY({});", getInput("DY")),
            -1, false});
    }
    else if (op == "motion_setx") {
        states.push_back({sid(),
            std::format("sp_.setX({});", getInput("X")),
            -1, false});
    }
    else if (op == "motion_sety") {
        states.push_back({sid(),
            std::format("sp_.setY({});", getInput("Y")),
            -1, false});
    }
    else if (op == "motion_pointindirection") {
        states.push_back({sid(),
            std::format("sp_.setDirection({});", getInput("DIRECTION")),
            -1, false});
    }
    else if (op == "motion_pointtowards") {
        states.push_back({sid(),
            std::format("sp_.pointTowards({});", getInputStr("TOWARDS")),
            -1, false});
    }
    else if (op == "motion_ifonedgebounce") {
        states.push_back({sid(), "sp_.ifOnEdgeBounce();", -1, false});
    }
    else if (op == "motion_setrotationstyle") {
        auto style = getField("STYLE");
        states.push_back({sid(),
            std::format("sp_.setRotationStyle(\"{}\");", EscapeStr(style)),
            -1, false});
    }
    else if (op == "motion_glideto" || op == "motion_glidesecstoxy") {
        int s1 = sid();
        int s2 = sid();
        std::string xExpr, yExpr;
        if (op == "motion_glideto") {
            // Uses TO menu → need to resolve sprite/mouse/random position at start
            xExpr = getInput("X"); yExpr = getInput("Y");
            // For glideto with a target menu, this is simplified
        } else {
            xExpr = getInput("X"); yExpr = getInput("Y");
        }
        states.push_back({s1,
            std::format("sp_.startGlide({}, {}, {});\nwait_ = static_cast<float>({});",
                        xExpr, yExpr, getInput("SECS"), getInput("SECS")),
            s2, true});
        states.push_back({s2,
            "wait_ -= dt;\nif (wait_ > 0) { sp_.updateGlide(dt); state_ = "
            + std::to_string(s2) + "; return true; }\nsp_.finishGlide();",
            -1, false});
    }

    // ═══════════════════════════════════════════════════════
    // LOOKS
    // ═══════════════════════════════════════════════════════
    else if (op == "looks_show") {
        states.push_back({sid(), "sp_.setVisible(true);", -1, false});
    }
    else if (op == "looks_hide") {
        states.push_back({sid(), "sp_.setVisible(false);", -1, false});
    }
    else if (op == "looks_setsizeto") {
        states.push_back({sid(),
            std::format("sp_.setSize({});", getInput("SIZE")),
            -1, false});
    }
    else if (op == "looks_changesizeby") {
        states.push_back({sid(),
            std::format("sp_.changeSize({});", getInput("CHANGE")),
            -1, false});
    }
    else if (op == "looks_switchcostumeto") {
        states.push_back({sid(),
            std::format("sp_.setCostumeByName({});", getInputStr("COSTUME")),
            -1, false});
    }
    else if (op == "looks_nextcostume") {
        states.push_back({sid(), "sp_.nextCostume();", -1, false});
    }
    else if (op == "looks_say") {
        states.push_back({sid(),
            std::format("sp_.say(std::string({}));", getInputStr("MESSAGE")),
            -1, false});
    }
    else if (op == "looks_sayforsecs") {
        int s1 = sid(), s2 = sid(), s3 = sid();
        states.push_back({s1,
            std::format("sp_.say(std::string({}));\nwait_ = static_cast<float>({});",
                        getInputStr("MESSAGE"), getInput("SECS")),
            s2, true});
        states.push_back({s2,
            "wait_ -= dt;\nif (wait_ > 0) { state_ = " + std::to_string(s2) + "; return true; }",
            s3, false});
        states.push_back({s3, "sp_.clearSayThink();", -1, false});
    }
    else if (op == "looks_think") {
        states.push_back({sid(),
            std::format("sp_.think(std::string({}));", getInputStr("MESSAGE")),
            -1, false});
    }
    else if (op == "looks_thinkforsecs") {
        int s1 = sid(), s2 = sid(), s3 = sid();
        states.push_back({s1,
            std::format("sp_.think(std::string({}));\nwait_ = static_cast<float>({});",
                        getInputStr("MESSAGE"), getInput("SECS")),
            s2, true});
        states.push_back({s2,
            "wait_ -= dt;\nif (wait_ > 0) { state_ = " + std::to_string(s2) + "; return true; }",
            s3, false});
        states.push_back({s3, "sp_.clearSayThink();", -1, false});
    }
    else if (op == "looks_switchbackdropto") {
        states.push_back({sid(),
            std::format("rt_.stage().switchBackdropTo({});", getInputStr("BACKDROP")),
            -1, false});
    }
    else if (op == "looks_nextbackdrop") {
        states.push_back({sid(), "rt_.stage().nextBackdrop();", -1, false});
    }
    else if (op == "looks_seteffectto") {
        auto effect = getField("EFFECT");
        states.push_back({sid(),
            std::format("sp_.setEffect(\"{}\", {});", EscapeStr(effect), getInput("VALUE")),
            -1, false});
    }
    else if (op == "looks_changeeffectby") {
        auto effect = getField("EFFECT");
        states.push_back({sid(),
            std::format("sp_.changeEffect(\"{}\", {});", EscapeStr(effect), getInput("CHANGE")),
            -1, false});
    }
    else if (op == "looks_cleargraphiceffects") {
        states.push_back({sid(), "sp_.clearGraphicEffects();", -1, false});
    }
    else if (op == "looks_gotofrontback") {
        auto fb = getField("FRONT_BACK");
        if (fb == "front")
            states.push_back({sid(), "sp_.goToFront();", -1, false});
        else
            states.push_back({sid(), "sp_.goToBack();", -1, false});
    }
    else if (op == "looks_goforwardbackwardlayers") {
        auto fb = getField("FORWARD_BACKWARD");
        if (fb == "forward")
            states.push_back({sid(),
                std::format("sp_.goForwardLayers(static_cast<int>({}));", getInput("NUM")),
                -1, false});
        else
            states.push_back({sid(),
                std::format("sp_.goBackwardLayers(static_cast<int>({}));", getInput("NUM")),
                -1, false});
    }

    // ═══════════════════════════════════════════════════════
    // SOUND
    // ═══════════════════════════════════════════════════════
    else if (op == "sound_play") {
        states.push_back({sid(),
            std::format("rt_.playSound({}, sp_);", getInputStr("SOUND_MENU")),
            -1, false});
    }
    else if (op == "sound_playuntildone") {
        if (warp) {
            // In warp mode, just play without waiting
            states.push_back({sid(),
                std::format("rt_.playSound({}, sp_);", getInputStr("SOUND_MENU")),
                -1, false});
        } else {
            int s1 = sid(), s2 = sid();
            states.push_back({s1,
                std::format("soundId_ = rt_.playSoundUntilDone({}, sp_);", getInputStr("SOUND_MENU")),
                s2, true});
            states.push_back({s2,
                "if (soundId_ >= 0 && rt_.isSoundPlaying(soundId_)) { state_ = "
                + std::to_string(s2) + "; return true; }",
                -1, false});
        }
    }
    else if (op == "sound_stopallsounds") {
        states.push_back({sid(), "rt_.stopAllSounds();", -1, false});
    }
    else if (op == "sound_setvolumeto") {
        states.push_back({sid(),
            std::format("sp_.setVolume({});", getInput("VOLUME")),
            -1, false});
    }
    else if (op == "sound_changevolumeby") {
        states.push_back({sid(),
            std::format("sp_.changeVolume({});", getInput("VOLUME")),
            -1, false});
    }
    else if (op == "sound_seteffectto") {
        auto effect = getField("EFFECT");
        states.push_back({sid(),
            std::format("sp_.setSoundEffect(\"{}\", {});", EscapeStr(effect), getInput("VALUE")),
            -1, false});
    }
    else if (op == "sound_changeeffectby") {
        auto effect = getField("EFFECT");
        states.push_back({sid(),
            std::format("sp_.changeSoundEffect(\"{}\", {});", EscapeStr(effect), getInput("VALUE")),
            -1, false});
    }
    else if (op == "sound_cleareffects") {
        states.push_back({sid(), "sp_.clearSoundEffects();", -1, false});
    }

    // ═══════════════════════════════════════════════════════
    // CONTROL
    // ═══════════════════════════════════════════════════════
    else if (op == "control_wait") {
        if (warp) {
            // In warp mode, waits are skipped (no yields)
            states.push_back({sid(), "// wait skipped (warp)", -1, false});
        } else {
            int s1 = sid(), s2 = sid();
            states.push_back({s1,
                std::format("wait_ = static_cast<float>({});", getInput("DURATION")),
                s2, true});
            states.push_back({s2,
                "wait_ -= dt;\nif (wait_ > 0) { state_ = "
                + std::to_string(s2) + "; return true; }",
                -1, false});
        }
    }
    else if (op == "control_forever") {
        auto substackIt = block.inputs.find("SUBSTACK");
        if (substackIt != block.inputs.end() && substackIt->second.isBlock) {
            int loopStart = stateCounter;
            auto bodyStates = EmitBlockChain(substackIt->second.blockId, target, stateCounter, currentProc, warp);

            for (auto& s : bodyStates) states.push_back(s);

            if (warp) {
                // In warp: loop back without yielding (tight loop, one frame)
                int loopBack = sid();
                states.push_back({loopBack, "", loopStart, false});
                if (!bodyStates.empty()) {
                    for (auto it = states.rbegin(); it != states.rend(); ++it) {
                        if (it->stateId == loopBack) continue;
                        if (it->nextState == stateCounter) it->nextState = loopBack;
                        break;
                    }
                }
            } else {
                int yieldState = sid();
                states.push_back({yieldState, "", loopStart, true});
                if (!bodyStates.empty()) {
                    for (auto it = states.rbegin(); it != states.rend(); ++it) {
                        if (it->stateId == yieldState) continue;
                        if (it->nextState == stateCounter) it->nextState = yieldState;
                        break;
                    }
                }
            }
        }
        return states; // forever never falls through
    }
    else if (op == "control_repeat") {
        auto substackIt = block.inputs.find("SUBSTACK");
        int initState = sid(), checkState = sid();

        std::string rc = (repeatDepth_ == 0) ? "repeat_counter_"
                                              : std::format("rc{}_", repeatDepth_);
        int myDepth = repeatDepth_++;
        if (repeatDepth_ > maxRepeatDepth_) maxRepeatDepth_ = repeatDepth_;

        states.push_back({initState,
            std::format("{} = static_cast<int>({});", rc, getInput("TIMES")),
            checkState, false});

        if (substackIt != block.inputs.end() && substackIt->second.isBlock) {
            auto bodyStates = EmitBlockChain(substackIt->second.blockId, target, stateCounter, currentProc, warp);
            int decrState = sid();

            states.push_back({checkState, "", -1, false}); // placeholder

            for (auto& s : bodyStates) states.push_back(s);

            states.push_back({decrState, std::format("{}--;", rc), checkState, !warp});

            // Fix body's last state to point to decrState
            for (auto it = states.rbegin(); it != states.rend(); ++it) {
                if (it->stateId == decrState) continue;
                if (it->nextState < 0 || it->nextState == stateCounter)
                    it->nextState = decrState;
                break;
            }

            int skipTarget = stateCounter;
            for (auto& s : states) {
                if (s.stateId == checkState) {
                    s.code = std::format(
                        "if ({} <= 0) {{ state_ = {}; break; }}", rc, skipTarget);
                    if (!bodyStates.empty())
                        s.nextState = bodyStates.front().stateId;
                    break;
                }
            }
        } else {
            states.push_back({checkState, "", -1, false});
        }
        repeatDepth_ = myDepth;
    }
    else if (op == "control_repeat_until") {
        auto condExpr = getInput("CONDITION");
        auto substackIt = block.inputs.find("SUBSTACK");

        int checkState = sid();

        if (substackIt != block.inputs.end() && substackIt->second.isBlock) {
            auto bodyStates = EmitBlockChain(substackIt->second.blockId, target, stateCounter, currentProc, warp);

            int afterAll;
            if (warp) {
                // No yield state in warp — loop back directly
                afterAll = stateCounter;
                states.push_back({checkState,
                    std::format("if ({}) {{ state_ = {}; break; }}", condExpr, afterAll),
                    -1, false});
                if (!bodyStates.empty())
                    states[states.size() - 1].nextState = bodyStates.front().stateId;
                for (auto& s : bodyStates) states.push_back(s);
                int loopBack = sid();
                states.push_back({loopBack, "", checkState, false});
                for (auto it = states.rbegin(); it != states.rend(); ++it) {
                    if (it->stateId == loopBack) continue;
                    if (it->nextState < 0 || it->nextState == stateCounter)
                        it->nextState = loopBack;
                    break;
                }
            } else {
                int yieldState = sid();
                afterAll = stateCounter;
                states.push_back({checkState,
                    std::format("if ({}) {{ state_ = {}; break; }}", condExpr, afterAll),
                    -1, false});
                if (!bodyStates.empty())
                    states[states.size() - 1].nextState = bodyStates.front().stateId;
                for (auto& s : bodyStates) states.push_back(s);
                states.push_back({yieldState, "", checkState, true});
                for (auto it = states.rbegin(); it != states.rend(); ++it) {
                    if (it->stateId == yieldState) continue;
                    if (it->nextState < 0 || it->nextState == stateCounter)
                        it->nextState = yieldState;
                    break;
                }
            }
        } else {
            // No body: just check condition each frame
            int afterAll = stateCounter;
            if (warp) {
                // Busy-loop without yielding until condition is true
                states.push_back({checkState,
                    std::format("if (!({0})) {{ state_ = {1}; break; }}", condExpr, checkState),
                    -1, false});
            } else {
                states.push_back({checkState,
                    std::format("if ({}) {{ state_ = {}; break; }}\nstate_ = {}; return true;",
                                condExpr, afterAll, checkState),
                    afterAll, false});
            }
        }
    }
    else if (op == "control_wait_until") {
        if (warp) {
            // In warp: busy-wait without yielding
            int checkState = sid();
            auto condExpr = getInput("CONDITION");
            states.push_back({checkState,
                std::format("if (!({0})) {{ state_ = {1}; break; }}", condExpr, checkState),
                -1, false});
        } else {
            int checkState = sid();
            auto condExpr = getInput("CONDITION");
            states.push_back({checkState,
                std::format("if (!({0})) {{ state_ = {1}; return true; }}", condExpr, checkState),
                -1, false});
        }
    }
    else if (op == "control_if") {
        auto condExpr = getInput("CONDITION");
        auto substackIt = block.inputs.find("SUBSTACK");

        if (substackIt != block.inputs.end() && substackIt->second.isBlock) {
            int guardState = sid();
            auto bodyStates = EmitBlockChain(substackIt->second.blockId, target, stateCounter, currentProc, warp);
            int afterAll = stateCounter;

            states.push_back({guardState,
                std::format("if (!({})) {{ state_ = {}; break; }}", condExpr, afterAll),
                -1, false});

            if (!bodyStates.empty())
                states[0].nextState = bodyStates.front().stateId;

            for (auto& s : bodyStates) states.push_back(s);
        } else {
            states.push_back({sid(), "// empty if", -1, false});
        }
    }
    else if (op == "control_if_else") {
        auto condExpr = getInput("CONDITION");
        auto substackIt  = block.inputs.find("SUBSTACK");
        auto substack2It = block.inputs.find("SUBSTACK2");

        int guardState = sid();

        std::vector<StateEntry> thenStates;
        if (substackIt != block.inputs.end() && substackIt->second.isBlock)
            thenStates = EmitBlockChain(substackIt->second.blockId, target, stateCounter, currentProc, warp);

        int jumpOverElse = sid();

        std::vector<StateEntry> elseStates;
        if (substack2It != block.inputs.end() && substack2It->second.isBlock)
            elseStates = EmitBlockChain(substack2It->second.blockId, target, stateCounter, currentProc, warp);

        int afterAll = stateCounter;

        int elseStart = elseStates.empty() ? afterAll : elseStates.front().stateId;
        int thenStart = thenStates.empty() ? jumpOverElse : thenStates.front().stateId;

        states.push_back({guardState,
            std::format("if (!({})) {{ state_ = {}; break; }}", condExpr, elseStart),
            thenStart, false});

        for (auto& s : thenStates) states.push_back(s);
        states.push_back({jumpOverElse, "", afterAll, false});
        for (auto& s : elseStates) states.push_back(s);
    }
    else if (op == "control_stop") {
        auto mode = getField("STOP_OPTION");
        if (mode == "this script") {
            // Inside a procedure, "stop this script" acts as a return
            // from the procedure (Scratch 3 semantics).
            if (!currentProc.empty()) {
                auto procIt = procStates_.find(currentProc);
                if (procIt != procStates_.end()) {
                    states.push_back({sid(), "",
                                      procIt->second.endState, false});
                } else {
                    states.push_back({sid(), "return false;", -1, false});
                }
            } else {
                states.push_back({sid(), "return false;", -1, false});
            }
        } else if (mode == "all") {
            states.push_back({sid(), "rt_.stopAll(); return false;", -1, false});
        } else if (mode == "other scripts in sprite") {
            states.push_back({sid(), "rt_.stopOtherScripts(this);", -1, false});
        } else {
            states.push_back({sid(), "return false;", -1, false});
        }
    }
    else if (op == "control_create_clone_of") {
        // Check if it's "_myself_" via the menu
        auto inputStr = getInputStr("CLONE_OPTION");
        states.push_back({sid(),
            std::format("if ({0} == std::string(\"_myself_\")) rt_.createCloneOfSprite(sp_);\n"
                        "else rt_.createCloneOf({0});", inputStr),
            -1, false});
    }
    else if (op == "control_delete_this_clone") {
        states.push_back({sid(), "if (sp_.isClone()) { rt_.deleteClone(&sp_); return false; }", -1, false});
    }

    // ═══════════════════════════════════════════════════════
    // EVENTS
    // ═══════════════════════════════════════════════════════
    else if (op == "event_broadcast") {
        states.push_back({sid(),
            std::format("rt_.broadcast(std::string({}));", getInputStr("BROADCAST_INPUT")),
            -1, false});
    }
    else if (op == "event_broadcastandwait") {
        int s1 = sid(), s2 = sid();
        states.push_back({s1,
            std::format("broadcastWaitId_ = rt_.broadcastAndWait(std::string({}));", getInputStr("BROADCAST_INPUT")),
            s2, true}); // yield to let spawned scripts start
        states.push_back({s2,
            "if (!rt_.isBroadcastWaitDone(broadcastWaitId_)) { state_ = " + std::to_string(s2) + "; return true; }",
            -1, false});
    }

    // ═══════════════════════════════════════════════════════
    // SENSING
    // ═══════════════════════════════════════════════════════
    else if (op == "sensing_askandwait") {
        int s1 = sid(), s2 = sid();
        states.push_back({s1,
            std::format("rt_.askAndWait(std::string({}));", getInputStr("QUESTION")),
            s2, true});
        states.push_back({s2,
            "if (rt_.isAsking()) { state_ = " + std::to_string(s2) + "; return true; }",
            -1, false});
    }
    else if (op == "sensing_resettimer") {
        states.push_back({sid(), "rt_.resetTimer();", -1, false});
    }
    else if (op == "sensing_setdragmode") {
        auto modeIt = block.fields.find("DRAG_MODE");
        std::string mode = modeIt != block.fields.end() ? modeIt->second.value : "not draggable";
        bool draggable = (mode == "draggable");
        states.push_back({sid(),
            std::format("sp_.setDraggable({});", draggable ? "true" : "false"),
            -1, false});
    }

    // ═══════════════════════════════════════════════════════
    // DATA (variables)
    // ═══════════════════════════════════════════════════════
    else if (op == "data_setvariableto") {
        auto varField = block.fields.find("VARIABLE");
        std::string varName = varField != block.fields.end() ? varField->second.value : "";
        std::string varId   = varField != block.fields.end() ? varField->second.id : "";
        if (IsLocalVar(varId, target)) {
            states.push_back({sid(),
                std::format("sp_.setLocalVarStr(\"{}\", {});", EscapeStr(varName), getInputStr("VALUE")),
                -1, false});
        } else {
            std::string scoped = ScopedVarName(varName, varId, target);
            states.push_back({sid(),
                std::format("rt_.setVarStr(\"{}\", {});", EscapeStr(scoped), getInputStr("VALUE")),
                -1, false});
        }
    }
    else if (op == "data_changevariableby") {
        auto varField = block.fields.find("VARIABLE");
        std::string varName = varField != block.fields.end() ? varField->second.value : "";
        std::string varId   = varField != block.fields.end() ? varField->second.id : "";
        if (IsLocalVar(varId, target)) {
            states.push_back({sid(),
                std::format("sp_.changeLocalVar(\"{}\", scratch::toNum({}));", EscapeStr(varName), getInput("VALUE")),
                -1, false});
        } else {
            std::string scoped = ScopedVarName(varName, varId, target);
            states.push_back({sid(),
                std::format("rt_.changeVar(\"{}\", scratch::toNum({}));", EscapeStr(scoped), getInput("VALUE")),
                -1, false});
        }
    }
    else if (op == "data_showvariable") {
        auto varField = block.fields.find("VARIABLE");
        std::string varName = varField != block.fields.end() ? varField->second.value : "";
        std::string varId   = varField != block.fields.end() ? varField->second.id : "";
        if (IsLocalVar(varId, target)) {
            states.push_back({sid(),
                std::format("sp_.showLocalVar(\"{}\");", EscapeStr(varName)),
                -1, false});
        } else {
            std::string scoped = ScopedVarName(varName, varId, target);
            states.push_back({sid(),
                std::format("rt_.showVariable(\"{}\");", EscapeStr(scoped)),
                -1, false});
        }
    }
    else if (op == "data_hidevariable") {
        auto varField = block.fields.find("VARIABLE");
        std::string varName = varField != block.fields.end() ? varField->second.value : "";
        std::string varId   = varField != block.fields.end() ? varField->second.id : "";
        if (IsLocalVar(varId, target)) {
            states.push_back({sid(),
                std::format("sp_.hideLocalVar(\"{}\");", EscapeStr(varName)),
                -1, false});
        } else {
            std::string scoped = ScopedVarName(varName, varId, target);
            states.push_back({sid(),
                std::format("rt_.hideVariable(\"{}\");", EscapeStr(scoped)),
                -1, false});
        }
    }

    // ═══════════════════════════════════════════════════════
    // DATA (lists)
    // ═══════════════════════════════════════════════════════
    else if (op == "data_addtolist") {
        auto lf = block.fields.find("LIST");
        std::string listName = lf != block.fields.end() ? lf->second.value : "";
        std::string listId   = lf != block.fields.end() ? lf->second.id : "";
        if (IsLocalList(listId, target)) {
            states.push_back({sid(),
                std::format("sp_.getLocalList(\"{}\").addStr({});", EscapeStr(listName), getInputStr("ITEM")),
                -1, false});
        } else {
            std::string scoped = ScopedListName(listName, listId, target);
            states.push_back({sid(),
                std::format("rt_.addToList(\"{}\", {});", EscapeStr(scoped), getInputStr("ITEM")),
                -1, false});
        }
    }
    else if (op == "data_deleteoflist") {
        auto lf = block.fields.find("LIST");
        std::string listName = lf != block.fields.end() ? lf->second.value : "";
        std::string listId   = lf != block.fields.end() ? lf->second.id : "";
        auto indexExpr = getInput("INDEX");
        if (IsLocalList(listId, target)) {
            states.push_back({sid(),
                std::format("sp_.getLocalList(\"{}\").deleteAt(static_cast<int>({}));",
                            EscapeStr(listName), indexExpr),
                -1, false});
        } else {
            std::string scoped = ScopedListName(listName, listId, target);
            states.push_back({sid(),
                std::format("rt_.deleteOfList(\"{}\", static_cast<int>({}));",
                            EscapeStr(scoped), indexExpr),
                -1, false});
        }
    }
    else if (op == "data_deletealloflist") {
        auto lf = block.fields.find("LIST");
        std::string listName = lf != block.fields.end() ? lf->second.value : "";
        std::string listId   = lf != block.fields.end() ? lf->second.id : "";
        if (IsLocalList(listId, target)) {
            states.push_back({sid(),
                std::format("sp_.getLocalList(\"{}\").deleteAll();", EscapeStr(listName)),
                -1, false});
        } else {
            std::string scoped = ScopedListName(listName, listId, target);
            states.push_back({sid(),
                std::format("rt_.deleteAllOfList(\"{}\");", EscapeStr(scoped)),
                -1, false});
        }
    }
    else if (op == "data_insertatlist") {
        auto lf = block.fields.find("LIST");
        std::string listName = lf != block.fields.end() ? lf->second.value : "";
        std::string listId   = lf != block.fields.end() ? lf->second.id : "";
        if (IsLocalList(listId, target)) {
            states.push_back({sid(),
                std::format("sp_.getLocalList(\"{}\").insertAtStr(static_cast<int>({}), {});",
                            EscapeStr(listName), getInput("INDEX"), getInputStr("ITEM")),
                -1, false});
        } else {
            std::string scoped = ScopedListName(listName, listId, target);
            states.push_back({sid(),
                std::format("rt_.insertAtList(\"{}\", static_cast<int>({}), {});",
                            EscapeStr(scoped), getInput("INDEX"), getInputStr("ITEM")),
                -1, false});
        }
    }
    else if (op == "data_replaceitemoflist") {
        auto lf = block.fields.find("LIST");
        std::string listName = lf != block.fields.end() ? lf->second.value : "";
        std::string listId   = lf != block.fields.end() ? lf->second.id : "";
        if (IsLocalList(listId, target)) {
            states.push_back({sid(),
                std::format("sp_.getLocalList(\"{}\").replaceAtStr(static_cast<int>({}), {});",
                            EscapeStr(listName), getInput("INDEX"), getInputStr("ITEM")),
                -1, false});
        } else {
            std::string scoped = ScopedListName(listName, listId, target);
            states.push_back({sid(),
                std::format("rt_.replaceItemOfList(\"{}\", static_cast<int>({}), {});",
                            EscapeStr(scoped), getInput("INDEX"), getInputStr("ITEM")),
                -1, false});
        }
    }
    else if (op == "data_showlist") {
        auto lf = block.fields.find("LIST");
        std::string listName = lf != block.fields.end() ? lf->second.value : "";
        std::string listId   = lf != block.fields.end() ? lf->second.id : "";
        std::string scoped   = ScopedListName(listName, listId, target);
        states.push_back({sid(),
            std::format("rt_.showList(\"{}\");", EscapeStr(scoped)),
            -1, false});
    }
    else if (op == "data_hidelist") {
        auto lf = block.fields.find("LIST");
        std::string listName = lf != block.fields.end() ? lf->second.value : "";
        std::string listId   = lf != block.fields.end() ? lf->second.id : "";
        std::string scoped   = ScopedListName(listName, listId, target);
        states.push_back({sid(),
            std::format("rt_.hideList(\"{}\");", EscapeStr(scoped)),
            -1, false});
    }

    // ═══════════════════════════════════════════════════════
    // PEN
    // ═══════════════════════════════════════════════════════
    else if (op == "pen_clear") {
        states.push_back({sid(), "rt_.penClear();", -1, false});
    }
    else if (op == "pen_stamp") {
        states.push_back({sid(), "sp_.stamp(rt_.penTexture());", -1, false});
    }
    else if (op == "pen_penDown") {
        states.push_back({sid(), "sp_.penDown();", -1, false});
    }
    else if (op == "pen_penUp") {
        states.push_back({sid(), "sp_.penUp();", -1, false});
    }
    else if (op == "pen_setPenColorToColor") {
        auto colorExpr = getInput("COLOR");
        states.push_back({sid(),
            std::format("sp_.setPenColorHex(static_cast<int>({}));", colorExpr),
            -1, false});
    }
    else if (op == "pen_setPenSizeTo") {
        states.push_back({sid(),
            std::format("sp_.setPenSize({});", getInput("SIZE")),
            -1, false});
    }
    else if (op == "pen_changePenSizeBy") {
        states.push_back({sid(),
            std::format("sp_.changePenSize({});", getInput("SIZE")),
            -1, false});
    }
    else if (op == "pen_setPenShadeToNumber") {
        states.push_back({sid(),
            std::format("sp_.setPenParam(\"brightness\", {});", getInput("SHADE")),
            -1, false});
    }
    else if (op == "pen_changePenShadeBy") {
        states.push_back({sid(),
            std::format("sp_.changePenParam(\"brightness\", {});", getInput("SHADE")),
            -1, false});
    }
    else if (op == "pen_setPenHueToNumber") {
        states.push_back({sid(),
            std::format("sp_.setPenParam(\"color\", {});", getInput("HUE")),
            -1, false});
    }
    else if (op == "pen_changePenHueBy") {
        states.push_back({sid(),
            std::format("sp_.changePenParam(\"color\", {});", getInput("HUE")),
            -1, false});
    }
    else if (op == "pen_setPenColorParamTo") {
        auto param = getInputStr("COLOR_PARAM");
        states.push_back({sid(),
            std::format("sp_.setPenParam({}, {});", param, getInput("VALUE")),
            -1, false});
    }
    else if (op == "pen_changePenColorParamBy") {
        auto param = getInputStr("COLOR_PARAM");
        states.push_back({sid(),
            std::format("sp_.changePenParam({}, {});", param, getInput("VALUE")),
            -1, false});
    }

    // ═══════════════════════════════════════════════════════
    // PROCEDURES (custom blocks)
    // ═══════════════════════════════════════════════════════
    else if (op == "procedures_call") {
        // Use the pre-generated procedure state machine via call stack
        auto procIt = procStates_.find(block.proccode);
        if (procIt != procStates_.end()) {
            auto& procInfo = procIt->second;

            // Build argument setup code — supports both numbers and strings
            std::ostringstream argSetup;
            auto* defBlock = FindProcedureDefinition(block.proccode, target);
            if (defBlock) {
                auto custIt = defBlock->inputs.find("custom_block");
                if (custIt != defBlock->inputs.end() && custIt->second.isBlock) {
                    auto protoIt = target.blocks.find(custIt->second.blockId);
                    if (protoIt != target.blocks.end()) {
                        auto& proto = protoIt->second;
                        for (std::size_t i = 0; i < proto.argumentIds.size(); ++i) {
                            std::string argId = proto.argumentIds[i];
                            std::string argName = i < proto.argumentNames.size()
                                                      ? proto.argumentNames[i] : argId;
                            auto argInputIt = block.inputs.find(argId);
                            if (argInputIt != block.inputs.end()) {
                                auto& inp = argInputIt->second;
                                // Variable or list reporter reference (inputType 12 or 13)
                                if (inp.inputType == 12 || inp.inputType == 13) {
                                    std::string argVal = EmitInputExpr(inp, target, currentProc);
                                    argSetup << std::format(
                                        "proc_args_[\"{}::{}\"] = ScratchVal({});\n",
                                        EscapeStr(block.proccode),
                                        EscapeStr(argName), argVal);
                                }
                                // Preserve string literals
                                else if (!inp.isBlock) {
                                    if (auto* s = std::get_if<std::string>(&inp.literal)) {
                                        argSetup << std::format(
                                            "proc_args_[\"{}::{}\"] = ScratchVal(\"{}\");\n",
                                            EscapeStr(block.proccode),
                                            EscapeStr(argName),
                                            EscapeStr(*s));
                                    } else {
                                        argSetup << std::format(
                                            "proc_args_[\"{}::{}\"] = ScratchVal({});\n",
                                            EscapeStr(block.proccode),
                                            EscapeStr(argName),
                                            std::get<double>(inp.literal));
                                    }
                                } else {
                                    // Block expression — check if it returns string
                                    auto bIt = target.blocks.find(inp.blockId);
                                    bool isStrExpr = false;
                                    if (bIt != target.blocks.end()) {
                                        auto& subOp = bIt->second.opcode;
                                        isStrExpr = (subOp == "operator_join" ||
                                                     subOp == "operator_letter_of" ||
                                                     subOp == "sensing_answer" ||
                                                     subOp == "sensing_username" ||
                                                     subOp == "looks_costumenumbername" ||
                                                     subOp == "looks_backdropnumbername" ||
                                                     subOp.find("_menu") != std::string::npos);
                                    }
                                    if (isStrExpr) {
                                        // Use string expression
                                        std::string strExpr;
                                        if (bIt != target.blocks.end())
                                            strExpr = "scratch::toStr(" + EmitExpression(bIt->second, target, currentProc) + ")";
                                        else
                                            strExpr = "\"\"";
                                        argSetup << std::format(
                                            "proc_args_[\"{}::{}\"] = ScratchVal({});\n",
                                            EscapeStr(block.proccode),
                                            EscapeStr(argName), strExpr);
                                    } else {
                                        std::string argVal = EmitInputExpr(inp, target, currentProc);
                                        argSetup << std::format(
                                            "proc_args_[\"{}::{}\"] = ScratchVal({});\n",
                                            EscapeStr(block.proccode),
                                            EscapeStr(argName), argVal);
                                    }
                                }
                            } else {
                                argSetup << std::format(
                                    "proc_args_[\"{}::{}\"] = ScratchVal(0.0);\n",
                                    EscapeStr(block.proccode),
                                    EscapeStr(argName));
                            }
                        }
                    }
                }
            }

            // Push call frame and jump to procedure
            int callState = sid();
            int returnState = stateCounter; // where we resume after the call
            std::ostringstream callCode;
            callCode << "call_stack_.push_back({" << (returnState) << ", proc_args_});\n";
            callCode << argSetup.str();
            states.push_back({callState, callCode.str(), procInfo.startState, false});
        } else {
            states.push_back({sid(),
                std::format("// procedure call: {} (no body found)", EscapeStr(block.proccode)),
                -1, false});
        }
    }

    // ═══════════════════════════════════════════════════════
    // SKIP internal blocks that aren't standalone statements
    // ═══════════════════════════════════════════════════════
    else if (op == "procedures_definition" ||
             op == "procedures_prototype" ||
             op == "argument_reporter_string_number" ||
             op == "argument_reporter_boolean") {
        // These are handled elsewhere
    }

    // ═══════════════════════════════════════════════════════
    // UNKNOWN
    // ═══════════════════════════════════════════════════════
    else {
        states.push_back({sid(),
            std::format("// TODO: unhandled opcode '{}'", op),
            -1, false});
    }

    return states;
}

// ─────────────────────────────────────────────────────────────
// Expression generation
// ─────────────────────────────────────────────────────────────
std::string CodeGenerator::EmitInputExpr(const BlockInput& input,
                                         const ScratchTarget& target,
                                         const std::string& currentProc)
{
    if (input.inputType == 12) {
        if (IsLocalVar(input.refId, target))
            return std::format("scratch::toNum(sp_.getLocalVar(\"{}\"))", EscapeStr(input.refName));
        std::string scoped = ScopedVarName(input.refName, input.refId, target);
        return std::format("scratch::toNum(rt_.getVar(\"{}\"))", EscapeStr(scoped));
    }
    if (input.inputType == 13) {
        if (IsLocalList(input.refId, target))
            return std::format("scratch::toNum(sp_.getLocalList(\"{}\").length())", EscapeStr(input.refName));
        std::string scoped = ScopedListName(input.refName, input.refId, target);
        return std::format("scratch::toNum(rt_.lengthOfList(\"{}\"))", EscapeStr(scoped));
    }
    if (!input.isBlock) {
        return LiteralToDouble(input.literal);
    }

    auto it = target.blocks.find(input.blockId);
    if (it == target.blocks.end())
        return "0";

    return EmitExpression(it->second, target, currentProc);
}

std::string CodeGenerator::EmitExpression(const ScratchBlock& block,
                                          const ScratchTarget& target,
                                          const std::string& currentProc)
{
    const auto& op = block.opcode;

    auto getInput = [&](const std::string& key) -> std::string {
        auto it = block.inputs.find(key);
        if (it == block.inputs.end()) return "0";
        return EmitInputExpr(it->second, target, currentProc);
    };

    auto getInputStr = [&](const std::string& key) -> std::string {
        auto it = block.inputs.find(key);
        if (it == block.inputs.end()) return "\"\"";
        if (it->second.inputType == 12) {
            if (IsLocalVar(it->second.refId, target))
                return std::format("sp_.getLocalVarStr(\"{}\")", EscapeStr(it->second.refName));
            std::string scoped = ScopedVarName(it->second.refName, it->second.refId, target);
            return std::format("rt_.getVarStr(\"{}\")", EscapeStr(scoped));
        }
        if (it->second.inputType == 13) {
            if (IsLocalList(it->second.refId, target))
                return std::format("sp_.getLocalList(\"{}\").joinedDisplay()", EscapeStr(it->second.refName));
            std::string scoped = ScopedListName(it->second.refName, it->second.refId, target);
            return std::format("rt_.listContents(\"{}\")", EscapeStr(scoped));
        }
        if (!it->second.isBlock) {
            if (auto* s = std::get_if<std::string>(&it->second.literal))
                return std::format("\"{}\"", EscapeStr(*s));
            return std::format("scratch::toStr({})", LiteralToDouble(it->second.literal));
        }
        auto bIt = target.blocks.find(it->second.blockId);
        if (bIt == target.blocks.end()) return "\"\"";
        auto& subBlock = bIt->second;
        if (subBlock.opcode.find("_menu") != std::string::npos) {
            for (auto& [fk, fv] : subBlock.fields)
                return std::format("\"{}\"", EscapeStr(fv.value));
        }
        // data_itemoflist: use string-returning version to preserve string list items
        if (subBlock.opcode == "data_itemoflist") {
            auto lf = subBlock.fields.find("LIST");
            std::string ln = lf != subBlock.fields.end() ? lf->second.value : "";
            std::string li = lf != subBlock.fields.end() ? lf->second.id : "";
            auto idxIt = subBlock.inputs.find("INDEX");
            std::string idx = idxIt != subBlock.inputs.end()
                ? EmitInputExpr(idxIt->second, target, currentProc) : "scratch::toNum(1)";
            if (IsLocalList(li, target))
                return std::format("sp_.getLocalList(\"{}\").itemStrAt(static_cast<int>({}))",
                                   EscapeStr(ln), idx);
            std::string sc = ScopedListName(ln, li, target);
            return std::format("rt_.itemStrOfList(\"{}\", static_cast<int>({}))",
                               EscapeStr(sc), idx);
        }
        return std::format("scratch::toStr({})", EmitExpression(subBlock, target, currentProc));
    };

    auto getField = [&](const std::string& key) -> std::string {
        auto it = block.fields.find(key);
        if (it == block.fields.end()) return "";
        return it->second.value;
    };

    // ── Operators ────────────────────────────────────────────
    if (op == "operator_add")
        return std::format("(scratch::toNum({}) + scratch::toNum({}))", getInput("NUM1"), getInput("NUM2"));
    if (op == "operator_subtract")
        return std::format("(scratch::toNum({}) - scratch::toNum({}))", getInput("NUM1"), getInput("NUM2"));
    if (op == "operator_multiply")
        return std::format("(scratch::toNum({}) * scratch::toNum({}))", getInput("NUM1"), getInput("NUM2"));
    if (op == "operator_divide") {
        auto denom = getInput("NUM2");
        return std::format("(scratch::toNum({0}) != 0 ? (scratch::toNum({1}) / scratch::toNum({0})) : 0.0)", denom, getInput("NUM1"));
    }
    if (op == "operator_mod")
        return std::format("scratch::scratchMod(scratch::toNum({}), scratch::toNum({}))", getInput("NUM1"), getInput("NUM2"));
    if (op == "operator_round")
        return std::format("std::round(scratch::toNum({}))", getInput("NUM"));
    if (op == "operator_random")
        return std::format("rt_.randomRange(scratch::toNum({}), scratch::toNum({}))", getInput("FROM"), getInput("TO"));

    if (op == "operator_gt")
        return std::format("scratch::scratchGt({}, {})", getInputStr("OPERAND1"), getInputStr("OPERAND2"));
    if (op == "operator_lt")
        return std::format("scratch::scratchLt({}, {})", getInputStr("OPERAND1"), getInputStr("OPERAND2"));
    if (op == "operator_equals")
        return std::format("scratch::scratchEquals({}, {})",
                           getInputStr("OPERAND1"), getInputStr("OPERAND2"));
    if (op == "operator_and")
        return std::format("({} && {})", getInput("OPERAND1"), getInput("OPERAND2"));
    if (op == "operator_or")
        return std::format("({} || {})", getInput("OPERAND1"), getInput("OPERAND2"));
    if (op == "operator_not")
        return std::format("(!({}))", getInput("OPERAND"));

    if (op == "operator_mathop") {
        auto func = getField("OPERATOR");
        auto num  = getInput("NUM");
        if (func == "abs")     return std::format("std::abs({})", num);
        if (func == "floor")   return std::format("std::floor({})", num);
        if (func == "ceiling") return std::format("std::ceil({})", num);
        if (func == "sqrt")    return std::format("std::sqrt({})", num);
        if (func == "sin")     return std::format("std::sin(({}) * 3.14159265358979 / 180.0)", num);
        if (func == "cos")     return std::format("std::cos(({}) * 3.14159265358979 / 180.0)", num);
        if (func == "tan")     return std::format("std::tan(({}) * 3.14159265358979 / 180.0)", num);
        if (func == "asin")    return std::format("(std::asin({}) * 180.0 / 3.14159265358979)", num);
        if (func == "acos")    return std::format("(std::acos({}) * 180.0 / 3.14159265358979)", num);
        if (func == "atan")    return std::format("(std::atan({}) * 180.0 / 3.14159265358979)", num);
        if (func == "ln")      return std::format("std::log({})", num);
        if (func == "log")     return std::format("std::log10({})", num);
        if (func == "e ^")     return std::format("std::exp({})", num);
        if (func == "10 ^")    return std::format("std::pow(10.0, {})", num);
        return std::format("({})", num);
    }

    // ── String operators ─────────────────────────────────────
    if (op == "operator_join")
        return std::format("scratch::Runtime::join({}, {})",
                           getInputStr("STRING1"), getInputStr("STRING2"));
    if (op == "operator_letter_of")
        return std::format("scratch::Runtime::letterOf(static_cast<int>({}), {})",
                           getInput("LETTER"), getInputStr("STRING"));
    if (op == "operator_length")
        return std::format("scratch::Runtime::lengthOfStr({})", getInputStr("STRING"));
    if (op == "operator_contains")
        return std::format("scratch::Runtime::strContains({}, {})",
                           getInputStr("STRING1"), getInputStr("STRING2"));

    // ── Sensing ──────────────────────────────────────────────
    if (op == "sensing_mousex")    return "rt_.mouseX()";
    if (op == "sensing_mousey")    return "rt_.mouseY()";
    if (op == "sensing_mousedown") return "rt_.mouseDown()";

    if (op == "sensing_keypressed") {
        auto key = getInputStr("KEY_OPTION");
        return std::format("rt_.keyPressed({})", key);
    }

    if (op == "sensing_touchingobject") {
        auto targetExpr = getInputStr("TOUCHINGOBJECTMENU");
        return std::format("sp_.touching({})", targetExpr);
    }
    if (op == "sensing_touchingobjectmenu") {
        auto name = getField("TOUCHINGOBJECTMENU");
        return std::format("\"{}\"", EscapeStr(name));
    }
    if (op == "sensing_touchingcolor") {
        auto colorExpr = getInput("COLOR");
        return std::format("rt_.touchingColor(sp_, static_cast<int>({}))", colorExpr);
    }
    if (op == "sensing_coloristouchingcolor") {
        auto colorExpr = getInput("COLOR");
        auto color2Expr = getInput("COLOR2");
        return std::format("rt_.colorTouchingColor(sp_, static_cast<int>({}), static_cast<int>({}))", colorExpr, color2Expr);
    }

    if (op == "sensing_distanceto") {
        auto target_name = getInputStr("DISTANCETOMENU");
        return std::format("sp_.distanceTo({})", target_name);
    }
    if (op == "sensing_distancetomenu") {
        auto name = getField("DISTANCETOMENU");
        return std::format("\"{}\"", EscapeStr(name));
    }

    if (op == "sensing_timer")     return "rt_.timer()";
    if (op == "sensing_answer")    return "scratch::toNum(rt_.answer())";

    if (op == "sensing_current") {
        auto what = getField("CURRENTMENU");
        std::string lower = what;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return std::format("rt_.current(\"{}\")", lower);
    }
    if (op == "sensing_dayssince2000") return "rt_.daysSince2000()";
    if (op == "sensing_username")      return "std::string(rt_.username())";
    if (op == "sensing_loudness")      return "rt_.loudness()";

    if (op == "sensing_of") {
        auto prop = getField("PROPERTY");
        auto obj  = getInputStr("OBJECT");
        return std::format("rt_.getAttributeOf(\"{}\", {})", EscapeStr(prop), obj);
    }

    // Key option menu blocks
    if (op == "sensing_keyoptions") {
        auto key = getField("KEY_OPTION");
        return std::format("\"{}\"", EscapeStr(key));
    }

    // ── Data (variables) ─────────────────────────────────────
    if (op == "data_variable") {
        auto varField = block.fields.find("VARIABLE");
        std::string varName = varField != block.fields.end() ? varField->second.value : "";
        std::string varId   = varField != block.fields.end() ? varField->second.id : "";
        if (IsLocalVar(varId, target))
            return std::format("sp_.getLocalVar(\"{}\")", EscapeStr(varName));
        std::string scoped = ScopedVarName(varName, varId, target);
        return std::format("rt_.getVar(\"{}\")", EscapeStr(scoped));
    }

    // ── Data (lists) ─────────────────────────────────────────
    if (op == "data_itemoflist") {
        auto lf = block.fields.find("LIST");
        std::string listName = lf != block.fields.end() ? lf->second.value : "";
        std::string listId   = lf != block.fields.end() ? lf->second.id : "";
        if (IsLocalList(listId, target))
            return std::format("sp_.getLocalList(\"{}\").itemAt(static_cast<int>({}))",
                               EscapeStr(listName), getInput("INDEX"));
        std::string scoped = ScopedListName(listName, listId, target);
        return std::format("rt_.itemOfList(\"{}\", static_cast<int>({}))",
                           EscapeStr(scoped), getInput("INDEX"));
    }
    if (op == "data_itemnumoflist") {
        auto lf = block.fields.find("LIST");
        std::string listName = lf != block.fields.end() ? lf->second.value : "";
        std::string listId   = lf != block.fields.end() ? lf->second.id : "";
        if (IsLocalList(listId, target))
            return std::format("sp_.getLocalList(\"{}\").itemNumStr({})",
                               EscapeStr(listName), getInputStr("ITEM"));
        std::string scoped = ScopedListName(listName, listId, target);
        return std::format("rt_.itemNumOfList(\"{}\", {})",
                           EscapeStr(scoped), getInputStr("ITEM"));
    }
    if (op == "data_lengthoflist") {
        auto lf = block.fields.find("LIST");
        std::string listName = lf != block.fields.end() ? lf->second.value : "";
        std::string listId   = lf != block.fields.end() ? lf->second.id : "";
        if (IsLocalList(listId, target))
            return std::format("sp_.getLocalList(\"{}\").length()", EscapeStr(listName));
        std::string scoped = ScopedListName(listName, listId, target);
        return std::format("rt_.lengthOfList(\"{}\")", EscapeStr(scoped));
    }
    if (op == "data_listcontainsitem") {
        auto lf = block.fields.find("LIST");
        std::string listName = lf != block.fields.end() ? lf->second.value : "";
        std::string listId   = lf != block.fields.end() ? lf->second.id : "";
        if (IsLocalList(listId, target))
            return std::format("sp_.getLocalList(\"{}\").containsStr({})",
                               EscapeStr(listName), getInputStr("ITEM"));
        std::string scoped = ScopedListName(listName, listId, target);
        return std::format("rt_.listContainsItem(\"{}\", {})",
                           EscapeStr(scoped), getInputStr("ITEM"));
    }
    if (op == "data_listcontents") {
        auto lf = block.fields.find("LIST");
        std::string listName = lf != block.fields.end() ? lf->second.value : "";
        std::string listId   = lf != block.fields.end() ? lf->second.id : "";
        if (IsLocalList(listId, target))
            return std::format("sp_.getLocalList(\"{}\").joinedDisplay()", EscapeStr(listName));
        std::string scoped = ScopedListName(listName, listId, target);
        return std::format("rt_.listContents(\"{}\")", EscapeStr(scoped));
    }

    // ── Motion reporters ─────────────────────────────────────
    if (op == "motion_xposition") return "sp_.x()";
    if (op == "motion_yposition") return "sp_.y()";
    if (op == "motion_direction") return "sp_.direction()";

    // ── Looks reporters ──────────────────────────────────────
    if (op == "looks_costumenumbername") {
        auto what = getField("NUMBER_NAME");
        if (what == "name")
            return "sp_.costumeName()";
        return "sp_.costumeIndex()";
    }
    if (op == "looks_backdropnumbername") {
        auto what = getField("NUMBER_NAME");
        if (what == "name")
            return "rt_.stage().backdropName()";
        return "rt_.stage().backdropNumber()";
    }
    if (op == "looks_size") return "sp_.size()";

    // ── Sound reporters ──────────────────────────────────────
    if (op == "sound_volume") return "sp_.getVolume()";

    // ── Procedure arguments ──────────────────────────────────
    if (op == "argument_reporter_string_number") {
        auto argName = getField("VALUE");
        // ScratchVal has operator double() for numeric contexts,
        // and scratch::toStr(ScratchVal) for string contexts.
        return std::format("proc_args_[\"{}::{}\"]", EscapeStr(currentProc), EscapeStr(argName));
    }
    if (op == "argument_reporter_boolean") {
        auto argName = getField("VALUE");
        return std::format("(static_cast<double>(proc_args_[\"{}::{}\"]) != 0)", EscapeStr(currentProc), EscapeStr(argName));
    }

    // ── Menu blocks (return string) ──────────────────────────
    if (op == "motion_goto_menu") {
        auto val = getField("TO");
        return std::format("\"{}\"", EscapeStr(val));
    }
    if (op == "motion_pointtowards_menu") {
        auto val = getField("TOWARDS");
        return std::format("\"{}\"", EscapeStr(val));
    }
    if (op == "motion_glideto_menu") {
        auto val = getField("TO");
        return std::format("\"{}\"", EscapeStr(val));
    }
    if (op == "control_create_clone_of_menu") {
        auto val = getField("CLONE_OPTION");
        return std::format("\"{}\"", EscapeStr(val));
    }
    if (op == "looks_costume") {
        auto val = getField("COSTUME");
        return std::format("\"{}\"", EscapeStr(val));
    }
    if (op == "looks_backdrops") {
        auto val = getField("BACKDROP");
        return std::format("\"{}\"", EscapeStr(val));
    }
    if (op == "sound_sounds_menu") {
        auto val = getField("SOUND_MENU");
        return std::format("\"{}\"", EscapeStr(val));
    }
    if (op == "pen_menu_colorParam") {
        auto val = getField("colorParam");
        return std::format("\"{}\"", EscapeStr(val));
    }

    // ── Fallback ─────────────────────────────────────────────
    return std::format("0 /* unknown reporter: {} */", op);
}

// ─────────────────────────────────────────────────────────────
// Asset writing
// ─────────────────────────────────────────────────────────────
void CodeGenerator::WriteAssets(const ScratchProject& project,
                                const fs::path& assetsDir)
{
    fs::create_directories(assetsDir / "costumes");
    fs::create_directories(assetsDir / "sounds");

    svgScales_.clear();

    for (auto& target : project.targets) {
        for (auto& costume : target.costumes) {
            auto it = project.assets.find(costume.md5ext);
            if (it == project.assets.end()) continue;

            if (costume.dataFormat == "png" || costume.dataFormat == "jpg") {
                auto dest = assetsDir / "costumes" / costume.md5ext;
                std::ofstream out(dest, std::ios::binary);
                out.write(reinterpret_cast<const char*>(it->second.data()),
                          static_cast<std::streamsize>(it->second.size()));
            }
            else if (costume.dataFormat == "svg") {
                constexpr float vectorScale = 2.0f;
                auto result = RasterizeSvg(it->second.data(), it->second.size(), vectorScale);

                // If rasterization fails (empty SVG), create a 2x2 transparent placeholder
                if (result.pixels.empty()) {
                    result.width = 2;
                    result.height = 2;
                    result.pixels.resize(2 * 2 * 4, 0); // fully transparent RGBA
                }

                auto pngData = EncodePng(result.pixels.data(), result.width, result.height);
                if (!pngData.empty()) {
                    std::string pngName = costume.md5ext;
                    auto dotPos = pngName.rfind('.');
                    if (dotPos != std::string::npos) pngName = pngName.substr(0, dotPos) + ".png";

                    auto dest = assetsDir / "costumes" / pngName;
                    std::ofstream out(dest, std::ios::binary);
                    out.write(reinterpret_cast<const char*>(pngData.data()),
                              static_cast<std::streamsize>(pngData.size()));

                    svgScales_[costume.md5ext] = {result.width, result.height};
                }
            }
        }

        for (auto& sound : target.sounds) {
            auto it = project.assets.find(sound.md5ext);
            if (it == project.assets.end()) continue;

            auto dest = assetsDir / "sounds" / sound.md5ext;

            // Check if this is an ADPCM WAV that needs conversion to PCM
            auto& data = it->second;
            bool converted = false;
            if (data.size() > 22 && sound.dataFormat == "wav") {
                uint16_t audioFmt = static_cast<uint16_t>(data[20]) |
                                    (static_cast<uint16_t>(data[21]) << 8);
                if (audioFmt == 17) { // IMA ADPCM
                    auto pcmData = ConvertAdpcmToPcm(data);
                    if (!pcmData.empty()) {
                        std::ofstream out(dest, std::ios::binary);
                        out.write(reinterpret_cast<const char*>(pcmData.data()),
                                  static_cast<std::streamsize>(pcmData.size()));
                        converted = true;
                    }
                }
            }
            if (!converted) {
                std::ofstream out(dest, std::ios::binary);
                out.write(reinterpret_cast<const char*>(data.data()),
                          static_cast<std::streamsize>(data.size()));
            }
        }
    }
}

} // namespace sc
