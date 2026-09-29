import * as THREE from "three";
import {
  snapTurnYaw,
  moveOffset,
  walkVelocityFromLook,
  smoothVelocity,
  teleportAimActive,
  applyStickDeadzone,
  smoothStick,
  smoothTurnYaw,
} from "./locomotion.js";
import {
  readThumbstick,
  toUeThumbstick,
  isTriggerClicked,
  isBClicked,
  snapshotGamepad,
  controllersByHand,
} from "./xr-input.js";
import { loadCatalogAndBindings, resolveInspectableRoot } from "./catalog.js";
import {
  collectCollisionMeshes,
  collectInspectableMeshes,
  pickFirst,
  pickTeleport,
  snapToFloor,
  resolveHorizontalMove,
} from "./collision.js";
import { createLaser, updateLaser, createTeleportMarker } from "./laser.js";
import { createInspectPanel, showPanel, hidePanel } from "./inspect-panel.js";
import { createDesktopInput } from "./desktop-input.js";
import { diagnoseXr, startXrSession, endXrSession, sessionHasLayers, xrHasViewerPose } from "./xr-session.js";
import { attachXrControllers, setPicoRay, pointerAim } from "./xr-controllers.js";
import { createXrRig, captureBaseSpace, clearXrRig, applyRigToReferenceSpace, applyStageFromPlayer } from "./xr-rig.js";
import { createDebugHud, updateDebugHud } from "./xr-debug.js";
import { createGreyboxScene, GREYBOX_SPAWN } from "./dev-greybox.js";
import { loadSpawn, tryLoadOfficeScene, worldOffsetToPutSpawnOnFloor } from "./scene-loader.js";
import { RoomEnvironment } from "three/addons/environments/RoomEnvironment.js";
import {
  ADULT_EYE_HEIGHT_M,
  INTERIOR_ACES_EXPOSURE,
  INTERIOR_ENV_INTENSITY,
  INTERIOR_FILL,
  INTERIOR_HEMI,
  INTERIOR_SUN,
  xrEyeLiftY,
} from "./exhibition-look.js";

const query = new URLSearchParams(window.location.search);
const requestedScale = Number(query.get("scale"));
const framebufferScale = Number.isFinite(requestedScale) && requestedScale > 0
  ? requestedScale
  : 0.85;
const debugPad = query.get("debugpad") === "1";

const statusEl = document.querySelector("#status");
const fpsEl = document.querySelector("#fps");
const statsEl = document.querySelector("#stats");
const enterVrBtn = document.querySelector("#enter-vr");
const padEl = document.querySelector("#pad-debug");
const hintEl = document.querySelector("#hint");

const clock = new THREE.Clock();
const scene = new THREE.Scene();
scene.background = new THREE.Color(0xb7c4d4);

const CM_TO_M = 0.01;
const camera = new THREE.PerspectiveCamera(70, window.innerWidth / window.innerHeight, 0.08, 400);
camera.rotation.order = "YXZ";

const renderer = new THREE.WebGLRenderer({
  antialias: false,
  alpha: false,
  depth: true,
  stencil: false,
  canvas: document.querySelector("#scene"),
  powerPreference: "high-performance",
});
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = INTERIOR_ACES_EXPOSURE;
renderer.setClearColor(0xb7c4d4, 1);
renderer.setPixelRatio(1);
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.xr.enabled = true;
renderer.xr.setFoveation(0);
renderer.xr.setFramebufferScaleFactor(framebufferScale);

const sky = new THREE.Mesh(
  new THREE.SphereGeometry(80, 16, 12),
  new THREE.MeshBasicMaterial({ color: 0xb7c4d4, side: THREE.BackSide, depthWrite: false }),
);
sky.name = "XrSky";
sky.frustumCulled = false;
scene.add(sky);

const hemi = new THREE.HemisphereLight(0xf2f4f8, 0x6d645c, INTERIOR_HEMI);
hemi.name = "OfficeHemi";
scene.add(hemi);
const sun = new THREE.DirectionalLight(0xfff3e4, INTERIOR_SUN);
sun.name = "OfficeSun";
sun.position.set(18, 32, 10);
scene.add(sun);
const fill = new THREE.DirectionalLight(0xd7e4f2, INTERIOR_FILL);
fill.position.set(-12, 10, -16);
scene.add(fill);

