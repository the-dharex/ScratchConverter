#include "core/SB3Parser.hpp"

#include <nlohmann/json.hpp>
#include <miniz.h>

#include <stdexcept>
#include <sstream>

using json = nlohmann::json;

namespace sc {

// ─────────────────────────────────────────────────────────────
// Helpers local to this TU
// ─────────────────────────────────────────────────────────────
namespace {

/// Extract all files from a ZIP archive in memory.
std::unordered_map<std::string, std::vector<uint8_t>>
ExtractZip(const std::filesystem::path& zipPath)
{
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipPath.string().c_str(), 0))
        throw std::runtime_error("No se pudo abrir el archivo .sb3");

    std::unordered_map<std::string, std::vector<uint8_t>> files;

    auto numFiles = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < numFiles; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            continue;
        if (mz_zip_reader_is_file_a_directory(&zip, i))
            continue;

        std::size_t size{};
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data) continue;

        std::vector<uint8_t> buf(static_cast<uint8_t*>(data),
                                 static_cast<uint8_t*>(data) + size);
        mz_free(data);

        files[stat.m_filename] = std::move(buf);
    }

    mz_zip_reader_end(&zip);
    return files;
}

/// Parse an input entry from project.json.
/// Scratch input format: [shadow, value] or [shadow, value, shadow_value]
/// value is either a block-id string or [type, literal].
BlockInput ParseInput(const json& j)
{
    BlockInput inp;

    if (!j.is_array() || j.size() < 2) return inp;

    const auto& val = j[1];

    if (val.is_string()) {
        // Reference to another block
        inp.isBlock = true;
        inp.blockId = val.get<std::string>();
    } else if (val.is_array() && val.size() >= 2) {
        int type = val[0].get<int>();
        if ((type == 12 || type == 13) && val.size() >= 3) {
            // Variable (12) or list (13) reporter reference
            inp.inputType = type;
            inp.refName = val[1].get<std::string>();
            inp.refId   = val[2].get<std::string>();
        } else {
            // Literal: [type, value]
            inp.isBlock = false;
            if (val[1].is_number())
                inp.literal = val[1].get<double>();
            else
                inp.literal = val[1].get<std::string>();
        }
    }

    return inp;
}

BlockField ParseField(const json& j)
{
    BlockField f;
    if (!j.is_array() || j.empty()) return f;

    f.value = j[0].is_string() ? j[0].get<std::string>() : std::string{};
    if (j.size() > 1 && j[1].is_string())
        f.id = j[1].get<std::string>();

    return f;
}

ScratchCostume ParseCostume(const json& j)
{
    ScratchCostume c;
    c.name       = j.value("name", "");
    c.md5ext     = j.value("md5ext", "");
    c.dataFormat = j.value("dataFormat", "");
    c.rotationCenterX = j.value("rotationCenterX", 0.0);
    c.rotationCenterY = j.value("rotationCenterY", 0.0);
    c.bitmapResolution = j.value("bitmapResolution", 1);
    return c;
}

ScratchSound ParseSound(const json& j)
{
    ScratchSound s;
    s.name       = j.value("name", "");
    s.md5ext     = j.value("md5ext", "");
    s.dataFormat = j.value("dataFormat", "");
    return s;
}

