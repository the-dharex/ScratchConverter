#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace sc {

// ─────────────────────────────────────────────────────────────
// Scratch value types – Scratch is dynamically typed
// ─────────────────────────────────────────────────────────────
using ScratchLiteral = std::variant<double, std::string>;

// ─────────────────────────────────────────────────────────────
// Input / Field helpers
// ─────────────────────────────────────────────────────────────
struct BlockInput {
    // An input is either a literal value or a reference to another block.
    bool          isBlock{false};
    std::string   blockId;       // populated when isBlock == true
    ScratchLiteral literal{0.0}; // populated when isBlock == false
    int           inputType{0};  // 0=literal, 12=variable ref, 13=list ref
    std::string   refName;       // variable/list name (for type 12/13)
    std::string   refId;         // variable/list id   (for type 12/13)
};

struct BlockField {
    std::string value;
    std::string id; // optional secondary id (e.g. variable id)
};

// ─────────────────────────────────────────────────────────────
// Scratch block
// ─────────────────────────────────────────────────────────────
struct ScratchBlock {
    std::string id;
    std::string opcode;
    std::string next;        // id of next block (empty if none)
    std::string parent;      // id of parent block (empty if top-level)
    bool        topLevel{false};

    std::unordered_map<std::string, BlockInput> inputs;
    std::unordered_map<std::string, BlockField> fields;

    // Mutation data (for procedures_prototype / procedures_call)
    std::string proccode;
    std::vector<std::string> argumentIds;
    std::vector<std::string> argumentNames;
    bool warp{false};
};

// ─────────────────────────────────────────────────────────────
// Costume & Sound
// ─────────────────────────────────────────────────────────────
struct ScratchCostume {
    std::string name;
    std::string md5ext;       // e.g. "abc123.png"
    std::string dataFormat;   // "png", "svg", ...
    double      rotationCenterX{0};
    double      rotationCenterY{0};
    int         bitmapResolution{1};
};

struct ScratchSound {
    std::string name;
    std::string md5ext;
    std::string dataFormat;  // "wav", "mp3"
};

// ─────────────────────────────────────────────────────────────
// Variable / List
// ─────────────────────────────────────────────────────────────
struct ScratchVariable {
    std::string    name;
    ScratchLiteral value; // default value
};

struct ScratchList {
    std::string                name;
    std::vector<ScratchLiteral> items;
};

// ─────────────────────────────────────────────────────────────
// Target (Sprite or Stage)
// ─────────────────────────────────────────────────────────────
struct ScratchTarget {
    bool        isStage{false};
    std::string name;

    double x{0}, y{0};
    double size{100};
    double direction{90};
    bool   visible{true};
    int    currentCostume{0};
    int    layerOrder{0};
    std::string rotationStyle{"all around"};

    std::unordered_map<std::string, ScratchBlock>    blocks;
    std::vector<ScratchCostume>                      costumes;
    std::vector<ScratchSound>                        sounds;
    std::unordered_map<std::string, ScratchVariable> variables; // key = var-id
    std::unordered_map<std::string, ScratchList>     lists;
};

// ─────────────────────────────────────────────────────────────
// Full project
// ─────────────────────────────────────────────────────────────
struct ScratchProject {
    std::vector<ScratchTarget> targets;     // index 0 is usually the Stage
    std::unordered_map<std::string, std::vector<uint8_t>> assets; // md5ext → data
};

} // namespace sc
