// Minimal spine-ts dual pets demo for plain JS usage.
// Requires a global `spine` (spine-ts webgl bundle) and a <canvas>.
// Exposes window.DualPets.initDualPets(options).

(function (global) {
  function initDualPets(options) {
    var canvas = options.canvas;
    var assetBaseUrl = (options.assetBaseUrl || "./assets").replace(/\/$/, "");
    var rosAtlas = options.rosAtlas || "rosmon.atlas";
    var rosSkeleton = options.rosSkeleton || "rosmon.skel";
    var susAtlas = options.susAtlas || "build_char_298_susuro_summer#6.atlas";
    var susSkeleton = options.susSkeleton || "build_char_298_susuro_summer#6.skel";

    var gl = canvas.getContext("webgl", { alpha: true, premultipliedAlpha: true });
    if (!gl) throw new Error("WebGL not available");

    var spineNS = global.spine;
    var assetManager = new spineNS.webgl.AssetManager(gl);

    var rosAtlasUrl = assetBaseUrl + "/" + rosAtlas;
    var rosSkelUrl = assetBaseUrl + "/" + rosSkeleton;
    var susAtlasUrl = assetBaseUrl + "/" + susAtlas;
    var susSkelUrl = assetBaseUrl + "/" + susSkeleton;

    assetManager.loadTextureAtlas(rosAtlasUrl);
    assetManager.loadBinary(rosSkelUrl);
    assetManager.loadTextureAtlas(susAtlasUrl);
    assetManager.loadBinary(susSkelUrl);

    function waitForAssets(done) {
      function loop() {
        if (assetManager.isLoadingComplete()) return done();
        if (assetManager.hasErrors()) throw new Error("Failed to load spine assets");
        requestAnimationFrame(loop);
      }
      loop();
    }

    waitForAssets(function () {
      var renderer = new spineNS.webgl.SceneRenderer(canvas, gl, false);
      renderer.premultipliedAlpha = true;

      function buildPet(kind, atlasUrl, skelUrl) {
        var atlas = assetManager.get(atlasUrl);
        var atlasLoader = new spineNS.AtlasAttachmentLoader(atlas);
        var skelBinary = new spineNS.SkeletonBinary(atlasLoader);
        var skelData = skelBinary.readSkeletonData(assetManager.get(skelUrl));

        var skeleton = new spineNS.Skeleton(skelData);
        var stateData = new spineNS.AnimationStateData(skelData);
        var state = new spineNS.AnimationState(stateData);

        skeleton.scaleY = -1;

        var anims = [];
        for (var i = 0; i < skelData.animations.length; i++) {
          anims.push(skelData.animations[i].name);
        }

        var idle = null, touch = null, walk = null, special = null;

        if (kind === "ros") {
          // RosMon：按照 run_dual_pets.cpp 中的逻辑确定 touch / key3(待机) / move
          var touchIndex = -1;
          for (var i = 0; i < anims.length; i++) {
            var n = anims[i];
            var lower = n.toLowerCase();
            if (lower.indexOf("touch") !== -1 || lower.indexOf("tap") !== -1) {
              touchIndex = i;
              break;
            }
          }
          if (touchIndex === -1 && anims.length > 1) touchIndex = 1;

          var indicesForNumbers = [];
          for (var j = 0; j < anims.length; j++) {
            if (j === touchIndex) continue;
            indicesForNumbers.push(j);
          }

          var key3Index = -1;
          if (indicesForNumbers.length >= 3) key3Index = indicesForNumbers[2];
          else if (anims.length > 0) key3Index = 0;

          var moveIndex = -1;
          for (var k = 0; k < anims.length; k++) {
            var n2 = anims[k];
            var lower2 = n2.toLowerCase();
            if (lower2.indexOf("move") !== -1 || lower2.indexOf("walk") !== -1) {
              moveIndex = k;
              break;
            }
          }
          if (moveIndex === -1 && anims.length >= 2) moveIndex = 1;

          idle = key3Index >= 0 ? anims[key3Index] : (anims.length > 0 ? anims[0] : null);
          touch = touchIndex >= 0 ? anims[touchIndex] : null;
          walk = moveIndex >= 0 ? anims[moveIndex] : null;
        } else {
          // Sussurro：固定索引映射 1 touch, 2 walk, 3 idle, 6 special
          function safe(idx) { return (idx >= 0 && idx < anims.length) ? idx : -1; }
          var tIdx = safe(1);
          var wIdx = safe(2);
          var iIdx = safe(3);
          var sIdx = safe(6);

          idle = iIdx >= 0 ? anims[iIdx] : (anims.length > 0 ? anims[0] : null);
          touch = tIdx >= 0 ? anims[tIdx] : null;
          walk = wIdx >= 0 ? anims[wIdx] : null;
          special = sIdx >= 0 ? anims[sIdx] : null;
        }

        if (!idle && anims.length > 0) idle = anims[0];
        if (idle) state.setAnimation(0, idle, true);

        return {
          kind: kind,
          skeleton: skeleton,
          state: state,
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
          playingSpecial: false
        };
      }

      var ros = buildPet("ros", rosAtlasUrl, rosSkelUrl);
      var sus = buildPet("sus", susAtlasUrl, susSkelUrl);

      var worldWidth = canvas.width;
      var worldHeight = canvas.height;

      function getAabb(p) {
        var bounds = new spineNS.SkeletonBounds();
        bounds.update(p.skeleton, true);
        return {
          minX: bounds.getMinX ? bounds.getMinX() : bounds.minX,
          maxX: bounds.getMaxX ? bounds.getMaxX() : bounds.maxX,
          minY: bounds.getMinY ? bounds.getMinY() : bounds.minY,
          maxY: bounds.getMaxY ? bounds.getMaxY() : bounds.maxY
        };
      }

      (function layoutCharacters() {
        var w = worldWidth;
        var h = worldHeight;

        var centerX = w * 0.5;
        if (centerX < ros.leftMargin) centerX = ros.leftMargin;
        if (centerX > w - ros.rightMargin) centerX = w - ros.rightMargin;

        ros.skeleton.x = centerX;
        ros.skeleton.y = h * 0.9375; // 类似 C++ 中 640 高度里的 600
        ros.skeleton.updateWorldTransform();

        var rosAabb = getAabb(ros);
        var rosMoveUp = 12 + 5 + 16;
        var bottomMargin = 4;
        var rosCurrentY = ros.skeleton.y;
        var rosDesiredY = rosCurrentY - rosMoveUp;
        var rosDeltaY = rosDesiredY - rosCurrentY;
        var rosNewMaxY = rosAabb.maxY + rosDeltaY;
        var targetBottom = h - bottomMargin;
        if (rosNewMaxY > targetBottom) {
          rosDesiredY -= (rosNewMaxY - targetBottom);
        }

        ros.baseX = centerX;
        ros.baseY = rosDesiredY;
        ros.skeleton.x = centerX;
        ros.skeleton.y = rosDesiredY;
        ros.skeleton.updateWorldTransform();

        var susDesiredY = rosDesiredY + 15;
        sus.baseX = centerX;
        sus.baseY = susDesiredY;
        sus.skeleton.x = centerX;
        sus.skeleton.y = susDesiredY;
        sus.skeleton.updateWorldTransform();
      })();

      canvas.addEventListener("click", function (ev) {
        var rect = canvas.getBoundingClientRect();
        var x = (ev.clientX - rect.left) * (canvas.width / rect.width);
        var y = (ev.clientY - rect.top) * (canvas.height / rect.height);

        function hit(p) {
          var bounds = new spineNS.SkeletonBounds();
          bounds.update(p.skeleton, true);
          return bounds.containsPoint(x, y);
        }

        var target = hit(sus) ? sus : (hit(ros) ? ros : null);
        if (!target) return;
        if (target.touchAnim) {
          target.state.setAnimation(0, target.touchAnim, false);
          target.idleHoldUntil = performance.now() + 1200;
          target.moveDir = 0;
          target.playingSpecial = false;
        }
      });

      var lastFrame = performance.now();

      function updatePet(p, now, dt) {
        p.state.update(dt / 1000);
        p.state.apply(p.skeleton);

        var elapsed = now;
        var cur = p.state.getCurrent(0);
        var inIdle = !!p.idleAnim && cur && cur.animation && cur.animation.name === p.idleAnim;
        var idleBlocked = elapsed < p.idleHoldUntil;

        if (inIdle && !idleBlocked && elapsed >= p.nextAutoWalk && p.walkAnim) {
          var aabb = getAabb(p);
          var canGoLeft = aabb.minX > p.leftMargin + 1;
          var canGoRight = aabb.maxX < worldWidth - p.rightMargin - 1;
          if (!canGoLeft && !canGoRight) {
            p.nextAutoWalk = elapsed + 5000 + Math.random() * 3000;
          } else {
            var dir = 0;
            if (!canGoLeft && canGoRight) dir = 1;
            else if (!canGoRight && canGoLeft) dir = -1;
            else dir = Math.random() < 0.5 ? -1 : 1;

            p.moveDir = dir;
            p.autoWalking = true;

            var curScaleX = (p.skeleton.scaleX === 0 ? 1 : p.skeleton.scaleX);
            var absScaleX = Math.abs(curScaleX) || 1;
            p.skeleton.scaleX = dir < 0 ? -absScaleX : absScaleX;

            p.state.setAnimation(0, p.walkAnim, true);

            var dur = 1000 + Math.random() * 1500;
            p.autoWalkEnd = elapsed + dur;
            p.nextAutoWalk = p.autoWalkEnd + 4000 + Math.random() * 3000;
          }
        }

        if (p.moveDir !== 0) {
          var dx = p.moveSpeed * (dt / 1000) * p.moveDir;
          p.skeleton.x += dx;

          var aabb2 = getAabb(p);
          if (aabb2.minX < p.leftMargin) {
            var shiftL = p.leftMargin - aabb2.minX;
            p.skeleton.x += shiftL;
          }
          if (aabb2.maxX > worldWidth - p.rightMargin) {
            var shiftR = (worldWidth - p.rightMargin) - aabb2.maxX;
            p.skeleton.x += shiftR;
          }
          if (elapsed >= p.autoWalkEnd) {
            p.moveDir = 0;
            p.autoWalking = false;
            if (p.idleAnim) p.state.setAnimation(0, p.idleAnim, true);
          }
        }

        p.skeleton.updateWorldTransform();
      }

      function renderPet(p) {
        renderer.drawSkeleton(p.skeleton, true);
      }

      function frame() {
        var now = performance.now();
        var dt = now - lastFrame;
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
    });
  }

  global.DualPets = global.DualPets || {};
  global.DualPets.initDualPets = initDualPets;
})(window);
