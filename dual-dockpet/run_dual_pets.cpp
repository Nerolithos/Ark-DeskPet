#include <SFML/Graphics.hpp>
#include <iostream>
#include <filesystem>
#include <mach-o/dyld.h>
#include "MacWindowUtils.h"

#include <spine/Extension.h>
#include <spine/Atlas.h>
#include <spine/TextureLoader.h>
#include <spine/SkeletonBinary.h>
#include <spine/Skeleton.h>
#include <spine/SkeletonData.h>
#include <spine/AnimationState.h>
#include <spine/AnimationStateData.h>
#include <spine/Animation.h>
#include <spine/RegionAttachment.h>
#include <spine/MeshAttachment.h>
#include <spine/Slot.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#include <CoreGraphics/CoreGraphics.h>
#endif
#include <random>
#include <memory>
#include <cmath>
#include <limits>
#include <chrono>
#include <algorithm>

// Provide default runtime extension (required by spine-cpp linkage)
namespace spine { SpineExtension* getDefaultExtension() { return new DefaultSpineExtension(); } }

class SfmlTextureLoader : public spine::TextureLoader {
public:
    SfmlTextureLoader(const std::string& atlasDir): _atlasDir(atlasDir) {}
    void load(spine::AtlasPage &page, const spine::String &path) override {
        std::string rel = path.buffer();
        std::filesystem::path p(rel);
        if (p.is_relative()) p = std::filesystem::path(_atlasDir) / p;
        std::string full = p.string();
        sf::Texture *tex = new sf::Texture();
        if (!tex->loadFromFile(full)) {
            std::cerr << "[TextureLoader] Primary load failed: " << full << " -> trying fuzzy search" << std::endl;
            auto stem = std::filesystem::path(rel).stem().string();
            bool loaded = false;
            try {
                for (auto &entry : std::filesystem::directory_iterator(_atlasDir)) {
                    if (!entry.is_regular_file()) continue;
                    auto ext = entry.path().extension().string();
                    if (ext == ".png" || ext == ".PNG") {
                        std::string fname = entry.path().filename().string();
                        if (fname.find(stem) != std::string::npos) {
                            if (tex->loadFromFile(entry.path().string())) { loaded = true; break; }
                        }
                    }
                }
            } catch(...) {}
            if (!loaded) { delete tex; page.setRendererObject(NULL); page.width = page.height = 0; return; }
        }
        tex->setSmooth(true);
        page.setRendererObject(tex);
        auto s = tex->getSize(); page.width = (int)s.x; page.height = (int)s.y;
    }
    void unload(void *texture) override { delete static_cast<sf::Texture*>(texture); }
private:
    std::string _atlasDir;
};

struct Character {
    // Spine core
    std::unique_ptr<spine::Atlas> atlas;
    std::unique_ptr<spine::SkeletonData> skeletonData;
    std::unique_ptr<spine::AnimationStateData> stateData;
    std::unique_ptr<spine::AnimationState> state;
    std::unique_ptr<spine::Skeleton> skeleton;

    // Animation indices & names
    std::vector<spine::String> animationNames;
    int currentAnimIndex = 0;
    int touchAnimIndex = -1;
    int key3AnimIndex = -1;      // main idle
    int moveAnimIndex = -1;      // RosMon-style move

    // Sussurro-specific indices (only used for entity2)
    int walkLeftKeyIndex = -1;
    int idleKeyIndex = -1;
    int specialKeyIndex = -1;

    // Movement & scheduling (shared logic, per character)
    enum MoveState { Move_None = 0, Move_Left = -1, Move_Right = 1 };
    MoveState moveState = Move_None;
    float moveSpeed = 90.0f;
    float leftMargin = 80.0f;
    float rightMargin = 80.0f;
    bool autoWalking = false;
    double autoWalkEndAt = 0.0;
    double nextAutoWalkAt = 0.0;

    // Timing / hold logic
    bool loop = true;
    bool pendingSwitch = false;
    float switchAt = 0.0f;
    int pendingAnimIndex = -1;
    const float minAnimHold = 0.5f;
    const float key3MinHold = 1.0f;
    double lastNonKey3Start = 0.0;
    double lastKey3Start = 0.0;
    double idleHoldUntil = 0.0;  // force short idle after touch/special

    // Touch & special flags
    bool inTouch = false;              // only used by RosMon currently
    bool playingSpecial = false;       // only meaningful for Sussurro
    double lastSpecialEndTime = -1e9;  // Sussurro
    double specialCooldown = 8.0;      // Sussurro

    // For auto-walk direction intent (RosMon-style)
    int pendingMoveAfterSwitch = 0;    // -1 left, +1 right
    bool pendingMoveIsAuto = false;

    // Dragging support
    float baseY = 0.0f;                // 初始指定高度（窗口坐标系中的 Y），用于拖拽结束后回落
    float windowOffsetY = 0.0f;        // 由窗口惯性视觉效果驱动的附加竖直偏移
};

// ==== Window inertia visual effect state ==== 
struct WindowInertiaState {
    bool dragging = false;      // 当前窗口是否被用户拖动（来自外部桥接）
    double lastUpdate = 0.0;    // 上次更新的时间戳
    double vySmooth = 0.0;      // 平滑后的窗口竖直速度
    double offsetY = 0.0;       // 作用到角色上的公共竖直偏移
};

static WindowInertiaState g_windowInertia;

// 供 Objective-C++ 桥接调用的接口（仅在 macOS 使用）
extern "C" void OnWindowKinematics(bool dragging, double px, double py,
                                    double vx, double vy, double ax, double ay) {
    // 只关心竖直方向速度 vy，用它来驱动一个非常有限的视觉偏移
    g_windowInertia.dragging = dragging;
    // 调试窗口运动：仅在有明显垂直速度或拖拽状态变化时打印
    static bool lastDragging = false;
    if (dragging != lastDragging) {
        std::cout << "[WIN] dragging=" << dragging
                  << " posY=" << py
                  << " vy=" << vy << std::endl;
        lastDragging = dragging;
    }

    double now = CFAbsoluteTimeGetCurrent();
    if (g_windowInertia.lastUpdate <= 0.0)
        g_windowInertia.lastUpdate = now;

    double dt = now - g_windowInertia.lastUpdate;
    if (dt <= 1e-4) dt = 1e-4;
    g_windowInertia.lastUpdate = now;

    // 1) 对 vy 做简单的指数平滑，避免抖动
    const double alpha = 0.15; // 平滑系数：越小越平滑
    g_windowInertia.vySmooth = (1.0 - alpha) * g_windowInertia.vySmooth + alpha * vy;

    // 2) 更新 offsetY：在拖动时由平滑速度推进，非拖动时指数衰减
    // 先把参数略微放大，方便肉眼观察惯性效果，后续可以视手感再调小
    const double maxOffset = 40.0; // 角色最大上下浮动像素
    const double driveScale = 0.05; // 速度到偏移的转换比例
    const double dampWhileDrag = 0.2; // 拖动中轻微阻尼
    const double dampFree = 1.2;     // 拖动结束后更强的阻尼

    if (dragging) {
        // 拖动窗口时：用 vySmooth 推进 offset，再施加轻微阻尼
        g_windowInertia.offsetY += g_windowInertia.vySmooth * driveScale;
        // 减少累积过多能量
        double decay = std::exp(-dampWhileDrag * dt);
        g_windowInertia.offsetY *= decay;
    } else {
        // 拖动结束：只做阻尼衰减，让角色在 0.5~1 秒内平滑回到 0
        double decay = std::exp(-dampFree * dt);
        g_windowInertia.offsetY *= decay;
    }

    // Clamp 偏移，避免“甩飞”
    if (g_windowInertia.offsetY > maxOffset) g_windowInertia.offsetY = maxOffset;
    if (g_windowInertia.offsetY < -maxOffset) g_windowInertia.offsetY = -maxOffset;

    // 周期性调试输出：每次调用都打印一次当前窗口惯性状态（频率通常较低）
    std::cout << "[WIN] kinematics: dragging=" << dragging
              << " posY=" << py
              << " vy=" << vy
              << " vySmooth=" << g_windowInertia.vySmooth
              << " offsetY=" << g_windowInertia.offsetY
              << std::endl;
}