const pmrem = new THREE.PMREMGenerator(renderer);
scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
scene.environmentIntensity = INTERIOR_ENV_INTENSITY;

const stage = new THREE.Group();
stage.name = "XrLocomotionStage";
scene.add(stage);

const worldRoot = new THREE.Group();
worldRoot.name = "WorldCmToMeters";
worldRoot.scale.setScalar(CM_TO_M);
stage.add(worldRoot);

const dolly = new THREE.Group();
dolly.name = "PlayerDolly";
scene.add(dolly);
dolly.add(camera);

const xrPointers = attachXrControllers(renderer, scene, THREE, (controller) => {
  if (!controller) {
    return;
  }
  const aim = worldAimFromObject(controller);
  const hoverInfo = resolveHover(aim.origin, aim.direction);
  setHover(hoverInfo.hover);
  onInspect();
});

const laser = createLaser(THREE);
scene.add(laser);
const teleportMarker = createTeleportMarker(THREE);
scene.add(teleportMarker);
const inspectPanel = createInspectPanel(THREE);
scene.add(inspectPanel.mesh);
const xrRig = createXrRig();
const debugHud = createDebugHud(THREE);
scene.add(debugHud.mesh);

const raycaster = new THREE.Raycaster();
const desktopInput = createDesktopInput(renderer.domElement);
const aimOrigin = new THREE.Vector3();
const aimDirection = new THREE.Vector3();
const up = new THREE.Vector3(0, 1, 0);
const lookQuat = new THREE.Quaternion();
const lookEuler = new THREE.Euler(0, 0, 0, "YXZ");
const ndc = new THREE.Vector2();
const walkDir = new THREE.Vector3();
const rightAimOrigin = new THREE.Vector3();
const rightAimDirection = new THREE.Vector3();

const state = {
  spawn: GREYBOX_SPAWN,
  source: "greybox",
  yaw: 0,
  pitch: 0,
  snapCooldown: 0,
  walkSpeed: 220,
  laserMax: 1200,
  teleportAiming: false,
  xrActive: false,
  hover: null,
  collisionMeshes: [],
  inspectMeshes: [],
  prevLeftPad: null,
  prevRightPad: null,
  smoothMove: { x: 0, y: 0 },
  walkVelocity: { x: 0, z: 0 },
  frames: 0,
  fpsTime: 0,
  fps: 0,
};

function setStatus(text) {
  if (statusEl) statusEl.textContent = text;
}

function worldAimFromObject(object) {
  return pointerAim(object, aimOrigin, aimDirection);
}

function desktopAim(pointerEvent) {
  camera.getWorldPosition(aimOrigin);
  if (desktopInput.isPointerLocked() || !pointerEvent) {
    camera.getWorldDirection(aimDirection);
    return { origin: aimOrigin, direction: aimDirection };
  }
  ndc.x = (pointerEvent.clientX / window.innerWidth) * 2 - 1;
  ndc.y = -(pointerEvent.clientY / window.innerHeight) * 2 + 1;
  raycaster.setFromCamera(ndc, camera);
  aimOrigin.copy(raycaster.ray.origin);
  aimDirection.copy(raycaster.ray.direction);
  return { origin: aimOrigin, direction: aimDirection };
}

function forHoverMeshes(node, visit) {
  if (!node) {
    return;
  }
  if (typeof node.traverse === "function") {
    node.traverse((child) => {
      if (child.isMesh && child.material) {
        visit(child);
      }
    });
    return;
  }
  if (node.isMesh && node.material) {
    visit(node);
  }
}

function restoreHoverMesh(mesh) {
  if (mesh.material?.emissive) {
    mesh.material.emissive.setHex(0x000000);
  }
  if (mesh.userData.baseColor != null && mesh.material?.color) {
    mesh.material.color.setHex(mesh.userData.baseColor);
  }
}

function highlightHoverMesh(mesh) {
  if (mesh.userData.baseColor == null && mesh.material?.color) {
    mesh.userData.baseColor = mesh.material.color.getHex();
  }
  if (mesh.material?.emissive) {
    mesh.material.emissive.setHex(0x145c2a);
  } else if (mesh.material?.color) {
    mesh.material.color.setHex(0x66ff88);
  }
}

