#pragma once
// ═══════════════════════════════════════════════════════════════
// ScratchRuntime – Complete SFML-based runtime for Scratch games
// Auto-copied by ScratchConverter.  Do not edit manually.
// ═══════════════════════════════════════════════════════════════

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <deque>
#include <filesystem>
#include <functional>
#include <list>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace scratch {

// ─────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────
class Runtime;
class Sprite;
class Script;

// ─────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────
inline constexpr float  kStageW = 480.f;
inline constexpr float  kStageH = 360.f;
inline constexpr double kPi     = 3.14159265358979323846;

// ─────────────────────────────────────────────────────────────
// Utility
// ─────────────────────────────────────────────────────────────
inline double toNum(const std::string& s) {
    try { return std::stod(s); } catch (...) { return 0.0; }
}
inline double toNum(const char* s) { return toNum(std::string(s)); }
inline double toNum(double d) { return d; }
inline double toNum(int d) { return static_cast<double>(d); }
inline std::string toStr(double d) {
    if (d == std::floor(d) && std::abs(d) < 1e15)
        return std::to_string(static_cast<long long>(d));
    std::ostringstream o; o << d; return o.str();
}

// ─────────────────────────────────────────────────────────────
// ScratchVal — dynamically-typed Scratch value (string or number)
// ─────────────────────────────────────────────────────────────
struct ScratchVal {
    std::variant<double, std::string> v{0.0};

    ScratchVal() = default;
    ScratchVal(double d) : v(d) {}
    ScratchVal(int d) : v(static_cast<double>(d)) {}
    ScratchVal(const std::string& s) : v(s) {}
    ScratchVal(const char* s) : v(std::string(s)) {}

    operator double() const {
        if (auto* d = std::get_if<double>(&v)) return *d;
        return toNum(std::get<std::string>(v));
    }

    bool isString() const { return std::holds_alternative<std::string>(v); }
    std::string asString() const {
        if (auto* s = std::get_if<std::string>(&v)) return *s;
        return toStr(std::get<double>(v));
    }
};

inline double toNum(const ScratchVal& val)    { return static_cast<double>(val); }
inline std::string toStr(const ScratchVal& val) { return val.asString(); }

inline double degToRad(double deg) { return deg * kPi / 180.0; }
inline double radToDeg(double rad) { return rad * 180.0 / kPi; }

// Scratch-style mod: always matches sign of divisor (non-negative for positive b)
inline double scratchMod(double a, double b) {
    if (b == 0) return 0;
    double r = std::fmod(a, b);
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

// Scratch-style comparison helpers (numeric if both parse, else case-insensitive string)
inline bool isNumeric(const std::string& s) {
    if (s.empty()) return false;
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

inline bool scratchEquals(const std::string& a, const std::string& b) {
    if (isNumeric(a) && isNumeric(b))
        return std::abs(std::stod(a) - std::stod(b)) < 1e-6;
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

inline bool scratchGt(const std::string& a, const std::string& b) {
    if (isNumeric(a) && isNumeric(b))
        return std::stod(a) > std::stod(b);
    return a > b;
}

inline bool scratchLt(const std::string& a, const std::string& b) {
    if (isNumeric(a) && isNumeric(b))
        return std::stod(a) < std::stod(b);
    return a < b;
}

// ─────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────
enum class RotationStyle { AllAround, LeftRight, DontRotate };

// ─────────────────────────────────────────────────────────────
// Graphic effects
// ─────────────────────────────────────────────────────────────
struct GraphicEffects {
    double color      = 0;
    double fisheye    = 0;
    double whirl      = 0;
    double pixelate   = 0;
    double mosaic     = 0;
    double brightness = 0;
    double ghost      = 0;

    void clear() { color = fisheye = whirl = pixelate = mosaic = brightness = ghost = 0; }

    static std::string lower(const std::string& s) {
        std::string r = s;
        for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return r;
    }

    double get(const std::string& n) const {
        auto k = lower(n);
        if (k == "color")      return color;
        if (k == "fisheye")    return fisheye;
        if (k == "whirl")      return whirl;
        if (k == "pixelate")   return pixelate;
        if (k == "mosaic")     return mosaic;
        if (k == "brightness") return brightness;
        if (k == "ghost")      return ghost;
        return 0;
    }
    void set(const std::string& n, double v) {
        auto k = lower(n);
        if (k == "color")      color = v;
        else if (k == "fisheye")    fisheye = v;
        else if (k == "whirl")      whirl = v;
        else if (k == "pixelate")   pixelate = v;
        else if (k == "mosaic")     mosaic = v;
        else if (k == "brightness") brightness = std::clamp(v, -100.0, 100.0);
        else if (k == "ghost")      ghost = std::clamp(v, 0.0, 100.0);
    }
    void change(const std::string& n, double d) { set(n, get(n) + d); }
};

// ─────────────────────────────────────────────────────────────
// Pen state
// ─────────────────────────────────────────────────────────────
struct PenState {
    bool     down         = false;
    sf::Color color       = {66, 66, 255, 255};
    double   size         = 1.0;
    double   hue          = 240.0;
    double   saturation   = 100.0;
    double   brightnessP  = 100.0;
    double   transparency = 0.0;
};

// ─────────────────────────────────────────────────────────────
// Costume / RuntimeConfig / Script
// ─────────────────────────────────────────────────────────────
struct Costume {
    std::string  name;
    sf::Texture  texture;
    sf::Vector2f center;
    int          bitmapResolution{1};
    mutable sf::Image imageCache;
    mutable bool      imageCached{false};
};

struct RuntimeConfig {
    std::string title{"Scratch Game"};
    int  width      = 960;
    int  height     = 720;
    bool fullscreen = false;
};

class Script {
public:
    virtual ~Script() = default;
    virtual bool step(float dt) = 0;
    virtual void reset() = 0;
    virtual std::unique_ptr<Script> clone(Runtime& rt, Sprite& sp) const { return nullptr; }
    virtual int activeSoundId() const { return -1; }

    Sprite* owner() const { return owner_; }
    void setOwner(Sprite* sp) { owner_ = sp; }

    int broadcastWaitId() const { return broadcastWaitId_; }
    void setBroadcastWaitId(int id) { broadcastWaitId_ = id; }
private:
    Sprite* owner_ = nullptr;
    int broadcastWaitId_ = 0;
};

using ScriptFactory = std::function<std::unique_ptr<Script>(Runtime&, Sprite&)>;

// ─────────────────────────────────────────────────────────────
// ScratchList
// ─────────────────────────────────────────────────────────────
class ScratchList {
public:
    void add(double v) { items_.push_back(v); }
    void addStr(const std::string& v) { strItems_[items_.size()] = v; items_.push_back(0); }

    void deleteAt(int idx) {
        if (idx == 0) { items_.clear(); strItems_.clear(); return; } // "all"
        int i = idx - 1;
        if (i >= 0 && i < static_cast<int>(items_.size())) {
            items_.erase(items_.begin() + i);
            rebuildStrMap(i);
        }
    }
    void deleteAll() { items_.clear(); strItems_.clear(); }

    void insertAt(int idx, double v) {
        int i = idx - 1;
        if (i < 0) i = 0;
        if (i > static_cast<int>(items_.size())) i = static_cast<int>(items_.size());
        items_.insert(items_.begin() + i, v);
        // Shift string keys at or after i
        std::unordered_map<int, std::string> newMap;
        for (auto& [k, sv] : strItems_) {
            if (k >= i) newMap[k + 1] = sv;
            else newMap[k] = sv;
        }
        strItems_ = std::move(newMap);
    }

    void insertAtStr(int idx, const std::string& v) {
        int i = idx - 1;
        if (i < 0) i = 0;
        if (i > static_cast<int>(items_.size())) i = static_cast<int>(items_.size());
        items_.insert(items_.begin() + i, 0);
        std::unordered_map<int, std::string> newMap;
        for (auto& [k, sv] : strItems_) {
            if (k >= i) newMap[k + 1] = sv;
            else newMap[k] = sv;
        }
        newMap[i] = v;
        strItems_ = std::move(newMap);
    }

    void replaceAt(int idx, double v) {
        int i = idx - 1;
        if (i >= 0 && i < static_cast<int>(items_.size())) {
            items_[i] = v;
            strItems_.erase(i);
        }
    }

    void replaceAtStr(int idx, const std::string& v) {
        int i = idx - 1;
        if (i >= 0 && i < static_cast<int>(items_.size())) {
            items_[i] = 0;
            strItems_[i] = v;
        }
    }

    double itemAt(int idx) const {
        int i = idx - 1;
        if (i >= 0 && i < static_cast<int>(items_.size())) return items_[i];
        return 0;
    }

    std::string itemStrAt(int idx) const {
        int i = idx - 1;
        if (i >= 0 && i < static_cast<int>(items_.size())) {
            auto it = strItems_.find(i);
            if (it != strItems_.end()) return it->second;
            return toStr(items_[i]);
        }
        return "";
    }

    int length() const { return static_cast<int>(items_.size()); }

    bool contains(double v) const {
        return std::find(items_.begin(), items_.end(), v) != items_.end();
    }
    bool containsStr(const std::string& v) const {
        for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
            auto it = strItems_.find(i);
            std::string s = it != strItems_.end() ? it->second : toStr(items_[i]);
            if (s == v) return true;
        }
        return false;
    }

    int itemNum(double v) const {
        for (int i = 0; i < static_cast<int>(items_.size()); ++i)
            if (items_[i] == v) return i + 1;
        return 0;
    }

    int itemNumStr(const std::string& v) const {
        for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
            auto it = strItems_.find(i);
            std::string s = it != strItems_.end() ? it->second : toStr(items_[i]);
            if (s == v) return i + 1;
        }
        return 0;
    }

    // Helper for monitors: get display string of all items
    std::string joinedDisplay(const std::string& sep = " ") const {
        std::string out;
        for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
            if (i > 0) out += sep;
            auto it = strItems_.find(i);
            out += (it != strItems_.end()) ? it->second : toStr(items_[i]);
        }
        return out;
    }

private:
    void rebuildStrMap(int removedIdx) {
        std::unordered_map<int, std::string> newMap;
        for (auto& [k, v] : strItems_) {
            if (k < removedIdx) newMap[k] = v;
            else if (k > removedIdx) newMap[k - 1] = v;
        }
        strItems_ = std::move(newMap);
    }

    std::vector<double> items_;
    std::unordered_map<int, std::string> strItems_;
};

// ═════════════════════════════════════════════════════════════
// Sprite
// ═════════════════════════════════════════════════════════════
class Sprite {
public:
    explicit Sprite(const std::string& name, Runtime& rt)
        : name_(name), rt_(rt) {}

    // ── Costume management ───────────────────────────────────
    void addCostume(const std::string& name, const std::string& path,
                    double cx = 0, double cy = 0, int bitmapRes = 1)
    {
        costumes_.push_back({name, {}, {static_cast<float>(cx), static_cast<float>(cy)}, bitmapRes});
        costumes_.back().texture.loadFromFile(path);
        costumes_.back().texture.setSmooth(false);
        if (costumes_.size() == 1) applyCostume();
    }