// Bounds helpers for a given skeleton
static std::pair<float,float> computeSkeletonBounds(spine::Skeleton& skeleton) {
    float minX = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    spine::Vector<spine::Slot*> &drawOrder = skeleton.getDrawOrder();
    for (int si = 0; si < (int)drawOrder.size(); ++si) {
        spine::Slot* slot = drawOrder[si];
        spine::Attachment* att = slot->getAttachment();
        if (!att) continue;
        if (att->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
            spine::RegionAttachment* ra = (spine::RegionAttachment*)att;
            float verts[8];
            ra->computeWorldVertices(slot->getBone(), verts, 0, 2);
            for (int v = 0; v < 4; ++v) {
                minX = std::min(minX, verts[v*2]);
                maxX = std::max(maxX, verts[v*2]);
            }
        } else if (att->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
            spine::MeshAttachment* ma = (spine::MeshAttachment*)att;
            int worldLength = ma->getWorldVerticesLength();
            spine::Vector<float> worldVertices;
            worldVertices.setSize(worldLength, 0);
            ma->computeWorldVertices(*slot, 0, worldLength, worldVertices.buffer(), 0, 2);
            for (int vi = 0; vi < worldLength/2; ++vi) {
                float x = worldVertices[vi*2];
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
            }
        }
    }
    if (minX == std::numeric_limits<float>::infinity()) {
        minX = maxX = skeleton.getX();
    }
    return std::make_pair(minX, maxX);
}