function resolveHover(origin, direction) {
  const hit = pickFirst(raycaster, origin, direction, state.inspectMeshes, state.laserMax);
  if (!hit) {
    return { hover: null, distance: state.laserMax };
  }
  return { hover: resolveInspectableRoot(hit.object), distance: hit.distance };
}

function setHover(next) {
  if (state.hover && state.hover !== next) {
    forHoverMeshes(state.hover, restoreHoverMesh);
  }
  state.hover = next;
  forHoverMeshes(next, highlightHoverMesh);
}

function headCamera() {
  return renderer.xr.isPresenting ? renderer.xr.getCamera() : camera;
}

function attachToHead(object, localOffset) {
  const cam = headCamera();
  cam.getWorldPosition(aimOrigin);
  cam.getWorldQuaternion(lookQuat);
  object.quaternion.copy(lookQuat);
  object.position.copy(aimOrigin).add(localOffset.clone().applyQuaternion(lookQuat));
}

function applyLocomotion(dt, moveX, moveY, turnX, teleportHeld, headYaw) {
  if (state.xrActive) {
    xrRig.yaw = smoothTurnYaw(xrRig.yaw, turnX, dt);
  } else {
    const snapped = snapTurnYaw(
      state.yaw,
      turnX,
      state.snapCooldown,
      dt,
      state.spawn.snapTurnDegrees,
    );
    state.snapCooldown = snapped.cooldown;
    state.yaw = snapped.yaw;
  }
  let offset;
  if (state.xrActive) {
    headCamera().getWorldDirection(walkDir);
    const desired = walkVelocityFromLook(
      walkDir.x,
      walkDir.z,
      moveX,
      moveY,
      state.walkSpeed,
    );
    state.walkVelocity = smoothVelocity(state.walkVelocity, desired, dt, 10);
    offset = {
      x: state.walkVelocity.x * dt,
      z: state.walkVelocity.z * dt,
    };
  } else {
    offset = moveOffset(headYaw, moveX, moveY, state.walkSpeed, dt);
  }
  const radius = (state.spawn.capsuleRadius || 34) * CM_TO_M;
  if (state.xrActive) {
    const next = resolveHorizontalMove(
      raycaster,
      { x: 0, y: 0, z: 0 },
      offset.x,
      offset.z,
      state.collisionMeshes,
      radius,
      THREE,
    );
    xrRig.x += next.x;
    xrRig.z += next.z;
    applyStageFromPlayer(stage, xrRig);
    captureBaseSpace(renderer, xrRig);
    applyRigToReferenceSpace(renderer, xrRig);
  } else {
    camera.rotation.y = -state.yaw;
    camera.rotation.x = state.pitch;
    const next = resolveHorizontalMove(
      raycaster,
      dolly.position,
      offset.x,
      -offset.z,
      state.collisionMeshes,
      radius,
      THREE,
    );
    dolly.position.x = next.x;
    dolly.position.z = next.z;
    snapToFloor(raycaster, dolly.position, state.collisionMeshes, THREE);
  }
  return teleportAimActive(teleportHeld ? -1 : 0, teleportHeld);
}

function updateTeleport(aiming, origin, direction) {
  if (!aiming) {
    if (state.teleportAiming) {
      const result = pickTeleport(raycaster, origin, direction, state.collisionMeshes, state.laserMax, THREE);
      if (result.valid) {
        if (state.xrActive) {
          xrRig.x += result.point.x;
          xrRig.z += result.point.z;
          applyStageFromPlayer(stage, xrRig);
          applyRigToReferenceSpace(renderer, xrRig);
        } else {
          dolly.position.set(result.point.x, result.point.y, result.point.z);
        }
      }
    }
    state.teleportAiming = false;
    teleportMarker.visible = false;
    return;
  }
  state.teleportAiming = true;
  const result = pickTeleport(raycaster, origin, direction, state.collisionMeshes, state.laserMax, THREE);
  teleportMarker.visible = result.valid;
  if (result.valid) {
    teleportMarker.position.copy(result.point);
    teleportMarker.position.y += 0.02;
  }
}

function currentHeadYaw() {
  headCamera().getWorldQuaternion(lookQuat);
  lookEuler.setFromQuaternion(lookQuat, "YXZ");
  return lookEuler.y;
}

