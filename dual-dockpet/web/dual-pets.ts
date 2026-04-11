// Minimal spine-ts based dual-pet demo for web
// This is framework-agnostic TypeScript you can copy into the weblog repo.
// It assumes you include the spine-ts webgl build on the page, exposing a global `spine` object
// (for example via a CDN script tag). Adjust to your actual spine-ts distribution.

/* global spine */

declare const spine: any;

export interface DualPetsOptions {
  canvas: HTMLCanvasElement;
  // Asset base path, e.g. "/pet/assets" or "./assets"
  assetBaseUrl?: string;
  // File names for rosmon & sussurro; you can change these to match extracted assets
  rosAtlas?: string;
  rosSkeleton?: string; // .skel or .json
  susAtlas?: string;
  susSkeleton?: string;
}

interface PetRuntime {
  kind: "ros" | "sus";
  skeleton: any;
  state: any;
  idleAnim: string | null;
  touchAnim: string | null;
  walkAnim: string | null;
  specialAnim: string | null; // only used by Sussurro
  baseX: number;
  baseY: number;
  moveDir: number; // -1, 0, 1
  moveSpeed: number;
   leftMargin: number;
   rightMargin: number;
   autoWalking: boolean;
  nextAutoWalk: number;
  autoWalkEnd: number;
  idleHoldUntil: number;
  playingSpecial: boolean;
}

