#include <SFML/Graphics.hpp>
#include <iostream>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <array>
#include <dirent.h>

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
#include <spine/Bone.h>

// Provide default runtime extension (required by spine-cpp linkage)
namespace spine { SpineExtension* getDefaultExtension() { return new DefaultSpineExtension(); } }

static std::filesystem::path findResourceRoot(std::filesystem::path base) {
    auto hasAssets = [](const std::filesystem::path &p){
        return std::filesystem::exists(p/"rosmon.atlas") && std::filesystem::exists(p/"rosmon.skel");
    };
    if (hasAssets(base)) return base;
    // If we are inside build/ try parent
    if (base.filename() == "build" && hasAssets(base.parent_path())) return base.parent_path();
    // If inside .app/Contents/MacOS handled earlier, Resources already tried.
    // Try one level up regardless
    if (hasAssets(base.parent_path())) return base.parent_path();
    return base; // fallback original (will fail later with diagnostics)
}

class SfmlTextureLoader : public spine::TextureLoader {
public:
    SfmlTextureLoader(const std::string& atlasDir): _atlasDir(atlasDir) {}
    virtual void load(spine::AtlasPage &page, const spine::String &path) {
        std::string rel = path.buffer();
        std::filesystem::path p(rel);
        if (p.is_relative()) p = std::filesystem::path(_atlasDir) / p; // prepend atlas directory
        std::string full = p.string();
        sf::Texture *tex = new sf::Texture();
        if (!tex->loadFromFile(full)) {
            // Fuzzy: try to locate alternative png containing original stem inside atlas directory
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
                            if (tex->loadFromFile(entry.path().string())) {
                                std::cerr << "[TextureLoader] Fuzzy matched texture: " << fname << std::endl;
                                loaded = true; break;
                            }
                        }
                    }
                }
            } catch(...) {}
            if (!loaded) {
                std::cerr << "[TextureLoader] ERROR: failed to load texture file: " << rel << " (full: " << full << ")" << std::endl;
                delete tex;
                page.setRendererObject(NULL);
                page.width = page.height = 0;
                return;
            }
        }
        tex->setSmooth(true);
        page.setRendererObject(tex);
        auto s = tex->getSize();
        page.width = (int)s.x;
        page.height = (int)s.y;
    }
    virtual void unload(void *texture) { delete static_cast<sf::Texture*>(texture); }
private:
    std::string _atlasDir;
};

static sf::Color multiplyColor(const spine::Color &a, const spine::Color &b, const spine::Color &c) {
    float r = a.r * b.r * c.r; float g = a.g * b.g * c.g; float bl = a.b * b.b * c.b; float al = a.a * b.a * c.a;
    return sf::Color((std::uint8_t)(r*255.0f), (std::uint8_t)(g*255.0f), (std::uint8_t)(bl*255.0f), (std::uint8_t)(al*255.0f));
}

static void listAnimations(spine::SkeletonData* data) {
    auto &anims = data->getAnimations();
    std::cout << "Animations (" << anims.size() << "):\n";
    for (int i=0;i<(int)anims.size();++i) {
        std::cout << "  [" << i << "] " << anims[i]->getName().buffer()
                  << " duration=" << anims[i]->getDuration() << "s" << std::endl;
    }
}

static spine::String chooseAnimation(spine::SkeletonData *data) {
    spine::Vector<spine::Animation*> &anims = data->getAnimations();
    for (int i=0;i<(int)anims.size();++i) if (std::strcmp(anims[i]->getName().buffer(), "idle") == 0) return anims[i]->getName();
    if (anims.size() > 0) return anims[0]->getName();
    return spine::String("");
}

static const char* BUILD_TAG = "BUILD-TAG-2025-09-24-A";

static std::string asciiOnly(const std::string &in) {
    std::string out; out.reserve(in.size());
    for (unsigned char c : in) {
        if (c >= 32 && c < 127) out.push_back((char)c); else out.push_back('?');
    }
    return out;
}

#include <mach-o/dyld.h>
#include <limits.h>