function updateStats(dt) {
  state.frames += 1;
  state.fpsTime += dt;
  if (state.fpsTime >= 0.5) {
    state.fps = Math.round(state.frames / state.fpsTime);
    state.frames = 0;
    state.fpsTime = 0;
  }
  const draws = renderer.info.render.calls;
  const tris = renderer.info.render.triangles;
  if (fpsEl) fpsEl.textContent = `${state.fps} FPS`;
  if (statsEl) {
    statsEl.textContent = `${state.source} · draws ${draws} · tris ${tris.toLocaleString()} · XR scale ${framebufferScale}`;
  }
}

function onInspect() {
  if (state.hover?.userData?.item) {
    showPanel(inspectPanel, state.hover.userData.item);
  }
}

function onClose() {
  hidePanel(inspectPanel);
}

function tick() {
  const dt = Math.min(clock.getDelta(), state.xrActive ? 0.033 : 0.05);
  const xrActive = renderer.xr.isPresenting;
  if (xrActive !== state.xrActive) {
    state.xrActive = xrActive;
    if (xrActive) {
      document.exitPointerLock?.();
      dolly.position.set(0, 0, 0);
      dolly.rotation.set(0, 0, 0);
      if (camera.parent === dolly) {
        dolly.remove(camera);
        scene.add(camera);
      }
      camera.position.set(0, 0, 0);
      camera.rotation.set(0, 0, 0);
      camera.near = 0.08;
      camera.far = 400;
      camera.updateProjectionMatrix();
      const spaceType = xrRig.spaceType;
      const standing = (state.spawn.eyeHeightFromFloor || 164) * CM_TO_M || ADULT_EYE_HEIGHT_M;
      clearXrRig(xrRig);
      xrRig.spaceType = spaceType;
      xrRig.eyeLift = xrEyeLiftY(spaceType, null, standing);
      xrRig.eyeLocked = spaceType !== "local-floor";
      applyStageFromPlayer(stage, xrRig);
      renderer.xr.setFoveation(0);
      state.smoothMove = { x: 0, y: 0 };
      state.walkVelocity = { x: 0, z: 0 };
      laser.visible = false;
      if (enterVrBtn) enterVrBtn.textContent = "退出 VR";
      setStatus("WebXR 会话已开始。头显里如果还能看到这个网页，就还没进沉浸式。");
    } else {
      clearXrRig(xrRig);
      if (camera.parent !== dolly) {
        scene.remove(camera);
        dolly.add(camera);
      }
      camera.position.set(0, state.spawn.eyeHeightFromFloor * CM_TO_M, 0);
      camera.rotation.set(state.pitch, -state.yaw, 0);
      applyStageFromPlayer(stage, xrRig);
      state.smoothMove = { x: 0, y: 0 };
      state.walkVelocity = { x: 0, z: 0 };
      laser.visible = true;
      if (enterVrBtn) enterVrBtn.textContent = "进入 VR";
      setStatus(state.source === "glb" ? "办公室 glTF" : "灰盒场景");
    }
  }

  let moveX = 0;
  let moveY = 0;
  let turnX = 0;
  let teleportHeld = false;
  let aim;

  if (xrActive) {
    const pads = [...(navigator.getGamepads?.() || [])];
    const hands = controllersByHand(renderer.xr.getSession(), renderer, pads);
    const leftPad = hands.left?.gamepad || null;
    const rightPad = hands.right?.gamepad || null;
    const rawLeft = toUeThumbstick(readThumbstick(leftPad));
    const leftStick = applyStickDeadzone(rawLeft.x, rawLeft.y);
    const rightStick = toUeThumbstick(readThumbstick(rightPad));
    state.smoothMove = smoothStick(state.smoothMove, leftStick, dt, 11);
    moveX = state.smoothMove.x;
    moveY = state.smoothMove.y;
    turnX = rightStick.x;
    teleportHeld = rightStick.y < -0.55;
    if (isTriggerClicked(state.prevRightPad, snapshotGamepad(rightPad))) onInspect();
    if (isBClicked(state.prevRightPad, snapshotGamepad(rightPad))) onClose();
    state.prevLeftPad = snapshotGamepad(leftPad);
    state.prevRightPad = snapshotGamepad(rightPad);
    const rightObj = hands.right?.object;
    aim = rightObj ? worldAimFromObject(rightObj) : worldAimFromObject(camera);
    rightAimOrigin.copy(aim.origin);
    rightAimDirection.copy(aim.direction);
    if (debugPad && padEl) {
      padEl.hidden = false;
      padEl.textContent = `L ${leftStick.x.toFixed(2)},${leftStick.y.toFixed(2)}  R ${rightStick.x.toFixed(2)},${rightStick.y.toFixed(2)}`;
    }
  } else {
    const look = desktopInput.consumeLook();
    state.yaw += look.x * 0.0025;
    state.pitch -= look.y * 0.0025;
    state.pitch = Math.max(-1.2, Math.min(1.2, state.pitch));
    const walk = desktopInput.axisFromKeys();
    moveX = walk.x;
    moveY = walk.y;
    turnX = desktopInput.snapFromKeys();
    teleportHeld = desktopInput.teleportHeld();
    aim = desktopAim();
  }

  const headYaw = xrActive ? currentHeadYaw() : state.yaw;
  if (xrActive && !xrRig.eyeLocked && xrHasViewerPose(renderer)) {
    const standing = (state.spawn.eyeHeightFromFloor || 164) * CM_TO_M || ADULT_EYE_HEIGHT_M;
    headCamera().getWorldPosition(aimOrigin);
    xrRig.eyeLift = xrEyeLiftY(xrRig.spaceType, aimOrigin.y, standing);
    xrRig.eyeLocked = true;
    applyStageFromPlayer(stage, xrRig);
  }
  const aimingTeleport = applyLocomotion(dt, moveX, moveY, turnX, teleportHeld, headYaw);
  if (xrActive) {
    laser.visible = false;
    const rayTargets = state.collisionMeshes.concat(state.inspectMeshes);
    for (const pointer of xrPointers) {
      const handAim = pointerAim(pointer.controller, aimOrigin, aimDirection);
      const handHover = resolveHover(handAim.origin, handAim.direction);
      const surface = pickFirst(raycaster, handAim.origin, handAim.direction, rayTargets, state.laserMax);
      const distance = surface?.distance ?? state.laserMax;
      setPicoRay(pointer.visual, distance, !!handHover.hover, !!surface);
      const hand = pointer.controller.userData.inputSource?.handedness;
      const isRight = hand === "right" || (hand == null && pointer === xrPointers[1]);
      if (isRight) {
        setHover(handHover.hover);
        rightAimOrigin.copy(handAim.origin);
        rightAimDirection.copy(handAim.direction);
      }
    }
    updateTeleport(aimingTeleport, rightAimOrigin, rightAimDirection);
  } else {
    const hoverInfo = resolveHover(aim.origin, aim.direction);
    setHover(hoverInfo.hover);
    updateLaser(laser, aim.origin, aim.direction, hoverInfo.distance, !!hoverInfo.hover, THREE);
    updateTeleport(aimingTeleport, aim.origin, aim.direction);
  }
  attachToHead(inspectPanel.mesh, new THREE.Vector3(0.18, -0.08, -0.9));
  const session = renderer.xr.getSession?.();
  const hasPose = xrActive && xrHasViewerPose(renderer);
  debugHud.mesh.visible = debugPad && !(xrActive && hasPose);
  const features = session?.enabledFeatures ? [...session.enabledFeatures].join(",") : "none";
  if (xrActive) {
    setStatus(hasPose
      ? `头显已出画 · ${sessionHasLayers(session) ? "layers" : "webgl-layer"} · ${xrRig.spaceType || "space?"} · 眼高+${xrRig.eyeLift.toFixed(2)}m · ${features}`
      : "会话已开始，但头显还没有姿态。若你仍能看到网页，说明还没进沉浸式。");
  }
  updateDebugHud(debugHud, [
    hasPose ? "沉浸式 VR：是（已收到头显姿态）" : (xrActive ? "沉浸式 VR：会话中，无姿态" : "沉浸式 VR：否（仍是网页）"),
    `手柄源 ${session?.inputSources?.length ?? 0}  左摇杆 ${moveX.toFixed(2)},${moveY.toFixed(2)}  右摇杆 ${turnX.toFixed(2)}  ${xrRig.spaceType || "no-space"}`,
    hasPose ? "头显转头看，手柄只负责走/射线" : "头显里不应再看到地址栏。若仍看到网页，点退出 VR 后把状态发给我",
  ]);
  updateStats(dt);
  renderer.render(scene, camera);
}