static std::pair<float,float> computeSkeletonYBounds(spine::Skeleton& skeleton) {
    float minY = std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
    spine::Vector<spine::Slot*> &drawOrder = skeleton.getDrawOrder();
    for (int si = 0; si < (int)drawOrder.size(); ++si) {
        spine::Slot* slot = drawOrder[si];
        spine::Attachment* att = slot->getAttachment();
        if (!att) continue;
        if (att->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
            spine::RegionAttachment* ra = (spine::RegionAttachment*)att;
            float verts[8];
            ra->computeWorldVertices(slot->getBone(), verts, 0, 2);
            for (int v = 0; v < 4; ++v) {
                minY = std::min(minY, verts[v*2+1]);
                maxY = std::max(maxY, verts[v*2+1]);
            }
        } else if (att->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
            spine::MeshAttachment* ma = (spine::MeshAttachment*)att;
            int worldLength = ma->getWorldVerticesLength();
            spine::Vector<float> worldVertices;
            worldVertices.setSize(worldLength, 0);
            ma->computeWorldVertices(*slot, 0, worldLength, worldVertices.buffer(), 0, 2);
            for (int vi = 0; vi < worldLength/2; ++vi) {
                float y = worldVertices[vi*2+1];
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (minY == std::numeric_limits<float>::infinity()) {
        minY = maxY = skeleton.getY();
    }
    return std::make_pair(minY, maxY);
}

// RosMon-style requestSwitchTo, parameterized by character & clocks
static void requestSwitchTo(Character& ch, sf::Clock& wallClock, int animIdx, bool shouldLoop) {
    if (!ch.state || animIdx < 0 || animIdx >= (int)ch.animationNames.size()) return;
    double now = wallClock.getElapsedTime().asSeconds();

    if (animIdx == ch.key3AnimIndex) {
        double earliest = ch.lastNonKey3Start + (double)ch.minAnimHold;
        if (now >= earliest) {
            ch.currentAnimIndex = animIdx;
            ch.state->setAnimation(0, ch.animationNames[ch.currentAnimIndex], shouldLoop);
            ch.lastKey3Start = now;
            // stop movement when entering key3
            if (ch.moveState != Character::Move_None) {
                ch.moveState = Character::Move_None;
                ch.autoWalking = false;
            }
        } else {
            ch.pendingSwitch = true;
            ch.switchAt = (float)earliest;
            ch.pendingAnimIndex = animIdx;
        }
        return;
    }

    if (ch.currentAnimIndex == ch.key3AnimIndex && ch.key3AnimIndex >= 0) {
        double earliest = ch.lastKey3Start + (double)ch.key3MinHold;
        if (now < earliest) {
            ch.pendingSwitch = true;
            ch.switchAt = (float)earliest;
            ch.pendingAnimIndex = animIdx;
            return;
        }
    }

    ch.currentAnimIndex = animIdx;
    ch.state->setAnimation(0, ch.animationNames[ch.currentAnimIndex], shouldLoop);
    ch.lastNonKey3Start = now;
}

// Touch listener for RosMon (entity1), extended to set idleHoldUntil
class RosMonTouchListener : public spine::AnimationStateListenerObject {
public:
    Character* ch;
    sf::Clock* wallClockPtr;
    RosMonTouchListener(Character* c, sf::Clock* wc) : ch(c), wallClockPtr(wc) {}

    void callback(spine::AnimationState* st, spine::EventType type, spine::TrackEntry* entry, spine::Event* event) override {
        if (!ch || !wallClockPtr) return;
        if (type == spine::EventType_Complete || type == spine::EventType_End) {
            if (!entry) return;
            spine::Animation* a = entry->getAnimation();
            if (!a) return;
            const char* name = a->getName().buffer();
            if (ch->touchAnimIndex >= 0 &&
                std::strcmp(name, ch->animationNames[ch->touchAnimIndex].buffer()) == 0) {
                // 触摸动画结束：立刻清除 inTouch，回到 idle，并设置 1~2 秒的 idleHold
                ch->inTouch = false;
                std::mt19937 localRng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
                std::uniform_real_distribution<double> idleHoldDist(1.0, 2.0);
                double now = wallClockPtr->getElapsedTime().asSeconds();
                ch->idleHoldUntil = now + idleHoldDist(localRng);
                if (ch->key3AnimIndex >= 0) {
                    ch->currentAnimIndex = ch->key3AnimIndex;
                    ch->state->setAnimation(0, ch->animationNames[ch->currentAnimIndex], true);
                    ch->lastKey3Start = now;
                }
                ch->moveState = Character::Move_None;
                ch->autoWalking = false;
                ch->pendingSwitch = false;
                ch->pendingAnimIndex = -1;
            }
        }
    }
};

int main(int argc, char** argv) {
    // ==== Shared resource dir resolution (from RosMon) ====
    std::filesystem::path resourceDir;
    std::error_code cwdEc;
    try { resourceDir = std::filesystem::current_path(cwdEc); } catch(...) {}
    char buf[1024]; uint32_t sz = sizeof(buf);
    if (resourceDir.empty()) {
        if (_NSGetExecutablePath(buf, &sz) == 0) {
            std::error_code ec; auto exe = std::filesystem::weakly_canonical(std::filesystem::path(buf), ec);
            if (!ec) {
                auto dir = exe.parent_path();
                std::string ds = dir.string();
                if (ds.find(".app/Contents/MacOS") != std::string::npos)
                    resourceDir = dir.parent_path() / "Resources";
                else
                    resourceDir = dir;
            }
        }
    } else {
        if (_NSGetExecutablePath(buf, &sz) == 0) {
            std::error_code ec; auto exe = std::filesystem::weakly_canonical(std::filesystem::path(buf), ec);
            if (!ec) {
                auto dir = exe.parent_path();
                std::string ds = dir.string();
                if (ds.find(".app/Contents/MacOS") != std::string::npos)
                    resourceDir = dir.parent_path() / "Resources";
            }
        }
    }
    std::cout << "ResourceDir: " << resourceDir << std::endl;

    // ==== Load RosMon (entity1) ====
    Character ros;
    {
        std::filesystem::path atlasPath = resourceDir / "rosmon.atlas";
        if (!std::filesystem::exists(atlasPath)) {
            std::cerr << "rosmon.atlas not found: " << atlasPath << std::endl;
            return 1;
        }
        std::string atlasDir = atlasPath.parent_path().string();
        if (atlasDir.empty()) atlasDir = ".";
        auto texLoader = std::make_unique<SfmlTextureLoader>(atlasDir);
        ros.atlas = std::make_unique<spine::Atlas>(atlasPath.string().c_str(), texLoader.get(), true);
        // texLoader lifetime is managed by Atlas; we intentionally leak texLoader or keep it static if needed
        spine::SkeletonBinary bin(ros.atlas.get());
        bin.setScale(1.0f);
        spine::SkeletonData* data = bin.readSkeletonDataFile((resourceDir/"rosmon.skel").string().c_str());
        if (!data) { std::cerr << "Failed to load rosmon skeleton data" << std::endl; return 2; }
        ros.skeletonData.reset(data);
        ros.stateData = std::make_unique<spine::AnimationStateData>(ros.skeletonData.get());
        ros.state = std::make_unique<spine::AnimationState>(ros.stateData.get());
        ros.skeleton = std::make_unique<spine::Skeleton>(ros.skeletonData.get());
        ros.skeleton->setPosition(480, 600);
        ros.skeleton->setScaleY(-1.0f);
        // gather animations
        for (int i = 0; i < (int)ros.skeletonData->getAnimations().size(); ++i)
            ros.animationNames.push_back(ros.skeletonData->getAnimations()[i]->getName());

        // --- 完全复用 runros 的设计思路来确定 RosMon 的“待机”(key3) ---
        // 1) 先找出 touch 动画索引（名字包含 touch/tap），否则回退到 1
        ros.touchAnimIndex = -1;
        for (int i = 0; i < (int)ros.animationNames.size(); ++i) {
            std::string n = ros.animationNames[i].buffer();
            std::string lower; lower.resize(n.size());
            std::transform(n.begin(), n.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
            if (lower.find("touch") != std::string::npos || lower.find("tap") != std::string::npos) {
                ros.touchAnimIndex = i; break;
            }
        }
        if (ros.touchAnimIndex == -1 && ros.animationNames.size() > 1) ros.touchAnimIndex = 1;

        // 2) 构建数字键映射列表（排除 touch），与 runros 的 animationIndicesForNumbers 一致
        std::vector<int> rosAnimationIndicesForNumbers;
        for (int i = 0; i < (int)ros.animationNames.size(); ++i) {
            if (i == ros.touchAnimIndex) continue;
            rosAnimationIndicesForNumbers.push_back(i);
        }

        // 3) 用“数字键 3 对应的动画”作为 key3/待机动画，这就是原版 runros 的待机定义
        if (rosAnimationIndicesForNumbers.size() >= 3)
            ros.key3AnimIndex = rosAnimationIndicesForNumbers[2];
        else if (!ros.animationNames.empty())
            ros.key3AnimIndex = 0;

        // 4) 设置当前动画为 key3，并启动循环
        ros.currentAnimIndex = (ros.key3AnimIndex >= 0 ? ros.key3AnimIndex : 0);
        ros.state->setAnimation(0, ros.animationNames[ros.currentAnimIndex], true);

        // 5) 移动动画索引：保持之前策略（名字包含 move/walk，找不到就用 1）
        ros.moveAnimIndex = -1;
        for (int i = 0; i < (int)ros.animationNames.size(); ++i) {
            std::string n = ros.animationNames[i].buffer();
            std::string lower; lower.resize(n.size());
            std::transform(n.begin(), n.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
            if (lower.find("move") != std::string::npos || lower.find("walk") != std::string::npos) {
                ros.moveAnimIndex = i; break;
            }
        }
        if (ros.moveAnimIndex == -1 && ros.animationNames.size() >= 2) ros.moveAnimIndex = 1;
    }

    // ==== Load Sussurro (entity2), reusing RosMon’s window/bounds later ====
    Character sus;
    {
        std::filesystem::path atlasPath = resourceDir / "build_char_298_susuro_summer#6.atlas";
        if (!std::filesystem::exists(atlasPath)) {
            std::cerr << "Sussurro atlas not found: " << atlasPath << std::endl;
            return 3;
        }
        std::string atlasDir = atlasPath.parent_path().string();
        if (atlasDir.empty()) atlasDir = ".";
        auto texLoader2 = std::make_unique<SfmlTextureLoader>(atlasDir);
        sus.atlas = std::make_unique<spine::Atlas>(atlasPath.string().c_str(), texLoader2.get(), true);
        spine::SkeletonBinary bin2(sus.atlas.get());
        bin2.setScale(1.0f);
        spine::SkeletonData* data2 = bin2.readSkeletonDataFile((resourceDir/"build_char_298_susuro_summer#6.skel").string().c_str());
        if (!data2) { std::cerr << "Failed to load Sussurro skeleton data" << std::endl; return 4; }
        sus.skeletonData.reset(data2);
        sus.stateData = std::make_unique<spine::AnimationStateData>(sus.skeletonData.get());
        sus.state = std::make_unique<spine::AnimationState>(sus.stateData.get());
        sus.skeleton = std::make_unique<spine::Skeleton>(sus.skeletonData.get());
        sus.skeleton->setScaleY(-1.0f);
        if (sus.skeleton->getScaleX() == 0.0f) sus.skeleton->setScaleX(1.0f);
        // animations
        for (int i = 0; i < (int)sus.skeletonData->getAnimations().size(); ++i)
            sus.animationNames.push_back(sus.skeletonData->getAnimations()[i]->getName());
        std::cout << "Sussurro animations:" << std::endl;
        for (int i = 0; i < (int)sus.animationNames.size(); ++i)
            std::cout << "  [" << i << "] " << sus.animationNames[i].buffer() << std::endl;
        // per your mapping: 1 touch, 2 walk, 3 idle, 6 special, ignoring 0/4/5
        auto safeIndex = [&](int idx) {
            return (idx >= 0 && idx < (int)sus.animationNames.size()) ? idx : -1;
        };
        sus.touchAnimIndex = safeIndex(1);
        sus.walkLeftKeyIndex = safeIndex(2);
        sus.idleKeyIndex = safeIndex(3);
        sus.specialKeyIndex = safeIndex(6);
        sus.key3AnimIndex = sus.idleKeyIndex;
        if (sus.key3AnimIndex >= 0) {
            sus.currentAnimIndex = sus.key3AnimIndex;
            sus.state->setAnimation(0, sus.animationNames[sus.currentAnimIndex], true);
        }
        sus.moveAnimIndex = sus.walkLeftKeyIndex;
        sus.specialCooldown = 8.0;
    }

    // ==== Window/background from RosMon ====
    unsigned int w = 960, h = 640;
    sf::Texture bgTex;
    std::unique_ptr<sf::Sprite> bgSpritePtr;
    bool haveBackground = false;
    {
        std::filesystem::path bgPath = resourceDir / "background.png";
        if (std::filesystem::exists(bgPath)) {
            if (bgTex.loadFromFile(bgPath.string())) {
                haveBackground = true;
                auto s = bgTex.getSize();
                unsigned int origW = s.x, origH = s.y;
#if defined(__APPLE__)
                CGRect screenBounds = CGDisplayBounds(CGMainDisplayID());
                unsigned int screenW = (unsigned int)CGRectGetWidth(screenBounds);
                unsigned int screenH = (unsigned int)CGRectGetHeight(screenBounds);
#else
                auto desktop = sf::VideoMode::getDesktopMode();
                unsigned int screenW = desktop.width;
                unsigned int screenH = desktop.height;
#endif
                const float maxFraction = 0.6f;
                float maxW = screenW * maxFraction;
                float maxH = screenH * maxFraction;
                float scale = 1.0f;
                if ((float)origW > maxW || (float)origH > maxH) {
                    float sx = maxW / (float)origW;
                    float sy = maxH / (float)origH;
                    scale = std::min(sx, sy);
                    if (scale <= 0.0f) scale = 1.0f;
                }
                w = (unsigned int)std::max(1u, (unsigned int)std::lround(origW * scale));
                h = (unsigned int)std::max(1u, (unsigned int)std::lround(origH * scale));
                bgSpritePtr.reset(new sf::Sprite(bgTex));
                if (scale != 1.0f) bgSpritePtr->setScale(sf::Vector2f(scale, scale));
                bgSpritePtr->setPosition(sf::Vector2f(0.f, 0.f));
            }
        }
        if (!haveBackground && ros.atlas && ros.atlas->getPages().size() > 0) {
            spine::AtlasPage* p = ros.atlas->getPages()[0];
            if (p && p->width > 0 && p->height > 0) { w = p->width; h = p->height; }
        }
    }

    sf::VideoMode vm(sf::Vector2u{w,h});
    sf::RenderWindow window(vm, "DualPets", sf::Style::None);
    auto got = window.getSettings();
    std::cout << "ContextSettings: depthBits=" << got.depthBits
              << " stencilBits=" << got.stencilBits
              << " aa=" << got.antiAliasingLevel
              << " major=" << got.majorVersion
              << " minor=" << got.minorVersion
              << " sRgb=" << got.sRgbCapable << std::endl;
    window.setFramerateLimit(60);
#ifdef __APPLE__
    mac_configure_window(window.getNativeHandle());
#endif
    window.requestFocus();

    // Center both characters; give RosMon and Sussurro independent vertical placement,
    // then scale Sussurro to roughly match RosMon size (with Sussurro slightly larger).
    {
        // ==== RosMon placement（实体 1）：单独计算并大幅上移 ====
        auto rosYB = computeSkeletonYBounds(*ros.skeleton);
        float rosCurrentY = ros.skeleton->getY();
        // 在原始基础上把实体 1 明显抬高（相对最初高度有较大提升）
        const float rosMoveUp = 12.0f + 5.0f + 16.0f;
        const float bottomMargin = 4.0f;
        float rosDesiredY = rosCurrentY - rosMoveUp;
        float rosSkMaxY = rosYB.second;
        float rosDeltaY = rosDesiredY - rosCurrentY;
        float rosNewMaxY = rosSkMaxY + rosDeltaY;
        float targetBottom = (float)h - bottomMargin;
        if (rosNewMaxY > targetBottom) rosDesiredY -= (rosNewMaxY - targetBottom);

        float sx = (float)w * 0.5f;
        if (sx < ros.leftMargin) sx = ros.leftMargin;
        if (sx > (float)w - ros.rightMargin) sx = (float)w - ros.rightMargin;
    ros.skeleton->setPosition(sx, rosDesiredY);
    ros.baseY = rosDesiredY;

    // ==== Sussurro placement（实体 2）：初始化在实体 1 下面 15 像素 ====
    // 此处直接以 RosMon 最终 Y 为基准，向下 15 像素放置实体 2
    float susDesiredY = rosDesiredY + 15.0f;
    sus.skeleton->setPosition(sx, susDesiredY);
    sus.baseY = susDesiredY;

        // ==== 宽度对齐 + 全局缩放 ====
        auto rosX = computeSkeletonBounds(*ros.skeleton);
        auto susX = computeSkeletonBounds(*sus.skeleton);
        float rosW = rosX.second - rosX.first;
        float susW = susX.second - susX.first;
        float susTargetScale = 1.0f;
        if (rosW > 1.0f && susW > 1.0f) {
            susTargetScale = rosW / susW;
            // 保守一些，避免 Sussurro 明显比 RosMon 大
            if (susTargetScale < 0.4f) susTargetScale = 0.4f;
            if (susTargetScale > 1.2f) susTargetScale = 1.2f;
        }
        float susSignX = sus.skeleton->getScaleX() >= 0.0f ? 1.0f : -1.0f;
        sus.skeleton->setScaleX(susSignX * susTargetScale);
        sus.skeleton->setScaleY(-std::abs(susTargetScale));

        // 再统一整体缩小一点，让两个角色在窗口里看起来都小一些且相近。
        // 将 RosMon 再略微放大一点
        const float globalScale = 0.83f; // 在 0.81f 基础上再提升
        float rosSignX = ros.skeleton->getScaleX() >= 0.0f ? 1.0f : -1.0f;
        ros.skeleton->setScaleX(rosSignX * globalScale);
        ros.skeleton->setScaleY(-std::abs(globalScale));

        // 对 Sussurro：在已经按宽度匹配后，再乘以一个稍大的因子，让它比 RosMon 略大
        const float susGlobalScale = 0.9f; // 比 RosMon 略大一些
        float scaleRatio = susGlobalScale / std::abs(susTargetScale);
        sus.skeleton->setScaleX(sus.skeleton->getScaleX() * scaleRatio);
        sus.skeleton->setScaleY(-std::abs(susGlobalScale));
    }

    // Shared timers & RNG
    sf::Clock clock;
    sf::Clock wallClock;
    std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
    std::normal_distribution<double> autoDelayDist(7.0, 1.6);
    std::uniform_real_distribution<double> walkDurDist(0.6, 2.2);
    auto sampleAutoDelay = [&](){ double d = autoDelayDist(rng); if (d < 3.0) d = 3.0; return d; };

    double startNow = wallClock.getElapsedTime().asSeconds();
    // RosMon: 更快看到第一次自动行走
    ros.nextAutoWalkAt = startNow + 3.0; // 大约 3 秒后开始第一次自动行走
    // Sussurro: 保持更快的首次自动行为
    sus.nextAutoWalkAt = startNow + 1.0; // 1 秒后即可开始首次自动行为

    // Attach RosMon touch listener
    RosMonTouchListener* rosTouchListener = new RosMonTouchListener(&ros, &wallClock);
    ros.state->setListener(rosTouchListener);

    enum ActiveEntity { ENTITY_ROS = 1, ENTITY_SUS = 2, ENTITY_BOTH = 3 };
    ActiveEntity active = ENTITY_ROS;

    // 全局暂停/播放开关，由空格控制
    bool paused = false;

    bool firstDiagDone = false;

    // ==== Mouse long-press dragging state ====
    bool mouseDown = false;
    double mouseDownTime = 0.0;            // 按下时刻（秒）
    sf::Vector2f mouseDownPos(0.f, 0.f);   // 按下时鼠标位置
    Character* pressedTarget = nullptr;    // 按下命中的角色
    bool dragging = false;
    sf::Vector2f dragOffset(0.f, 0.f);     // skeleton 位置与鼠标的偏移量
    // 拖拽结束后的平滑下落
    Character* droppingTarget = nullptr;   // 正在下落回 baseY 的角色
    float dropStartY = 0.0f;               // 下落开始时的 Y
    double dropStartTime = 0.0;            // 下落开始时间
    const double dropDuration = 0.25;      // 下落持续时间（秒），稍微加快一点

    while (window.isOpen()) {
        // ==== Input ====
        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) { window.close(); }

            if (ev->is<sf::Event::MouseButtonPressed>()) {
                auto mb = ev->getIf<sf::Event::MouseButtonPressed>();
                if (!mb) continue;
                if (mb->button != sf::Mouse::Button::Left) continue;

                mouseDown = true;
                mouseDownTime = wallClock.getElapsedTime().asSeconds();
                mouseDownPos = sf::Vector2f((float)mb->position.x, (float)mb->position.y);
                dragging = false;
                pressedTarget = nullptr;

                float mx = mouseDownPos.x;
                float my = mouseDownPos.y;

                // 计算两个实体当前的包围盒，用于点击判定
                auto rosXb = computeSkeletonBounds(*ros.skeleton);
                auto rosYb = computeSkeletonYBounds(*ros.skeleton);
                bool hitRos = (mx >= rosXb.first && mx <= rosXb.second &&
                               my >= rosYb.first && my <= rosYb.second);

                auto susXb = computeSkeletonBounds(*sus.skeleton);
                auto susYb = computeSkeletonYBounds(*sus.skeleton);
                bool hitSus = (mx >= susXb.first && mx <= susXb.second &&
                               my >= susYb.first && my <= susYb.second);

                // 记录被按下的目标，但暂不触发触摸动画，等待长按/短按判定
                if (active == ENTITY_ROS) {
                    if (hitRos) pressedTarget = &ros;
                } else if (active == ENTITY_SUS) {
                    if (hitSus) pressedTarget = &sus;
                } else { // ENTITY_BOTH
                    if (hitRos && hitSus) {
                        pressedTarget = &sus; // 都点到时优先实体 2
                    } else if (hitSus) {
                        pressedTarget = &sus;
                    } else if (hitRos) {
                        pressedTarget = &ros;
                    }
                }

                if (pressedTarget) {
                    // 预先计算拖拽偏移
                    if (!pressedTarget->skeleton) {
                        std::cout << "[DRAG] mouseDown but target has null skeleton!" << std::endl;
                    } else {
                        float sx = pressedTarget->skeleton->getX();
                        float sy = pressedTarget->skeleton->getY();
                        dragOffset = sf::Vector2f(sx - mouseDownPos.x, sy - mouseDownPos.y);
                        std::cout << "[DRAG] mouseDown on target="
                                  << (pressedTarget == &ros ? "Ros" : "Sus")
                                  << ", sx=" << sx << ", sy=" << sy
                                  << ", mx=" << mouseDownPos.x << ", my=" << mouseDownPos.y << std::endl;
                    }
                }
            }

            if (ev->is<sf::Event::MouseButtonReleased>()) {
                auto mr = ev->getIf<sf::Event::MouseButtonReleased>();
                if (!mr) continue;
                if (mr->button != sf::Mouse::Button::Left) continue;

                double upTime = wallClock.getElapsedTime().asSeconds();
                double held = upTime - mouseDownTime;

                // 如果没有进入拖拽，并且按下时间短于 0.5 秒，则作为“短按触摸”处理
                if (!dragging && pressedTarget && held < 0.5) {
                    Character* target = pressedTarget;
                    std::cout << "[DRAG] short click, target="
                              << (target == &ros ? "Ros" : "Sus")
                              << " held=" << held << std::endl;
                    if (target->touchAnimIndex >= 0) {
                        target->moveState = Character::Move_None;
                        target->autoWalking = false;
                        target->playingSpecial = false;
                        if (target == &ros) {
                            target->inTouch = true;
                        }
                        requestSwitchTo(*target, wallClock, target->touchAnimIndex, false);
                    }
                }

                // 鼠标抬起时，如果正在拖拽，启动平滑下落回各自 baseY 并恢复随机循环
                if (dragging && pressedTarget && pressedTarget->skeleton) {
                    float sx = pressedTarget->skeleton->getX();
                    dropStartY = pressedTarget->skeleton->getY();
                    dropStartTime = wallClock.getElapsedTime().asSeconds();
                    droppingTarget = pressedTarget;
                    std::cout << "[DRAG] release after drag, target="
                              << (pressedTarget == &ros ? "Ros" : "Sus")
                              << " dropStartY=" << dropStartY
                              << " baseY=" << pressedTarget->baseY << std::endl;
                    pressedTarget->moveState = Character::Move_None;
                    pressedTarget->autoWalking = false;
                    if (pressedTarget->key3AnimIndex >= 0) {
                        requestSwitchTo(*pressedTarget, wallClock, pressedTarget->key3AnimIndex, true);
                    }
                }

                mouseDown = false;
                dragging = false;
                pressedTarget = nullptr;
            }

            if (ev->is<sf::Event::KeyPressed>()) {
                auto kp = ev->getIf<sf::Event::KeyPressed>();
                if (!kp) continue;
                if (kp->code == sf::Keyboard::Key::Escape) { window.close(); break; }

                // 空格：暂停/播放（仅暂停动画和自动逻辑，仍可切换模式等）
                if (kp->code == sf::Keyboard::Key::Space) {
                    paused = !paused;
                    continue;
                }

                // Switch active entity: 1 = RosMon, 2 = Sussurro, 3 = both
                if (kp->code == sf::Keyboard::Key::Num1) {
                    active = ENTITY_ROS;
                    continue;
                }
                if (kp->code == sf::Keyboard::Key::Num2) {
                    active = ENTITY_SUS;
                    continue;
                }
                if (kp->code == sf::Keyboard::Key::Num3) {
                    active = ENTITY_BOTH;
                    continue;
                }
                // 除了 1/2/3/Space/ESC，其它按键一律忽略
            }
        }

        float dt = clock.restart().asSeconds();
        if (dt > 0.1f) dt = 0.1f;
        if (paused) {
            // 暂停时：只画画面，不更新动画/移动/拖拽
            window.clear(sf::Color::White);
            if (haveBackground && bgSpritePtr) window.draw(*bgSpritePtr);

            auto drawSkeleton = [&](Character& ch) {
                if (!ch.skeleton) return;
                spine::Vector<spine::Slot*>& drawOrder = ch.skeleton->getDrawOrder();
                for (int i = 0; i < (int)drawOrder.size(); ++i) {
                    spine::Slot* slot = drawOrder[i];
                    spine::Attachment* att = slot->getAttachment();
                    if (!att) continue;
                    if (att->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
                        spine::RegionAttachment* ra = (spine::RegionAttachment*)att;
                        float verts[8];
                        ra->computeWorldVertices(slot->getBone(), verts, 0, 2);
                        float* uvs = ra->getUVs().buffer();
                        spine::AtlasRegion* region = (spine::AtlasRegion*)ra->getRendererObject();
                        sf::Texture* tex = NULL;
                        if (region && region->page)
                            tex = static_cast<sf::Texture*>(region->page->getRendererObject());
                        if (!tex) continue;
                        sf::Vertex quad[6]; sf::Vector2f pos[4]; sf::Vector2f texc[4];
                        for (int v = 0; v < 4; ++v) {
                            pos[v] = sf::Vector2f(verts[v*2], verts[v*2+1]);
                            texc[v] = sf::Vector2f(uvs[v*2] * tex->getSize().x,
                                                   uvs[v*2+1] * tex->getSize().y);
                        }
                        sf::Color col(255,255,255,255);
                        auto setV=[&](int idx,int src){ quad[idx].position = pos[src]; quad[idx].texCoords = texc[src]; quad[idx].color = col; };
                        setV(0,0); setV(1,1); setV(2,2); setV(3,0); setV(4,2); setV(5,3);
                        sf::RenderStates states; states.texture = tex;
                        window.draw(quad, 6, sf::PrimitiveType::Triangles, states);
                    } else if (att->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
                        spine::MeshAttachment* ma = (spine::MeshAttachment*)att;
                        int worldLength = ma->getWorldVerticesLength();
                        spine::Vector<float> worldVertices; worldVertices.setSize(worldLength, 0);
                        ma->computeWorldVertices(*slot, 0, worldLength, worldVertices.buffer(), 0, 2);
                        spine::Vector<unsigned short>& tris = ma->getTriangles();
                        float* uvs = ma->getUVs().buffer();
                        spine::AtlasRegion* region = (spine::AtlasRegion*)ma->getRendererObject();
                        if (!region || !region->page) continue;
                        sf::Texture* tex = static_cast<sf::Texture*>(region->page->getRendererObject());
                        if (!tex) continue;
                        sf::VertexArray arr(sf::PrimitiveType::Triangles, tris.size());
                        for (int t = 0; t < (int)tris.size(); ++t) {
                            int vi = tris[t];
                            float x = worldVertices[vi*2];
                            float y = worldVertices[vi*2+1];
                            arr[t].position = sf::Vector2f(x,y);
                            arr[t].texCoords = sf::Vector2f(uvs[vi*2] * tex->getSize().x,
                                                            uvs[vi*2+1] * tex->getSize().y);
                            arr[t].color = sf::Color::White;
                        }
                        sf::RenderStates rs; rs.texture = tex;
                        window.draw(arr, rs);
                    }
                }
            };

            if (active == ENTITY_ROS) {
                drawSkeleton(ros);
            } else if (active == ENTITY_SUS) {
                drawSkeleton(sus);
            } else {
                drawSkeleton(ros);
                drawSkeleton(sus);
            }

            window.display();
            continue;
        }
        double nowSec = wallClock.getElapsedTime().asSeconds();

        // ==== Dragging update（长按拖拽）：独立于行走动画的平移逻辑 ====
        if (mouseDown && !dragging && pressedTarget) {
            if (nowSec - mouseDownTime >= 0.35) {
                dragging = true;
                if (pressedTarget->key3AnimIndex >= 0) {
                    requestSwitchTo(*pressedTarget, wallClock, pressedTarget->key3AnimIndex, true);
                }
                pressedTarget->moveState = Character::Move_None;
                pressedTarget->autoWalking = false;
                if (pressedTarget->skeleton) {
                    std::cout << "[DRAG] begin dragging target="
                              << (pressedTarget == &ros ? "Ros" : "Sus")
                              << " at sx=" << pressedTarget->skeleton->getX()
                              << " sy=" << pressedTarget->skeleton->getY() << std::endl;
                } else {
                    std::cout << "[DRAG] begin dragging but target skeleton is null" << std::endl;
                }
            }
        }

        if (dragging && pressedTarget && pressedTarget->skeleton) {
            sf::Vector2i mp = sf::Mouse::getPosition(window);
            float mx = (float)mp.x;
            float my = (float)mp.y;

            float targetX = mx + dragOffset.x;
            float targetY = my + dragOffset.y;

            unsigned int winW = window.getSize().x;
            unsigned int winH = window.getSize().y;
            if (targetX < 0.0f) targetX = 0.0f;
            if (targetX > (float)winW) targetX = (float)winW;
            if (targetY < 0.0f) targetY = 0.0f;
            if (targetY > (float)winH) targetY = (float)winH;

            pressedTarget->skeleton->setPosition(targetX, targetY);
            static int dragDebugCounter = 0;
            if ((dragDebugCounter++ % 10) == 0) {
                std::cout << "[DRAG] pos target="
                          << (pressedTarget == &ros ? "Ros" : "Sus")
                          << " -> x=" << targetX << " y=" << targetY
                          << " (mouse=" << mx << "," << my << ")" << std::endl;
            }
        }

        // 下落插值更新：从当前 Y 以固定时间插值回 baseY
        if (droppingTarget && droppingTarget->skeleton) {
            double t = (nowSec - dropStartTime) / dropDuration;
            if (t >= 1.0) {
                // 下落结束：确保精确落到 baseY，并强制待机 1 秒，避免立刻抽到行走动画
                float sx = droppingTarget->skeleton->getX();
                droppingTarget->skeleton->setPosition(sx, droppingTarget->baseY);
                std::cout << "[DROP] finished for "
                          << (droppingTarget == &ros ? "Ros" : "Sus")
                          << " at x=" << sx << " baseY=" << droppingTarget->baseY << std::endl;
                // 如果有待机动画，确保当前就是待机动画
                if (droppingTarget->key3AnimIndex >= 0) {
                    requestSwitchTo(*droppingTarget, wallClock, droppingTarget->key3AnimIndex, true);
                }
                // 设置 1 秒的 idleHold，防止立刻触发自动行走
                droppingTarget->idleHoldUntil = nowSec + 1.0;
                droppingTarget = nullptr;
            } else if (t >= 0.0) {
                float sx = droppingTarget->skeleton->getX();
                float sy = droppingTarget->skeleton->getY();
                float targetY = droppingTarget->baseY;
                float newY = (float)((1.0 - t) * dropStartY + t * targetY);
                droppingTarget->skeleton->setPosition(sx, newY);
                std::cout << "[DROP] interpolating for "
                          << (droppingTarget == &ros ? "Ros" : "Sus")
                          << " t=" << t << " fromY=" << dropStartY
                          << " to=" << targetY << " newY=" << newY << std::endl;
            }
        }

        // ==== Window inertia visual offset update ====
        // 将全局窗口惯性偏移通过一层低通滤波后再应用到角色，以减少“离散感”/抖动
        static float visibleOffsetY = 0.0f; // 实际用于渲染的平滑偏移
        float targetOffsetY = (float)g_windowInertia.offsetY;

        if (!dragging && !droppingTarget) {
            // 基于当前帧 dt 对 targetOffsetY 做一次一阶低通滤波
            // smoothingRate 越大，跟随越快；这里取一个中等速度，让变化更顺滑一点
            const float smoothingRate = 8.0f; // 单位：1/秒
            float lerpFactor = smoothingRate * dt;
            if (lerpFactor > 1.0f) lerpFactor = 1.0f;
            visibleOffsetY += (targetOffsetY - visibleOffsetY) * lerpFactor;
        } else {
            // 拖拽 / 下落时不叠加惯性，且逐渐将可见偏移收敛回 0
            const float smoothingRate = 10.0f;
            float lerpFactor = smoothingRate * dt;
            if (lerpFactor > 1.0f) lerpFactor = 1.0f;
            visibleOffsetY += (0.0f - visibleOffsetY) * lerpFactor;
        }

        // 实际赋值给两个角色的 windowOffsetY
        ros.windowOffsetY = (!dragging && !droppingTarget) ? visibleOffsetY : 0.0f;
        sus.windowOffsetY = (!dragging && !droppingTarget) ? visibleOffsetY : 0.0f;

        // 低频记录一下面板上的惯性偏移，方便调试
        static int winOffCounter = 0;
        if ((winOffCounter++ % 60) == 0) {
            std::cout << "[WIN] frame offsetY(raw)=" << targetOffsetY
                      << " visible=" << visibleOffsetY << std::endl;
        }

        // ==== Update both characters (state + one-shot completion + auto-walk + movement) ====
        auto updateCharacter = [&](Character& ch, bool isSussurro) {
            if (!ch.state || !ch.skeleton) return;

            // 正在拖拽或下落某个角色时：该角色的动画可以继续更新，但位置交给拖拽/下落逻辑控制
            if ((dragging && &ch == pressedTarget) || (droppingTarget && &ch == droppingTarget)) {
                ch.state->update(dt);
                ch.state->apply(*ch.skeleton);
                ch.skeleton->updateWorldTransform();
                return;
            }
            ch.state->update(dt);
            ch.state->apply(*ch.skeleton);
            ch.skeleton->updateWorldTransform();

            // 应用窗口惯性视觉偏移：在非拖拽/下落状态下，基于 baseY + windowOffsetY 设置最终 Y
            if (ch.skeleton) {
                float sx = ch.skeleton->getX();
                float finalY = ch.baseY + ch.windowOffsetY;
                ch.skeleton->setPosition(sx, finalY);
            }

            // One-shot completion for Sussurro (touch/special) – from sussurro_main
            if (isSussurro) {
                spine::TrackEntry* te = ch.state->getCurrent(0);
                bool isTouch = (ch.currentAnimIndex == ch.touchAnimIndex);
                bool isSpecial = (ch.currentAnimIndex == ch.specialKeyIndex);
                bool shouldCheckOneShot = (isTouch || isSpecial);
                if (shouldCheckOneShot && te) {
                    float tt = te->getTrackTime();
                    float dur = te->getAnimation() ? te->getAnimation()->getDuration() : 0.0f;
                    if (dur > 0.0f && tt >= dur) {
                        if (isSpecial) {
                            ch.playingSpecial = false;
                            ch.lastSpecialEndTime = nowSec;
                        }
                        if (ch.idleKeyIndex >= 0) {
                            requestSwitchTo(ch, wallClock, ch.idleKeyIndex, true);
                            ch.moveState = Character::Move_None;
                            ch.autoWalking = false;
                            std::uniform_real_distribution<double> idleHoldDist(1.0, 2.0);
                            ch.idleHoldUntil = nowSec + idleHoldDist(rng);
                        }
                    }
                }
                if (shouldCheckOneShot && !te) {
                    if (isSpecial) {
                        ch.playingSpecial = false;
                        ch.lastSpecialEndTime = nowSec;
                    }
                    if (ch.idleKeyIndex >= 0) {
                        requestSwitchTo(ch, wallClock, ch.idleKeyIndex, true);
                        ch.moveState = Character::Move_None;
                        ch.autoWalking = false;
                        std::uniform_real_distribution<double> idleHoldDist(1.0, 2.0);
                        ch.idleHoldUntil = nowSec + idleHoldDist(rng);
                    }
                }

                bool inKey3Loop = (ch.currentAnimIndex == ch.key3AnimIndex &&
                                   ch.moveState == Character::Move_None &&
                                   !ch.autoWalking && !ch.playingSpecial);
                bool idleHoldActive = (nowSec < ch.idleHoldUntil);
                if (inKey3Loop && !idleHoldActive && ch.specialKeyIndex >= 0 &&
                    nowSec - ch.lastSpecialEndTime >= ch.specialCooldown) {
                    std::uniform_real_distribution<double> probDist(0.0, 1.0);
                    double p = probDist(rng);
                    if (p < 0.0012) {
                        requestSwitchTo(ch, wallClock, ch.specialKeyIndex, false);
                        ch.playingSpecial = true;
                        ch.autoWalking = false;
                        ch.moveState = Character::Move_None;
                        inKey3Loop = false;
                    }
                }

                // auto-walk like RosMon, but obey idleHold
                bool idleHoldActive2 = (nowSec < ch.idleHoldUntil);
                bool inIdleForWalk = (ch.currentAnimIndex == ch.key3AnimIndex &&
                                      ch.moveState == Character::Move_None &&
                                      !ch.autoWalking && !ch.playingSpecial);
                if (inIdleForWalk && !idleHoldActive2 && nowSec >= ch.nextAutoWalkAt) {
                    auto bounds = computeSkeletonBounds(*ch.skeleton);
                    float skMin = bounds.first;
                    float skMax = bounds.second;
                    unsigned int winW = window.getSize().x;
                    bool canGoLeft = (skMin > ch.leftMargin + 1.0f);
                    bool canGoRight = (skMax < (float)winW - ch.rightMargin - 1.0f);
                    if (!canGoLeft && !canGoRight) {
                        ch.nextAutoWalkAt = nowSec + sampleAutoDelay();
                    } else {
                        int dir = 0;
                        if (!canGoLeft && canGoRight) dir = 1;
                        else if (!canGoRight && canGoLeft) dir = -1;
                        else { std::uniform_int_distribution<int> dirPick(0,1); dir = (dirPick(rng)==0 ? -1 : 1); }
                        if (dir == -1) {
                            ch.moveState = Character::Move_Left;
                            if (ch.skeleton->getScaleX() == 0.0f) ch.skeleton->setScaleX(-1.0f);
                            else ch.skeleton->setScaleX(-std::abs(ch.skeleton->getScaleX()));
                        } else {
                            ch.moveState = Character::Move_Right;
                            if (ch.skeleton->getScaleX() == 0.0f) ch.skeleton->setScaleX(1.0f);
                            else ch.skeleton->setScaleX(std::abs(ch.skeleton->getScaleX()));
                        }
                        if (ch.moveAnimIndex >= 0)
                            requestSwitchTo(ch, wallClock, ch.moveAnimIndex, true);
                        ch.autoWalking = true;
                        spine::TrackEntry* te2 = ch.state->getCurrent(0);
                        double dur = te2 && te2->getAnimation() ? te2->getAnimation()->getDuration() : walkDurDist(rng);
                        ch.autoWalkEndAt = nowSec + dur;
                        ch.nextAutoWalkAt = ch.autoWalkEndAt + sampleAutoDelay();
                    }
                }

            } else { // RosMon
                // Auto-walk scheduling (RosMon), including idleHold 与 inTouch
                    bool inKey3Loop = (ch.currentAnimIndex == ch.key3AnimIndex &&
                                        ch.loop && ch.moveState == Character::Move_None && !ch.autoWalking && !ch.inTouch);
                    bool idleHoldActive = (nowSec < ch.idleHoldUntil);
                    if (inKey3Loop && !idleHoldActive && nowSec >= ch.nextAutoWalkAt) {
                    auto bounds = computeSkeletonBounds(*ch.skeleton);
                    float skMin = bounds.first;
                    float skMax = bounds.second;
                    unsigned int winW = window.getSize().x;
                    bool canGoLeft = (skMin > ch.leftMargin + 1.0f);
                    bool canGoRight = (skMax < (float)winW - ch.rightMargin - 1.0f);
                    if (!canGoLeft && !canGoRight) {
                        ch.nextAutoWalkAt = nowSec + sampleAutoDelay();
                    } else {
                        int dir = 0;
                        if (!canGoLeft && canGoRight) dir = 1;
                        else if (!canGoRight && canGoLeft) dir = -1;
                        else { std::uniform_int_distribution<int> dirPick(0,1); dir = (dirPick(rng)==0 ? -1 : 1); }
                        int animToPlay = -1;
                        if (dir == -1) {
                            if (ch.currentAnimIndex == ch.key3AnimIndex && ch.loop) {
                                ch.pendingMoveAfterSwitch = -1;
                                ch.pendingMoveIsAuto = true;
                            } else {
                                ch.moveState = Character::Move_Left;
                            }
                            if (ch.skeleton->getScaleX() == 0.0f) ch.skeleton->setScaleX(-1.0f);
                            else ch.skeleton->setScaleX(-std::abs(ch.skeleton->getScaleX()));
                            animToPlay = ch.moveAnimIndex;
                        } else {
                            if (ch.currentAnimIndex == ch.key3AnimIndex && ch.loop) {
                                ch.pendingMoveAfterSwitch = 1;
                                ch.pendingMoveIsAuto = true;
                            } else {
                                ch.moveState = Character::Move_Right;
                            }
                            if (ch.skeleton->getScaleX() == 0.0f) ch.skeleton->setScaleX(1.0f);
                            else ch.skeleton->setScaleX(std::abs(ch.skeleton->getScaleX()));
                            animToPlay = ch.moveAnimIndex;
                        }
                        if (animToPlay >= 0)
                            requestSwitchTo(ch, wallClock, animToPlay, true);
                        ch.autoWalking = true;
                        double dur = walkDurDist(rng);
                        ch.autoWalkEndAt = nowSec + dur;
                        ch.nextAutoWalkAt = ch.autoWalkEndAt + sampleAutoDelay();
                    }
                }
            }

            // Movement update (shared): clamp to margins, return to idle at edges or time end
            if (ch.moveState != Character::Move_None) {
                float sx = ch.skeleton->getX();
                unsigned int winW = window.getSize().x;
                float delta = ch.moveSpeed * dt * (ch.moveState == Character::Move_Right ? 1.0f : -1.0f);
                auto bounds = computeSkeletonBounds(*ch.skeleton);
                float skMin = bounds.first;
                float skMax = bounds.second;
                float desiredX = sx + delta;
                float newMin = skMin + (desiredX - sx);
                float newMax = skMax + (desiredX - sx);
                float targetLeft = ch.leftMargin;
                float targetRight = (float)winW - ch.rightMargin;
                bool reached = false;
                if (ch.moveState == Character::Move_Right) {
                    // 向右行走过程中，如果到达或越过右边界：立刻打断，回到 idle，并反转朝向
                    if (newMax >= targetRight || skMax >= targetRight - 0.5f) {
                        desiredX = std::min(desiredX, sx + (targetRight - skMax));
                        reached = true;
                    }
                } else if (ch.moveState == Character::Move_Left) {
                    // 向左行走过程中，如果到达或越过左边界：立刻打断，回到 idle，并反转朝向
                    if (newMin <= targetLeft || skMin <= targetLeft + 0.5f) {
                        desiredX = std::max(desiredX, sx + (targetLeft - skMin));
                        reached = true;
                    }
                }
                ch.skeleton->setPosition(desiredX, ch.skeleton->getY());
                if (reached) {
                    ch.moveState = Character::Move_None;
                    // 立刻切回 idle 循环
                    if (ch.key3AnimIndex >= 0)
                        requestSwitchTo(ch, wallClock, ch.key3AnimIndex, true);
                    // 在边界处反转朝向，方便下一次自动/手动向内侧行走
                    float sxScale = ch.skeleton->getScaleX();
                    if (sxScale == 0.0f) sxScale = 1.0f;
                    ch.skeleton->setScaleX(-sxScale);
                    if (ch.autoWalking) ch.autoWalking = false;
                }
            }

            if (ch.autoWalking && nowSec >= ch.autoWalkEndAt) {
                ch.autoWalking = false;
                ch.moveState = Character::Move_None;
                if (ch.key3AnimIndex >= 0)
                    requestSwitchTo(ch, wallClock, ch.key3AnimIndex, true);
            }

            // Handle pending scheduled switches
            if (ch.pendingSwitch) {
                if (ch.inTouch) {
                    // 触摸过程中不执行 pending 切换，等待触摸结束后由 Listener 直接恢复 idle
                } else {
                    float nowF = wallClock.getElapsedTime().asSeconds();
                    if (nowF >= ch.switchAt) {
                        ch.pendingSwitch = false;
                        if (ch.pendingAnimIndex >= 0 && ch.pendingAnimIndex < (int)ch.animationNames.size())
                            requestSwitchTo(ch, wallClock, ch.pendingAnimIndex, true);
                        ch.pendingAnimIndex = -1;
                    }
                }
            }

            if (ch.pendingMoveAfterSwitch != 0) {
                if (!(ch.currentAnimIndex == ch.key3AnimIndex && ch.loop)) {
                    if (ch.pendingMoveAfterSwitch == -1) ch.moveState = Character::Move_Left;
                    else if (ch.pendingMoveAfterSwitch == 1) ch.moveState = Character::Move_Right;
                    if (ch.pendingMoveIsAuto) ch.autoWalking = true;
                    ch.pendingMoveAfterSwitch = 0;
                    ch.pendingMoveIsAuto = false;
                }
            }
        };

    updateCharacter(ros, false);
    updateCharacter(sus, true);

        // ==== Render ====
        window.clear(sf::Color::White);
        if (haveBackground && bgSpritePtr) window.draw(*bgSpritePtr);

        auto drawSkeleton = [&](Character& ch) {
            if (!ch.skeleton) return;
            spine::Vector<spine::Slot*>& drawOrder = ch.skeleton->getDrawOrder();
            for (int i = 0; i < (int)drawOrder.size(); ++i) {
                spine::Slot* slot = drawOrder[i];
                spine::Attachment* att = slot->getAttachment();
                if (!att) continue;
                if (att->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
                    spine::RegionAttachment* ra = (spine::RegionAttachment*)att;
                    float verts[8];
                    ra->computeWorldVertices(slot->getBone(), verts, 0, 2);
                    float* uvs = ra->getUVs().buffer();
                    spine::AtlasRegion* region = (spine::AtlasRegion*)ra->getRendererObject();
                    sf::Texture* tex = NULL;
                    if (region && region->page)
                        tex = static_cast<sf::Texture*>(region->page->getRendererObject());
                    if (!tex) continue;
                    sf::Vertex quad[6];
                    sf::Vector2f pos[4];
                    sf::Vector2f texc[4];
                    for (int v = 0; v < 4; ++v) {
                        pos[v] = sf::Vector2f(verts[v*2], verts[v*2+1]);
                        texc[v] = sf::Vector2f(uvs[v*2] * tex->getSize().x,
                                               uvs[v*2+1] * tex->getSize().y);
                    }
                    sf::Color col(255, 255, 255, 255);
                    auto setV = [&](int idx, int src) {
                        quad[idx].position = pos[src];
                        quad[idx].texCoords = texc[src];
                        quad[idx].color = col;
                    };
                    setV(0,0); setV(1,1); setV(2,2); setV(3,0); setV(4,2); setV(5,3);
                    sf::RenderStates states;
                    states.texture = tex;
                    window.draw(quad, 6, sf::PrimitiveType::Triangles, states);
                } else if (att->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
                    spine::MeshAttachment* ma = (spine::MeshAttachment*)att;
                    int worldLength = ma->getWorldVerticesLength();
                    spine::Vector<float> worldVertices;
                    worldVertices.setSize(worldLength, 0);
                    ma->computeWorldVertices(*slot, 0, worldLength, worldVertices.buffer(), 0, 2);
                    spine::Vector<unsigned short>& tris = ma->getTriangles();
                    float* uvs = ma->getUVs().buffer();
                    spine::AtlasRegion* region = (spine::AtlasRegion*)ma->getRendererObject();
                    if (!region || !region->page) continue;
                    sf::Texture* tex = static_cast<sf::Texture*>(region->page->getRendererObject());
                    if (!tex) continue;
                    sf::VertexArray arr(sf::PrimitiveType::Triangles, tris.size());
                    for (int t = 0; t < (int)tris.size(); ++t) {
                        int vi = tris[t];
                        float x = worldVertices[vi*2];
                        float y = worldVertices[vi*2+1];
                        arr[t].position = sf::Vector2f(x,y);
                        arr[t].texCoords = sf::Vector2f(uvs[vi*2] * tex->getSize().x,
                                                        uvs[vi*2+1] * tex->getSize().y);
                        arr[t].color = sf::Color::White;
                    }
                    sf::RenderStates rs;
                    rs.texture = tex;
                    window.draw(arr, rs);
                }
            }
        };

        if (active == ENTITY_ROS) {
            drawSkeleton(ros);
        } else if (active == ENTITY_SUS) {
            drawSkeleton(sus);
        } else { // ENTITY_BOTH
            drawSkeleton(ros);
            drawSkeleton(sus);
        }

        window.display();

        if (!firstDiagDone) {
            firstDiagDone = true;
#if defined(__APPLE__)
            GLint x = 10, y = 10; GLubyte px[4] = {0,0,0,0};
            glFlush();
            glReadBuffer(GL_BACK);
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            std::cout << "FB Pixel RGBA at ("<<x<<","<<y<<") = ("<<(int)px[0]<<","<<(int)px[1]<<","<<(int)px[2]<<","<<(int)px[3]<<")" << std::endl;
#endif
        }
    }

    return 0;
}