export async function initDualPets(options: DualPetsOptions): Promise<void> {
  const {
    canvas,
    assetBaseUrl = "./assets",
    rosAtlas = "rosmon.atlas",
    rosSkeleton = "rosmon.skel",
    susAtlas = "build_char_298_susuro_summer#6.atlas",
    susSkeleton = "build_char_298_susuro_summer#6.skel",
  } = options;

  const gl = canvas.getContext("webgl", { alpha: true, premultipliedAlpha: true });
  if (!gl) throw new Error("WebGL not available");

  const assetManager = new spine.webgl.AssetManager(gl);

  const rosAtlasUrl = assetBaseUrl.replace(/\/$/, "") + "/" + rosAtlas;
  const rosSkelUrl = assetBaseUrl.replace(/\/$/, "") + "/" + rosSkeleton;
  const susAtlasUrl = assetBaseUrl.replace(/\/$/, "") + "/" + susAtlas;
  const susSkelUrl = assetBaseUrl.replace(/\/$/, "") + "/" + susSkeleton;

  assetManager.loadTextureAtlas(rosAtlasUrl);
  assetManager.loadBinary(rosSkelUrl);
  assetManager.loadTextureAtlas(susAtlasUrl);
  assetManager.loadBinary(susSkelUrl);

  await new Promise<void>((resolve, reject) => {
    (function wait() {
      if (assetManager.isLoadingComplete()) return resolve();
      if (assetManager.hasErrors()) return reject(new Error("Failed to load spine assets"));
      requestAnimationFrame(wait);
    })();
  });

  const renderer = new spine.webgl.SceneRenderer(canvas, gl, false);
  renderer.premultipliedAlpha = true;

  function buildPet(kind: "ros" | "sus", atlasUrl: string, skelUrl: string, flipX: boolean): PetRuntime {
    const atlas = assetManager.get(atlasUrl);
    const atlasLoader = new spine.AtlasAttachmentLoader(atlas);
    const skelBinary = new spine.SkeletonBinary(atlasLoader);
    const skelData = skelBinary.readSkeletonData(assetManager.get(skelUrl));

    const skeleton = new spine.Skeleton(skelData);
    const stateData = new spine.AnimationStateData(skelData);
    const state = new spine.AnimationState(stateData);

    if (flipX) skeleton.scaleX = -1;
    skeleton.scaleY = -1;

    const anims: string[] = [];
    for (let i = 0; i < skelData.animations.length; i++) {
      anims.push(skelData.animations[i].name as string);
    }

    let idle: string | null = null;
    let touch: string | null = null;
    let walk: string | null = null;
    let special: string | null = null;

    if (kind === "ros") {
      // RosMon：尽量复刻 run_dual_pets.cpp 中的 key3/待机 和 move 选择逻辑
      let touchIndex = -1;
      for (let i = 0; i < anims.length; i++) {
        const n = anims[i];
        const lower = n.toLowerCase();
        if (lower.includes("touch") || lower.includes("tap")) {
          touchIndex = i;
          break;
        }
      }
      if (touchIndex === -1 && anims.length > 1) touchIndex = 1;

      const indicesForNumbers: number[] = [];
      for (let i = 0; i < anims.length; i++) {
        if (i === touchIndex) continue;
        indicesForNumbers.push(i);
      }

      let key3Index = -1;
      if (indicesForNumbers.length >= 3) key3Index = indicesForNumbers[2];
      else if (anims.length > 0) key3Index = 0;

      let moveIndex = -1;
      for (let i = 0; i < anims.length; i++) {
        const n = anims[i];
        const lower = n.toLowerCase();
        if (lower.includes("move") || lower.includes("walk")) {
          moveIndex = i;
          break;
        }
      }
      if (moveIndex === -1 && anims.length >= 2) moveIndex = 1;

      idle = key3Index >= 0 ? anims[key3Index] : (anims.length > 0 ? anims[0] : null);
      touch = touchIndex >= 0 ? anims[touchIndex] : null;
      walk = moveIndex >= 0 ? anims[moveIndex] : null;
    } else {
      // Sussurro：索引约定 1 touch, 2 walk, 3 idle, 6 special（超界时自动忽略）
      const safe = (idx: number) => (idx >= 0 && idx < anims.length ? idx : -1);
      const touchIndex = safe(1);
      const walkIndex = safe(2);
      const idleIndex = safe(3);
      const specialIndex = safe(6);

      idle = idleIndex >= 0 ? anims[idleIndex] : (anims.length > 0 ? anims[0] : null);
      touch = touchIndex >= 0 ? anims[touchIndex] : null;
      walk = walkIndex >= 0 ? anims[walkIndex] : null;
      special = specialIndex >= 0 ? anims[specialIndex] : null;
    }

    // 通用兜底（防止上面逻辑没选到 idle）
    if (!idle && anims.length > 0) idle = anims[0];

    if (idle) state.setAnimation(0, idle, true);

    return {
      kind,
      skeleton,
      state,
      idleAnim: idle,
      touchAnim: touch,
      walkAnim: walk,
      specialAnim: special,
      baseX: 0,
      baseY: 0,
      moveDir: 0,
      moveSpeed: 90,
      leftMargin: 80,
      rightMargin: 80,
      autoWalking: false,
      nextAutoWalk: performance.now() + 4000,
      autoWalkEnd: 0,
      idleHoldUntil: 0,
      playingSpecial: false,
    };
  }

  const ros = buildPet("ros", rosAtlasUrl, rosSkelUrl, false);
  const sus = buildPet("sus", susAtlasUrl, susSkelUrl, false);

  const worldWidth = canvas.width;
  const worldHeight = canvas.height;

  function getAabb(p: PetRuntime) {
    const bounds = new spine.SkeletonBounds();
    bounds.update(p.skeleton, true);
    return {
      minX: bounds.minX as number,
      maxX: bounds.maxX as number,
      minY: bounds.minY as number,
      maxY: bounds.maxY as number,
    };
  }

  // 尝试按照 C++ 版的思路摆放：先把 RosMon 放到底部附近，再在其下方放 Sussurro
  (function layoutCharacters() {
    const w = worldWidth;
    const h = worldHeight;

    const centerX = Math.max(ros.leftMargin, Math.min(w - ros.rightMargin, w * 0.5));

    // 初始放到略高于底部的位置（大致对应 C++ 中 y=600, h=640）
    ros.skeleton.x = centerX;
    ros.skeleton.y = h * 0.9375;
    ros.skeleton.updateWorldTransform();

    const rosAabb = getAabb(ros);
    const rosMoveUp = 12 + 5 + 16;
    const bottomMargin = 4;
    const rosCurrentY = ros.skeleton.y as number;
    let rosDesiredY = rosCurrentY - rosMoveUp;
    const rosDeltaY = rosDesiredY - rosCurrentY;
    const rosNewMaxY = rosAabb.maxY + rosDeltaY;
    const targetBottom = h - bottomMargin;
    if (rosNewMaxY > targetBottom) {
      rosDesiredY -= (rosNewMaxY - targetBottom);
    }

    ros.baseX = centerX;
    ros.baseY = rosDesiredY;
    ros.skeleton.x = centerX;
    ros.skeleton.y = rosDesiredY;
    ros.skeleton.updateWorldTransform();

    const susDesiredY = rosDesiredY + 15;
    sus.baseX = centerX;
    sus.baseY = susDesiredY;
    sus.skeleton.x = centerX;
    sus.skeleton.y = susDesiredY;
    sus.skeleton.updateWorldTransform();
  })();

  canvas.addEventListener("click", (ev) => {
    const rect = canvas.getBoundingClientRect();
    const x = (ev.clientX - rect.left) * (canvas.width / rect.width);
    const y = (ev.clientY - rect.top) * (canvas.height / rect.height);

    function hit(p: PetRuntime): boolean {
      const bounds = new spine.SkeletonBounds();
      bounds.update(p.skeleton, true);
      return bounds.containsPoint(x, y);
    }

    const target = hit(sus) ? sus : hit(ros) ? ros : null;
    if (!target) return;

    if (target.touchAnim) {
      target.state.setAnimation(0, target.touchAnim, false);
      target.idleHoldUntil = performance.now() + 1200;
      target.moveDir = 0;
      target.playingSpecial = false;
    }
  });

  let lastFrame = performance.now();

  function updatePet(p: PetRuntime, now: number, dt: number) {
    p.state.update(dt / 1000);
    p.state.apply(p.skeleton);

    const elapsed = now;

    const inIdle = !!p.idleAnim && p.state.getCurrent(0)?.animation?.name === p.idleAnim;
    const idleBlocked = elapsed < p.idleHoldUntil;

    if (inIdle && !idleBlocked && elapsed >= p.nextAutoWalk && p.walkAnim) {
      // 参考 C++：根据当前在窗口内的位置判断能否向左/向右走
      const aabb = getAabb(p);
      const canGoLeft = aabb.minX > p.leftMargin + 1;
      const canGoRight = aabb.maxX < worldWidth - p.rightMargin - 1;
      if (!canGoLeft && !canGoRight) {
        // 两边都被卡住，就延后下一次尝试
        p.nextAutoWalk = elapsed + 5000 + Math.random() * 3000;
      } else {
        let dir = 0;
        if (!canGoLeft && canGoRight) dir = 1;
        else if (!canGoRight && canGoLeft) dir = -1;
        else dir = Math.random() < 0.5 ? -1 : 1;

        p.moveDir = dir;
        p.autoWalking = true;

        // 根据移动方向翻转朝向：向左走朝左，向右走朝右
        const curScaleX = p.skeleton.scaleX === 0 ? 1 : p.skeleton.scaleX;
        const absScaleX = Math.abs(curScaleX) || 1;
        p.skeleton.scaleX = dir < 0 ? -absScaleX : absScaleX;

        p.state.setAnimation(0, p.walkAnim, true);

        // 行走时长：这里先用简单随机时长，和原 C++ 类似
        const dur = 1000 + Math.random() * 1500;
        p.autoWalkEnd = elapsed + dur;
        p.nextAutoWalk = p.autoWalkEnd + 4000 + Math.random() * 3000;
      }
    }

    if (p.moveDir !== 0) {
      const dx = p.moveSpeed * (dt / 1000) * p.moveDir;
      p.skeleton.x += dx;
      const aabb = getAabb(p);
      // 到达左右边界时夹紧
      if (aabb.minX < p.leftMargin) {
        const shift = p.leftMargin - aabb.minX;
        p.skeleton.x += shift;
      }
      if (aabb.maxX > worldWidth - p.rightMargin) {
        const shift = (worldWidth - p.rightMargin) - aabb.maxX;
        p.skeleton.x += shift;
      }
      if (elapsed >= p.autoWalkEnd) {
        p.moveDir = 0;
        p.autoWalking = false;
        if (p.idleAnim) p.state.setAnimation(0, p.idleAnim, true);
      }
    }

    p.skeleton.updateWorldTransform();
  }

  function renderPet(p: PetRuntime) {
    renderer.drawSkeleton(p.skeleton, true);
  }

  function frame() {
    const now = performance.now();
    const dt = now - lastFrame;
    lastFrame = now;

    updatePet(ros, now, dt);
    updatePet(sus, now, dt);

    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    renderer.begin();
    renderPet(ros);
    renderPet(sus);
    renderer.end();

    requestAnimationFrame(frame);
  }

  requestAnimationFrame(frame);
}