static std::filesystem::path locateResourceDir(char* argv0) {
    // Prefer real executable path (handles double-click from Finder)
    char buf[PATH_MAX]; uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) == 0) {
        std::error_code ec;
        auto exe = std::filesystem::weakly_canonical(std::filesystem::path(buf), ec);
        if (!ec) {
            auto dir = exe.parent_path();
            // If inside MyApp.app/Contents/MacOS, redirect to ../Resources
            std::string dirStr = dir.string();
            auto pos = dirStr.rfind(".app/Contents/MacOS");
            if (pos != std::string::npos) {
                auto resources = dir.parent_path().parent_path() / "Resources"; // .../MyApp.app/Contents/Resources
                if (std::filesystem::exists(resources)) return resources;
            }
            return dir; // fallback to executable directory
        }
    }
    // Fallback: use argv0 directory if relative
    if (argv0) {
        std::filesystem::path p(argv0);
        if (p.has_parent_path()) return std::filesystem::weakly_canonical(p.parent_path());
    }
    return std::filesystem::current_path();
}

int main(int argc, char** argv) {
    std::cout << "[BUILD] " << BUILD_TAG << std::endl;

    auto resourceDir = locateResourceDir(argc>0?argv[0]:nullptr);
    resourceDir = findResourceRoot(resourceDir);
    std::cout << "[PATH] ResourceDir=" << resourceDir << std::endl;

    // Ensure working directory set to resourceDir so relative atlas/png works when double-clicked.
    std::error_code chEc; std::filesystem::current_path(resourceDir, chEc);
    if (chEc) {
        std::cout << "[WARN] Cannot change CWD to resourceDir: " << chEc.message() << std::endl;
    }

    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(960,640)), "Spine SFML Minimal");
    window.setFramerateLimit(60);

    // Determine atlas directory for relative texture paths
    std::filesystem::path atlasPath(resourceDir / "rosmon.atlas");
    std::string atlasDir = atlasPath.parent_path().string();
    if (atlasDir.empty()) atlasDir = ".";

    SfmlTextureLoader texLoader(atlasDir);
    spine::Atlas atlas(atlasPath.string().c_str(), &texLoader, true);

    spine::SkeletonBinary bin(&atlas);
    bin.setScale(1.0f); // can adjust global scale
    spine::SkeletonData *data = bin.readSkeletonDataFile((resourceDir/"rosmon.skel").string().c_str());
    if (!data) { std::cerr << "Failed to load skeleton data (looked in " << resourceDir << ")" << std::endl; return 1; }

    spine::String animName = chooseAnimation(data);
    if (animName.length() == 0) { std::cerr << "No animation found." << std::endl; return 2; }

    listAnimations(data);
    std::vector<spine::String> animationNames;
    for (int i=0;i<(int)data->getAnimations().size(); ++i) animationNames.push_back(data->getAnimations()[i]->getName());
    int currentAnimIndex = 0; // will adjust below
    // Ensure currentAnimIndex matches chosen anim
    for (int i=0;i<(int)animationNames.size(); ++i) if (animationNames[i] == animName) { currentAnimIndex = i; break; }

    spine::AnimationStateData stateData(data);
    spine::AnimationState state(&stateData);
    spine::Skeleton skeleton(data);

    skeleton.setPosition(480, 600); // place near bottom center (Y down in SFML)
    skeleton.setScaleY(-1.0f); // Spine uses Y up, SFML Y down -> flip vertically

    bool loop = true;
    auto *entry = state.setAnimation(0, animName, loop);

    bool debugPrint = false; // toggled by D
    float accumForFps = 0.f; int framesForFps = 0; float fps = 0.f;

    char cwdBuf[1024];
    if (getcwd(cwdBuf, sizeof(cwdBuf))) {
        std::cout << "[INFO] CWD: " << cwdBuf << std::endl;
    }
    std::cout << "[INFO] Expect files in resourceDir: rosmon.atlas rosmon.skel rosmon.png" << std::endl;

    setvbuf(stdout, NULL, _IONBF, 0); // unbuffer stdout
    auto fileExists=[&](const char* f){ struct stat st; return ::stat(f,&st)==0; };
    std::cout << "[DIAG] rosmon.atlas exists? " << (fileExists((resourceDir/"rosmon.atlas").string().c_str())?"YES":"NO") << std::endl;
    std::cout << "[DIAG] rosmon.skel exists?  " << (fileExists((resourceDir/"rosmon.skel").string().c_str())?"YES":"NO") << std::endl;
    std::cout << "[DIAG] rosmon.png exists?   " << (fileExists((resourceDir/"rosmon.png").string().c_str())?"YES":"NO") << std::endl;

    std::cout << "[DIAG] Skeleton bones=" << skeleton.getBones().size() << " slots=" << skeleton.getSlots().size()
              << " animations=" << data->getAnimations().size() << std::endl;
    if (data->getAnimations().size()==1) std::cout << "[NOTE] 只有一个动画, 自动轮播不会显著切换." << std::endl;

    std::ofstream logFile("runros_log.txt", std::ios::app);
    logFile << "===== Start runros_sfml PID=" << getpid() << " =====\n";
    const char* sshEnv = getenv("SSH_CONNECTION");
    if (sshEnv) {
        std::cout << "[WARN] 检测到 SSH 环境, 可能没有本地图形界面." << std::endl;
        logFile << "[WARN] SSH env: " << sshEnv << "\n";
    }

    if (animationNames.empty()) {
        std::cerr << "[FATAL] 没有动画, 程序退出." << std::endl; return 3;
    }

    long frameCount = 0; float heartbeatTimer = 0.f;

    sf::Clock clock;
    float autoCycleTimer = 0.f; int autoCycleIndex = currentAnimIndex; bool anyKeyReceived = false;
    window.requestFocus();
    std::cout.flush();
    std::array<bool,512> prevKeys{}; // large enough for key codes
    while (window.isOpen()) {
        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) window.close();
            if (ev->is<sf::Event::KeyPressed>()) {
                anyKeyReceived = true;
                auto key = ev->getIf<sf::Event::KeyPressed>();
                if (key) {
                    std::cout << "[KEY] scancode=" << (int)key->scancode << " code=" << (int)key->code << std::endl;
                    auto applyAnimIndex=[&](int idx){
                        if (idx>=0 && idx < (int)animationNames.size()) {
                            currentAnimIndex = idx; autoCycleIndex = idx;
                            auto chosen = animationNames[currentAnimIndex];
                            state.setAnimation(0, chosen, loop);
                            std::cout << "Switch animation -> " << chosen.buffer() << (loop?" (loop)":" (once)") << std::endl;
                        }
                    };
                    if (key->scancode >= sf::Keyboard::Scancode::Num1 && key->scancode <= sf::Keyboard::Scancode::Num9) {
                        int idx = (int)key->scancode - (int)sf::Keyboard::Scancode::Num1; applyAnimIndex(idx);
                    } else if (key->code >= sf::Keyboard::Key::Num1 && key->code <= sf::Keyboard::Key::Num9) {
                        int idx = (int)key->code - (int)sf::Keyboard::Key::Num1; applyAnimIndex(idx);
                    } else if (key->scancode == sf::Keyboard::Scancode::Right || key->code == sf::Keyboard::Key::Right) {
                        if (!animationNames.empty()) { currentAnimIndex = (currentAnimIndex + 1) % animationNames.size(); autoCycleIndex = currentAnimIndex; auto chosen = animationNames[currentAnimIndex]; state.setAnimation(0, chosen, loop); std::cout << "Next animation -> " << chosen.buffer() << std::endl; }
                    } else if (key->scancode == sf::Keyboard::Scancode::Left || key->code == sf::Keyboard::Key::Left) {
                        if (!animationNames.empty()) { currentAnimIndex = (currentAnimIndex - 1 + (int)animationNames.size()) % animationNames.size(); autoCycleIndex = currentAnimIndex; auto chosen = animationNames[currentAnimIndex]; state.setAnimation(0, chosen, loop); std::cout << "Prev animation -> " << chosen.buffer() << std::endl; }
                    } else if (key->scancode == sf::Keyboard::Scancode::Space || key->code == sf::Keyboard::Key::Space) {
                        loop = !loop; auto chosen = animationNames[currentAnimIndex]; state.setAnimation(0, chosen, loop); std::cout << "Toggle loop -> " << (loop?"ON":"OFF") << " for animation " << chosen.buffer() << std::endl;
                    } else if (key->scancode == sf::Keyboard::Scancode::D || key->code == sf::Keyboard::Key::D) {
                        debugPrint = !debugPrint; std::cout << "Debug print " << (debugPrint?"ENABLED":"DISABLED") << std::endl;
                    } else if (key->scancode == sf::Keyboard::Scancode::Escape || key->code == sf::Keyboard::Key::Escape) { window.close(); }
                }
            } else if (ev->is<sf::Event::TextEntered>()) {
                anyKeyReceived = true;
                auto te = ev->getIf<sf::Event::TextEntered>();
                if (te) {
                    char32_t uni = te->unicode;
                    if (uni >= U'1' && uni <= U'9') {
                        int idx = (int)(uni - U'1');
                        if (idx < (int)animationNames.size()) {
                            currentAnimIndex = idx; autoCycleIndex = idx;
                            auto chosen = animationNames[currentAnimIndex];
                            state.setAnimation(0, chosen, loop);
                            std::cout << "(TextEntered) Switch animation -> " << chosen.buffer() << std::endl;
                        }
                    }
                }
            }
        }
        float dt = clock.restart().asSeconds();
        if (dt > 0.1f) dt = 0.1f; // clamp
        autoCycleTimer += dt;
        // ---- Fallback polling (edge detection) ----
        auto pollKeyEdge = [&](sf::Keyboard::Key key){
            int idx = (int)key;
            bool pressed = sf::Keyboard::isKeyPressed(key);
            bool was = prevKeys[idx];
            prevKeys[idx] = pressed;
            return pressed && !was; // rising edge
        };
        bool anyPoll = false;
        if (!anyKeyReceived) {
            if (pollKeyEdge(sf::Keyboard::Key::Num1) || pollKeyEdge(sf::Keyboard::Key::Numpad1)) { if (animationNames.size()>0){ currentAnimIndex=0; autoCycleIndex=0; state.setAnimation(0, animationNames[0], loop); std::cout<<"[POLL] 1 -> "<<animationNames[0].buffer()<<std::endl; anyPoll=true; } }
            if (pollKeyEdge(sf::Keyboard::Key::Num2) || pollKeyEdge(sf::Keyboard::Key::Numpad2)) { if (animationNames.size()>1){ currentAnimIndex=1; autoCycleIndex=1; state.setAnimation(0, animationNames[1], loop); std::cout<<"[POLL] 2 -> "<<animationNames[1].buffer()<<std::endl; anyPoll=true; } }
            if (pollKeyEdge(sf::Keyboard::Key::Right)) { if(!animationNames.empty()){ currentAnimIndex=(currentAnimIndex+1)%animationNames.size(); autoCycleIndex=currentAnimIndex; state.setAnimation(0, animationNames[currentAnimIndex], loop); std::cout<<"[POLL] Right -> "<<animationNames[currentAnimIndex].buffer()<<std::endl; anyPoll=true; }}
            if (pollKeyEdge(sf::Keyboard::Key::Left)) { if(!animationNames.empty()){ currentAnimIndex=(currentAnimIndex-1+animationNames.size())%animationNames.size(); autoCycleIndex=currentAnimIndex; state.setAnimation(0, animationNames[currentAnimIndex], loop); std::cout<<"[POLL] Left -> "<<animationNames[currentAnimIndex].buffer()<<std::endl; anyPoll=true; }}
            if (pollKeyEdge(sf::Keyboard::Key::Space)) { loop=!loop; state.setAnimation(0, animationNames[currentAnimIndex], loop); std::cout<<"[POLL] Toggle loop -> "<<(loop?"ON":"OFF")<<std::endl; anyPoll=true; }
            if (pollKeyEdge(sf::Keyboard::Key::D)) { debugPrint=!debugPrint; std::cout<<"[POLL] Debug -> "<<(debugPrint?"ON":"OFF")<<std::endl; anyPoll=true; }
            if (pollKeyEdge(sf::Keyboard::Key::Escape)) { window.close(); }
            if (anyPoll) { anyKeyReceived = true; }
        }
        if (!anyKeyReceived && autoCycleTimer >= 4.f && animationNames.size() > 1) {
            autoCycleTimer = 0.f;
            autoCycleIndex = (autoCycleIndex + 1) % animationNames.size();
            currentAnimIndex = autoCycleIndex;
            auto chosen = animationNames[currentAnimIndex];
            state.setAnimation(0, chosen, loop);
            std::cout << "[AUTO] cycle animation -> " << chosen.buffer() << std::endl;
        }

        state.update(dt);
        state.apply(skeleton);
        skeleton.updateWorldTransform();

        accumForFps += dt; framesForFps++;
        if (accumForFps >= 0.5f) {
            fps = framesForFps / accumForFps;
            accumForFps = 0.f; framesForFps = 0;
        }

        if (debugPrint) {
            // Print one bone position every ~1s via accumForFps rollover or simple timer
            static float debugTimer = 0.f; debugTimer += dt;
            if (debugTimer >= 1.f) {
                debugTimer = 0.f;
                auto &bones = skeleton.getBones();
                if (bones.size() > 0) {
                    spine::Bone* root = bones[0];
                    std::cout << "Root bone pos(" << root->getWorldX() << "," << root->getWorldY() << ") time=" << state.getCurrent(0)->getTrackTime() << std::endl;
                }
            }
        }

        // Update window title with animation info + fps
        if (state.getCurrent(0)) {
            auto *cur = state.getCurrent(0)->getAnimation();
            std::ostringstream oss; oss.setf(std::ios::fixed); oss.precision(2);
            oss << "Spine SFML Minimal | Anim: " << cur->getName().buffer() << (loop?"(loop)":"(once)")
                << " t=" << state.getCurrent(0)->getTrackTime() << "/" << cur->getDuration() << "s FPS=" << fps;
            if (!anyKeyReceived) oss << " | no-input press 1/2 arrows space D";
            window.setTitle(asciiOnly(oss.str()));
        } else {
            window.setTitle(asciiOnly("Spine SFML Minimal | no current animation"));
        }

        window.clear(sf::Color(30,30,40));

        // Draw each slot in draw order
        spine::Vector<spine::Slot*> &drawOrder = skeleton.getDrawOrder();
        for (int i=0; i<(int)drawOrder.size(); ++i) {
            spine::Slot *slot = drawOrder[i];
            spine::Attachment *att = slot->getAttachment();
            if (!att) continue;

            if (att->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
                spine::RegionAttachment *ra = (spine::RegionAttachment*)att;
                float verts[8];
                ra->computeWorldVertices(slot->getBone(), verts, 0, 2);
                float *uvs = ra->getUVs().buffer();
                spine::AtlasRegion *region = (spine::AtlasRegion*)ra->getRendererObject();
                sf::Texture *tex = NULL;
                if (region && region->page) tex = static_cast<sf::Texture*>(region->page->getRendererObject());
                if (!tex) { continue; } // skip drawing if texture missing
                sf::Vertex quad[6];
                // original 4 verts: 0,1,2,3 -> triangles: (0,1,2) (0,2,3)
                sf::Vector2f pos[4]; sf::Vector2f texc[4];
                for (int v=0; v<4; ++v) {
                    pos[v] = sf::Vector2f(verts[v*2], verts[v*2+1]);
                    texc[v] = sf::Vector2f(uvs[v*2] * tex->getSize().x, uvs[v*2+1] * tex->getSize().y);
                }
                sf::Color col = multiplyColor(skeleton.getColor(), slot->getColor(), ra->getColor());
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
                if (!region || !region->page) continue;
                sf::Texture *tex = static_cast<sf::Texture*>(region->page->getRendererObject());
                if (!tex) { continue; }
                sf::Color col = multiplyColor(skeleton.getColor(), slot->getColor(), ma->getColor());
                sf::VertexArray arr(sf::PrimitiveType::Triangles, tris.size());
                for (int t=0; t<(int)tris.size(); ++t) {
                    int vi = tris[t];
                    float x = worldVertices[vi*2];
                    float y = worldVertices[vi*2+1];
                    float u = uvs[vi*2];
                    float v = uvs[vi*2+1];
                    arr[t].position = sf::Vector2f(x,y);
                    arr[t].texCoords = sf::Vector2f(u * tex->getSize().x, v * tex->getSize().y);
                    arr[t].color = col;
                }
                sf::RenderStates states; states.texture = tex; window.draw(arr, states);
            }
        }

        window.display();
    }

    logFile << "===== Normal exit =====\n";
    logFile.close();

    return 0;
}