    void setCostume(int index) {
        if (index >= 0 && index < static_cast<int>(costumes_.size())) {
            costumeIdx_ = index;
            applyCostume();
        }
    }
    void setCostumeByName(const std::string& n) {
        for (int i = 0; i < static_cast<int>(costumes_.size()); ++i)
            if (costumes_[i].name == n) { setCostume(i); return; }
    }
    void nextCostume() {
        if (!costumes_.empty())
            setCostume((costumeIdx_ + 1) % static_cast<int>(costumes_.size()));
    }
    int costumeIndex() const { return costumeIdx_ + 1; } // Scratch is 1-based
    std::string costumeName() const {
        if (costumeIdx_ >= 0 && costumeIdx_ < static_cast<int>(costumes_.size()))
            return costumes_[costumeIdx_].name;
        return "";
    }
    int costumeCount() const { return static_cast<int>(costumes_.size()); }

    // ── Backdrop (stage-only aliases) ────────────────────────
    void switchBackdropTo(const std::string& n) { setCostumeByName(n); }
    void nextBackdrop() { nextCostume(); }
    int backdropNumber() const { return costumeIndex(); }
    std::string backdropName() const { return costumeName(); }

    // ── Position (Scratch coordinates) ───────────────────────
    double x() const { return x_; }
    double y() const { return y_; }
    void setPosition(double sx, double sy) { x_ = sx; y_ = sy; }
    void setX(double sx) { x_ = sx; }
    void setY(double sy) { y_ = sy; }
    void changeX(double d) { x_ += d; }
    void changeY(double d) { y_ += d; }

    // ── Direction ────────────────────────────────────────────
    double direction() const { return dir_; }
    void setDirection(double d) {
        dir_ = d;
        while (dir_ >  180.0) dir_ -= 360.0;
        while (dir_ <= -180.0) dir_ += 360.0;
    }
    void turnRight(double deg) { setDirection(dir_ + deg); }
    void turnLeft(double deg)  { setDirection(dir_ - deg); }

    // ── Rotation style ───────────────────────────────────────
    void setRotationStyle(RotationStyle rs) { rotStyle_ = rs; }
    void setRotationStyle(const std::string& s) {
        if (s == "all around")   rotStyle_ = RotationStyle::AllAround;
        else if (s == "left-right")  rotStyle_ = RotationStyle::LeftRight;
        else if (s == "don't rotate") rotStyle_ = RotationStyle::DontRotate;
    }

    // ── Motion ───────────────────────────────────────────────
    void moveSteps(double steps) {
        double rad = degToRad(dir_);
        x_ += steps * std::sin(rad);
        y_ += steps * std::cos(rad);
    }

    void gotoXY(double tx, double ty) { x_ = tx; y_ = ty; }

    // goto sprite / mouse / random – implemented after Runtime is defined
    void gotoTarget(const std::string& target);
    void pointTowards(const std::string& target);

    void ifOnEdgeBounce() {
        if (costumes_.empty()) return;
        auto& tex = costumes_[costumeIdx_].texture;
        float scl = static_cast<float>(size_ / 100.0);
        float hw = tex.getSize().x * scl / 2.f;
        float hh = tex.getSize().y * scl / 2.f;
        float right = kStageW / 2.f, left = -right;
        float top = kStageH / 2.f, bottom = -top;
        bool bounced = false;
        if (x_ + hw > right)  { x_ = right - hw;  setDirection(-dir_);      bounced = true; }
        if (x_ - hw < left)   { x_ = left + hw;   setDirection(-dir_);      bounced = true; }
        if (y_ + hh > top)    { y_ = top - hh;    setDirection(180.0 - dir_); if (!bounced) bounced = true; }
        if (y_ - hh < bottom) { y_ = bottom + hh; setDirection(180.0 - dir_); }
    }

    // Glide support
    void startGlide(double tx, double ty, double secs) {
        glideFromX_ = x_; glideFromY_ = y_;
        glideToX_ = tx;   glideToY_ = ty;
        glideDur_ = secs;  glideElapsed_ = 0;
    }
    void updateGlide(float dt) {
        glideElapsed_ += dt;
        double t = std::clamp(glideElapsed_ / glideDur_, 0.0, 1.0);
        x_ = glideFromX_ + (glideToX_ - glideFromX_) * t;
        y_ = glideFromY_ + (glideToY_ - glideFromY_) * t;
    }
    void finishGlide() { x_ = glideToX_; y_ = glideToY_; }

    // ── Looks ────────────────────────────────────────────────
    double size() const { return size_; }
    void setSize(double s) { size_ = std::max(0.0, s); }
    void changeSize(double d) { setSize(size_ + d); }