function bindUi() {
  enterVrBtn?.addEventListener("click", async () => {
    try {
      if (renderer.xr.isPresenting) {
        await endXrSession(renderer);
        return;
      }
      const { session } = await startXrSession(renderer, (spaceType) => {
        const standing = (state.spawn.eyeHeightFromFloor || 164) * CM_TO_M || ADULT_EYE_HEIGHT_M;
        xrRig.spaceType = spaceType;
        xrRig.eyeLift = xrEyeLiftY(spaceType, null, standing);
        xrRig.eyeLocked = spaceType !== "local-floor";
        applyStageFromPlayer(stage, xrRig);
      });
      enterVrBtn.textContent = "退出 VR";
      session.addEventListener("end", () => {
        enterVrBtn.textContent = "进入 VR";
        refreshVrButton();
      });
    } catch (error) {
      setStatus(`无法进入 VR：${error.message}`);
    }
  });

  window.addEventListener("keydown", (event) => {
    if (event.code === "Escape" || event.code === "KeyB") {
      onClose();
    }
  });

  window.addEventListener("mousedown", (event) => {
    if (state.xrActive) {
      return;
    }
    if (event.button === 0) {
      const aim = desktopAim(event);
      const hoverInfo = resolveHover(aim.origin, aim.direction);
      setHover(hoverInfo.hover);
      onInspect();
    }
    if (event.button === 2) {
      onClose();
    }
  });

  window.addEventListener("contextmenu", (event) => event.preventDefault());
  window.addEventListener("resize", () => {
    if (renderer.xr.isPresenting) {
      return;
    }
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
  });
}