ScratchTarget ParseTarget(const json& j)
{
    ScratchTarget t;
    t.isStage        = j.value("isStage", false);
    t.name           = j.value("name", "");
    t.x              = j.value("x", 0.0);
    t.y              = j.value("y", 0.0);
    t.size           = j.value("size", 100.0);
    t.direction      = j.value("direction", 90.0);
    t.visible        = j.value("visible", true);
    t.currentCostume = j.value("currentCostume", 0);
    t.layerOrder     = j.value("layerOrder", 0);
    t.rotationStyle  = j.value("rotationStyle", std::string("all around"));

    // ── Blocks ───────────────────────────────────────────────
    if (j.contains("blocks") && j["blocks"].is_object()) {
        for (auto& [id, bj] : j["blocks"].items()) {
            // Skip blocks that are stored as arrays (top-level shadow reporters)
            if (!bj.is_object()) continue;

            ScratchBlock block;
            block.id       = id;
            block.opcode   = bj.value("opcode", "");
            block.topLevel = bj.value("topLevel", false);

            if (bj.contains("next") && bj["next"].is_string())
                block.next = bj["next"].get<std::string>();

            if (bj.contains("parent") && bj["parent"].is_string())
                block.parent = bj["parent"].get<std::string>();

            // Inputs
            if (bj.contains("inputs") && bj["inputs"].is_object()) {
                for (auto& [key, iv] : bj["inputs"].items())
                    block.inputs[key] = ParseInput(iv);
            }

            // Fields
            if (bj.contains("fields") && bj["fields"].is_object()) {
                for (auto& [key, fv] : bj["fields"].items())
                    block.fields[key] = ParseField(fv);
            }

            // Mutation (for procedures)
            if (bj.contains("mutation") && bj["mutation"].is_object()) {
                auto& mut = bj["mutation"];
                if (mut.contains("proccode") && mut["proccode"].is_string())
                    block.proccode = mut["proccode"].get<std::string>();
                if (mut.contains("warp") && (mut["warp"].is_boolean() || mut["warp"].is_string())) {
                    if (mut["warp"].is_boolean()) block.warp = mut["warp"].get<bool>();
                    else block.warp = (mut["warp"].get<std::string>() == "true");
                }
                if (mut.contains("argumentids") && mut["argumentids"].is_string()) {
                    auto argIdsStr = mut["argumentids"].get<std::string>();
                    auto argIds = json::parse(argIdsStr);
                    if (argIds.is_array())
                        for (auto& a : argIds) block.argumentIds.push_back(a.get<std::string>());
                }
                if (mut.contains("argumentnames") && mut["argumentnames"].is_string()) {
                    auto argNamesStr = mut["argumentnames"].get<std::string>();
                    auto argNames = json::parse(argNamesStr);
                    if (argNames.is_array())
                        for (auto& a : argNames) block.argumentNames.push_back(a.get<std::string>());
                }
            }

            t.blocks[id] = std::move(block);
        }
    }

    // ── Costumes ─────────────────────────────────────────────
    if (j.contains("costumes") && j["costumes"].is_array()) {
        for (auto& cj : j["costumes"])
            t.costumes.push_back(ParseCostume(cj));
    }

    // ── Sounds ───────────────────────────────────────────────
    if (j.contains("sounds") && j["sounds"].is_array()) {
        for (auto& sj : j["sounds"])
            t.sounds.push_back(ParseSound(sj));
    }

    // ── Variables ────────────────────────────────────────────
    if (j.contains("variables") && j["variables"].is_object()) {
        for (auto& [vid, varj] : j["variables"].items()) {
            ScratchVariable v;
            if (varj.is_array() && varj.size() >= 2) {
                v.name = varj[0].get<std::string>();
                if (varj[1].is_number())
                    v.value = varj[1].get<double>();
                else
                    v.value = varj[1].get<std::string>();
            }
            t.variables[vid] = std::move(v);
        }
    }

    // ── Lists ────────────────────────────────────────────────
    if (j.contains("lists") && j["lists"].is_object()) {
        for (auto& [lid, lj] : j["lists"].items()) {
            ScratchList l;
            if (lj.is_array() && lj.size() >= 2) {
                l.name = lj[0].get<std::string>();
                if (lj[1].is_array()) {
                    for (auto& item : lj[1]) {
                        if (item.is_number())
                            l.items.emplace_back(item.get<double>());
                        else
                            l.items.emplace_back(item.get<std::string>());
                    }
                }
            }
            t.lists[lid] = std::move(l);
        }
    }

    return t;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────
// SB3Parser
// ─────────────────────────────────────────────────────────────
SB3Parser::SB3Parser(StatusCallback status)
    : status_(std::move(status))
{}

ScratchProject SB3Parser::Parse(const std::filesystem::path& sb3Path)
{
    status_("Extrayendo archivo .sb3...", 5);

    auto zipContents = ExtractZip(sb3Path);

    // Locate project.json
    auto it = zipContents.find("project.json");
    if (it == zipContents.end())
        throw std::runtime_error("project.json no encontrado en el .sb3");

    status_("Parseando project.json...", 15);

    std::string jsonStr(it->second.begin(), it->second.end());
    auto doc = json::parse(jsonStr);

    ScratchProject project;

    // ── Parse targets ────────────────────────────────────────
    if (doc.contains("targets") && doc["targets"].is_array()) {
        for (auto& tj : doc["targets"]) {
            project.targets.push_back(ParseTarget(tj));
        }
    }

    // ── Store assets (images, sounds) ────────────────────────
    status_("Extrayendo assets...", 25);
    for (auto& [filename, data] : zipContents) {
        if (filename == "project.json") continue;
        project.assets[filename] = std::move(data);
    }

    status_("Proyecto parseado correctamente.", 30);
    return project;
}

} // namespace sc