    bool visible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }

    void say(const std::string& msg) { sayText_ = msg; sayType_ = 0; }
    void think(const std::string& msg) { sayText_ = msg; sayType_ = 1; }
    void clearSayThink() { sayText_.clear(); }
    const std::string& sayText() const { return sayText_; }
    int sayType() const { return sayType_; } // 0=say, 1=think

    // ── Graphic effects ──────────────────────────────────────
    void setEffect(const std::string& name, double val)    { effects_.set(name, val); }
    void changeEffect(const std::string& name, double d)   { effects_.change(name, d); }
    void clearGraphicEffects()                              { effects_.clear(); }
    double getEffect(const std::string& name) const        { return effects_.get(name); }
    const GraphicEffects& effects() const                   { return effects_; }

    // ── Layer ordering ───────────────────────────────────────
    int  layerOrder() const { return layerOrder_; }
    void setLayerOrder(int o) { layerOrder_ = o; }
    void goToFront();   // impl after Runtime
    void goToBack();
    void goForwardLayers(int n);
    void goBackwardLayers(int n);

    // ── Pen ──────────────────────────────────────────────────
    PenState& pen() { return pen_; }
    const PenState& pen() const { return pen_; }
    void penDown() { pen_.down = true; }
    void penUp()   { pen_.down = false; }
    bool isPenDown() const { return pen_.down; }

    void setPenColor(int r, int g, int b, int a = 255) {
        pen_.color = sf::Color(r, g, b, a);
    }
    void setPenColorHex(int hex) {
        pen_.color = sf::Color((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
    }
    void setPenSize(double s) { pen_.size = std::max(0.0, s); }
    void changePenSize(double d) { pen_.size = std::max(0.0, pen_.size + d); }

    void setPenParam(const std::string& param, double val) {
        if (param == "color") {
            pen_.hue = std::fmod(val, 100.0) / 100.0 * 360.0;
            updatePenColorFromHSB();
        } else if (param == "saturation") {
            pen_.saturation = std::clamp(val, 0.0, 100.0);
            updatePenColorFromHSB();
        } else if (param == "brightness") {
            pen_.brightnessP = std::clamp(val, 0.0, 100.0);
            updatePenColorFromHSB();
        } else if (param == "transparency") {
            pen_.transparency = std::clamp(val, 0.0, 100.0);
            pen_.color.a = static_cast<sf::Uint8>((1.0 - pen_.transparency / 100.0) * 255);
        }
    }
    void changePenParam(const std::string& param, double d) {
        if (param == "color") setPenParam("color", pen_.hue / 360.0 * 100.0 + d);
        else if (param == "saturation") setPenParam("saturation", pen_.saturation + d);
        else if (param == "brightness") setPenParam("brightness", pen_.brightnessP + d);
        else if (param == "transparency") setPenParam("transparency", pen_.transparency + d);
    }

    // ── Clone info ───────────────────────────────────────────
    bool isClone() const { return isClone_; }
    void setIsClone(bool v) { isClone_ = v; }
    bool isStage() const { return isStage_; }
    void setIsStage(bool v) { isStage_ = v; }

    // ── Per-sprite ("for this sprite only") variables ────────
    double getLocalVar(const std::string& name) const {
        auto it = localVarStrs_.find(name);
        if (it != localVarStrs_.end()) {
            char* end = nullptr;
            double d = std::strtod(it->second.c_str(), &end);
            if (end != it->second.c_str()) return d;
            return 0;
        }
        return 0;
    }
    std::string getLocalVarStr(const std::string& name) const {
        auto it = localVarStrs_.find(name);
        return it != localVarStrs_.end() ? it->second : "";
    }
    void setLocalVar(const std::string& name, double v) {
        localVarStrs_[name] = scratch::toStr(v);
    }
    void setLocalVarStr(const std::string& name, const std::string& v) {
        localVarStrs_[name] = v;
    }
    void changeLocalVar(const std::string& name, double delta) {
        setLocalVar(name, getLocalVar(name) + delta);
    }
    void showLocalVar(const std::string& name) { /* monitor stub */ }
    void hideLocalVar(const std::string& name) { /* monitor stub */ }

    // Per-sprite lists
    ScratchList& getLocalList(const std::string& n) { return localLists_[n]; }

    // ── Collision ────────────────────────────────────────────
    bool touching(const std::string& targetName) const;
    bool isDraggable() const { return draggable_; }
    void setDraggable(bool v) { draggable_ = v; }
    bool isDragging() const { return dragging_; }
    void setDragging(bool v) { dragging_ = v; }
    double distanceTo(const std::string& target) const;

    // Pixel-perfect collision helpers
    const sf::Image& costumeImage() const {
        auto& c = costumes_[costumeIdx_];
        if (!c.imageCached) {
            c.imageCache = c.texture.copyToImage();
            c.imageCached = true;
        }
        return c.imageCache;
    }

    // Check if this sprite has a non-transparent pixel at stage-space (480x360) position
    bool isOpaqueAt(float worldX, float worldY) const {
        if (costumes_.empty() || !visible_) return false;
        auto& c = costumes_[costumeIdx_];
        float bmpRes = static_cast<float>(c.bitmapResolution);
        float scl = static_cast<float>(size_ / 100.0) / bmpRes;
        if (scl < 1e-6f) return false;

        auto sfPos = scratchToSfml(x_, y_);
        float dx = worldX - sfPos.x;
        float dy = worldY - sfPos.y;

        float sclX = scl, sclY = scl;
        float angle = 0;
        switch (rotStyle_) {
        case RotationStyle::AllAround:
            angle = static_cast<float>((dir_ - 90.0) * 3.14159265358979 / 180.0);
            break;
        case RotationStyle::LeftRight:
            if (dir_ < 0) sclX = -scl;
            break;
        case RotationStyle::DontRotate:
            break;
        }

        // Un-rotate
        float cosA = std::cos(-angle);
        float sinA = std::sin(-angle);
        float rx = dx * cosA - dy * sinA;
        float ry = dx * sinA + dy * cosA;

        // Un-scale and add origin
        float tx = rx / sclX + c.center.x;
        float ty = ry / sclY + c.center.y;

        int ix = static_cast<int>(std::floor(tx));
        int iy = static_cast<int>(std::floor(ty));

        auto texSize = c.texture.getSize();
        if (ix < 0 || iy < 0 || ix >= static_cast<int>(texSize.x) || iy >= static_cast<int>(texSize.y))
            return false;

        return costumeImage().getPixel(ix, iy).a > 0;
    }

    // Pixel-perfect collision between two sprites
    bool pixelTouching(const Sprite& other) const {
        auto myBounds = bounds();
        auto otherBounds = other.bounds();
        if (!myBounds.intersects(otherBounds)) return false;

        // Compute overlap region in stage space
        float left   = std::max(myBounds.left, otherBounds.left);
        float top    = std::max(myBounds.top, otherBounds.top);
        float right  = std::min(myBounds.left + myBounds.width,
                                otherBounds.left + otherBounds.width);
        float bottom = std::min(myBounds.top + myBounds.height,
                                otherBounds.top + otherBounds.height);
        if (left >= right || top >= bottom) return false;

        // Step size: use the larger of the two effective pixel sizes
        // to avoid redundant checks on the same texel
        auto pixelSize = [](const Sprite& s) -> float {
            if (s.costumes_.empty()) return 1.f;
            float br = static_cast<float>(s.costumes_[s.costumeIdx_].bitmapResolution);
            return static_cast<float>(s.size_ / 100.0) / br;
        };
        float step = std::max(1.f, std::min(pixelSize(*this), pixelSize(other)) * 0.5f);

        for (float wy = top; wy < bottom; wy += step) {
            for (float wx = left; wx < right; wx += step) {
                if (isOpaqueAt(wx, wy) && other.isOpaqueAt(wx, wy))
                    return true;
            }
        }
        return false;
    }

    sf::FloatRect bounds() const {
        if (costumes_.empty()) return {0, 0, 0, 0};
        auto& tex = costumes_[costumeIdx_].texture;
        float bmpRes = static_cast<float>(costumes_[costumeIdx_].bitmapResolution);
        float scl = static_cast<float>(size_ / 100.0) / bmpRes;
        float w = tex.getSize().x * scl;
        float h = tex.getSize().y * scl;
        auto sfPos = scratchToSfml(x_, y_);
        return {sfPos.x - w / 2.f, sfPos.y - h / 2.f, w, h};
    }

    // ── Rendering ────────────────────────────────────────────
    void draw(sf::RenderTarget& target, float scaleX, float scaleY,
              const sf::Font* font = nullptr) const
    {
        if (!visible_ || costumes_.empty()) return;

        sf::Sprite spr;
        spr.setTexture(costumes_[costumeIdx_].texture);

        // Stage backdrop: draw with costume center at stage center, 1:1 / bitmapRes
        if (isStage_) {
            auto& center = costumes_[costumeIdx_].center;
            float bmpRes = static_cast<float>(costumes_[costumeIdx_].bitmapResolution);
            float scl = 1.0f / bmpRes;
            spr.setOrigin(center.x, center.y);
            spr.setScale(scl * scaleX, scl * scaleY);
            spr.setPosition((kStageW / 2.f) * scaleX, (kStageH / 2.f) * scaleY);
        } else {
            auto& center = costumes_[costumeIdx_].center;
            spr.setOrigin(center.x, center.y);

            auto sfPos = scratchToSfml(x_, y_);
            spr.setPosition(sfPos.x * scaleX, sfPos.y * scaleY);

            float bmpRes = static_cast<float>(costumes_[costumeIdx_].bitmapResolution);
            float scl = static_cast<float>(size_ / 100.0) / bmpRes;

            // Rotation style
            switch (rotStyle_) {
            case RotationStyle::AllAround:
                spr.setScale(scl * scaleX, scl * scaleY);
                spr.setRotation(static_cast<float>(dir_ - 90.0));
                break;
            case RotationStyle::LeftRight:
                spr.setRotation(0.f);
                if (dir_ < 0)
                    spr.setScale(-scl * scaleX, scl * scaleY);
                else
                    spr.setScale(scl * scaleX, scl * scaleY);
                break;
            case RotationStyle::DontRotate:
                spr.setScale(scl * scaleX, scl * scaleY);
                spr.setRotation(0.f);
                break;
            }
        }

        // Ghost effect → alpha
        sf::Uint8 alpha = static_cast<sf::Uint8>(std::clamp(255.0 * (1.0 - effects_.ghost / 100.0), 0.0, 255.0));
        // Brightness effect → color tint
        int bri = static_cast<int>(std::clamp(255.0 + effects_.brightness * 2.55, 0.0, 510.0));
        sf::Uint8 tint = static_cast<sf::Uint8>(std::min(bri, 255));
        spr.setColor(sf::Color(tint, tint, tint, alpha));

        // If color effect is non-zero, draw with hue-shift shader
        if (effects_.color != 0 && sf::Shader::isAvailable()) {
            static sf::Shader hueShader;
            static bool hueShaderLoaded = false;
            if (!hueShaderLoaded) {
                static const char* fragSrc = R"glsl(
                    uniform sampler2D texture;
                    uniform float hueShift;
                    vec3 rgb2hsv(vec3 c) {
                        vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
                        vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
                        vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
                        float d = q.x - min(q.w, q.y);
                        float e = 1.0e-10;
                        return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
                    }
                    vec3 hsv2rgb(vec3 c) {
                        vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
                        vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
                        return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
                    }
                    void main() {
                        vec4 pixel = texture2D(texture, gl_TexCoord[0].xy);
                        vec3 hsv = rgb2hsv(pixel.rgb);
                        hsv.x = fract(hsv.x + hueShift);
                        pixel.rgb = hsv2rgb(hsv);
                        gl_FragColor = pixel * gl_Color;
                    }
                )glsl";
                hueShaderLoaded = hueShader.loadFromMemory(fragSrc, sf::Shader::Fragment);
                if (hueShaderLoaded)
                    hueShader.setUniform("texture", sf::Shader::CurrentTexture);
            }
            if (hueShaderLoaded) {
                // Scratch color effect: 0-200 maps to 0-360 degrees = 0.0-1.0 hue shift
                float hueVal = static_cast<float>(std::fmod(effects_.color, 200.0));
                if (hueVal < 0) hueVal += 200.f;
                hueShader.setUniform("hueShift", hueVal / 200.f);
                target.draw(spr, &hueShader);
            } else {
                target.draw(spr);
            }
        } else {
            target.draw(spr);
        }

        // Say / Think bubble
        if (!sayText_.empty() && font) {
            drawBubble(target, scaleX, scaleY, *font);
        }
    }

    // ── Pen drawing on a render texture ──────────────────────
    void drawPenLine(sf::RenderTexture& penTex, double fromX, double fromY,
                     double toX, double toY) const
    {
        auto p1 = scratchToSfml(fromX, fromY);
        auto p2 = scratchToSfml(toX, toY);
        float thickness = static_cast<float>(pen_.size);

        float dx = p2.x - p1.x, dy = p2.y - p1.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f) {
            sf::CircleShape dot(thickness / 2.f);
            dot.setFillColor(pen_.color);
            dot.setOrigin(thickness / 2.f, thickness / 2.f);
            dot.setPosition(p1);
            penTex.draw(dot);
            return;
        }

        sf::RectangleShape line(sf::Vector2f(len, thickness));
        line.setFillColor(pen_.color);
        line.setOrigin(0, thickness / 2.f);
        line.setPosition(p1);
        line.setRotation(std::atan2(dy, dx) * 180.f / static_cast<float>(kPi));
        penTex.draw(line);

        // Round caps
        sf::CircleShape cap(thickness / 2.f);
        cap.setFillColor(pen_.color);
        cap.setOrigin(thickness / 2.f, thickness / 2.f);
        cap.setPosition(p1); penTex.draw(cap);
        cap.setPosition(p2); penTex.draw(cap);
    }

    void stamp(sf::RenderTexture& penTex) const {
        if (costumes_.empty()) return;
        sf::Sprite spr;
        spr.setTexture(costumes_[costumeIdx_].texture);
        auto& center = costumes_[costumeIdx_].center;
        spr.setOrigin(center.x, center.y);
        auto sfPos = scratchToSfml(x_, y_);
        spr.setPosition(sfPos);
        float scl = static_cast<float>(size_ / 100.0);
        spr.setScale(scl, scl);
        spr.setRotation(static_cast<float>(dir_ - 90.0));
        sf::Uint8 alpha = static_cast<sf::Uint8>(std::clamp(255.0 * (1.0 - effects_.ghost / 100.0), 0.0, 255.0));
        spr.setColor(sf::Color(255, 255, 255, alpha));
        penTex.draw(spr);
    }

    const std::string& name() const { return name_; }

    // ── Per-sprite sound properties ──────────────────────────
    void setVolume(double v);    // defined after Runtime
    void changeVolume(double d); // defined after Runtime
    double getVolume() const    { return sprVolume_; }

    void setSoundEffect(const std::string& effect, double val) {
        std::string e = effect;
        for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (e == "pitch")     sprPitch_ = val;
        else if (e == "pan")  sprPan_ = std::clamp(val, -100.0, 100.0);
    }
    void changeSoundEffect(const std::string& effect, double val) {
        std::string e = effect;
        for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (e == "pitch")     sprPitch_ += val;
        else if (e == "pan")  sprPan_ = std::clamp(sprPan_ + val, -100.0, 100.0);
    }
    void clearSoundEffects() { sprPitch_ = 0.0; sprPan_ = 0.0; }
    double soundPitch() const { return sprPitch_; }
    double soundPan()   const { return sprPan_; }

    // For clone creation – copy state
    void copyStateFrom(const Sprite& other) {
        costumes_     = other.costumes_;   // copies textures (shared GPU handles in SFML)
        costumeIdx_   = other.costumeIdx_;
        x_ = other.x_; y_ = other.y_;
        dir_ = other.dir_; size_ = other.size_;
        visible_ = other.visible_;
        layerOrder_ = other.layerOrder_;
        rotStyle_ = other.rotStyle_;
        effects_ = other.effects_;
        pen_ = other.pen_;
        sayText_.clear();
        isClone_ = true;
        sprVolume_ = other.sprVolume_;
        sprPitch_  = other.sprPitch_;
        sprPan_    = other.sprPan_;
        localVarStrs_ = other.localVarStrs_;
        localLists_   = other.localLists_;
    }

    // Previous position tracking for pen lines
    double prevX() const { return prevX_; }
    double prevY() const { return prevY_; }
    void savePrevPos() { prevX_ = x_; prevY_ = y_; }

private:
    sf::Vector2f scratchToSfml(double sx, double sy) const {
        return {static_cast<float>(sx + kStageW / 2.f),
                static_cast<float>(kStageH / 2.f - sy)};
    }

    void applyCostume() { /* draw() reads from costumes_[costumeIdx_] */ }

    void updatePenColorFromHSB() {
        double h = pen_.hue, s = pen_.saturation / 100.0, v = pen_.brightnessP / 100.0;
        double c = v * s, x = c * (1.0 - std::abs(std::fmod(h / 60.0, 2.0) - 1.0)), m = v - c;
        double r = 0, g = 0, b = 0;
        if (h < 60)       { r = c; g = x; }
        else if (h < 120) { r = x; g = c; }
        else if (h < 180) { g = c; b = x; }
        else if (h < 240) { g = x; b = c; }
        else if (h < 300) { r = x; b = c; }
        else              { r = c; b = x; }
        pen_.color.r = static_cast<sf::Uint8>((r + m) * 255);
        pen_.color.g = static_cast<sf::Uint8>((g + m) * 255);
        pen_.color.b = static_cast<sf::Uint8>((b + m) * 255);
        pen_.color.a = static_cast<sf::Uint8>((1.0 - pen_.transparency / 100.0) * 255);
    }

    void drawBubble(sf::RenderTarget& target, float scaleX, float scaleY,
                    const sf::Font& font) const
    {
        sf::Text text;
        text.setFont(font);
        text.setString(sayText_);
        text.setCharacterSize(static_cast<unsigned>(12 * scaleY));
        text.setFillColor(sf::Color::Black);

        auto tb = text.getLocalBounds();
        float pad = 6.f * scaleX;
        float bw = tb.width + pad * 2, bh = tb.height + pad * 2;

        auto sfPos = scratchToSfml(x_, y_);
        float bx = sfPos.x * scaleX - bw / 2.f;
        float by = sfPos.y * scaleY - (bounds().height * scaleY / 2.f) - bh - 4.f * scaleY;

        sf::RectangleShape bg(sf::Vector2f(bw, bh));
        bg.setPosition(bx, by);
        bg.setFillColor(sf::Color::White);
        bg.setOutlineColor(sf::Color(180, 180, 180));
        bg.setOutlineThickness(1.f);
        target.draw(bg);

        text.setPosition(bx + pad, by + pad / 2.f);
        target.draw(text);
    }

    Runtime&              rt_;
    std::string           name_;
    std::vector<Costume>  costumes_;
    int                   costumeIdx_ = 0;

    double x_ = 0, y_ = 0;
    double prevX_ = 0, prevY_ = 0;
    double dir_  = 90;
    double size_ = 100;
    bool   visible_ = true;
    int    layerOrder_ = 0;

    RotationStyle   rotStyle_ = RotationStyle::AllAround;
    GraphicEffects  effects_;
    PenState        pen_;

    std::string sayText_;
    int         sayType_ = 0; // 0=say, 1=think

    bool isClone_ = false;
    bool isStage_ = false;
    bool draggable_ = false;
    bool dragging_  = false;

    // Glide state
    double glideFromX_ = 0, glideFromY_ = 0;
    double glideToX_   = 0, glideToY_   = 0;
    double glideDur_   = 0, glideElapsed_ = 0;

    // Per-sprite sound properties (Scratch: per-sprite)
    double sprVolume_ = 100.0;
    double sprPitch_  = 0.0;   // Scratch PITCH effect
    double sprPan_    = 0.0;   // Scratch PAN effect (-100..100)

    // Per-sprite ("for this sprite only") variables & lists
    std::unordered_map<std::string, std::string> localVarStrs_;
    mutable std::unordered_map<std::string, ScratchList> localLists_;
};

