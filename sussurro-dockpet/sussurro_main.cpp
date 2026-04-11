// Replace with a copy of the RosMon loop logic, adapted for Sussurro assets
#include <SFML/Graphics.hpp>
#include <iostream>
#include <filesystem>
#include <mach-o/dyld.h>
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
#include <random>

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

int main(int argc, char** argv) {
    // Copy of RosMon resource resolution
    std::filesystem::path resourceDir;
    std::error_code cwdEc;
    try { resourceDir = std::filesystem::current_path(cwdEc); } catch(...) {}
    char buf[1024]; uint32_t sz = sizeof(buf);
    if (resourceDir.empty()) {
        if (_NSGetExecutablePath(buf, &sz) == 0) {
            std::error_code ec; auto exe = std::filesystem::weakly_canonical(std::filesystem::path(buf), ec);
            if (!ec) {
                auto dir = exe.parent_path(); std::string ds = dir.string();
                if (ds.find(".app/Contents/MacOS") != std::string::npos) resourceDir = dir.parent_path() / "Resources";
                else resourceDir = dir;
            }
        }
    } else {
        if (_NSGetExecutablePath(buf, &sz) == 0) {
            std::error_code ec; auto exe = std::filesystem::weakly_canonical(std::filesystem::path(buf), ec);
            if (!ec) {
                auto dir = exe.parent_path(); std::string ds = dir.string();
                if (ds.find(".app/Contents/MacOS") != std::string::npos) resourceDir = dir.parent_path() / "Resources";
            }
        }
    }
    std::cout << "ResourceDir: " << resourceDir << std::endl;

    // Use Sussurro atlas/skel
    std::filesystem::path atlasPath = resourceDir / "build_char_298_susuro_summer#6.atlas";
    if (!std::filesystem::exists(atlasPath)) { std::cerr << "Atlas not found: " << atlasPath << std::endl; return 1; }
    std::string atlasDir = atlasPath.parent_path().string(); if (atlasDir.empty()) atlasDir = ".";
    SfmlTextureLoader texLoader(atlasDir);
    spine::Atlas atlas(atlasPath.string().c_str(), &texLoader, true);
    spine::SkeletonBinary bin(&atlas); bin.setScale(1.0f);
    spine::SkeletonData *data = bin.readSkeletonDataFile((resourceDir/"build_char_298_susuro_summer#6.skel").string().c_str());
    if (!data) { std::cerr << "Failed to load skeleton data" << std::endl; return 2; }

    spine::AnimationStateData stateData(data);
    spine::AnimationState state(&stateData);
    spine::Skeleton skeleton(data);

    // Window sizing: start from atlas/background, then enlarge for desktop pet
    unsigned int w = 960, h = 640;
    sf::Texture bgTex; std::unique_ptr<sf::Sprite> bgSpritePtr; bool haveBackground = false;
    std::filesystem::path bgPath = resourceDir / "background.png";
    if (std::filesystem::exists(bgPath)) {
        if (bgTex.loadFromFile(bgPath.string())) {
            haveBackground = true;
            auto s = bgTex.getSize(); unsigned int origW = s.x; unsigned int origH = s.y;
            auto desktop = sf::VideoMode::getDesktopMode(); unsigned int screenW = desktop.size.x; unsigned int screenH = desktop.size.y;
            const float maxFraction = 0.6f; float maxW = screenW * maxFraction; float maxH = screenH * maxFraction;
            float scale = 1.0f;
            if ((float)origW > maxW || (float)origH > maxH) {
                float sx = maxW / (float)origW; float sy = maxH / (float)origH; scale = std::min(sx, sy); if (scale <= 0.0f) scale = 1.0f;
            }
            w = (unsigned int)std::max(1u, (unsigned int)std::lround(origW * scale));
            h = (unsigned int)std::max(1u, (unsigned int)std::lround(origH * scale));
            bgSpritePtr.reset(new sf::Sprite(bgTex)); if (scale != 1.0f) bgSpritePtr->setScale(sf::Vector2f(scale, scale)); bgSpritePtr->setPosition(sf::Vector2f(0.f, 0.f));
        }
    }
    if (!haveBackground && atlas.getPages().size() > 0) {
        spine::AtlasPage *p = atlas.getPages()[0]; if (p && p->width > 0 && p->height > 0) { w = p->width; h = p->height; }
    }
    // Enlarge relative to content so窗口不会太小
    w = (unsigned int)std::lround((double)w * 2.5); // 宽度放大 2.5 倍
    h = (unsigned int)std::lround((double)h * 1.6); // 高度放大 1.6 倍
    // Clamp to reasonable min/max based on screen
    {
        auto desktop = sf::VideoMode::getDesktopMode();
        unsigned int screenW = desktop.size.x;
        unsigned int screenH = desktop.size.y;
        unsigned int minW = 1000, minH = 700;
        unsigned int maxW = (unsigned int)(screenW * 0.85f);
        unsigned int maxH = (unsigned int)(screenH * 0.85f);
        if (w < minW) w = minW;
        if (h < minH) h = minH;
        if (w > maxW) w = maxW;
        if (h > maxH) h = maxH;
    }

    sf::RenderWindow window(sf::VideoMode(sf::Vector2u{w,h}), "SussurroDockPet", sf::Style::Default);
    window.setFramerateLimit(60);
    window.requestFocus();

    // Build animation list like RosMon and print
    std::vector<spine::String> animationNames;
    for (int i = 0; i < (int)data->getAnimations().size(); ++i) animationNames.push_back(data->getAnimations()[i]->getName());
    std::cout << "Animations (index:name):" << std::endl;
    for (int i = 0; i < (int)animationNames.size(); ++i) {
        std::cout << "  [" << i << "] " << animationNames[i].buffer() << std::endl;
    }

    // 按你的约定固定各个键位对应的动画 index：
    // 0/4/5: 永远不使用（既不响应键盘也不进随机池）
    // 1: 触摸动画（只在点击或按 1 时触发一次，不进随机池）
    // 2: 左行走动画
    // 3: 主待机循环动画
    // 6: 特殊动画（冷却 12 秒，只能在待机时以低概率打断一次）
    int currentAnimIndex = 3; // 默认用 3 号待机
    int touchAnimIndex = 1;
    int walkLeftKeyIndex = 2;
    int idleKeyIndex = 3;
    int specialKeyIndex = 6;
    // 如果资源数量不足，做一下边界保护
    auto safeIndex = [&](int idx) {
        return (idx >= 0 && idx < (int)animationNames.size()) ? idx : -1;
    };
    touchAnimIndex = safeIndex(touchAnimIndex);
    walkLeftKeyIndex = safeIndex(walkLeftKeyIndex);
    idleKeyIndex = safeIndex(idleKeyIndex);
    specialKeyIndex = safeIndex(specialKeyIndex);
    if (idleKeyIndex >= 0) currentAnimIndex = idleKeyIndex;

    // 随机池目前只需要待机 3 和特殊 6（行走由自动逻辑控制，不进随机池）
    // 这里预留 randomIdlePool 方便以后扩展
    std::vector<int> randomIdlePool;
    if (idleKeyIndex >= 0) randomIdlePool.push_back(idleKeyIndex);

    bool loop = true;
    // key3 baseline: 我们把 key3AnimIndex 定义为“主待机循环动画”（3 号）
    int key3AnimIndex = idleKeyIndex;
    if (key3AnimIndex >= 0) {
        state.setAnimation(0, animationNames[key3AnimIndex], true);
    }

    // Movement state (copied from RosMon but simplified): auto-walk left/right inside margins
    sf::Clock clock; sf::Clock wallClock;
    bool pendingSwitch = false; float switchAt = 0.0f; int pendingAnimIndex = -1;
    const float minAnimHold = 0.5f; double lastNonKey3Start = 0.0; const float key3MinHold = 1.0f; double lastKey3Start = 0.0;
    enum MoveState { Move_None = 0, Move_Left = -1, Move_Right = 1 };
    MoveState moveState = Move_None;
    float moveSpeed = 90.0f; float leftMargin = 80.0f; float rightMargin = 80.0f;
    bool autoWalking = false; double autoWalkEndAt = 0.0;
    // 特殊动画相关状态：12 秒冷却，只能在待机时以低概率触发
    double lastSpecialEndTime = -1e9; // 很早以前
    const double specialCooldown = 8.0; // 每 8 秒内不会播放两次
    bool playingSpecial = false;
    // 在触摸/特殊动画结束后，强制播放一小段待机时间（1-2 秒）再允许进入自动行走/特殊
    double idleHoldUntil = 0.0;
    std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
    std::normal_distribution<double> autoDelayDist(7.0, 1.6);
    std::uniform_real_distribution<double> walkDurDist(0.6, 2.2);
    auto sampleAutoDelay = [&](){ double d = autoDelayDist(rng); if (d < 3.0) d = 3.0; return d; };
    double startTime = wallClock.getElapsedTime().asSeconds();
    // 让程序启动后尽快触发第一次自动行为（1 秒内）
    double nextAutoWalkAt = startTime + 1.0;

    // Bounds helpers copied from RosMon
    auto computeSkeletonBounds = [&skeleton]() {
        float minX = std::numeric_limits<float>::infinity(); float maxX = -std::numeric_limits<float>::infinity();
        spine::Vector<spine::Slot*> &drawOrder = skeleton.getDrawOrder();
        for (int si = 0; si < (int)drawOrder.size(); ++si) {
            spine::Slot* slot = drawOrder[si]; spine::Attachment* att = slot->getAttachment(); if (!att) continue;
            if (att->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
                spine::RegionAttachment* ra = (spine::RegionAttachment*)att; float verts[8]; ra->computeWorldVertices(slot->getBone(), verts, 0, 2);
                for (int v = 0; v < 4; ++v) { minX = std::min(minX, verts[v*2]); maxX = std::max(maxX, verts[v*2]); }
            } else if (att->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
                spine::MeshAttachment* ma = (spine::MeshAttachment*)att; int worldLength = ma->getWorldVerticesLength(); spine::Vector<float> worldVertices; worldVertices.setSize(worldLength, 0);
                ma->computeWorldVertices(*slot, 0, worldLength, worldVertices.buffer(), 0, 2);
                for (int vi = 0; vi < worldLength/2; ++vi) { float x = worldVertices[vi*2]; minX = std::min(minX, x); maxX = std::max(maxX, x); }
            }
        }
        if (minX == std::numeric_limits<float>::infinity()) { minX = maxX = skeleton.getX(); }
        return std::pair<float,float>(minX, maxX);
    };
    auto computeSkeletonYBounds = [&skeleton]() {
        float minY = std::numeric_limits<float>::infinity(); float maxY = -std::numeric_limits<float>::infinity();
        spine::Vector<spine::Slot*> &drawOrder = skeleton.getDrawOrder();
        for (int si = 0; si < (int)drawOrder.size(); ++si) {
            spine::Slot* slot = drawOrder[si]; spine::Attachment* att = slot->getAttachment(); if (!att) continue;
            if (att->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
                spine::RegionAttachment* ra = (spine::RegionAttachment*)att; float verts[8]; ra->computeWorldVertices(slot->getBone(), verts, 0, 2);
                for (int v = 0; v < 4; ++v) { minY = std::min(minY, verts[v*2+1]); maxY = std::max(maxY, verts[v*2+1]); }
            } else if (att->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
                spine::MeshAttachment* ma = (spine::MeshAttachment*)att; int worldLength = ma->getWorldVerticesLength(); spine::Vector<float> worldVertices; worldVertices.setSize(worldLength, 0);
                ma->computeWorldVertices(*slot, 0, worldLength, worldVertices.buffer(), 0, 2);
                for (int vi = 0; vi < worldLength/2; ++vi) { float y = worldVertices[vi*2+1]; minY = std::min(minY, y); maxY = std::max(maxY, y); }
            }
        }
        if (minY == std::numeric_limits<float>::infinity()) { minY = maxY = skeleton.getY(); }
        return std::pair<float,float>(minY, maxY);
    };

    // Initial placement similar to RosMon: center, clamp bottom
    if (haveBackground) {
        float sx = (float)w * 0.5f; if (sx < leftMargin) sx = leftMargin; if (sx > (float)w - rightMargin) sx = (float)w - rightMargin;
        float currentY = skeleton.getY(); const float moveUp = 12.0f; const float bottomMargin = 4.0f; float desiredY = currentY - moveUp;
        auto ybounds = computeSkeletonYBounds(); float skMaxY = ybounds.second; float deltaY = desiredY - currentY; float newMaxY = skMaxY + deltaY; float targetBottom = (float)h - bottomMargin;
        if (newMaxY > targetBottom) desiredY -= (newMaxY - targetBottom);
        skeleton.setPosition(sx, desiredY);
    } else {
        skeleton.setPosition((float)w * 0.5f, (float)h * 0.65f);
    }
    skeleton.setScaleY(-1.0f);
    if (skeleton.getScaleX() == 0.0f) skeleton.setScaleX(1.0f); else skeleton.setScaleX(std::abs(skeleton.getScaleX()));

    // Choose walk animation: 2 号为左行走，右行走使用同一动画镜像
    int walkLeftAnimIndex = walkLeftKeyIndex;
    int walkRightAnimIndex = walkLeftKeyIndex;
    std::cout << "Selected walkLeft index: " << walkLeftAnimIndex << ", name: "
              << (walkLeftAnimIndex>=0?animationNames[walkLeftAnimIndex].buffer():"<none>") << std::endl;
    std::cout << "Selected walkRight index: " << walkRightAnimIndex << ", name: "
              << (walkRightAnimIndex>=0?animationNames[walkRightAnimIndex].buffer():"<none>") << std::endl;

    // Simple switch helper with RosMon-style key3 min-hold
    auto requestSwitchTo = [&](int animIdx, bool shouldLoop) {
        double now = wallClock.getElapsedTime().asSeconds();
        if (animIdx == key3AnimIndex) {
            double earliest = lastNonKey3Start + (double)minAnimHold;
            if (now >= earliest) {
                currentAnimIndex = animIdx; state.setAnimation(0, animationNames[currentAnimIndex], shouldLoop); lastKey3Start = now;
            } else {
                pendingSwitch = true; switchAt = (float)earliest; pendingAnimIndex = animIdx;
            }
            return;
        }
        if (currentAnimIndex == key3AnimIndex) {
            double earliest = lastKey3Start + (double)key3MinHold;
            if (now < earliest) {
                pendingSwitch = true; switchAt = (float)earliest; pendingAnimIndex = animIdx; return;
            }
        }
        currentAnimIndex = animIdx; state.setAnimation(0, animationNames[currentAnimIndex], shouldLoop); lastNonKey3Start = now;
    };

    // Start in key3 loop
    if (key3AnimIndex >= 0) {
        currentAnimIndex = key3AnimIndex;
        state.setAnimation(0, animationNames[currentAnimIndex], true);
        lastKey3Start = wallClock.getElapsedTime().asSeconds();
    }

    // Main loop: copy RosMon behavior (idle loop with occasional auto-walk)
    while (window.isOpen()) {
        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) window.close();
            if (ev->is<sf::Event::MouseButtonPressed>()) {
                // 鼠标单击触发触摸动画（只播一次），之后回到待机循环
                if (touchAnimIndex >= 0) {
                    requestSwitchTo(touchAnimIndex, false);
                    moveState = Move_None;
                    autoWalking = false;
                    playingSpecial = false;
                }
            }
            if (ev->is<sf::Event::KeyPressed>()) {
                auto kp = ev->getIf<sf::Event::KeyPressed>(); if (!kp) continue;
                if (kp->code == sf::Keyboard::Key::Escape) { window.close(); break; }

                int num = -1;
                switch (kp->code) {
                    case sf::Keyboard::Key::Num0: num = 0; break;
                    case sf::Keyboard::Key::Num1: num = 1; break;
                    case sf::Keyboard::Key::Num2: num = 2; break;
                    case sf::Keyboard::Key::Num3: num = 3; break;
                    case sf::Keyboard::Key::Num4: num = 4; break;
                    case sf::Keyboard::Key::Num5: num = 5; break;
                    case sf::Keyboard::Key::Num6: num = 6; break;
                    case sf::Keyboard::Key::Num7: num = 7; break;
                    case sf::Keyboard::Key::Num8: num = 8; break;
                    case sf::Keyboard::Key::Num9: num = 9; break;
                    default: break;
                }

                if (num == 0 || num == 4 || num == 5) {
                    // 显式忽略 0/4/5
                    continue;
                }

                if (num == 1 && touchAnimIndex >= 0) {
                    // 1: 触摸动画，只播一次
                    requestSwitchTo(touchAnimIndex, false);
                    moveState = Move_None;
                    autoWalking = false;
                    playingSpecial = false;
                } else if (num == 2 && walkLeftAnimIndex >= 0) {
                    // 2: 左行走动画，实际上根据空间决定向左还是向右，但都使用同一动画镜像
                    auto bounds = computeSkeletonBounds(); float skMin = bounds.first; float skMax = bounds.second; unsigned int winW = window.getSize().x;
                    bool canGoLeft = (skMin > leftMargin + 1.0f);
                    bool canGoRight = (skMax < (float)winW - rightMargin - 1.0f);
                    int dir = 0;
                    if (!canGoLeft && canGoRight) dir = 1;
                    else if (!canGoRight && canGoLeft) dir = -1;
                    else if (canGoLeft || canGoRight) dir = -1; // 默认左
                    if (dir != 0) {
                        moveState = (dir < 0) ? Move_Left : Move_Right;
                        if (dir < 0) {
                            if (skeleton.getScaleX() == 0.0f) skeleton.setScaleX(-1.0f); else skeleton.setScaleX(-std::abs(skeleton.getScaleX()));
                        } else {
                            if (skeleton.getScaleX() == 0.0f) skeleton.setScaleX(1.0f); else skeleton.setScaleX(std::abs(skeleton.getScaleX()));
                        }
                        requestSwitchTo(walkLeftAnimIndex, true);
                        autoWalking = false;
                        playingSpecial = false;
                    }
                } else if (num == 3 && idleKeyIndex >= 0) {
                    // 3: 主待机动画
                    requestSwitchTo(idleKeyIndex, true);
                    moveState = Move_None;
                    autoWalking = false;
                    playingSpecial = false;
                } else if (num == 6 && specialKeyIndex >= 0) {
                    // 6: 特殊动画，只能在待机时触发，且遵守 12 秒冷却
                    double now = wallClock.getElapsedTime().asSeconds();
                    bool inIdle = (currentAnimIndex == idleKeyIndex && moveState == Move_None && !autoWalking && !playingSpecial);
                    if (inIdle && now - lastSpecialEndTime >= specialCooldown) {
                        requestSwitchTo(specialKeyIndex, false);
                        moveState = Move_None;
                        autoWalking = false;
                        playingSpecial = true;
                    }
                } else {
                    // 其它数字键暂时不做处理
                }
            }
        }

        float dt = clock.restart().asSeconds(); if (dt > 0.1f) dt = 0.1f;
        state.update(dt); state.apply(skeleton); skeleton.updateWorldTransform();

        // 监听“非循环一次性动画”（触摸/特殊）结束：一旦结束自动回到待机
        // 这里通过 currentAnimIndex 判断当前是不是触摸或特殊，并用 TrackEntry 的时间判断是否播完
        {
            spine::TrackEntry* te = state.getCurrent(0);
            bool shouldCheckOneShot = (currentAnimIndex == touchAnimIndex || currentAnimIndex == specialKeyIndex);
            if (shouldCheckOneShot && te) {
                float tt = te->getTrackTime();
                float dur = te->getAnimation() ? te->getAnimation()->getDuration() : 0.0f;
                if (dur > 0.0f && tt >= dur) {
                    // 动画时间已到：如果是特殊动画，记录冷却结束时间
                    if (currentAnimIndex == specialKeyIndex) {
                        playingSpecial = false;
                        lastSpecialEndTime = wallClock.getElapsedTime().asSeconds();
                    }
                    // 统一回到待机循环 3 号，并设置一个 1-2 秒的强制待机窗口
                    if (idleKeyIndex >= 0) {
                        requestSwitchTo(idleKeyIndex, true);
                        moveState = Move_None;
                        autoWalking = false;
                        std::uniform_real_distribution<double> idleHoldDist(1.0, 2.0);
                        idleHoldUntil = wallClock.getElapsedTime().asSeconds() + idleHoldDist(rng);
                    }
                }
            }
            // 如果轨道已经被清空，但还处在触摸/特殊标记下，也强制回到待机
            if (shouldCheckOneShot && !te) {
                if (currentAnimIndex == specialKeyIndex) {
                    playingSpecial = false;
                    lastSpecialEndTime = wallClock.getElapsedTime().asSeconds();
                }
                if (idleKeyIndex >= 0) {
                    requestSwitchTo(idleKeyIndex, true);
                    moveState = Move_None;
                    autoWalking = false;
                    std::uniform_real_distribution<double> idleHoldDist(1.0, 2.0);
                    idleHoldUntil = wallClock.getElapsedTime().asSeconds() + idleHoldDist(rng);
                }
            }
        }

    // 当前是否在主待机循环
    double nowSec = wallClock.getElapsedTime().asSeconds();
    bool inKey3Loop = (currentAnimIndex == key3AnimIndex && moveState == Move_None && !autoWalking && !playingSpecial);
    // 如果处于“一次性动画结束后强制待机”的时间窗口内，则禁止自动行走和特殊触发
    bool idleHoldActive = (nowSec < idleHoldUntil);

        // 在待机时，以很低的概率触发一次特殊动画（遵守 12 秒冷却）
        if (inKey3Loop && !idleHoldActive && specialKeyIndex >= 0 && nowSec - lastSpecialEndTime >= specialCooldown) {
            // 略微提高出现概率：比如每帧约 0.0012，配合 8 秒冷却，整体仍然偏稀有
            std::uniform_real_distribution<double> probDist(0.0, 1.0);
            double p = probDist(rng);
            if (p < 0.0012) {
                requestSwitchTo(specialKeyIndex, false);
                playingSpecial = true;
                autoWalking = false;
                moveState = Move_None;
                // 特殊动画触发时，本帧不要再尝试自动行走
                inKey3Loop = false;
            }
        }

        // Auto-walk like RosMon: only when在主待机并且没有在播特殊动画
    if (inKey3Loop && !idleHoldActive && nowSec >= nextAutoWalkAt) {
            auto bounds = computeSkeletonBounds(); float skMin = bounds.first; float skMax = bounds.second; unsigned int winW = window.getSize().x;
            bool canGoLeft = (skMin > leftMargin + 1.0f);
            bool canGoRight = (skMax < (float)winW - rightMargin - 1.0f);
            if (!canGoLeft && !canGoRight) {
                nextAutoWalkAt = nowSec + sampleAutoDelay();
            } else {
                int dir = 0;
                if (!canGoLeft && canGoRight) dir = 1;
                else if (!canGoRight && canGoLeft) dir = -1;
                else { std::uniform_int_distribution<int> dirPick(0,1); dir = (dirPick(rng)==0 ? -1 : 1); }

                if (dir == -1) {
                    moveState = Move_Left;
                    if (skeleton.getScaleX() == 0.0f) skeleton.setScaleX(-1.0f); else skeleton.setScaleX(-std::abs(skeleton.getScaleX()));
                    if (walkLeftAnimIndex >= 0) requestSwitchTo(walkLeftAnimIndex, true);
                } else {
                    moveState = Move_Right;
                    if (skeleton.getScaleX() == 0.0f) skeleton.setScaleX(1.0f); else skeleton.setScaleX(std::abs(skeleton.getScaleX()));
                    if (walkRightAnimIndex >= 0) requestSwitchTo(walkRightAnimIndex, true);
                }
                autoWalking = true;
                spine::TrackEntry* te = state.getCurrent(0);
                double dur = te && te->getAnimation() ? te->getAnimation()->getDuration() : walkDurDist(rng);
                autoWalkEndAt = nowSec + dur;
                nextAutoWalkAt = autoWalkEndAt + sampleAutoDelay();
            }
        }

        // Movement update copied from RosMon: clamp to margins, stop at edge and return to idle
        if (moveState != Move_None) {
            float sx = skeleton.getX(); unsigned int winW = window.getSize().x;
            float delta = moveSpeed * dt * (moveState == Move_Right ? 1.0f : -1.0f);
            auto bounds = computeSkeletonBounds(); float skMin = bounds.first; float skMax = bounds.second;
            float desiredX = sx + delta; float newMin = skMin + (desiredX - sx); float newMax = skMax + (desiredX - sx);
            float targetLeft = leftMargin; float targetRight = (float)winW - rightMargin; bool reached = false;
            if (moveState == Move_Right) {
                if (newMax >= targetRight) { desiredX = desiredX - (newMax - targetRight); reached = true; }
            } else if (moveState == Move_Left) {
                if (newMin <= targetLeft) { desiredX = desiredX + (targetLeft - newMin); reached = true; }
            }
            skeleton.setPosition(desiredX, skeleton.getY());
            if (reached) {
                moveState = Move_None;
                if (key3AnimIndex >= 0) requestSwitchTo(key3AnimIndex, true);
                if (autoWalking) autoWalking = false;
            }
        }
        if (autoWalking && wallClock.getElapsedTime().asSeconds() >= autoWalkEndAt) {
            autoWalking = false; moveState = Move_None;
            if (key3AnimIndex >= 0) requestSwitchTo(key3AnimIndex, true);
        }

        window.clear(sf::Color::White);
        if (haveBackground && bgSpritePtr) window.draw(*bgSpritePtr);
        spine::Vector<spine::Slot*> &drawOrder = skeleton.getDrawOrder();
        for (int i=0;i<(int)drawOrder.size(); ++i) {
            spine::Slot *slot = drawOrder[i]; spine::Attachment *att = slot->getAttachment(); if (!att) continue;
            if (att->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
                spine::RegionAttachment *ra = (spine::RegionAttachment*)att; float verts[8]; ra->computeWorldVertices(slot->getBone(), verts, 0, 2);
                float *uvs = ra->getUVs().buffer(); spine::AtlasRegion *region = (spine::AtlasRegion*)ra->getRendererObject();
                sf::Texture *tex = NULL; if (region && region->page) tex = static_cast<sf::Texture*>(region->page->getRendererObject()); if (!tex) continue;
                sf::Vertex quad[6]; sf::Vector2f pos[4]; sf::Vector2f texc[4];
                for (int v=0; v<4; ++v) { pos[v] = sf::Vector2f(verts[v*2], verts[v*2+1]); texc[v] = sf::Vector2f(uvs[v*2] * tex->getSize().x, uvs[v*2+1] * tex->getSize().y); }
                sf::Color col(255,255,255,255); auto setV=[&](int idx,int src){ quad[idx].position=pos[src]; quad[idx].texCoords=texc[src]; quad[idx].color=col; };
                setV(0,0); setV(1,1); setV(2,2); setV(3,0); setV(4,2); setV(5,3);
                sf::RenderStates states; states.texture = tex; window.draw(quad, 6, sf::PrimitiveType::Triangles, states);
            } else if (att->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
                spine::MeshAttachment *ma = (spine::MeshAttachment*)att; int worldLength = ma->getWorldVerticesLength();
                spine::Vector<float> worldVertices; worldVertices.setSize(worldLength, 0);
                ma->computeWorldVertices(*slot, 0, worldLength, worldVertices.buffer(), 0, 2);
                spine::Vector<unsigned short> &tris = ma->getTriangles(); float *uvs = ma->getUVs().buffer();
                spine::AtlasRegion *region = (spine::AtlasRegion*)ma->getRendererObject(); if (!region || !region->page) continue;
                sf::Texture *tex = static_cast<sf::Texture*>(region->page->getRendererObject()); if (!tex) continue;
                sf::VertexArray arr(sf::PrimitiveType::Triangles, tris.size());
                for (int t=0; t<(int)tris.size(); ++t) {
                    int vi = tris[t]; float x = worldVertices[vi*2]; float y = worldVertices[vi*2+1];
                    arr[t].position = sf::Vector2f(x,y);
                    arr[t].texCoords = sf::Vector2f(uvs[vi*2] * tex->getSize().x, uvs[vi*2+1] * tex->getSize().y);
                    arr[t].color = sf::Color::White;
                }
                sf::RenderStates rs; rs.texture = tex; window.draw(arr, rs);
            }
        }
        window.display();

        // Pending switches (like RosMon)
        if (pendingSwitch) {
            float now = wallClock.getElapsedTime().asSeconds();
            if (now >= switchAt) {
                pendingSwitch = false;
                if (pendingAnimIndex >= 0 && pendingAnimIndex < (int)animationNames.size()) requestSwitchTo(pendingAnimIndex, true);
                pendingAnimIndex = -1;
            }
        }
    }

    return 0;
}
