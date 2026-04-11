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

// Provide default runtime extension (required by spine-cpp linkage)
namespace spine { SpineExtension* getDefaultExtension() { return new DefaultSpineExtension(); } }

class SfmlTextureLoader : public spine::TextureLoader {
public:
    SfmlTextureLoader(const std::string& atlasDir): _atlasDir(atlasDir) {}
    virtual void load(spine::AtlasPage &page, const spine::String &path) {
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
    virtual void unload(void *texture) { delete static_cast<sf::Texture*>(texture); }
private:
    std::string _atlasDir;
};

int main(int argc, char** argv) {
    // Start with a safe default: try to obtain the CWD but fall back to executable directory if that fails.
    std::filesystem::path resourceDir;
    std::error_code cwdEc;
    try {
        resourceDir = std::filesystem::current_path(cwdEc);
    } catch(...) {
        // In case current_path throws (deleted cwd), leave resourceDir empty and resolve via executable
    }

    // If current_path failed or produced an empty path, resolve via executable location
    char buf[1024]; uint32_t sz = sizeof(buf);
    if (resourceDir.empty()) {
        if (_NSGetExecutablePath(buf, &sz) == 0) {
            std::error_code ec; auto exe = std::filesystem::weakly_canonical(std::filesystem::path(buf), ec);
            if (!ec) {
                auto dir = exe.parent_path();
                std::string ds = dir.string();
                if (ds.find(".app/Contents/MacOS") != std::string::npos) {
                    resourceDir = dir.parent_path() / "Resources";
                } else {
                    resourceDir = dir;
                }
            }
        }
    } else {
        // If we did get a current_path, still check if we are inside .app bundle and prefer its Resources
        if (_NSGetExecutablePath(buf, &sz) == 0) {
            std::error_code ec; auto exe = std::filesystem::weakly_canonical(std::filesystem::path(buf), ec);
            if (!ec) {
                auto dir = exe.parent_path();
                std::string ds = dir.string();
                if (ds.find(".app/Contents/MacOS") != std::string::npos) {
                    resourceDir = dir.parent_path() / "Resources";
                }
            }
        }
    }

    std::cout << "ResourceDir: " << resourceDir << std::endl;
    std::filesystem::path atlasPath = resourceDir / "rosmon.atlas";
    if (!std::filesystem::exists(atlasPath)) { std::cerr << "rosmon.atlas not found in Resources: " << atlasPath << std::endl; return 1; }

    // Determine atlas directory
    std::string atlasDir = atlasPath.parent_path().string(); if (atlasDir.empty()) atlasDir = ".";
    SfmlTextureLoader texLoader(atlasDir);
    spine::Atlas atlas(atlasPath.string().c_str(), &texLoader, true);
    spine::SkeletonBinary bin(&atlas);
    bin.setScale(1.0f);
    spine::SkeletonData *data = bin.readSkeletonDataFile((resourceDir/"rosmon.skel").string().c_str());
    if (!data) { std::cerr << "Failed to load skeleton data" << std::endl; return 2; }

    spine::AnimationStateData stateData(data);
    spine::AnimationState state(&stateData);
    spine::Skeleton skeleton(data);

    skeleton.setPosition(480, 600);
    skeleton.setScaleY(-1.0f);

    // Build animation name list and choose initial animation (prefer "idle")
    std::vector<spine::String> animationNames;
    for (int i = 0; i < (int)data->getAnimations().size(); ++i) animationNames.push_back(data->getAnimations()[i]->getName());
    int currentAnimIndex = 0;
    for (int i = 0; i < (int)animationNames.size(); ++i) {
        if (std::strcmp(animationNames[i].buffer(), "idle") == 0) { currentAnimIndex = i; break; }
    }

    // Determine the 'touch' animation index. Prefer an animation containing "touch" (case-insensitive), otherwise fallback to index 1 if available.
    int touchAnimIndex = -1;
    for (int i = 0; i < (int)animationNames.size(); ++i) {
        std::string n = animationNames[i].buffer();
        std::string lower; lower.resize(n.size());
        std::transform(n.begin(), n.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
        if (lower.find("touch") != std::string::npos || lower.find("tap") != std::string::npos) { touchAnimIndex = i; break; }
    }
    if (touchAnimIndex == -1 && animationNames.size() > 1) touchAnimIndex = 1;

    // Build numeric key mapping (1..n) to animation indices, excluding the touch animation which will be mouse-triggered
    std::vector<int> animationIndicesForNumbers;
    for (int i = 0; i < (int)animationNames.size(); ++i) {
        if (i == touchAnimIndex) continue;
        animationIndicesForNumbers.push_back(i);
    }

    bool loop = true;
    // initial animation will be set after we determine which animation maps to key 3

    // Create window sized to background image (background.png) if present, otherwise to atlas texture or defaults
    unsigned int w = 960, h = 640;
    sf::Texture bgTex;
    std::unique_ptr<sf::Sprite> bgSpritePtr;
    bool haveBackground = false;
    std::filesystem::path bgPath = resourceDir / "background.png";
    if (std::filesystem::exists(bgPath)) {
        if (bgTex.loadFromFile(bgPath.string())) {
            haveBackground = true;
            auto s = bgTex.getSize(); unsigned int origW = s.x; unsigned int origH = s.y;
            // Determine a reasonable maximum size for the window on this display
#if defined(__APPLE__)
            CGRect screenBounds = CGDisplayBounds(CGMainDisplayID());
            unsigned int screenW = (unsigned int)CGRectGetWidth(screenBounds);
            unsigned int screenH = (unsigned int)CGRectGetHeight(screenBounds);
#else
            auto desktop = sf::VideoMode::getDesktopMode();
            unsigned int screenW = desktop.width; unsigned int screenH = desktop.height;
#endif
            // Keep background no larger than 60% of screen dims by default
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
            // compute final window size
            w = (unsigned int)std::max(1u, (unsigned int)std::lround(origW * scale));
            h = (unsigned int)std::max(1u, (unsigned int)std::lround(origH * scale));
            // construct sprite after texture loaded
            bgSpritePtr.reset(new sf::Sprite(bgTex));
            // apply scale if needed
            if (scale != 1.0f) bgSpritePtr->setScale(sf::Vector2f(scale, scale));
            bgSpritePtr->setPosition(sf::Vector2f(0.f, 0.f));
        }
    }
    // try to size to first page texture if available and no explicit background
    if (!haveBackground && atlas.getPages().size() > 0) {
        spine::AtlasPage *p = atlas.getPages()[0];
        if (p && p->width > 0 && p->height > 0) { w = p->width; h = p->height; }
    }

    sf::VideoMode vm(sf::Vector2u{w,h});
    // Create window (SFML v3 ContextSettings doesn't expose alphaBits on macOS)
    sf::RenderWindow window(vm, "RosMonDockPet", sf::Style::None);

    // Print actual context settings we got for debugging (depth/stencil/AA/sRGB)
    auto got = window.getSettings();
    std::cout << "ContextSettings: depthBits=" << got.depthBits << " stencilBits=" << got.stencilBits << " antiAliasingLevel=" << got.antiAliasingLevel << " major=" << got.majorVersion << " minor=" << got.minorVersion << " sRgbCapable=" << got.sRgbCapable << std::endl;
    window.setFramerateLimit(60);

#ifdef __APPLE__
    auto handle = window.getNativeHandle();
    mac_configure_window(handle);
#endif

    sf::Clock clock;
    sf::Clock wallClock; // used for scheduling delayed switches
    bool pendingSwitch = false;
    float switchAt = 0.0f;
    int pendingAnimIndex = -1;
    // Prevent extremely short animation toggles: require a small minimum hold time for non-key3
    const float minAnimHold = 0.5f; // seconds
    double lastNonKey3Start = 0.0; // wallClock time when a non-key3 animation was started
    // Enforce a minimum hold for key3 when we return to it (mouse clicks must play key3 for at least 1s)
    const float key3MinHold = 1.0f; // seconds
    double lastKey3Start = 0.0; // wallClock time when key3 was started
    // If we schedule a switch because of key3MinHold, we may need to remember an intent to start
    // movement once the pending switch executes. Store that intent here.
    int pendingMoveAfterSwitch = 0; // 0 none, -1 left, 1 right
    bool pendingMoveIsAuto = false;
    
    // Movement state: left/right walking that translates the whole skeleton
    enum MoveState { Move_None = 0, Move_Left = -1, Move_Right = 1 };
    MoveState moveState = Move_None;
    float moveSpeed = 90.0f; // pixels per second (tweakable) - slightly slower per request
    float leftMargin = 80.0f; // pixels from left edge to stop
    float rightMargin = 80.0f; // pixels from right edge to stop
    float moveTargetXLeft = 0.0f;
    float moveTargetXRight = 0.0f;
    // Auto-walk (when in key3 loop) scheduling
    std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
    // Use a Normal distribution for auto-walk delay with mean 7s and smaller stddev to reduce variance.
    std::normal_distribution<double> autoDelayDist(7.0, 1.6); // mean 7s, sd 1.6s (clamped >= 3s)
    std::uniform_real_distribution<double> walkDurDist(0.6, 2.2); // walk duration in seconds (random)
    auto sampleAutoDelay = [&](void){ double d = autoDelayDist(rng); if (d < 3.0) d = 3.0; return d; };
    double nextAutoWalkAt = wallClock.getElapsedTime().asSeconds() + sampleAutoDelay();
    double autoWalkEndAt = 0.0;
    bool autoWalking = false;
    // After a mouse-triggered touch animation finishes, enforce a short idle period (1–2s)
    double idleHoldUntil = 0.0;
    // Request focus so keyboard events are delivered
    window.requestFocus();

    // Helper: compute skeleton horizontal bounds (minX, maxX) in window coordinates
    auto computeSkeletonBounds = [&skeleton]() {
        float minX = std::numeric_limits<float>::infinity();
        float maxX = -std::numeric_limits<float>::infinity();

        spine::Vector<spine::Slot*> &drawOrder = skeleton.getDrawOrder();
        for (int si = 0; si < (int)drawOrder.size(); ++si) {
            spine::Slot* slot = drawOrder[si];
            spine::Attachment* att = slot->getAttachment();
            if (!att) continue;
            if (att->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
                spine::RegionAttachment* ra = (spine::RegionAttachment*)att;
                float verts[8]; ra->computeWorldVertices(slot->getBone(), verts, 0, 2);
                for (int v = 0; v < 4; ++v) { minX = std::min(minX, verts[v*2]); maxX = std::max(maxX, verts[v*2]); }
            } else if (att->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
                spine::MeshAttachment* ma = (spine::MeshAttachment*)att;
                int worldLength = ma->getWorldVerticesLength();
                spine::Vector<float> worldVertices; worldVertices.setSize(worldLength, 0);
                ma->computeWorldVertices(*slot, 0, worldLength, worldVertices.buffer(), 0, 2);
                for (int vi = 0; vi < worldLength/2; ++vi) { float x = worldVertices[vi*2]; minX = std::min(minX, x); maxX = std::max(maxX, x); }
            }
        }
        if (minX == std::numeric_limits<float>::infinity()) {
            // no attachments - fall back to skeleton origin
            minX = maxX = skeleton.getX();
        }
        return std::pair<float,float>(minX, maxX);
    };

    // Helper: compute skeleton vertical bounds (minY, maxY) in window coordinates
    auto computeSkeletonYBounds = [&skeleton]() {
        float minY = std::numeric_limits<float>::infinity();
        float maxY = -std::numeric_limits<float>::infinity();

        spine::Vector<spine::Slot*> &drawOrder = skeleton.getDrawOrder();
        for (int si = 0; si < (int)drawOrder.size(); ++si) {
            spine::Slot* slot = drawOrder[si];
            spine::Attachment* att = slot->getAttachment();
            if (!att) continue;
            if (att->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
                spine::RegionAttachment* ra = (spine::RegionAttachment*)att;
                float verts[8]; ra->computeWorldVertices(slot->getBone(), verts, 0, 2);
                for (int v = 0; v < 4; ++v) { minY = std::min(minY, verts[v*2+1]); maxY = std::max(maxY, verts[v*2+1]); }
            } else if (att->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
                spine::MeshAttachment* ma = (spine::MeshAttachment*)att;
                int worldLength = ma->getWorldVerticesLength();
                spine::Vector<float> worldVertices; worldVertices.setSize(worldLength, 0);
                ma->computeWorldVertices(*slot, 0, worldLength, worldVertices.buffer(), 0, 2);
                for (int vi = 0; vi < worldLength/2; ++vi) { float y = worldVertices[vi*2+1]; minY = std::min(minY, y); maxY = std::max(maxY, y); }
            }
        }
        if (minY == std::numeric_limits<float>::infinity()) {
            minY = maxY = skeleton.getY();
        }
        return std::pair<float,float>(minY, maxY);
    };

    // Determine the animation index that numeric key 3 maps to (if available)
    int key3AnimIndex = -1;
    if (animationIndicesForNumbers.size() >= 3) key3AnimIndex = animationIndicesForNumbers[2];
    else if (!animationNames.empty()) key3AnimIndex = 0;

    // Start app in key3's looping animation by default
    if (key3AnimIndex >= 0) {
        currentAnimIndex = key3AnimIndex;
        state.setAnimation(0, animationNames[currentAnimIndex], true);
    }

    // Flag set while a mouse-triggered 'touch' animation is active.
    // Declared here so helper lambdas (like requestSwitchTo) can see it.
    bool inTouch = false;

    // Helper to request switching to an animation while respecting a minimum hold time
    auto requestSwitchTo = [&](int animIdx, bool shouldLoop) {
        double now = wallClock.getElapsedTime().asSeconds();
        // If we're currently in a touch (mouse click) sequence, block switching to any animation
        // other than the touch animation itself. Re-triggering the touch animation is allowed
        // (to interrupt and restart it) as requested.
        if (inTouch && touchAnimIndex >= 0 && animIdx != touchAnimIndex) {
            // ignore the request; after touch completes TouchListener will schedule key3 only
            return;
        }
        // If requesting key3
        if (animIdx == key3AnimIndex) {
            // ensure we held the previous non-key3 for at least minAnimHold
            double earliest = lastNonKey3Start + (double)minAnimHold;
            if (now >= earliest) {
                currentAnimIndex = animIdx;
                state.setAnimation(0, animationNames[currentAnimIndex], shouldLoop);
                lastKey3Start = now;
                // Immediately stop any translation when entering key3 (prevent concurrent move+key3)
                if (moveState != Move_None) {
                    moveState = Move_None;
                    autoWalking = false;
                }
            } else {
                // schedule pending switch for when earliest arrives
                pendingSwitch = true;
                switchAt = (float)earliest;
                pendingAnimIndex = animIdx;
            }
            return;
        }

        // Switching to a non-key3 animation: ensure key3 has been played long enough if we are currently in key3
        if (currentAnimIndex == key3AnimIndex) {
            double earliest = lastKey3Start + (double)key3MinHold;
            if (now < earliest) {
                // schedule pending switch for when key3 minimum hold elapses
                pendingSwitch = true;
                switchAt = (float)earliest;
                pendingAnimIndex = animIdx;
                return;
            }
        }

        // Otherwise switch immediately and record the non-key3 start time
        currentAnimIndex = animIdx;
        state.setAnimation(0, animationNames[currentAnimIndex], shouldLoop);
        lastNonKey3Start = now;
    };

    // If we have a background, adjust skeleton initial X to be centered horizontally in the window
    if (haveBackground) {
        // center skeleton horizontally in the window
        float sx = (float)w * 0.5f;
        // ensure skeleton respects left/right margins
        if (sx < leftMargin) sx = leftMargin;
        if (sx > (float)w - rightMargin) sx = (float)w - rightMargin;
        // move the character slightly upward but ensure its visual bottom doesn't go outside the window
        float currentY = skeleton.getY();
        const float moveUp = 12.0f; // pixels to nudge up
        const float bottomMargin = 4.0f; // pixels of breathing room from window bottom
        float desiredY = currentY - moveUp;
        // compute current vertical bounds and clamp so visual bottom <= windowHeight - bottomMargin
        auto ybounds = computeSkeletonYBounds(); float skMinY = ybounds.first; float skMaxY = ybounds.second;
        float deltaY = desiredY - currentY; // negative when moving up
        float newMaxY = skMaxY + deltaY;
        float targetBottom = (float)h - bottomMargin;
        if (newMaxY > targetBottom) {
            // push up less; adjust desiredY so newMaxY == targetBottom
            desiredY -= (newMaxY - targetBottom);
        }
        skeleton.setPosition(sx, desiredY);
    }

    // Find the 'move' animation index (try "move" then "walk"), fallback to a sensible index
    int moveAnimIndex = -1;
    for (int i = 0; i < (int)animationNames.size(); ++i) {
        std::string n = animationNames[i].buffer(); std::string lower; lower.resize(n.size());
        std::transform(n.begin(), n.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
        if (lower.find("move") != std::string::npos || lower.find("walk") != std::string::npos) { moveAnimIndex = i; break; }
    }
    if (moveAnimIndex == -1) {
        // fallback: use the animation mapped to numeric 2 (if present)
        if (animationIndicesForNumbers.size() >= 2) moveAnimIndex = animationIndicesForNumbers[1];
        else if (!animationNames.empty()) moveAnimIndex = 0;
    }

    // Set up an AnimationState listener to detect when the touch animation completes.
    // Use AnimationStateListenerObject (class) because the build may not enable std::function typedef.
    class TouchListener : public spine::AnimationStateListenerObject {
    public:
        std::vector<spine::String>* animationNames;
        int touchIndex;
        int* key3IndexPtr;
        sf::Clock* wallClockPtr;
        bool* pendingSwitchPtr;
        float* switchAtPtr;
        int* pendingAnimIndexPtr;
        double* lastNonKey3StartPtr;
                const float* minAnimHoldPtr;
                bool* inTouchPtr;
                double* idleHoldUntilPtr;

                TouchListener(std::vector<spine::String>* names, int tIdx, int* k3, sf::Clock* wc,
                                            bool* ps, float* sa, int* pai,
                                            double* lastNonKey3, const float* minHold,
                                            bool* inTouch, double* idleHoldUntil)
                : animationNames(names), touchIndex(tIdx), key3IndexPtr(k3), wallClockPtr(wc),
                    pendingSwitchPtr(ps), switchAtPtr(sa), pendingAnimIndexPtr(pai),
                    lastNonKey3StartPtr(lastNonKey3), minAnimHoldPtr(minHold),
                    inTouchPtr(inTouch), idleHoldUntilPtr(idleHoldUntil) {}

        void callback(spine::AnimationState* st, spine::EventType type, spine::TrackEntry* entry, spine::Event* event) override {
            if (type == spine::EventType_Complete || type == spine::EventType_End) {
                if (!entry) return;
                spine::Animation* a = entry->getAnimation();
                if (!a) return;
                const char* name = a->getName().buffer();
                if (touchIndex >= 0 && std::strcmp(name, (*animationNames)[touchIndex].buffer()) == 0) {
                    // clear inTouch flag now that touch animation completed
                    if (inTouchPtr) *inTouchPtr = false;
                    // also enforce a short idle period (1–2s) before auto-walk can resume
                    if (idleHoldUntilPtr && wallClockPtr) {
                        std::mt19937 localRng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
                        std::uniform_real_distribution<double> idleHoldDist(1.0, 2.0);
                        double now = wallClockPtr->getElapsedTime().asSeconds();
                        *idleHoldUntilPtr = now + idleHoldDist(localRng);
                    }
                    if (key3IndexPtr && *key3IndexPtr >= 0) {
                        double now = wallClockPtr->getElapsedTime().asSeconds();
                        double earliest = 0.0;
                        if (lastNonKey3StartPtr && minAnimHoldPtr) earliest = *lastNonKey3StartPtr + (double)(*minAnimHoldPtr);
                        double sched = now >= earliest ? now : earliest;
                        *pendingSwitchPtr = true;
                        *switchAtPtr = (float)sched;
                        *pendingAnimIndexPtr = *key3IndexPtr;
                    }
                }
            }
        }
    };

    // allocate on heap and pass into AnimationState; keep alive for lifetime of program
    TouchListener* touchListener = new TouchListener(&animationNames, touchAnimIndex, &key3AnimIndex,
                                                    &wallClock, &pendingSwitch, &switchAt, &pendingAnimIndex,
                                                    &lastNonKey3Start, &minAnimHold,
                                                    &inTouch, &idleHoldUntil);
    state.setListener(touchListener);
    while (window.isOpen()) {
        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) window.close();
                if (ev->is<sf::Event::MouseButtonPressed>()) {
                // On mouse click: immediately stop any movement/auto-walk and trigger touch animation if available
                // This ensures clicks interrupt left/right walking and play the single-shot touch animation.
                if (moveState != Move_None) {
                    moveState = Move_None;
                    autoWalking = false;
                }
                if (touchAnimIndex >= 0 && touchAnimIndex < (int)animationNames.size()) {
                    // mark that touch is active; blocks switches to anything else until touch completes
                    inTouch = true;
                    requestSwitchTo(touchAnimIndex, false);
                }
            }
            if (ev->is<sf::Event::KeyPressed>()) {
                auto kp = ev->getIf<sf::Event::KeyPressed>();
                if (!kp) continue;
                // Esc -> close
                if (kp->code == sf::Keyboard::Key::Escape) { window.close(); break; }
                // Numbers 1-9 -> special handling: Num1 = move left (flipped move), Num2 = move right (move), other numbers map to the existing mapping
                if (kp->code >= sf::Keyboard::Key::Num1 && kp->code <= sf::Keyboard::Key::Num9) {
                    int numIdx = (int)kp->code - (int)sf::Keyboard::Key::Num1; // 0-based
                    // Num1 -> move left (use moveAnimIndex, flipped)
                    if (numIdx == 0) {
                        // already at (or left of) left margin -> invalid
                        float sx = skeleton.getX();
                        if (sx <= leftMargin) {
                            // ignore
                        } else {
                            // start leftward movement
                            if (currentAnimIndex == key3AnimIndex && loop) {
                                pendingMoveAfterSwitch = -1;
                                pendingMoveIsAuto = false;
                            } else {
                                moveState = Move_Left;
                            }
                            // flip skeleton horizontally for left movement
                            if (skeleton.getScaleX() == 0.0f) skeleton.setScaleX(-1.0f);
                            else skeleton.setScaleX(-std::abs(skeleton.getScaleX()));
                            if (moveAnimIndex >= 0) { requestSwitchTo(moveAnimIndex, true); }
                        }
                    } else if (numIdx == 1) {
                        // Num2 -> move right
                        float sx = skeleton.getX();
                        unsigned int winW = window.getSize().x;
                        // compute right-side effective limit (we'll stop when skeleton.x >= winW - rightMargin)
                        if (sx >= (float)winW - rightMargin) {
                            // already at/near right edge -> ignore
                        } else {
                            if (currentAnimIndex == key3AnimIndex && loop) {
                                pendingMoveAfterSwitch = 1;
                                pendingMoveIsAuto = false;
                            } else {
                                moveState = Move_Right;
                            }
                            // ensure skeleton faces right for right movement
                            if (skeleton.getScaleX() == 0.0f) skeleton.setScaleX(1.0f);
                            else skeleton.setScaleX(std::abs(skeleton.getScaleX()));
                            if (moveAnimIndex >= 0) { requestSwitchTo(moveAnimIndex, true); }
                        }
                    } else {
                        // if currently moving, interrupt movement immediately and switch to the requested animation
                        if (moveState != Move_None) {
                            moveState = Move_None;
                        }
                        // other numbers: use existing mapping but skipping the touch animation as before
                        if (numIdx >= 0) {
                            if (numIdx < (int)animationIndicesForNumbers.size()) {
                                int animIdx = animationIndicesForNumbers[numIdx];
                                requestSwitchTo(animIdx, loop);
                            }
                        }
                    }
                }
                // Arrow keys -> next/prev (cycle over all animations)
                if (kp->code == sf::Keyboard::Key::Right) {
                    if (!animationNames.empty()) { currentAnimIndex = (currentAnimIndex + 1) % animationNames.size(); state.setAnimation(0, animationNames[currentAnimIndex], loop); }
                } else if (kp->code == sf::Keyboard::Key::Left) {
                    if (!animationNames.empty()) { currentAnimIndex = (currentAnimIndex - 1 + (int)animationNames.size()) % animationNames.size(); state.setAnimation(0, animationNames[currentAnimIndex], loop); }
                }
                // Space -> toggle loop for current animation
                if (kp->code == sf::Keyboard::Key::Space) { loop = !loop; if (!animationNames.empty()) state.setAnimation(0, animationNames[currentAnimIndex], loop); }
                // D -> toggle simple debug print (not implemented visually here)
                // (we keep parity with runros_sfml behavior; can extend later)
            }
        }

    float dt = clock.restart().asSeconds(); if (dt > 0.1f) dt = 0.1f;
        state.update(dt);
        state.apply(skeleton);
        skeleton.updateWorldTransform();

        // Auto-walk scheduling: only when currently in key3 loop and not already moving or autoWalking
        bool inKey3Loop = (currentAnimIndex == key3AnimIndex && loop && moveState == Move_None && !autoWalking);
        double nowSec = wallClock.getElapsedTime().asSeconds();
        bool idleHoldActive = (nowSec < idleHoldUntil);
        if (inKey3Loop && !idleHoldActive && nowSec >= nextAutoWalkAt) {
            // decide direction: avoid directions that would immediately exceed edges
            auto bounds = computeSkeletonBounds(); float skMin = bounds.first; float skMax = bounds.second; unsigned int winW = window.getSize().x;
            bool canGoLeft = (skMin > leftMargin + 1.0f);
            bool canGoRight = (skMax < (float)winW - rightMargin - 1.0f);
            if (!canGoLeft && !canGoRight) {
                // cannot walk either way; schedule next try
                nextAutoWalkAt = nowSec + sampleAutoDelay();
            } else {
                int dir = 0; // -1 left, 1 right
                int animToPlay = -1;
                // If at left edge (can't go left) but can go right, force right and pick numeric-2 animation if available
                if (!canGoLeft && canGoRight) {
                    dir = 1;
                    if (animationIndicesForNumbers.size() >= 2) animToPlay = animationIndicesForNumbers[1];
                }
                // If at right edge (can't go right) but can go left, force left and pick numeric-1 animation if available
                else if (!canGoRight && canGoLeft) {
                    dir = -1;
                    if (animationIndicesForNumbers.size() >= 1) animToPlay = animationIndicesForNumbers[0];
                } else {
                    // both directions possible: randomly choose direction
                    std::uniform_int_distribution<int> dirPick(0,1);
                    int pick = dirPick(rng);
                    dir = pick == 0 ? -1 : 1;
                }

                // start auto-walk in the chosen direction
                if (dir == -1) {
                    if (currentAnimIndex == key3AnimIndex && loop) {
                        pendingMoveAfterSwitch = -1;
                        pendingMoveIsAuto = true;
                    } else {
                        moveState = Move_Left;
                    }
                    if (skeleton.getScaleX() == 0.0f) skeleton.setScaleX(-1.0f); else skeleton.setScaleX(-std::abs(skeleton.getScaleX()));
                    // if we didn't pick a numeric mapping explicitly, prefer moveAnimIndex then numeric 1
                    if (animToPlay == -1) {
                        if (moveAnimIndex >= 0) animToPlay = moveAnimIndex;
                        else if (animationIndicesForNumbers.size() >= 1) animToPlay = animationIndicesForNumbers[0];
                    }
                } else {
                    if (currentAnimIndex == key3AnimIndex && loop) {
                        pendingMoveAfterSwitch = 1;
                        pendingMoveIsAuto = true;
                    } else {
                        moveState = Move_Right;
                    }
                    if (skeleton.getScaleX() == 0.0f) skeleton.setScaleX(1.0f); else skeleton.setScaleX(std::abs(skeleton.getScaleX()));
                    if (animToPlay == -1) {
                        if (moveAnimIndex >= 0) animToPlay = moveAnimIndex;
                        else if (animationIndicesForNumbers.size() >= 2) animToPlay = animationIndicesForNumbers[1];
                    }
                }

                // Only request the animation that will result in movement away from the edge (avoid in-place stepping)
                if (animToPlay >= 0 && animToPlay < (int)animationNames.size()) {
                    requestSwitchTo(animToPlay, true);
                }
                autoWalking = true;
                double dur = walkDurDist(rng);
                autoWalkEndAt = nowSec + dur;
                // schedule next auto-walk after this one finishes plus another random delay (clamped)
                nextAutoWalkAt = autoWalkEndAt + sampleAutoDelay();
            }
        }

        // Movement update: translate skeleton while in a Move state
        if (moveState != Move_None) {
            float sx = skeleton.getX();
            unsigned int winW = window.getSize().x;
            float delta = moveSpeed * dt * (moveState == Move_Right ? 1.0f : -1.0f);
            float desiredX = sx + delta;
            // compute visual bounds and ensure we don't push any part of the skeleton outside margins
            auto bounds = computeSkeletonBounds();
            float skMin = bounds.first; float skMax = bounds.second;
            float skWidth = skMax - skMin;
            // when skeleton origin moves by (desiredX - sx), bounds also move by same amount
            float newMin = skMin + (desiredX - sx);
            float newMax = skMax + (desiredX - sx);
            float targetLeft = leftMargin;
            float targetRight = (float)winW - rightMargin;
            bool reached = false;
            if (moveState == Move_Right) {
                if (newMax >= targetRight) {
                    // clamp so visual right edge == targetRight
                    desiredX = desiredX - (newMax - targetRight);
                    reached = true;
                }
            } else if (moveState == Move_Left) {
                if (newMin <= targetLeft) {
                    desiredX = desiredX + (targetLeft - newMin);
                    reached = true;
                }
            }
            skeleton.setPosition(desiredX, skeleton.getY());
            if (reached) {
                moveState = Move_None;
                if (key3AnimIndex >= 0) {
                    requestSwitchTo(key3AnimIndex, true);
                }
                // if this was an auto-walk, mark it finished
                if (autoWalking) { autoWalking = false; }
            }
        }

        // If autoWalking is active, also stop after time expiry (in case time ends before edge reached)
            if (autoWalking && wallClock.getElapsedTime().asSeconds() >= autoWalkEndAt) {
            // finish movement immediately (don't overshoot)
            autoWalking = false;
            moveState = Move_None;
            if (key3AnimIndex >= 0) { requestSwitchTo(key3AnimIndex, true); }
        }

    // Clear to white (as fallback) then draw background image if available
    window.clear(sf::Color::White);
    if (haveBackground && bgSpritePtr) {
        window.draw(*bgSpritePtr);
    }

        spine::Vector<spine::Slot*> &drawOrder = skeleton.getDrawOrder();
        for (int i=0;i<(int)drawOrder.size(); ++i) {
            spine::Slot *slot = drawOrder[i];
            spine::Attachment *att = slot->getAttachment();
            if (!att) continue;
            if (att->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
                spine::RegionAttachment *ra = (spine::RegionAttachment*)att;
                float verts[8]; ra->computeWorldVertices(slot->getBone(), verts, 0, 2);
                float *uvs = ra->getUVs().buffer();
                spine::AtlasRegion *region = (spine::AtlasRegion*)ra->getRendererObject();
                sf::Texture *tex = NULL; if (region && region->page) tex = static_cast<sf::Texture*>(region->page->getRendererObject());
                if (!tex) continue;
                sf::Vertex quad[6]; sf::Vector2f pos[4]; sf::Vector2f texc[4];
                for (int v=0; v<4; ++v) { pos[v] = sf::Vector2f(verts[v*2], verts[v*2+1]); texc[v] = sf::Vector2f(uvs[v*2] * tex->getSize().x, uvs[v*2+1] * tex->getSize().y); }
                sf::Color col(255,255,255,255);
                auto setV=[&](int idx,int src){ quad[idx].position=pos[src]; quad[idx].texCoords=texc[src]; quad[idx].color=col; };
                setV(0,0); setV(1,1); setV(2,2); setV(3,0); setV(4,2); setV(5,3);
                sf::RenderStates states; states.texture = tex; window.draw(quad, 6, sf::PrimitiveType::Triangles, states);
            } else if (att->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
                spine::MeshAttachment *ma = (spine::MeshAttachment*)att;
                int worldLength = ma->getWorldVerticesLength();
                spine::Vector<float> worldVertices; worldVertices.setSize(worldLength, 0);
                ma->computeWorldVertices(*slot, 0, worldLength, worldVertices.buffer(), 0, 2);
                spine::Vector<unsigned short> &tris = ma->getTriangles();
                float *uvs = ma->getUVs().buffer();
                spine::AtlasRegion *region = (spine::AtlasRegion*)ma->getRendererObject();
                if (!region || !region->page) continue; sf::Texture *tex = static_cast<sf::Texture*>(region->page->getRendererObject());
                if (!tex) continue;
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

        // One-time framebuffer alpha diagnostic: read a pixel where we expect transparency
        static bool didDiag = false;
        if (!didDiag) {
            didDiag = true;
            // pick a pixel at (10,10) which should be background (no skeleton)
            GLint x = 10, y = 10; GLubyte px[4] = {0,0,0,0};
#if defined(__APPLE__)
            glFlush();
            glReadBuffer(GL_BACK);
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            std::cout << "FB Pixel RGBA at ("<<x<<","<<y<<") = ("<<(int)px[0]<<","<<(int)px[1]<<","<<(int)px[2]<<","<<(int)px[3]<<")" << std::endl;
#else
            std::cout << "FB readback not implemented on this platform" << std::endl;
#endif
        }
        // Handle pending scheduled switches
        if (pendingSwitch) {
            // If a mouse-triggered touch is active, defer executing any pending switch
            // (prevents a previously scheduled key-3 switch from interrupting a restarted touch).
            if (inTouch) {
                // leave pendingSwitch as-is; it will be evaluated again next frame once inTouch is false
            } else {
                float now = wallClock.getElapsedTime().asSeconds();
                if (now >= switchAt) {
                    pendingSwitch = false;
                    if (pendingAnimIndex >= 0 && pendingAnimIndex < (int)animationNames.size()) {
                        requestSwitchTo(pendingAnimIndex, true); // loop
                    }
                    pendingAnimIndex = -1;
                }
            }
        }
        // If there was an intent to start moving after a scheduled switch, and we're no longer in key3, activate it now.
        if (pendingMoveAfterSwitch != 0) {
            if (!(currentAnimIndex == key3AnimIndex && loop)) {
                if (pendingMoveAfterSwitch == -1) moveState = Move_Left;
                else if (pendingMoveAfterSwitch == 1) moveState = Move_Right;
                if (pendingMoveIsAuto) autoWalking = true;
                // clear pending intent
                pendingMoveAfterSwitch = 0;
                pendingMoveIsAuto = false;
            }
        }
    }

    return 0;
}