// ─────────────────────────────────────────────────────────────
// Event entry types
// ─────────────────────────────────────────────────────────────
struct GreenFlagEntry     { std::unique_ptr<Script> script; };
struct KeyPressedEntry    { std::string key; std::unique_ptr<Script> script; };
struct SpriteClickedEntry { std::string spriteName; std::unique_ptr<Script> script; };
struct BroadcastEntry     { std::string message; std::unique_ptr<Script> script; };
struct BackdropEntry      { std::string backdrop; std::unique_ptr<Script> script; };
struct CloneStartEntry    { std::string spriteName; ScriptFactory factory; };
struct LoudnessEntry      { double threshold; std::unique_ptr<Script> script; };
struct StageClickedEntry  { std::unique_ptr<Script> script; };
struct GreaterThanEntry   { std::string sensor; double threshold; std::unique_ptr<Script> script; bool wasAbove = false; };

// ─────────────────────────────────────────────────────────────
// Microphone loudness recorder (lazy-started)
// ─────────────────────────────────────────────────────────────
class LoudnessRecorder : public sf::SoundRecorder {
public:
    LoudnessRecorder() { setProcessingInterval(sf::milliseconds(50)); }
    double getLoudness() const { return loudness_.load(); }
protected:
    bool onStart() override { return true; }
    bool onProcessSamples(const sf::Int16* samples, std::size_t count) override {
        if (count == 0) return true;
        double sumSq = 0;
        for (std::size_t i = 0; i < count; ++i) {
            double s = samples[i] / 32768.0;
            sumSq += s * s;
        }
        double rms = std::sqrt(sumSq / static_cast<double>(count));
        // Map RMS 0..1 to Scratch 0..100 (roughly matching Scratch's scaling)
        loudness_.store(std::min(rms * 300.0, 100.0));
        return true;
    }
    void onStop() override {}
private:
    std::atomic<double> loudness_{0};
};

// ═════════════════════════════════════════════════════════════
// Runtime
// ═════════════════════════════════════════════════════════════
class Runtime {
public:
    explicit Runtime(const RuntimeConfig& cfg)
        : cfg_(cfg), stage_("Stage", *this)
    {
#ifdef _WIN32
        // Set working directory to the executable's folder
        // so relative asset paths resolve correctly.
        {
            wchar_t buf[MAX_PATH]{};
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            auto exeDir = std::filesystem::path(buf).parent_path();
            if (!exeDir.empty())
                std::filesystem::current_path(exeDir);
        }
#endif
        rng_.seed(std::random_device{}());
        timerStart_ = std::chrono::steady_clock::now();
        stage_.setIsStage(true);
    }

    // ── Sprite management ────────────────────────────────────
    Sprite& addSprite(const std::string& name) {
        sprites_.push_back(std::make_unique<Sprite>(name, *this));
        return *sprites_.back();
    }
    Sprite& stage()     { return stage_; }
    Sprite* findSprite(const std::string& name) {
        for (auto& sp : sprites_) if (sp->name() == name) return sp.get();
        for (auto& cl : clones_)  if (cl->name() == name) return cl.get();
        return nullptr;
    }
    const std::vector<std::unique_ptr<Sprite>>& sprites() const { return sprites_; }
    const std::vector<std::unique_ptr<Sprite>>& clones() const { return clones_; }

    // ── Script registration ──────────────────────────────────
    void onGreenFlag(std::unique_ptr<Script> s)                           { greenFlagScripts_.push_back({std::move(s)}); }
    void onKeyPressed(const std::string& key, std::unique_ptr<Script> s)  { keyScripts_.push_back({key, std::move(s)}); }
    void onSpriteClicked(const std::string& n, std::unique_ptr<Script> s) { clickScripts_.push_back({n, std::move(s)}); }
    void onBroadcast(const std::string& msg, std::unique_ptr<Script> s)   { broadcastScripts_.push_back({msg, std::move(s)}); }
    void onBackdropSwitch(const std::string& bd, std::unique_ptr<Script> s) { backdropScripts_.push_back({bd, std::move(s)}); }
    void onCloneStart(const std::string& spName, ScriptFactory factory)     { cloneStartScripts_.push_back({spName, std::move(factory)}); }
    void onStageClicked(std::unique_ptr<Script> s)                          { stageClickScripts_.push_back({std::move(s)}); }
    void onGreaterThan(const std::string& sensor, double threshold, std::unique_ptr<Script> s) { greaterThanScripts_.push_back({sensor, threshold, std::move(s)}); }

    // ── Variable system ──────────────────────────────────────
    void   setVar(const std::string& n, double v)    { vars_[n] = v; varStrs_.erase(n); }
    double getVar(const std::string& n) const         { auto it = vars_.find(n); return it != vars_.end() ? it->second : 0.0; }
    void   changeVar(const std::string& n, double d)  { vars_[n] += d; varStrs_.erase(n); }
    void   setVarStr(const std::string& n, const std::string& v) { varStrs_[n] = v; vars_[n] = toNum(v); }
    std::string getVarStr(const std::string& n) const {
        auto it = varStrs_.find(n);
        if (it != varStrs_.end()) return it->second;
        auto nit = vars_.find(n);
        return nit != vars_.end() ? toStr(nit->second) : "0";
    }

    // ── List system ──────────────────────────────────────────
    ScratchList& getList(const std::string& n) { return lists_[n]; }
    void addToList(const std::string& list, double v)              { lists_[list].add(v); }
    void addToList(const std::string& list, const std::string& v)  { lists_[list].addStr(v); }
    void deleteOfList(const std::string& list, int idx)            { lists_[list].deleteAt(idx); }
    void deleteAllOfList(const std::string& list)                  { lists_[list].deleteAll(); }
    void insertAtList(const std::string& list, int idx, double v)  { lists_[list].insertAt(idx, v); }
    void insertAtList(const std::string& list, int idx, const std::string& v) { lists_[list].insertAtStr(idx, v); }
    void replaceItemOfList(const std::string& list, int idx, double v) { lists_[list].replaceAt(idx, v); }
    void replaceItemOfList(const std::string& list, int idx, const std::string& v) { lists_[list].replaceAtStr(idx, v); }
    double itemOfList(const std::string& list, int idx) const {
        auto it = lists_.find(list); return it != lists_.end() ? it->second.itemAt(idx) : 0;
    }
    std::string itemStrOfList(const std::string& list, int idx) const {
        auto it = lists_.find(list); return it != lists_.end() ? it->second.itemStrAt(idx) : "";
    }
    int lengthOfList(const std::string& list) const {
        auto it = lists_.find(list); return it != lists_.end() ? it->second.length() : 0;
    }
    bool listContainsItem(const std::string& list, double v) const {
        auto it = lists_.find(list); return it != lists_.end() && it->second.contains(v);
    }
    bool listContainsItem(const std::string& list, const std::string& v) const {
        auto it = lists_.find(list); return it != lists_.end() && it->second.containsStr(v);
    }
    int itemNumOfList(const std::string& list, double v) const {
        auto it = lists_.find(list); return it != lists_.end() ? it->second.itemNum(v) : 0;
    }
    int itemNumOfList(const std::string& list, const std::string& v) const {
        auto it = lists_.find(list); return it != lists_.end() ? it->second.itemNumStr(v) : 0;
    }
    std::string listContents(const std::string& list) const {
        auto it = lists_.find(list);
        if (it == lists_.end()) return "";
        std::string out;
        for (int i = 1; i <= it->second.length(); ++i) {
            if (i > 1) out += ' ';
            out += it->second.itemStrAt(i);
        }
        return out;
    }

    // ── Variable / List monitors (show/hide on screen) ───────
    enum class MonitorKind { Variable, List };
    struct Monitor {
        MonitorKind kind;
        std::string key;        // scoped variable/list name
        std::string label;      // display label
        bool visible = true;
        float x = 5, y = 5;    // auto-positioned
    };

    void showVariable(const std::string& name) {
        auto& m = getOrCreateMonitor(name, MonitorKind::Variable);
        m.visible = true;
    }
    void hideVariable(const std::string& name) {
        auto it = monitors_.find(name);
        if (it != monitors_.end()) it->second.visible = false;
    }
    void showList(const std::string& name) {
        auto& m = getOrCreateMonitor(name, MonitorKind::List);
        m.visible = true;
    }
    void hideList(const std::string& name) {
        auto it = monitors_.find(name);
        if (it != monitors_.end()) it->second.visible = false;
    }

    // ── String operations ────────────────────────────────────
    static std::string join(const std::string& a, const std::string& b) { return a + b; }
    static std::string letterOf(int idx, const std::string& s) {
        if (idx < 1 || idx > static_cast<int>(s.size())) return "";
        return std::string(1, s[idx - 1]);
    }
    static int lengthOfStr(const std::string& s) { return static_cast<int>(s.size()); }
    static bool strContains(const std::string& haystack, const std::string& needle) {
        if (needle.empty()) return true;
        std::string h = haystack, n = needle;
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return h.find(n) != std::string::npos;
    }

    // ── Sensing ──────────────────────────────────────────────
    double mouseX() const { return mouseX_; }
    double mouseY() const { return mouseY_; }
    bool   mouseDown() const { return mouseDown_; }

    bool keyPressed(const std::string& key) const {
        if (key == "any") {
            // Check all keys
            for (auto& [k, code] : keyMap_)
                if (code != sf::Keyboard::Unknown && sf::Keyboard::isKeyPressed(code))
                    return true;
            return false;
        }
        auto it = keyMap_.find(key);
        if (it == keyMap_.end()) return false;
        return sf::Keyboard::isKeyPressed(it->second);
    }