async function refreshVrButton() {
  const info = await diagnoseXr();
  const prefix = state.source === "glb" ? "办公室 glTF" : "灰盒";
  if (enterVrBtn) {
    enterVrBtn.hidden = !info.canRequest;
    enterVrBtn.textContent = "进入 VR";
  }
  if (info.reason === "no-xr" || info.reason === "not-secure") {
    setStatus(`${prefix} · 当前窗口没有 WebXR。请关掉这个标签，用 start.cmd 新开的 Edge`);
    return;
  }
  if (info.ready) {
    setStatus(`${prefix} · 已检测到头显，点右下角进入 VR`);
    return;
  }
  setStatus(`${prefix} · 浏览器有 WebXR，但还没看到头显。用 start.cmd 打开的 Edge 再点进入 VR`);
}

async function boot() {
  bindUi();
  const { items, bindings } = await loadCatalogAndBindings("/data");
  const spawn = await loadSpawn("/data");
  setStatus("正在加载办公室模型…");
  const loaded = await tryLoadOfficeScene(spawn, items, bindings, "/data", (ratio) => {
    setStatus(`正在加载办公室 ${Math.round(ratio * 100)}%`);
  });
  let world;
  if (loaded) {
    world = loaded.root;
    state.source = "glb";
    state.spawn = spawn;
    setStatus("已加载 architecture.glb");
  } else {
    world = createGreyboxScene();
    state.source = "greybox";
    state.spawn = { ...GREYBOX_SPAWN, ...spawn, gltfUp: "y" };
    setStatus("未找到 architecture.glb，使用灰盒");
  }
  worldRoot.add(world);
  if (state.source === "glb") {
    const offset = worldOffsetToPutSpawnOnFloor(state.spawn);
    worldRoot.position.set(offset.x, offset.y, offset.z);
    dolly.position.set(0, 0, 0);
  } else {
    const start = state.spawn.playerStart || { x: 0, y: 0, z: 92 };
    dolly.position.set(start.x * CM_TO_M, 0, 0);
  }
  worldRoot.updateMatrixWorld(true);
  state.walkSpeed = state.spawn.walkSpeed * CM_TO_M;
  state.laserMax = state.spawn.laserMaxDistance * CM_TO_M;
  state.collisionMeshes = collectCollisionMeshes(world);
  state.inspectMeshes = collectInspectableMeshes(world);
  camera.position.set(0, state.spawn.eyeHeightFromFloor * CM_TO_M, 0);
  snapToFloor(raycaster, dolly.position, state.collisionMeshes, THREE);

  await refreshVrButton();
  window.setInterval(() => {
    if (!state.xrActive) {
      refreshVrButton();
    }
  }, 3000);
  renderer.setAnimationLoop(tick);
}

boot().catch((error) => {
  setStatus(error.message);
  console.error(error);
});