    double timer() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - timerStart_).count();
    }
    void resetTimer() { timerStart_ = std::chrono::steady_clock::now(); }

    // ── Loudness (microphone) ────────────────────────────────
    double loudness() {
        if (!micStarted_) {
            if (sf::SoundRecorder::isAvailable()) {
                micRecorder_ = std::make_unique<LoudnessRecorder>();
                micRecorder_->start(44100);
                micStarted_ = true;
            } else {
                micStarted_ = true; // don't retry
            }
        }
        return micRecorder_ ? micRecorder_->getLoudness() : 0.0;
    }

    const std::string& answer() const { return answer_; }
    void askAndWait(const std::string& question) {
        askQuestion_ = question;
        asking_ = true;
        askInput_.clear();
    }
    bool isAsking() const { return asking_; }

    double daysSince2000() const {
        auto now = std::chrono::system_clock::now();
        auto epoch2000 = std::chrono::system_clock::from_time_t(946684800);
        return std::chrono::duration<double, std::ratio<86400>>(now - epoch2000).count();
    }

    double current(const std::string& what) const {
        auto now = std::time(nullptr);
        auto* tm = std::localtime(&now);
        if (what == "year")   return tm->tm_year + 1900;
        if (what == "month")  return tm->tm_mon + 1;
        if (what == "date")   return tm->tm_mday;
        if (what == "dayofweek") return tm->tm_wday + 1;
        if (what == "hour")   return tm->tm_hour;
        if (what == "minute") return tm->tm_min;
        if (what == "second") return tm->tm_sec;
        return 0;
    }

    std::string username() const {
#ifdef _WIN32
        char buf[256];
        DWORD size = sizeof(buf);
        if (GetUserNameA(buf, &size)) return std::string(buf);
#else
        const char* user = std::getenv("USER");
        if (!user) user = std::getenv("USERNAME");
        if (user) return std::string(user);
#endif
        return "";
    }

    double distanceBetween(const Sprite& a, const std::string& target) const {
        double tx, ty;
        if (target == "_mouse_") {
            tx = mouseX_; ty = mouseY_;
        } else {
            auto* sp = const_cast<Runtime*>(this)->findSprite(target);
            if (!sp) return 10000;
            tx = sp->x(); ty = sp->y();
        }
        double dx = a.x() - tx, dy = a.y() - ty;
        return std::sqrt(dx * dx + dy * dy);
    }

    // ── Color touching ───────────────────────────────────────
    // Renders the scene (excluding `self`) to a temp texture and checks pixels
    bool touchingColor(const Sprite& self, int colorVal) const {
        sf::Uint8 tr = (colorVal >> 16) & 0xFF;
        sf::Uint8 tg = (colorVal >> 8)  & 0xFF;
        sf::Uint8 tb =  colorVal        & 0xFF;

        // Get the sprite's bounding box in stage coords
        auto b = self.bounds();
        if (b.width <= 0 || b.height <= 0) return false;

        // Clamp to stage
        float x0 = std::max(0.f, b.left), y0 = std::max(0.f, b.top);
        float x1 = std::min(kStageW, b.left + b.width);
        float y1 = std::min(kStageH, b.top + b.height);
        if (x0 >= x1 || y0 >= y1) return false;

        // Render scene without self into a temp texture
        sf::RenderTexture sceneTex;
        sceneTex.create(static_cast<unsigned>(kStageW), static_cast<unsigned>(kStageH));
        sceneTex.clear(sf::Color::White);
        const_cast<Sprite&>(const_cast<Runtime*>(this)->stage_).draw(sceneTex, 1.f, 1.f, nullptr);
        // Pen layer
        sf::Sprite penSpr(const_cast<sf::RenderTexture&>(penTex_).getTexture());
        sceneTex.draw(penSpr);
        // All sprites except self
        std::vector<const Sprite*> sorted;
        for (auto& sp : sprites_)  if (sp.get() != &self) sorted.push_back(sp.get());
        for (auto& cl : clones_)   if (cl.get() != &self) sorted.push_back(cl.get());
        std::sort(sorted.begin(), sorted.end(),
                  [](const Sprite* a, const Sprite* b) { return a->layerOrder() < b->layerOrder(); });
        for (auto* sp : sorted)
            const_cast<Sprite*>(sp)->draw(sceneTex, 1.f, 1.f, nullptr);
        sceneTex.display();
        sf::Image sceneImg = sceneTex.getTexture().copyToImage();

        // Check each opaque pixel of self against the scene
        int ix0 = static_cast<int>(x0), iy0 = static_cast<int>(y0);
        int ix1 = static_cast<int>(x1), iy1 = static_cast<int>(y1);
        for (int py = iy0; py < iy1; ++py) {
            for (int px = ix0; px < ix1; ++px) {
                if (!self.isOpaqueAt(static_cast<float>(px), static_cast<float>(py))) continue;
                auto sc = sceneImg.getPixel(px, py);
                if (sc.a < 128) continue;
                // Scratch uses exact match on lower 5 bits (~8 tolerance)
                if (std::abs(sc.r - tr) < 8 && std::abs(sc.g - tg) < 8 && std::abs(sc.b - tb) < 8)
                    return true;
            }
        }
        return false;
    }

    bool colorTouchingColor(const Sprite& self, int selfColor, int sceneColor) const {
        sf::Uint8 sr = (selfColor >> 16) & 0xFF;
        sf::Uint8 sg = (selfColor >> 8)  & 0xFF;
        sf::Uint8 sb =  selfColor        & 0xFF;
        sf::Uint8 tr = (sceneColor >> 16) & 0xFF;
        sf::Uint8 tg = (sceneColor >> 8)  & 0xFF;
        sf::Uint8 tb =  sceneColor        & 0xFF;

        auto b = self.bounds();
        if (b.width <= 0 || b.height <= 0) return false;
        float x0 = std::max(0.f, b.left), y0 = std::max(0.f, b.top);
        float x1 = std::min(kStageW, b.left + b.width);
        float y1 = std::min(kStageH, b.top + b.height);
        if (x0 >= x1 || y0 >= y1) return false;

        // Render self alone to get its rendered colors
        sf::RenderTexture selfTex;
        selfTex.create(static_cast<unsigned>(kStageW), static_cast<unsigned>(kStageH));
        selfTex.clear(sf::Color::Transparent);
        const_cast<Sprite*>(&self)->draw(selfTex, 1.f, 1.f, nullptr);
        selfTex.display();
        sf::Image selfImg = selfTex.getTexture().copyToImage();

        // Render scene without self
        sf::RenderTexture sceneTex;
        sceneTex.create(static_cast<unsigned>(kStageW), static_cast<unsigned>(kStageH));
        sceneTex.clear(sf::Color::White);
        const_cast<Sprite&>(const_cast<Runtime*>(this)->stage_).draw(sceneTex, 1.f, 1.f, nullptr);
        sf::Sprite penSpr(const_cast<sf::RenderTexture&>(penTex_).getTexture());
        sceneTex.draw(penSpr);
        std::vector<const Sprite*> sorted;
        for (auto& sp : sprites_)  if (sp.get() != &self) sorted.push_back(sp.get());
        for (auto& cl : clones_)   if (cl.get() != &self) sorted.push_back(cl.get());
        std::sort(sorted.begin(), sorted.end(),
                  [](const Sprite* a, const Sprite* b) { return a->layerOrder() < b->layerOrder(); });
        for (auto* sp : sorted)
            const_cast<Sprite*>(sp)->draw(sceneTex, 1.f, 1.f, nullptr);
        sceneTex.display();
        sf::Image sceneImg = sceneTex.getTexture().copyToImage();

        int ix0 = static_cast<int>(x0), iy0 = static_cast<int>(y0);
        int ix1 = static_cast<int>(x1), iy1 = static_cast<int>(y1);
        for (int py = iy0; py < iy1; ++py) {
            for (int px = ix0; px < ix1; ++px) {
                auto selfPx = selfImg.getPixel(px, py);
                if (selfPx.a < 128) continue;
                if (!(std::abs(selfPx.r - sr) < 8 && std::abs(selfPx.g - sg) < 8 && std::abs(selfPx.b - sb) < 8))
                    continue;
                auto scenePx = sceneImg.getPixel(px, py);
                if (scenePx.a < 128) continue;
                if (std::abs(scenePx.r - tr) < 8 && std::abs(scenePx.g - tg) < 8 && std::abs(scenePx.b - tb) < 8)
                    return true;
            }
        }
        return false;
    }

    // ── Sound ────────────────────────────────────────────────
    void updateSoundsForSprite(Sprite* sp) {
        float vol = static_cast<float>(sp->getVolume());
        for (auto& entry : sounds_) {
            if (entry.owner == sp)
                entry.sound.setVolume(vol);
        }
    }

    void playSound(const std::string& name, Sprite& sp) {
        loadSoundIfNeeded(name);
        auto it = soundBuffers_.find(name);
        if (it == soundBuffers_.end()) return;
        sounds_.push_back({sf::Sound(), &sp});
        auto& snd = sounds_.back().sound;
        snd.setBuffer(it->second);
        snd.setVolume(static_cast<float>(sp.getVolume()));
        double pitch = sp.soundPitch();
        double pan   = sp.soundPan();
        if (pitch != 0.0) {
            float ratio = std::pow(2.f, static_cast<float>(pitch) / 120.f);
            snd.setPitch(std::clamp(ratio, 0.01f, 100.f));
        }
        if (pan != 0.0) {
            float p = static_cast<float>(std::clamp(pan, -100.0, 100.0) / 100.0);
            snd.setPosition(p, 0.f, 0.f);
            snd.setRelativeToListener(true);
            snd.setMinDistance(1.f);
            snd.setAttenuation(0.f);
        }
        snd.play();
        cleanSounds();
    }

    // Play and return an ID the script can poll
    int playSoundUntilDone(const std::string& name, Sprite& sp) {
        loadSoundIfNeeded(name);
        auto it = soundBuffers_.find(name);
        if (it == soundBuffers_.end()) return -1;
        sounds_.push_back({sf::Sound(), &sp});
        auto& snd = sounds_.back().sound;
        snd.setBuffer(it->second);
        snd.setVolume(static_cast<float>(sp.getVolume()));
        double pitch = sp.soundPitch();
        double pan   = sp.soundPan();
        if (pitch != 0.0) {
            float ratio = std::pow(2.f, static_cast<float>(pitch) / 120.f);
            snd.setPitch(std::clamp(ratio, 0.01f, 100.f));
        }
        if (pan != 0.0) {
            float p = static_cast<float>(std::clamp(pan, -100.0, 100.0) / 100.0);
            snd.setPosition(p, 0.f, 0.f);
            snd.setRelativeToListener(true);
            snd.setMinDistance(1.f);
            snd.setAttenuation(0.f);
        }
        snd.play();
        return soundIdCounter_++;
    }

    bool isSoundPlaying(int id) const {
        int idx = id - soundIdBase_;
        if (idx < 0 || idx >= static_cast<int>(sounds_.size())) return false;
        return sounds_[idx].sound.getStatus() != sf::Sound::Stopped;
    }

    void registerSound(const std::string& name, const std::string& path) {
        soundPaths_[name] = path;
    }

    void stopAllSounds() {
        for (auto& e : sounds_) e.sound.stop();
        sounds_.clear();
        soundIdBase_ = soundIdCounter_;
    }

    void stopSound(int id) {
        int idx = id - soundIdBase_;
        if (idx >= 0 && idx < static_cast<int>(sounds_.size()))
            sounds_[idx].sound.stop();
    }

    // ── Broadcast ────────────────────────────────────────────
    void broadcast(const std::string& msg) { pendingBroadcasts_.push_back(msg); }
    int broadcastAndWait(const std::string& msg) {
        int waitId = ++broadcastWaitIdCounter_;
        pendingBroadcastAndWaits_.push_back({msg, waitId});
        return waitId;
    }
    bool isBroadcastWaitDone(int waitId) const {
        // Check if any active script still has this waitId
        for (auto* s : activeScripts_) {
            if (s->broadcastWaitId() == waitId) return false;
        }
        return true;
    }

    // ── Random ───────────────────────────────────────────────
    double randomRange(double lo, double hi) {
        if (lo > hi) std::swap(lo, hi);
        if (lo == std::floor(lo) && hi == std::floor(hi)) {
            std::uniform_int_distribution<int> dist(static_cast<int>(lo), static_cast<int>(hi));
            return dist(rng_);
        }
        std::uniform_real_distribution<double> dist(lo, hi);
        return dist(rng_);
    }

    // ── Control ──────────────────────────────────────────────
    void stopAll() { running_ = false; }
    void stopOtherScripts(Script* keep) {
        pendingStopKeeps_.push_back(keep);
    }
    // Also store the owner to scope the stop
    Sprite* pendingStopOwner() const { return !pendingStopKeeps_.empty() ? pendingStopKeeps_.back()->owner() : nullptr; }

    // ── Clone system ─────────────────────────────────────────
    void createCloneOf(const std::string& spriteName) {
        Sprite* original = nullptr;
        if (spriteName == "_myself_") {
            // Need to be called from a context that knows itself – handled via the sprite reference
            return;
        }
        original = findSprite(spriteName);
        if (!original) return;
        createCloneOfSprite(*original);
    }

    void createCloneOfSprite(Sprite& original) {
        if (clones_.size() >= 300) return; // Scratch clone limit

        auto clone = std::make_unique<Sprite>(original.name(), *this);
        clone->copyStateFrom(original);
        clone->setIsClone(true);

        Sprite* clonePtr = clone.get();
        clones_.push_back(std::move(clone));

        // Fire "when I start as a clone" scripts (deferred to avoid
        // modifying activeScripts_ while it is being iterated).
        for (auto& entry : cloneStartScripts_) {
            if (entry.spriteName == original.name()) {
                auto script = entry.factory(*this, *clonePtr);
                script->reset();
                pendingScriptStarts_.push_back(script.get());
                cloneScripts_.push_back(std::move(script));
            }
        }
    }

    void deleteClone(Sprite* sp) {
        if (!sp || !sp->isClone()) return;
        pendingCloneDeletes_.push_back(sp);
    }

    // ── Pen ──────────────────────────────────────────────────
    void penClear() { penDirty_ = true; needsPenClear_ = true; }
    sf::RenderTexture& penTexture() { return penTex_; }
    bool penDirty() const { return penDirty_; }
    void markPenDirty() { penDirty_ = true; }

    // ── Attribute "of" reporter ──────────────────────────────
    double getAttributeOf(const std::string& attr, const std::string& spriteName) {
        if (spriteName == "Stage" || spriteName == "_stage_") {
            if (attr == "backdrop #") return stage_.backdropNumber();
            if (attr == "backdrop name") return 0; // string – simplified
            if (attr == "volume") return stage_.getVolume();
            // Check variables
            return getVar(attr);
        }
        auto* sp = findSprite(spriteName);
        if (!sp) return 0;
        if (attr == "x position") return sp->x();
        if (attr == "y position") return sp->y();
        if (attr == "direction")  return sp->direction();
        if (attr == "costume #")  return sp->costumeIndex();
        if (attr == "costume name") return 0; // string
        if (attr == "size")       return sp->size();
        if (attr == "volume")     return sp->getVolume();
        return getVar(attr);
    }

    // ── Main loop ────────────────────────────────────────────
    void run() {
        sf::VideoMode mode(cfg_.width, cfg_.height);
        auto style = cfg_.fullscreen ? sf::Style::Fullscreen
                                     : sf::Style::Close | sf::Style::Titlebar;
        sf::RenderWindow window(mode, cfg_.title, style);
        window.setFramerateLimit(60);

        float scaleX = static_cast<float>(cfg_.width)  / kStageW;
        float scaleY = static_cast<float>(cfg_.height) / kStageH;

        // Initialize pen texture (stage resolution)
        penTex_.create(static_cast<unsigned>(kStageW), static_cast<unsigned>(kStageH));
        penTex_.clear(sf::Color::Transparent);
        penTex_.display();

        // Try to load a system font for say/think bubbles and ask
        sf::Font font;
        const sf::Font* fontPtr = nullptr;
        #ifdef _WIN32
        if (font.loadFromFile("C:\\Windows\\Fonts\\arial.ttf")) fontPtr = &font;
        #else
        if (font.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")) fontPtr = &font;
        else if (font.loadFromFile("/usr/share/fonts/TTF/DejaVuSans.ttf")) fontPtr = &font;
        #endif

        // Save initial positions for pen tracking
        for (auto& sp : sprites_) sp->savePrevPos();

        // Fire green flag
        for (auto& entry : greenFlagScripts_) {
            entry.script->reset();
            activeScripts_.push_back(entry.script.get());
        }

        sf::Clock clock;
        running_ = true;
        constexpr float kScratchTickRate = 1.f / 30.f; // Scratch runs at 30 ticks/sec
        float tickAccum = 0.f;
        clock.restart(); // discard time spent loading assets

        while (running_ && window.isOpen()) {
            float dt = clock.restart().asSeconds();
            dt = std::min(dt, kScratchTickRate * 2.f); // cap to at most 2 ticks worth
            tickAccum = std::min(tickAccum + dt, kScratchTickRate * 2.f); // prevent burst catch-up

            // ── Events ───────────────────────────────────────
            sf::Event evt{};
            while (window.pollEvent(evt)) {
                if (evt.type == sf::Event::Closed)
                    window.close();

                if (evt.type == sf::Event::KeyPressed) {
                    if (asking_) {
                        if (evt.key.code == sf::Keyboard::Enter) {
                            answer_ = askInput_;
                            asking_ = false;
                            askInput_.clear();
                        } else if (evt.key.code == sf::Keyboard::BackSpace && !askInput_.empty()) {
                            askInput_.pop_back();
                        }
                    }

                    for (auto& ks : keyScripts_) {
                        if (ks.key == "any") {
                            ks.script->reset();
                            activeScripts_.push_back(ks.script.get());
                        } else {
                            auto mapped = keyMap_.find(ks.key);
                            if (mapped != keyMap_.end() && evt.key.code == mapped->second) {
                                ks.script->reset();
                                activeScripts_.push_back(ks.script.get());
                            }
                        }
                    }
                }

                if (evt.type == sf::Event::TextEntered && asking_) {
                    if (evt.text.unicode >= 32 && evt.text.unicode < 128)
                        askInput_ += static_cast<char>(evt.text.unicode);
                }

                if (evt.type == sf::Event::MouseButtonPressed &&
                    evt.mouseButton.button == sf::Mouse::Left) {
                    float mx = static_cast<float>(evt.mouseButton.x);
                    float my = static_cast<float>(evt.mouseButton.y);
                    // Check clones first (higher layer), then sprites
                    auto checkClick = [&](Sprite* sp) {
                        auto b = sp->bounds();
                        b.left *= scaleX; b.top *= scaleY;
                        b.width *= scaleX; b.height *= scaleY;
                        return b.contains(mx, my);
                    };
                    for (auto& cs : clickScripts_) {
                        // Check clones — create a new script instance bound to the clone
                        for (auto& cl : clones_) {
                            if (cl->name() == cs.spriteName && cl->visible() && checkClick(cl.get())) {
                                auto clonedScript = cs.script->clone(*this, *cl);
                                if (clonedScript) {
                                    clonedScript->setOwner(cl.get());
                                    clonedScript->reset();
                                    activeScripts_.push_back(clonedScript.get());
                                    cloneScripts_.push_back(std::move(clonedScript));
                                }
                            }
                        }
                        // Original sprite
                        auto* sp = findSprite(cs.spriteName);
                        if (sp && sp->visible() && checkClick(sp)) {
                            cs.script->reset();
                            activeScripts_.push_back(cs.script.get());
                        }
                    }
                }
                // Stage click — fire if no sprite was clicked
                for (auto& sc : stageClickScripts_) {
                    sc.script->reset();
                    activeScripts_.push_back(sc.script.get());
                }
            }

            // ── Update mouse ─────────────────────────────────
            auto mpos = sf::Mouse::getPosition(window);
            mouseX_ = mpos.x / scaleX - kStageW / 2.0;
            mouseY_ = kStageH / 2.0 - mpos.y / scaleY;
            bool wasDown = mouseDown_;
            mouseDown_ = sf::Mouse::isButtonPressed(sf::Mouse::Left);

            // ── Drag handling ────────────────────────────────
            if (mouseDown_ && !wasDown) {
                // Start drag — find topmost draggable sprite under cursor
                float mx = static_cast<float>(mpos.x);
                float my = static_cast<float>(mpos.y);
                auto checkHit = [&](Sprite* sp) {
                    auto b = sp->bounds();
                    b.left *= scaleX; b.top *= scaleY;
                    b.width *= scaleX; b.height *= scaleY;
                    return b.contains(mx, my);
                };
                Sprite* best = nullptr;
                int bestLayer = -1;
                for (auto& sp : sprites_) {
                    if (sp->visible() && sp->isDraggable() && checkHit(sp.get()) && sp->layerOrder() > bestLayer) {
                        best = sp.get(); bestLayer = sp->layerOrder();
                    }
                }
                for (auto& cl : clones_) {
                    if (cl->visible() && cl->isDraggable() && checkHit(cl.get()) && cl->layerOrder() > bestLayer) {
                        best = cl.get(); bestLayer = cl->layerOrder();
                    }
                }
                if (best) best->setDragging(true);
            }
            if (!mouseDown_) {
                // Release all drags
                for (auto& sp : sprites_) sp->setDragging(false);
                for (auto& cl : clones_)  cl->setDragging(false);
            }
            // Move dragged sprites to mouse position
            for (auto& sp : sprites_) {
                if (sp->isDragging()) sp->gotoXY(mouseX_, mouseY_);
            }
            for (auto& cl : clones_) {
                if (cl->isDragging()) cl->gotoXY(mouseX_, mouseY_);
            }

            // ── Scratch tick loop (30 Hz) ─────────────────────
            while (tickAccum >= kScratchTickRate) {
            tickAccum -= kScratchTickRate;
            float tickDt = kScratchTickRate;

            // ── Process broadcasts ───────────────────────────
            auto dispatchBroadcastToClones = [&](const std::string& msg, int waitId) {
                for (auto& bs : broadcastScripts_) {
                    if (bs.message != msg) continue;
                    Sprite* owner = bs.script->owner();
                    if (!owner) continue;
                    for (auto& cl : clones_) {
                        if (cl->name() != owner->name()) continue;
                        auto clonedScript = bs.script->clone(*this, *cl);
                        if (!clonedScript) continue;
                        clonedScript->setOwner(cl.get());
                        clonedScript->reset();
                        clonedScript->setBroadcastWaitId(waitId);
                        activeScripts_.push_back(clonedScript.get());
                        cloneScripts_.push_back(std::move(clonedScript));
                    }
                }
            };

            auto broadcasts = std::move(pendingBroadcasts_);
            pendingBroadcasts_.clear();
            // Deduplicate: In Scratch, multiple broadcasts of the same message
            // in one frame are equivalent to a single broadcast.
            std::sort(broadcasts.begin(), broadcasts.end());
            broadcasts.erase(std::unique(broadcasts.begin(), broadcasts.end()), broadcasts.end());
            for (auto& msg : broadcasts) {
                for (auto& bs : broadcastScripts_) {
                    if (bs.message == msg) {
                        bs.script->reset();
                        bs.script->setBroadcastWaitId(0);
                        if (std::find(activeScripts_.begin(), activeScripts_.end(), bs.script.get()) == activeScripts_.end())
                            activeScripts_.push_back(bs.script.get());
                    }
                }
                dispatchBroadcastToClones(msg, 0);
                // Check backdrop-switch events
                for (auto& bds : backdropScripts_) {
                    if (bds.backdrop == msg) {
                        bds.script->reset();
                        bds.script->setBroadcastWaitId(0);
                        if (std::find(activeScripts_.begin(), activeScripts_.end(), bds.script.get()) == activeScripts_.end())
                            activeScripts_.push_back(bds.script.get());
                    }
                }
            }

            // ── Process broadcast-and-wait ───────────────────
            auto bwList = std::move(pendingBroadcastAndWaits_);
            pendingBroadcastAndWaits_.clear();
            // Deduplicate by message (keep highest waitId for each message)
            {
                std::unordered_map<std::string, int> seen;
                for (auto& bw : bwList) {
                    auto it = seen.find(bw.message);
                    if (it == seen.end() || bw.waitId > it->second)
                        seen[bw.message] = bw.waitId;
                }
                std::erase_if(bwList, [&seen](const BroadcastAndWaitEntry& bw) {
                    auto it = seen.find(bw.message);
                    if (it != seen.end() && it->second == bw.waitId) {
                        seen.erase(it); // keep first occurrence with matching waitId
                        return false;
                    }
                    return true;
                });
            }
            for (auto& bw : bwList) {
                for (auto& bs : broadcastScripts_) {
                    if (bs.message == bw.message) {
                        bs.script->reset();
                        bs.script->setBroadcastWaitId(bw.waitId);
                        if (std::find(activeScripts_.begin(), activeScripts_.end(), bs.script.get()) == activeScripts_.end())
                            activeScripts_.push_back(bs.script.get());
                    }
                }
                dispatchBroadcastToClones(bw.message, bw.waitId);
                for (auto& bds : backdropScripts_) {
                    if (bds.backdrop == bw.message) {
                        bds.script->reset();
                        bds.script->setBroadcastWaitId(bw.waitId);
                        activeScripts_.push_back(bds.script.get());
                    }
                }
            }

            // ── Check "when > than" hat events ──────────────
            for (auto& gt : greaterThanScripts_) {
                double sensorVal = 0;
                if (gt.sensor == "TIMER") sensorVal = timer();
                else if (gt.sensor == "LOUDNESS") sensorVal = loudness();
                bool isAbove = sensorVal > gt.threshold;
                if (isAbove && !gt.wasAbove) {
                    gt.script->reset();
                    activeScripts_.push_back(gt.script.get());
                }
                gt.wasAbove = isAbove;
            }

            // ── Pen drawing (before script step) ─────────────
            for (auto& sp : sprites_) sp->savePrevPos();
            for (auto& cl : clones_)  cl->savePrevPos();

            // ── Step scripts ─────────────────────────────────
            std::erase_if(activeScripts_, [tickDt](Script* s) { return !s->step(tickDt); });

            // Apply deferred stopOtherScripts (scoped to the same sprite)
            for (auto* keep : pendingStopKeeps_) {
                Sprite* owner = keep->owner();
                // Stop sounds of scripts being killed
                for (auto* s : activeScripts_) {
                    if (s != keep && s->owner() == owner) {
                        int sid = s->activeSoundId();
                        if (sid >= 0) stopSound(sid);
                    }
                }
                std::erase_if(activeScripts_, [keep, owner](Script* s) {
                    return s != keep && s->owner() == owner;
                });
            }
            pendingStopKeeps_.clear();

            // Activate scripts that were deferred during this tick
            if (!pendingScriptStarts_.empty()) {
                activeScripts_.insert(activeScripts_.end(),
                                      pendingScriptStarts_.begin(),
                                      pendingScriptStarts_.end());
                pendingScriptStarts_.clear();
            }

            // ── Pen lines from moved sprites ─────────────────
            if (needsPenClear_) {
                penTex_.clear(sf::Color::Transparent);
                penTex_.display();
                needsPenClear_ = false;
            }
            auto drawPenFor = [&](Sprite* sp) {
                if (sp->isPenDown()) {
                    if (sp->x() != sp->prevX() || sp->y() != sp->prevY()) {
                        sp->drawPenLine(penTex_, sp->prevX(), sp->prevY(), sp->x(), sp->y());
                        penDirty_ = true;
                    }
                }
            };
            for (auto& sp : sprites_) drawPenFor(sp.get());
            for (auto& cl : clones_)  drawPenFor(cl.get());
            if (penDirty_) { penTex_.display(); penDirty_ = false; }

            // ── Process clone deletes ────────────────────────
            for (auto* del : pendingCloneDeletes_) {
                // Remove active scripts owned by this clone
                std::erase_if(activeScripts_, [del](Script* s) {
                    return s->owner() == del;
                });
                // Remove owned clone scripts (frees memory)
                std::erase_if(cloneScripts_, [del](const auto& s) {
                    return s->owner() == del;
                });
                // Remove the clone itself
                std::erase_if(clones_, [del](const auto& c) { return c.get() == del; });
            }
            pendingCloneDeletes_.clear();

            } // end Scratch tick loop

            // ── Render ───────────────────────────────────────
            window.clear(sf::Color::White);

            // Render everything to a 480x360 stage texture (clips sprites like Scratch)
            static sf::RenderTexture stageTex;
            static bool stageTexInit = false;
            if (!stageTexInit) {
                stageTex.create(static_cast<unsigned>(kStageW), static_cast<unsigned>(kStageH));
                stageTexInit = true;
            }
            stageTex.clear(sf::Color::White);

            // 1. Stage backdrop (draw at 1:1 into stage texture)
            stage_.draw(stageTex, 1.f, 1.f, fontPtr);

            // 2. Pen layer
            sf::Sprite penSpr(penTex_.getTexture());
            penSpr.setScale(1.f, 1.f);
            stageTex.draw(penSpr);

            // 3. Sprites + clones sorted by layer
            std::vector<Sprite*> sorted;
            sorted.reserve(sprites_.size() + clones_.size());
            for (auto& sp : sprites_) sorted.push_back(sp.get());
            for (auto& cl : clones_)  sorted.push_back(cl.get());
            std::sort(sorted.begin(), sorted.end(),
                      [](const Sprite* a, const Sprite* b) { return a->layerOrder() < b->layerOrder(); });

            for (auto* sp : sorted)
                sp->draw(stageTex, 1.f, 1.f, fontPtr);

            stageTex.display();

            // Draw the stage texture scaled to the window
            sf::Sprite stageSprite(stageTex.getTexture());
            stageSprite.setScale(scaleX, scaleY);
            window.draw(stageSprite);

            // 4. Variable / list monitors
            if (fontPtr) {
                float monY = 5.f;
                for (auto& [mkey, mon] : monitors_) {
                    if (!mon.visible) continue;
                    if (mon.kind == MonitorKind::Variable) {
                        std::string valStr = getVarStr(mon.key);
                        std::string display = mon.label + ": " + valStr;

                        sf::Text txt;
                        txt.setFont(*fontPtr);
                        txt.setString(display);
                        txt.setCharacterSize(static_cast<unsigned>(11 * scaleY));
                        txt.setFillColor(sf::Color::White);

                        auto tb = txt.getLocalBounds();
                        float pad = 4.f * scaleX;
                        float bw = tb.width + pad * 2.f + 8.f * scaleX;
                        float bh = tb.height + pad * 2.f + 4.f * scaleY;

                        sf::RectangleShape bg(sf::Vector2f(bw, bh));
                        bg.setPosition(mon.x * scaleX, monY);
                        bg.setFillColor(sf::Color(230, 109, 36));
                        bg.setOutlineColor(sf::Color(190, 85, 25));
                        bg.setOutlineThickness(1.f);
                        window.draw(bg);

                        // Value box (white inset for the number)
                        float valBoxW = std::max(30.f * scaleX, tb.width - mon.label.size() * 6.f * scaleX + pad * 2);
                        sf::RectangleShape valBg(sf::Vector2f(valBoxW, bh - 4.f * scaleY));
                        valBg.setPosition(mon.x * scaleX + bw - valBoxW - 2.f * scaleX, monY + 2.f * scaleY);
                        valBg.setFillColor(sf::Color(220, 95, 30));
                        valBg.setOutlineColor(sf::Color::Transparent);
                        valBg.setOutlineThickness(0);
                        // Just draw the combined text for simplicity

                        txt.setPosition(mon.x * scaleX + pad, monY + pad * 0.5f);
                        window.draw(txt);

                        monY += bh + 3.f * scaleY;
                    } else {
                        // List monitor
                        auto lit = lists_.find(mon.key);
                        int len = lit != lists_.end() ? lit->second.length() : 0;

                        // Header
                        sf::Text header;
                        header.setFont(*fontPtr);
                        header.setString(mon.label);
                        header.setCharacterSize(static_cast<unsigned>(11 * scaleY));
                        header.setFillColor(sf::Color::White);

                        float listW = 100.f * scaleX;
                        float rowH  = 16.f * scaleY;
                        float headerH = 20.f * scaleY;
                        int maxRows = std::min(len, 10);
                        float totalH = headerH + maxRows * rowH + 18.f * scaleY;

                        sf::RectangleShape bg(sf::Vector2f(listW, totalH));
                        bg.setPosition(mon.x * scaleX, monY);
                        bg.setFillColor(sf::Color(209, 57, 47));
                        bg.setOutlineColor(sf::Color(180, 45, 40));
                        bg.setOutlineThickness(1.f);
                        window.draw(bg);

                        header.setPosition(mon.x * scaleX + 4.f * scaleX, monY + 2.f * scaleY);
                        window.draw(header);

                        // Rows
                        for (int r = 0; r < maxRows; ++r) {
                            float ry = monY + headerH + r * rowH;
                            sf::RectangleShape row(sf::Vector2f(listW - 6.f * scaleX, rowH - 2.f * scaleY));
                            row.setPosition(mon.x * scaleX + 3.f * scaleX, ry);
                            row.setFillColor(sf::Color(230, 230, 230));
                            window.draw(row);

                            // Row number
                            sf::Text idx;
                            idx.setFont(*fontPtr);
                            idx.setString(std::to_string(r + 1));
                            idx.setCharacterSize(static_cast<unsigned>(9 * scaleY));
                            idx.setFillColor(sf::Color(150, 150, 150));
                            idx.setPosition(mon.x * scaleX + 5.f * scaleX, ry + 1.f * scaleY);
                            window.draw(idx);

                            // Value
                            std::string val = lit != lists_.end() ? lit->second.itemStrAt(r + 1) : "";
                            sf::Text vt;
                            vt.setFont(*fontPtr);
                            vt.setString(val);
                            vt.setCharacterSize(static_cast<unsigned>(10 * scaleY));
                            vt.setFillColor(sf::Color::Black);
                            vt.setPosition(mon.x * scaleX + 20.f * scaleX, ry + 1.f * scaleY);
                            window.draw(vt);
                        }

                        // Length footer
                        sf::Text footer;
                        footer.setFont(*fontPtr);
                        footer.setString("length " + std::to_string(len));
                        footer.setCharacterSize(static_cast<unsigned>(9 * scaleY));
                        footer.setFillColor(sf::Color::White);
                        footer.setPosition(mon.x * scaleX + 4.f * scaleX, monY + totalH - 15.f * scaleY);
                        window.draw(footer);

                        monY += totalH + 3.f * scaleY;
                    }
                }
            }

            // 5. Ask input UI
            if (asking_ && fontPtr) {
                float barH = 30.f * scaleY;
                float barY = cfg_.height - barH;

                sf::RectangleShape bar(sf::Vector2f(static_cast<float>(cfg_.width), barH));
                bar.setPosition(0, barY);
                bar.setFillColor(sf::Color(240, 240, 240));
                bar.setOutlineColor(sf::Color(200, 200, 200));
                bar.setOutlineThickness(1.f);
                window.draw(bar);

                sf::Text qText;
                qText.setFont(*fontPtr);
                qText.setString(askQuestion_);
                qText.setCharacterSize(static_cast<unsigned>(12 * scaleY));
                qText.setFillColor(sf::Color::Black);
                qText.setPosition(8.f * scaleX, barY + 2.f * scaleY);
                window.draw(qText);

                sf::Text iText;
                iText.setFont(*fontPtr);
                iText.setString(askInput_ + "_");
                iText.setCharacterSize(static_cast<unsigned>(12 * scaleY));
                iText.setFillColor(sf::Color(50, 50, 50));
                auto qb = qText.getLocalBounds();
                iText.setPosition(8.f * scaleX + qb.width + 12.f * scaleX, barY + 2.f * scaleY);
                window.draw(iText);
            }

            window.display();

            // ── Clean sounds ─────────────────────────────────
            cleanSounds();
        }
    }

private:
    void loadSoundIfNeeded(const std::string& name) {
        if (soundBuffers_.count(name)) return;
        std::string path;
        auto pit = soundPaths_.find(name);
        if (pit != soundPaths_.end())
            path = pit->second;
        else
            path = "assets/sounds/" + name;
        auto& buf = soundBuffers_[name];
        if (!buf.loadFromFile(path)) {
            soundBuffers_.erase(name);
        }
    }

    struct SoundEntry {
        sf::Sound sound;
        Sprite*   owner = nullptr;
    };

    void cleanSounds() {
        // Remove stopped sounds from the front only to keep id mapping stable
        while (!sounds_.empty() && sounds_.front().sound.getStatus() == sf::Sound::Stopped) {
            sounds_.pop_front();
            soundIdBase_++;
        }
    }

    RuntimeConfig cfg_;
    bool          running_ = false;

    Sprite                                stage_;
    std::vector<std::unique_ptr<Sprite>>  sprites_;
    std::vector<std::unique_ptr<Sprite>>  clones_;
    std::vector<Sprite*>                  pendingCloneDeletes_;

    // Scripts
    std::vector<GreenFlagEntry>     greenFlagScripts_;
    std::vector<KeyPressedEntry>    keyScripts_;
    std::vector<SpriteClickedEntry> clickScripts_;
    std::vector<BroadcastEntry>     broadcastScripts_;
    std::vector<BackdropEntry>      backdropScripts_;
    std::vector<CloneStartEntry>    cloneStartScripts_;
    std::vector<StageClickedEntry>  stageClickScripts_;
    std::vector<GreaterThanEntry>   greaterThanScripts_;
    std::vector<Script*>            activeScripts_;
    std::vector<Script*>            pendingScriptStarts_;
    std::vector<Script*>                   pendingStopKeeps_;
    std::vector<std::unique_ptr<Script>> cloneScripts_;

    // Variables + Lists
    std::unordered_map<std::string, double>      vars_;
    std::unordered_map<std::string, std::string>  varStrs_;
    mutable std::unordered_map<std::string, ScratchList> lists_;

    // Sound
    std::unordered_map<std::string, sf::SoundBuffer> soundBuffers_;
    std::unordered_map<std::string, std::string>       soundPaths_;
    std::deque<SoundEntry> sounds_;
    int    soundIdCounter_ = 0; // monotonic ID for playSoundUntilDone
    int    soundIdBase_ = 0;    // base offset after cleanSounds removes from front

    // Input / sensing
    double mouseX_ = 0, mouseY_ = 0;
    bool   mouseDown_ = false;
    std::chrono::steady_clock::time_point timerStart_;

    // Microphone
    std::unique_ptr<LoudnessRecorder> micRecorder_;
    bool micStarted_ = false;

    // Ask
    bool        asking_ = false;
    std::string askQuestion_;
    std::string askInput_;
    std::string answer_;

    // Broadcast
    std::vector<std::string> pendingBroadcasts_;
    struct BroadcastAndWaitEntry { std::string message; int waitId; };
    std::vector<BroadcastAndWaitEntry> pendingBroadcastAndWaits_;
    int broadcastWaitIdCounter_ = 0;

    // Monitors
    std::unordered_map<std::string, Monitor> monitors_;

    Monitor& getOrCreateMonitor(const std::string& name, MonitorKind kind) {
        auto it = monitors_.find(name);
        if (it != monitors_.end()) return it->second;
        Monitor m;
        m.kind = kind;
        m.key = name;
        // Strip sprite prefix for display label
        auto sep = name.find("::");
        m.label = (sep != std::string::npos) ? name.substr(sep + 2) : name;
        m.visible = true;
        m.x = 5;
        m.y = 5;
        monitors_[name] = m;
        return monitors_[name];
    }

    // Pen
    sf::RenderTexture penTex_;
    bool penDirty_      = false;
    bool needsPenClear_ = false;

    // RNG
    std::mt19937 rng_;

    // Key mapping
    inline static const std::unordered_map<std::string, sf::Keyboard::Key> keyMap_ = {
        {"space",       sf::Keyboard::Space},
        {"up arrow",    sf::Keyboard::Up},
        {"down arrow",  sf::Keyboard::Down},
        {"left arrow",  sf::Keyboard::Left},
        {"right arrow", sf::Keyboard::Right},
        {"a", sf::Keyboard::A}, {"b", sf::Keyboard::B}, {"c", sf::Keyboard::C},
        {"d", sf::Keyboard::D}, {"e", sf::Keyboard::E}, {"f", sf::Keyboard::F},
        {"g", sf::Keyboard::G}, {"h", sf::Keyboard::H}, {"i", sf::Keyboard::I},
        {"j", sf::Keyboard::J}, {"k", sf::Keyboard::K}, {"l", sf::Keyboard::L},
        {"m", sf::Keyboard::M}, {"n", sf::Keyboard::N}, {"o", sf::Keyboard::O},
        {"p", sf::Keyboard::P}, {"q", sf::Keyboard::Q}, {"r", sf::Keyboard::R},
        {"s", sf::Keyboard::S}, {"t", sf::Keyboard::T}, {"u", sf::Keyboard::U},
        {"v", sf::Keyboard::V}, {"w", sf::Keyboard::W}, {"x", sf::Keyboard::X},
        {"y", sf::Keyboard::Y}, {"z", sf::Keyboard::Z},
        {"0", sf::Keyboard::Num0}, {"1", sf::Keyboard::Num1}, {"2", sf::Keyboard::Num2},
        {"3", sf::Keyboard::Num3}, {"4", sf::Keyboard::Num4}, {"5", sf::Keyboard::Num5},
        {"6", sf::Keyboard::Num6}, {"7", sf::Keyboard::Num7}, {"8", sf::Keyboard::Num8},
        {"9", sf::Keyboard::Num9}, {"enter", sf::Keyboard::Enter},
        {"any", sf::Keyboard::Unknown},
    };
};

// ═════════════════════════════════════════════════════════════
// Deferred Sprite methods (need Runtime)
// ═════════════════════════════════════════════════════════════
inline void Sprite::gotoTarget(const std::string& target) {
    if (target == "_mouse_") {
        x_ = rt_.mouseX(); y_ = rt_.mouseY();
    } else if (target == "_random_") {
        x_ = rt_.randomRange(-kStageW / 2, kStageW / 2);
        y_ = rt_.randomRange(-kStageH / 2, kStageH / 2);
    } else {
        auto* sp = rt_.findSprite(target);
        if (sp) { x_ = sp->x(); y_ = sp->y(); }
    }
}

inline void Sprite::pointTowards(const std::string& target) {
    double tx, ty;
    if (target == "_mouse_") {
        tx = rt_.mouseX(); ty = rt_.mouseY();
    } else {
        auto* sp = rt_.findSprite(target);
        if (!sp) return;
        tx = sp->x(); ty = sp->y();
    }
    double dx = tx - x_, dy = ty - y_;
    if (dx == 0 && dy == 0) return;
    setDirection(radToDeg(std::atan2(dx, dy)));
}

inline double Sprite::distanceTo(const std::string& target) const {
    return rt_.distanceBetween(*this, target);
}

inline bool Sprite::touching(const std::string& targetName) const {
    if (targetName == "_edge_") {
        if (costumes_.empty()) return false;
        auto& tex = costumes_[costumeIdx_].texture;
        float bmpRes = static_cast<float>(costumes_[costumeIdx_].bitmapResolution);
        float scl = static_cast<float>(size_ / 100.0) / bmpRes;
        float hw = tex.getSize().x * scl / 2.f;
        float hh = tex.getSize().y * scl / 2.f;
        return (x_ + hw > kStageW / 2.f) || (x_ - hw < -kStageW / 2.f) ||
               (y_ + hh > kStageH / 2.f) || (y_ - hh < -kStageH / 2.f);
    }
    if (targetName == "_mouse_") {
        auto b = bounds();
        float mx = static_cast<float>(rt_.mouseX() + kStageW / 2.0);
        float my = static_cast<float>(kStageH / 2.0 - rt_.mouseY());
        return b.contains(mx, my);
    }
    // Check all sprites AND clones with this name (visible only)
    // Use pixel-perfect collision like Scratch
    for (auto& sp : rt_.sprites()) {
        if (sp.get() != this && sp->name() == targetName && sp->visible()) {
            if (pixelTouching(*sp)) return true;
        }
    }
    for (auto& cl : const_cast<Runtime&>(rt_).clones()) {
        if (cl.get() != this && cl->name() == targetName && cl->visible()) {
            if (pixelTouching(*cl)) return true;
        }
    }
    return false;
}

inline void Sprite::goToFront() {
    int maxLayer = 0;
    for (auto& sp : rt_.sprites()) maxLayer = std::max(maxLayer, sp->layerOrder());
    for (auto& cl : rt_.clones())  maxLayer = std::max(maxLayer, cl->layerOrder());
    layerOrder_ = maxLayer + 1;
}
inline void Sprite::goToBack() {
    int minLayer = 999;
    for (auto& sp : rt_.sprites()) minLayer = std::min(minLayer, sp->layerOrder());
    for (auto& cl : rt_.clones())  minLayer = std::min(minLayer, cl->layerOrder());
    layerOrder_ = minLayer - 1;
}
inline void Sprite::goForwardLayers(int n) { layerOrder_ += n; }
inline void Sprite::goBackwardLayers(int n) { layerOrder_ -= n; }

inline void Sprite::setVolume(double v) {
    sprVolume_ = std::clamp(v, 0.0, 100.0);
    rt_.updateSoundsForSprite(this);
}
inline void Sprite::changeVolume(double d) { setVolume(sprVolume_ + d); }

} // namespace scratch
