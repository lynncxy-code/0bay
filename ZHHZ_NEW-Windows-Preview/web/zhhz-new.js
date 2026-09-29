import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { HDRLoader } from "three/addons/loaders/HDRLoader.js";
import * as BufferGeometryUtils from "three/addons/utils/BufferGeometryUtils.js";
import { startTwinRuntime } from "./zhhz-twin-runtime.js?build=20260904-fullhd60-v71-screen-uv";

const ASSET_ROOT = "/data/";
const GLTF_URL = `${ASSET_ROOT}architecture.gltf`;
const LIGHTMAP_MANIFEST_URL = `${ASSET_ROOT}lightmap-bindings.json`;
const REFLECTION_MANIFEST_URL = `${ASSET_ROOT}reflection-captures.json`;
const COPLANAR_FIX_MANIFEST_URL = `${ASSET_ROOT}coplanar-surface-fixes.json`;
const HDRI_URL = `${ASSET_ROOT}environment/AVIC_Show_environment.hdr`;
const query = new URLSearchParams(location.search);
const ENABLE_GPU_TIMER = query.get("gpuTimer") === "1";
// Windows QA defaults to the authored full baked set. Xinchuang can opt into
// the bounded streamer explicitly with ?lightmaps=stream after GPU testing.
const LIGHTMAP_MODE = query.get("lightmaps") === "stream" ? "stream" : "full";
const INITIAL_LIGHTMAP_BUDGET = LIGHTMAP_MODE === "full" ? 189 : 40;
const HIGH_QUALITY_LIGHTMAP_BUDGET = 64;
const LIGHTMAP_REFRESH_INTERVAL_MS = 900;
const LIGHTMAP_MOVE_THRESHOLD_METERS = 2.5;
const ARCHITECTURE_LOAD_RETRIES = 3;
// Static architecture batches are authored already, but a second conservative
// pass can join nearby meshes that use the exact same material/lightmap atlas.
// The cell keeps frustum bounds local; no texture, lightmap or vertex data is
// reduced.  A query switch is retained for A/B testing (staticBatch=0).
const STATIC_BATCH_ENABLED = query.get("staticBatch") !== "0";
const STATIC_BATCH_CELL_SIZE = 40;
const STATIC_BATCH_MAX_RADIUS = 60;
const NOOP_RENDER_CALLBACK = () => {};

const elements = {
  canvas: document.querySelector("#scene"),
  status: document.querySelector("#status"),
  fps: document.querySelector("#fps"),
  nodes: document.querySelector("#nodes"),
  meshes: document.querySelector("#meshes"),
  bindings: document.querySelector("#bindings"),
  lightmaps: document.querySelector("#lightmaps"),
  reflection: document.querySelector("#reflection"),
  drawCalls: document.querySelector("#draw-calls"),
  triangles: document.querySelector("#triangles"),
  textureMemory: document.querySelector("#texture-memory"),
  gpuTime: document.querySelector("#gpu-time"),
  progressBar: document.querySelector("#progress-bar"),
  progressCopy: document.querySelector("#progress-copy"),
  loading: document.querySelector("#loading"),
  loadingDetail: document.querySelector("#loading-detail"),
  loadingProgressBar: document.querySelector("#loading-progress-bar"),
  loadingPercent: document.querySelector("#loading-percent"),
  startupView: document.querySelector("#startup-view"),
  overview: document.querySelector("#overview"),
  moreLightmaps: document.querySelector("#more-lightmaps"),
  panel: document.querySelector("#panel"),
  togglePanel: document.querySelector("#toggle-panel"),
  showPanel: document.querySelector("#show-panel"),
};

const runtime = {
  phase: "boot",
  errors: [],
  warnings: [],
  localLightCount: 0,
  geometryLoaded: false,
  lightmapManifestLoaded: false,
  matchedBindings: 0,
  boundMeshes: 0,
  residentLightmapAtlases: 0,
  requestedLightmapBudget: INITIAL_LIGHTMAP_BUDGET,
  lightmapMode: LIGHTMAP_MODE,
  reflectionCapture: null,
  reflectionCaptureCount: 0,
  reflectionSwitchCount: 0,
  hdriLoaded: false,
  coplanarConflictPairs: 0,
  coplanarTargets: 0,
  coplanarTargetsApplied: 0,
  reflectionMode: "stable-nearest",
  renderTriangles: 0,
  renderCalls: 0,
  residentTextureBytes: 0,
  gpuTimerSupported: false,
  gpuTimeMs: null,
  gpuTimer: null,
};
window.__zhhzNewViewer = runtime;

function setStatus(message, progress = null) {
  elements.status.textContent = message;
  elements.progressCopy.textContent = message;
  elements.loadingDetail.textContent = message;
  if (progress !== null) {
    const boundedProgress = Math.max(0, Math.min(100, progress));
    elements.progressBar.style.width = `${boundedProgress}%`;
    if (elements.loadingProgressBar) elements.loadingProgressBar.style.width = `${boundedProgress}%`;
    if (elements.loadingPercent) elements.loadingPercent.textContent = `总体 ${Math.round(boundedProgress)}%`;
  }
}

function recordError(error) {
  const message = error instanceof Error ? `${error.name}: ${error.message}` : String(error);
  runtime.errors.push(message);
  runtime.phase = "error";
  setStatus(`加载失败：${message}`, 100);
  elements.loadingDetail.textContent = `加载失败：${message}`;
  console.error("[ZHHZ_NEW]", error);
}

async function loadArchitectureWithRetry(onProgress) {
  let lastError = null;
  for (let attempt = 1; attempt <= ARCHITECTURE_LOAD_RETRIES; attempt += 1) {
    try {
      const loader = new GLTFLoader();
      return await new Promise((resolve, reject) => {
        loader.load(GLTF_URL, resolve, onProgress, reject);
      });
    } catch (error) {
      lastError = error;
      if (attempt < ARCHITECTURE_LOAD_RETRIES) {
        setStatus(`建筑资源传输中断，正在重试（${attempt + 1}/${ARCHITECTURE_LOAD_RETRIES}）…`, 15);
        await new Promise((resolve) => setTimeout(resolve, 350 * attempt));
      }
    }
  }
  throw lastError ?? new Error("建筑几何与材质加载失败");
}

const renderer = new THREE.WebGLRenderer({
  canvas: elements.canvas,
  antialias: true,
  alpha: true,
  powerPreference: "high-performance",
  reversedDepthBuffer: true,
});
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
// UE baked reference is brighter than Three.js' unit-exposure ACES output.
// Keep this as an explicit renderer exposure, never as a runtime light.
renderer.toneMappingExposure = 1.8;
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1));
renderer.setSize(window.innerWidth, window.innerHeight, false);
try {
  const timerContext = renderer.getContext();
  const timerExtension = ENABLE_GPU_TIMER
    ? timerContext?.getExtension("EXT_disjoint_timer_query_webgl2")
    : null;
  runtime.gpuTimerSupported = Boolean(timerExtension && typeof timerContext.createQuery === "function");
  if (runtime.gpuTimerSupported) {
    runtime.gpuTimer = { context: timerContext, extension: timerExtension, query: null, frame: 0 };
  }
  if (elements.gpuTime) elements.gpuTime.textContent = runtime.gpuTimerSupported
    ? "EXT timer 低频采样"
    : ENABLE_GPU_TIMER ? "浏览器未提供 GPU 计时" : "默认关闭（?gpuTimer=1 开启）";
  document.body.dataset.gpuTimerSupported = String(runtime.gpuTimerSupported);
} catch {
  runtime.gpuTimerSupported = false;
  if (elements.gpuTime) elements.gpuTime.textContent = "浏览器未提供 GPU 计时";
}

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(58, window.innerWidth / window.innerHeight, 0.08, 2500);
const controls = new OrbitControls(camera, elements.canvas);
controls.enableDamping = true;
controls.dampingFactor = 0.075;
controls.screenSpacePanning = true;
controls.minDistance = 0.1;
controls.maxDistance = 1500;

// UE anchor: (-1265.340, -165.271, 339.107) cm, Pitch 7.2, Yaw 0.8.
// UE (X,Y,Z) -> glTF/Three (X,Z,Y), centimeters -> meters.
const startupPosition = new THREE.Vector3(-12.653401, 3.391065, -1.652708);
const pitch = THREE.MathUtils.degToRad(7.2);
const yaw = THREE.MathUtils.degToRad(0.8);
const startupForward = new THREE.Vector3(
  Math.cos(pitch) * Math.cos(yaw),
  Math.sin(pitch),
  Math.cos(pitch) * Math.sin(yaw),
).normalize();
const startupTarget = startupPosition.clone().addScaledVector(startupForward, 14);

const ueQaCameraPresets = {
  project_anchor_1: { location: [-1978.49761, -3048.553076, 665], rotation: [-9.8, 58, 0] },
  project_anchor_2: { location: [-1265.340117, -165.270805, 339.106533], rotation: [7.2, 0.8, 0] },
  overview_southeast: { location: [6100, -5000, 3300], rotation: [-18.327452, 143.130096, 0] },
  interior_east: { location: [4300, 2300, 850], rotation: [-2.187696, -151.476883, 0] },
  interior_west: { location: [-4500, -2600, 850], rotation: [-2.367541, 29.744881, 0] },
};

function useUeCameraPose(location, rotation, fov = 78) {
  const [pitchDegrees, yawDegrees] = rotation;
  const position = uePointToThree(location);
  const posePitch = THREE.MathUtils.degToRad(pitchDegrees);
  const poseYaw = THREE.MathUtils.degToRad(yawDegrees);
  const forward = new THREE.Vector3(
    Math.cos(posePitch) * Math.cos(poseYaw),
    Math.sin(posePitch),
    Math.cos(posePitch) * Math.sin(poseYaw),
  ).normalize();
  camera.fov = fov;
  camera.position.copy(position);
  controls.target.copy(position).addScaledVector(forward, 14);
  camera.updateProjectionMatrix();
  controls.update();
}

function useCameraPreset(name) {
  const preset = ueQaCameraPresets[name];
  if (!preset) return false;
  useUeCameraPose(preset.location, preset.rotation);
  runtime.activeCameraPreset = name;
  document.body.dataset.cameraPreset = name;
  refreshLightmapResidency(camera.position, true);
  updateNearestReflection(camera.position, true);
  return true;
}

function useStartupView() {
  camera.fov = 58;
  camera.position.copy(startupPosition);
  controls.target.copy(startupTarget);
  controls.update();
}
useStartupView();
runtime.useCameraPreset = useCameraPreset;

let sceneRoot = null;
let overviewBox = null;
let lightmapManifest = null;
let coplanarFixManifest = null;
let gltfResult = null;
let textureRecords = new Map();
let textureSpatialPoints = new Map();
let pendingLightmapKeys = new Set();
let desiredLightmapKeys = new Set();
let lightmapRefreshPromise = null;
let lastLightmapRefreshPosition = new THREE.Vector3(Infinity, Infinity, Infinity);
let lastLightmapRefreshAt = 0;
let reflectionCaptures = [];
let activeReflectionCapture = null;

const texturePairs = new Map();
const textureLoader = new THREE.TextureLoader();
const dummyTextures = new Map();
const patchedMaterialCache = new WeakMap();

function updateRenderMetrics() {
  const info = renderer.info;
  runtime.renderCalls = Number(info.render.calls || 0);
  runtime.renderTriangles = Number(info.render.triangles || 0);
  let residentBytes = 0;
  for (const key of texturePairs.keys()) {
    const record = textureRecords.get(key);
    for (const file of record?.files ?? []) {
      // This is the uncompressed RGBA8 upload estimate, intentionally
      // conservative. It makes the X300 memory budget auditable even when
      // the browser does not expose per-texture GPU allocations.
      residentBytes += Number(file.width || 0) * Number(file.height || 0) * 4;
    }
  }
  runtime.residentTextureBytes = residentBytes;
  if (elements.drawCalls) elements.drawCalls.textContent = runtime.renderCalls.toLocaleString();
  if (elements.triangles) elements.triangles.textContent = runtime.renderTriangles.toLocaleString();
  if (elements.textureMemory) {
    const gib = residentBytes / (1024 ** 3);
    elements.textureMemory.textContent = `${gib.toFixed(2)} GiB（RGBA8 估算）`;
  }
  document.body.dataset.renderCalls = String(runtime.renderCalls);
  document.body.dataset.renderTriangles = String(runtime.renderTriangles);
  document.body.dataset.residentTextureBytes = String(residentBytes);
  document.body.dataset.rendererTextureCount = String(info.memory?.textures || 0);
  document.body.dataset.rendererGeometryCount = String(info.memory?.geometries || 0);
}

function pollGpuTimer() {
  const timer = runtime.gpuTimer;
  if (!timer?.query) return;
  const { context: gl, extension: ext, query } = timer;
  try {
    if (!gl.getQuery(query, gl.QUERY_RESULT_AVAILABLE)) return;
    const disjoint = gl.getParameter(ext.GPU_DISJOINT_EXT);
    const elapsed = gl.getQuery(query, gl.QUERY_RESULT);
    gl.deleteQuery(query);
    timer.query = null;
    if (!disjoint && Number.isFinite(elapsed)) {
      runtime.gpuTimeMs = Number(elapsed) / 1e6;
      if (elements.gpuTime) elements.gpuTime.textContent = `${runtime.gpuTimeMs.toFixed(2)} ms`;
      document.body.dataset.gpuTimeMs = runtime.gpuTimeMs.toFixed(3);
    }
  } catch {
    timer.query = null;
    runtime.gpuTimerSupported = false;
    document.body.dataset.gpuTimerSupported = "false";
    if (elements.gpuTime) elements.gpuTime.textContent = "GPU 计时不可用";
  }
}

function beginGpuTimer() {
  const timer = runtime.gpuTimer;
  if (!timer || timer.query) return;
  const { context: gl, extension: ext } = timer;
  try {
    timer.query = gl.createQuery();
    gl.beginQuery(ext.TIME_ELAPSED_EXT, timer.query);
  } catch {
    timer.query = null;
    runtime.gpuTimerSupported = false;
    document.body.dataset.gpuTimerSupported = "false";
  }
}

function endGpuTimer() {
  const timer = runtime.gpuTimer;
  if (!timer?.query) return;
  try {
    timer.context.endQuery(timer.extension.TIME_ELAPSED_EXT);
  } catch {
    timer.query = null;
    runtime.gpuTimerSupported = false;
    document.body.dataset.gpuTimerSupported = "false";
  }
}

function findNodeIndex(object) {
  let cursor = object;
  while (cursor && cursor !== sceneRoot?.parent) {
    const association = gltfResult?.parser?.associations?.get(cursor);
    if (association?.nodes !== undefined) return association.nodes;
    cursor = cursor.parent;
  }
  return null;
}

function nodeExtras(object) {
  const nodeIndex = findNodeIndex(object);
  return nodeIndex === null ? null : gltfResult?.parser?.json?.nodes?.[nodeIndex]?.extras ?? null;
}

function cloneWithPolygonOffset(material, target) {
  if (!material) return material;
  const clone = material.clone();
  clone.polygonOffset = true;
  clone.polygonOffsetFactor = target.polygon_offset_factor;
  clone.polygonOffsetUnits = target.polygon_offset_units;
  clone.userData = {
    ...clone.userData,
    zhhzCoplanarLayerRank: target.layer_rank,
    zhhzCoplanarTarget: target.node_index === undefined
      ? "batched"
      : `${target.node_index}:${target.primitive_index}`,
  };
  clone.needsUpdate = true;
  return clone;
}

function applyCoplanarSurfaceFixes() {
  const targetByKey = new Map((coplanarFixManifest?.targets ?? []).map((target) => [
    `${target.node_index}:${target.primitive_index}`,
    target,
  ]));
  let applied = 0;
  let authoredTargets = 0;
  sceneRoot.traverse((object) => {
    if (!object.isMesh) return;
    const extras = nodeExtras(object);
    const authoredOffset = object.userData?.zhhz_coplanar
      ?? object.userData?.extras?.zhhz_coplanar
      ?? object.userData?.gltfExtensions?.zhhz_coplanar
      ?? extras?.zhhz_coplanar
      ?? null;
    const nodeIndex = findNodeIndex(object);
    if (nodeIndex === null && !authoredOffset) return;
    const association = gltfResult.parser.associations.get(object);
    const primitiveIndex = association?.primitives ?? association?.primitive ?? 0;
    const target = authoredOffset ?? targetByKey.get(`${nodeIndex}:${primitiveIndex}`);
    if (!target) return;
    object.material = Array.isArray(object.material)
      ? object.material.map((material) => cloneWithPolygonOffset(material, target))
      : cloneWithPolygonOffset(object.material, target);
    object.renderOrder = Math.max(object.renderOrder, target.layer_rank ?? 1);
    object.userData.zhhzCoplanarTarget = target;
    applied += 1;
    if (authoredOffset) authoredTargets += 1;
  });
  runtime.coplanarConflictPairs = coplanarFixManifest.conflict_pair_count;
  runtime.coplanarTargets = authoredTargets > 0 ? authoredTargets : coplanarFixManifest.offset_target_count;
  runtime.coplanarTargetsApplied = applied;
  document.body.dataset.coplanarPairs = String(runtime.coplanarConflictPairs);
  document.body.dataset.coplanarTargets = String(runtime.coplanarTargets);
  document.body.dataset.coplanarApplied = String(applied);
  if (applied !== runtime.coplanarTargets) {
    runtime.warnings.push(`共面修复命中 ${applied}/${runtime.coplanarTargets}`);
  }
}

function createDummyTexture(channel) {
  if (dummyTextures.has(channel)) return dummyTextures.get(channel);
  const texture = new THREE.DataTexture(new Uint8Array([0, 0, 0, 255]), 1, 1, THREE.RGBAFormat);
  texture.colorSpace = THREE.NoColorSpace;
  texture.flipY = false;
  texture.channel = channel;
  texture.needsUpdate = true;
  dummyTextures.set(channel, texture);
  return texture;
}

function patchLightmapMaterial(source, coordinateIndex, perVertex = false) {
  if (!source?.isMeshStandardMaterial && !source?.isMeshPhysicalMaterial) return source;
  let byChannel = patchedMaterialCache.get(source);
  if (!byChannel) {
    byChannel = new Map();
    patchedMaterialCache.set(source, byChannel);
  }
  const patchKey = `${coordinateIndex}:${perVertex ? "vertex" : "uniform"}`;
  if (byChannel.has(patchKey)) return byChannel.get(patchKey);

  const material = source.clone();
  const previousCompile = source.onBeforeCompile;
  const previousKey = source.customProgramCacheKey?.bind(source);
  material.lightMap = createDummyTexture(coordinateIndex);
  material.lightMapIntensity = 0;
  material.userData.zhhzLightmapChannel = coordinateIndex;
  material.userData.zhhzLightmapPerVertex = perVertex;
  material.onBeforeCompile = (shader, renderContext) => {
    if (previousCompile) previousCompile(shader, renderContext);
    shader.uniforms.zhhzLayer0 = { value: createDummyTexture(coordinateIndex) };
    shader.uniforms.zhhzLayer1 = { value: createDummyTexture(coordinateIndex) };
    if (!perVertex) {
      shader.uniforms.zhhzCoordinateScaleBias = { value: new THREE.Vector4(1, 1, 0, 0) };
      shader.uniforms.zhhzScale0 = { value: new THREE.Vector4() };
      shader.uniforms.zhhzAdd0 = { value: new THREE.Vector4() };
      shader.uniforms.zhhzScale1 = { value: new THREE.Vector4() };
      shader.uniforms.zhhzAdd1 = { value: new THREE.Vector4() };
    }
    shader.uniforms.zhhzHasLightmap = { value: 0 };
    shader.uniforms.zhhzLightmapStrength = { value: 1 };
    if (perVertex) {
      shader.vertexShader = `attribute vec4 _zhhz_coord;
attribute vec4 _zhhz_scale0;
attribute vec4 _zhhz_add0;
attribute vec4 _zhhz_scale1;
attribute vec4 _zhhz_add1;
varying vec4 vZhhzCoordinateScaleBias;
varying vec4 vZhhzScale0;
varying vec4 vZhhzAdd0;
varying vec4 vZhhzScale1;
varying vec4 vZhhzAdd1;
${shader.vertexShader}`;
      shader.vertexShader = shader.vertexShader.replace(
        "#include <begin_vertex>",
        `vZhhzCoordinateScaleBias = _zhhz_coord;
vZhhzScale0 = _zhhz_scale0;
vZhhzAdd0 = _zhhz_add0;
vZhhzScale1 = _zhhz_scale1;
vZhhzAdd1 = _zhhz_add1;
#include <begin_vertex>`,
      );
    }
    const parameterBlock = perVertex
      ? `varying vec4 vZhhzCoordinateScaleBias;
varying vec4 vZhhzScale0;
varying vec4 vZhhzAdd0;
varying vec4 vZhhzScale1;
varying vec4 vZhhzAdd1;`
      : `uniform vec4 zhhzCoordinateScaleBias;
uniform vec4 zhhzScale0;
uniform vec4 zhhzAdd0;
uniform vec4 zhhzScale1;
uniform vec4 zhhzAdd1;`;
    const coord = perVertex ? "vZhhzCoordinateScaleBias" : "zhhzCoordinateScaleBias";
    const scale0 = perVertex ? "vZhhzScale0" : "zhhzScale0";
    const add0 = perVertex ? "vZhhzAdd0" : "zhhzAdd0";
    const scale1 = perVertex ? "vZhhzScale1" : "zhhzScale1";
    const add1 = perVertex ? "vZhhzAdd1" : "zhhzAdd1";
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <lightmap_pars_fragment>",
      `#include <lightmap_pars_fragment>
uniform sampler2D zhhzLayer0;
uniform sampler2D zhhzLayer1;
${parameterBlock}
uniform float zhhzHasLightmap;
uniform float zhhzLightmapStrength;`,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <lights_fragment_end>",
      `#include <lights_fragment_end>
#ifdef USE_LIGHTMAP
  if (zhhzHasLightmap > 0.5) {
    vec2 zhhzUv = vLightMapUv * ${coord}.xy + ${coord}.zw;
    vec4 zhhzLm0 = texture2D(zhhzLayer0, zhhzUv);
    vec4 zhhzLm1 = texture2D(zhhzLayer1, zhhzUv);
    float zhhzLogL = zhhzLm0.a * 255.0 + zhhzLm1.a - 0.5;
    zhhzLogL = zhhzLogL * ${scale0}.w + ${add0}.w;
    vec3 zhhzUVW = zhhzLm0.rgb * zhhzLm0.rgb * ${scale0}.rgb + ${add0}.rgb;
    float zhhzLuminance = max(exp2(zhhzLogL) - 0.01858136, 0.0);
    vec4 zhhzSH = (zhhzLm1 * 255.0) * ${scale1} + ${add1};
    vec3 zhhzWorldNormal = inverseTransformDirection(normal, viewMatrix);
    float zhhzDirectionality = max(0.0, dot(zhhzSH, vec4(zhhzWorldNormal.z, zhhzWorldNormal.y, zhhzWorldNormal.x, 1.0)));
    vec3 zhhzBakedDiffuse = zhhzLuminance * zhhzDirectionality * zhhzUVW;
    reflectedLight.indirectDiffuse += zhhzBakedDiffuse * diffuseColor.rgb * zhhzLightmapStrength;
  } else {
    reflectedLight.indirectDiffuse += diffuseColor.rgb * 0.12;
  }
#endif`,
    );
    material.userData.zhhzShader = shader;
  };
  material.customProgramCacheKey = () => `${previousKey ? previousKey() : ""}|zhhz-hq-vt-lightmap-v2-${perVertex ? "vertex" : "uniform"}-ch${coordinateIndex}`;
  material.needsUpdate = true;
  byChannel.set(patchKey, material);
  return material;
}

function updateMeshLightmapUniforms(mesh) {
  const material = mesh.material;
  const binding = mesh.userData.zhhzLightmapBinding;
  const shader = material?.userData?.zhhzShader;
  if (!binding || !shader) return;
  const pair = texturePairs.get(binding.texture_key);
  const pairKey = `${binding.texture_key}|${pair?.layer0?.uuid ?? "missing"}`;
  // In full mode the atlas pair is immutable. Avoid rewriting the same
  // sampler and decode constants on every draw; this keeps the baked result
  // identical while removing per-frame CPU work from hundreds of callbacks.
  if (mesh.userData.zhhzLightmapUniformKey === pairKey) return;
  shader.uniforms.zhhzHasLightmap.value = pair ? 1 : 0;
  if (pair) {
    shader.uniforms.zhhzLayer0.value = pair.layer0;
    shader.uniforms.zhhzLayer1.value = pair.layer1;
  }
  if (!mesh.userData.zhhzLightmapPerVertex) {
    shader.uniforms.zhhzCoordinateScaleBias.value.fromArray(binding.coordinate_scale_bias);
    shader.uniforms.zhhzScale0.value.fromArray(binding.lightmap_scale_0);
    shader.uniforms.zhhzAdd0.value.fromArray(binding.lightmap_add_0);
    shader.uniforms.zhhzScale1.value.fromArray(binding.lightmap_scale_1);
    shader.uniforms.zhhzAdd1.value.fromArray(binding.lightmap_add_1);
  }
  mesh.userData.zhhzLightmapUniformKey = pairKey;
}

let fullLightmapCallbacksFrozen = false;
function freezeFullLightmapUniforms() {
  if (LIGHTMAP_MODE !== "full" || fullLightmapCallbacksFrozen || !sceneRoot) return;
  let pending = 0;
  sceneRoot.traverse((object) => {
    if (!object.isMesh || !object.userData?.zhhzLightmapBinding) return;
    const materials = Array.isArray(object.material) ? object.material : [object.material];
    if (materials.some((material) => !material?.userData?.zhhzShader)) {
      pending += 1;
      return;
    }
    updateMeshLightmapUniforms(object);
    // Three.js calls this hook unconditionally for renderable objects; keep a
    // callable no-op after the one-time lightmap upload instead of assigning
    // null (which would abort the render loop).
    object.onBeforeRender = NOOP_RENDER_CALLBACK;
  });
  if (pending === 0) {
    fullLightmapCallbacksFrozen = true;
    document.body.dataset.lightmapCallbacksFrozen = "true";
  }
}

function bindLightmapsToScene() {
  const bindingByLabel = new Map(lightmapManifest.bindings.map((binding) => [binding.actor_label, binding]));
  const matched = new Set();
  let matchedBatchBindings = 0;
  let batchCandidates = 0;
  let batchWithAttributes = 0;
  let batchSampleAttributes = "";
  let boundMeshes = 0;
  let unsupportedMaterials = 0;

  sceneRoot.traverse((object) => {
    if (!object.isMesh) return;
    const extras = nodeExtras(object);
    const batchBinding = object.userData?.zhhz_lightmap
      ?? object.userData?.extras?.zhhz_lightmap
      ?? object.userData?.gltfExtensions?.zhhz_lightmap
      ?? extras?.zhhz_lightmap
      ?? null;
    if (batchBinding) batchCandidates += 1;
    if (batchBinding && !batchSampleAttributes) batchSampleAttributes = Object.keys(object.geometry.attributes).join(",");
    const nodeIndex = findNodeIndex(object);
    const nodeName = nodeIndex === null ? null : gltfResult.parser.json.nodes[nodeIndex]?.name;
    const binding = batchBinding
      ? { texture_key: batchBinding.texture_key }
      : bindingByLabel.get(nodeName);
    if (!binding) return;
    const perVertex = Boolean(batchBinding);
    const coordinateIndex = Number(batchBinding?.coordinate_index ?? binding.lightmap_coordinate_index ?? 1);
    const attributeName = coordinateIndex === 0 ? "uv" : `uv${coordinateIndex}`;
    if (!object.geometry.getAttribute(attributeName)) return;
    if (perVertex && !object.geometry.getAttribute("_zhhz_coord")) return;
    if (perVertex) batchWithAttributes += 1;

    if (Array.isArray(object.material)) {
      object.material = object.material.map((material) => patchLightmapMaterial(material, coordinateIndex, perVertex));
      unsupportedMaterials += object.material.filter((material) => !material?.userData?.zhhzLightmapChannel).length;
    } else {
      const patched = patchLightmapMaterial(object.material, coordinateIndex, perVertex);
      if (patched === object.material && !patched?.userData?.zhhzLightmapChannel) unsupportedMaterials += 1;
      object.material = patched;
    }
    object.userData.zhhzLightmapBinding = binding;
    object.userData.zhhzLightmapPerVertex = perVertex;
    object.onBeforeRender = () => {
      if (Array.isArray(object.material)) {
        for (const material of object.material) {
          if (material?.userData?.zhhzShader) updateMeshLightmapUniforms({ material, userData: object.userData });
        }
      } else {
        updateMeshLightmapUniforms(object);
      }
    };
    if (perVertex) matchedBatchBindings += Number(batchBinding.source_binding_count ?? 1);
    else matched.add(binding.stable_component_key);
    boundMeshes += 1;
  });

  runtime.matchedBindings = Math.min(lightmapManifest.binding_count, matched.size + matchedBatchBindings);
  runtime.boundMeshes = boundMeshes;
  runtime.warnings.push(...(unsupportedMaterials ? [`${unsupportedMaterials} 个非 PBR 材质未接入 Lightmap shader`] : []));
  const totalMatched = runtime.matchedBindings;
  document.body.dataset.lightmapBatchCandidates = String(batchCandidates);
  document.body.dataset.lightmapBatchWithAttributes = String(batchWithAttributes);
  document.body.dataset.lightmapBatchSampleAttributes = batchSampleAttributes;
  elements.bindings.textContent = `${totalMatched.toLocaleString()} / ${lightmapManifest.binding_count.toLocaleString()}`;
  if (totalMatched !== lightmapManifest.binding_count) {
    runtime.warnings.push(`Lightmap 绑定未全量命中：${totalMatched}/${lightmapManifest.binding_count}`);
  }
}

function staticGeometryCompatibilityKey(geometry) {
  const attributes = Object.entries(geometry.attributes ?? {})
    .sort(([left], [right]) => left.localeCompare(right))
    .map(([name, attribute]) => `${name}:${attribute.itemSize}:${attribute.array?.constructor?.name ?? "array"}:${attribute.normalized ? 1 : 0}`)
    .join(",");
  return `${attributes}|index:${geometry.index ? "indexed" : "none"}`;
}

function staticTextureCompatibilityKey(texture) {
  if (!texture) return "none";
  const imageKey = texture.name || texture.image?.src || texture.image?.currentSrc || "image";
  const offset = texture.offset ? `${texture.offset.x},${texture.offset.y}` : "0,0";
  const repeat = texture.repeat ? `${texture.repeat.x},${texture.repeat.y}` : "1,1";
  return [imageKey, texture.channel ?? 0, texture.colorSpace, texture.wrapS, texture.wrapT, offset, repeat, texture.rotation ?? 0].join(":");
}

function staticMaterialCompatibilityKey(material) {
  const color = material.color?.getHex?.() ?? "";
  const emissive = material.emissive?.getHex?.() ?? "";
  return [
    material.type,
    color,
    emissive,
    material.opacity,
    material.transparent ? 1 : 0,
    material.alphaTest,
    material.depthWrite ? 1 : 0,
    material.depthTest ? 1 : 0,
    material.side,
    material.vertexColors ? 1 : 0,
    material.flatShading ? 1 : 0,
    material.fog ? 1 : 0,
    material.toneMapped ? 1 : 0,
    material.blending,
    material.premultipliedAlpha ? 1 : 0,
    material.metalness,
    material.roughness,
    material.emissiveIntensity,
    material.polygonOffset ? 1 : 0,
    material.polygonOffsetFactor,
    material.polygonOffsetUnits,
    staticTextureCompatibilityKey(material.map),
    staticTextureCompatibilityKey(material.normalMap),
    staticTextureCompatibilityKey(material.roughnessMap),
    staticTextureCompatibilityKey(material.metalnessMap),
    staticTextureCompatibilityKey(material.aoMap),
    staticTextureCompatibilityKey(material.emissiveMap),
    material.userData?.zhhzTwinCoplanarLayerRank ?? "",
  ].join("|");
}

function mergeStaticArchitectureBatches() {
  const stats = { enabled: STATIC_BATCH_ENABLED, source: 0, render: 0, reduced: 0, groups: 0, skippedLarge: 0 };
  document.body.dataset.staticBatchEnabled = String(STATIC_BATCH_ENABLED);
  if (!STATIC_BATCH_ENABLED || !sceneRoot) return stats;

  sceneRoot.updateMatrixWorld(true);
  const inverseRoot = new THREE.Matrix4().copy(sceneRoot.matrixWorld).invert();
  const groups = new Map();
  const sources = [];
  const centerWorld = new THREE.Vector3();
  const centerLocal = new THREE.Vector3();
  const worldScale = new THREE.Vector3();
  const sphere = new THREE.Sphere();

  sceneRoot.traverse((object) => {
    // Uniform lightmap bindings cannot be mixed: their scale/bias is per mesh.
    // Per-vertex bindings carry all authored decode data in the geometry, so
    // they are safe to merge as long as the atlas and material stay identical.
    if (!object.isMesh || !object.geometry || Array.isArray(object.material)
      || !object.userData?.zhhzLightmapPerVertex || !object.userData?.zhhzLightmapBinding
      || !object.visible) return;
    stats.source += 1;
    if (!object.geometry.boundingSphere) object.geometry.computeBoundingSphere();
    sphere.copy(object.geometry.boundingSphere);
    centerWorld.copy(sphere.center).applyMatrix4(object.matrixWorld);
    centerLocal.copy(centerWorld).applyMatrix4(inverseRoot);
    object.getWorldScale(worldScale);
    const worldRadius = sphere.radius * Math.max(Math.abs(worldScale.x), Math.abs(worldScale.y), Math.abs(worldScale.z));
    if (worldRadius > STATIC_BATCH_MAX_RADIUS) {
      stats.skippedLarge += 1;
      return;
    }
    const cellX = Math.floor(centerLocal.x / STATIC_BATCH_CELL_SIZE);
    const cellZ = Math.floor(centerLocal.z / STATIC_BATCH_CELL_SIZE);
    const binding = object.userData.zhhzLightmapBinding;
    const coplanarRank = object.material.userData?.zhhzTwinCoplanarLayerRank ?? "";
    const key = [
      `${cellX}:${cellZ}`,
      staticMaterialCompatibilityKey(object.material),
      binding.texture_key,
      object.renderOrder,
      object.castShadow ? 1 : 0,
      object.receiveShadow ? 1 : 0,
      coplanarRank,
      staticGeometryCompatibilityKey(object.geometry),
    ].join("|");
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(object);
    sources.push(object);
  });

  const batchRoot = new THREE.Group();
  batchRoot.name = `ZHHZ_StaticRenderBatch_${STATIC_BATCH_CELL_SIZE}m`;
  batchRoot.userData.zhhzStaticRenderBatch = true;
  let batchIndex = 0;
  for (const group of groups.values()) {
    if (group.length < 2) continue;
    const geometries = group.map((object) => {
      const geometry = object.geometry.clone();
      geometry.applyMatrix4(inverseRoot.clone().multiply(object.matrixWorld));
      return geometry;
    });
    let mergedGeometry = null;
    try {
      mergedGeometry = BufferGeometryUtils.mergeGeometries(geometries, false);
    } catch (error) {
      runtime.warnings.push(`静态建筑合批跳过：${error.message}`);
    }
    geometries.forEach((geometry) => geometry.dispose());
    if (!mergedGeometry) continue;
    mergedGeometry.computeBoundingBox();
    mergedGeometry.computeBoundingSphere();
    const first = group[0];
    const mergedMesh = new THREE.Mesh(mergedGeometry, first.material);
    mergedMesh.name = `${batchRoot.name}_${batchIndex++}`;
    mergedMesh.renderOrder = first.renderOrder;
    mergedMesh.castShadow = first.castShadow;
    mergedMesh.receiveShadow = first.receiveShadow;
    mergedMesh.frustumCulled = true;
    mergedMesh.userData.zhhzLightmapBinding = first.userData.zhhzLightmapBinding;
    mergedMesh.userData.zhhzLightmapPerVertex = true;
    mergedMesh.userData.zhhzStaticMergedSourceCount = group.length;
    // The full mode keeps every lightmap pair resident, so one update at the
    // merged mesh is sufficient and preserves the original shader/material.
    mergedMesh.onBeforeRender = () => updateMeshLightmapUniforms(mergedMesh);
    batchRoot.add(mergedMesh);
    group.forEach((object) => {
      object.parent?.remove(object);
      object.geometry.dispose();
    });
    stats.reduced += group.length - 1;
  }
  stats.groups = batchIndex;
  stats.render = stats.source - stats.reduced;
  if (batchIndex > 0) sceneRoot.add(batchRoot);
  document.body.dataset.staticBatchSourceMeshes = String(stats.source);
  document.body.dataset.staticBatchRenderMeshes = String(stats.render);
  document.body.dataset.staticBatchDrawsReduced = String(stats.reduced);
  document.body.dataset.staticBatchGroups = String(stats.groups);
  document.body.dataset.staticBatchCellSize = String(STATIC_BATCH_CELL_SIZE);
  return stats;
}

function indexLightmapSpatialData() {
  textureRecords = new Map(lightmapManifest.textures.map((record) => [record.texture_key, record]));
  textureSpatialPoints = new Map();
  for (const binding of lightmapManifest.bindings) {
    const [x, y, z] = binding.bounds_origin_cm;
    const point = new THREE.Vector3(x * 0.01, z * 0.01, y * 0.01);
    if (!textureSpatialPoints.has(binding.texture_key)) textureSpatialPoints.set(binding.texture_key, []);
    textureSpatialPoints.get(binding.texture_key).push(point);
  }
}

function configureLightmapTexture(texture) {
  texture.colorSpace = THREE.NoColorSpace;
  texture.flipY = false;
  texture.wrapS = THREE.ClampToEdgeWrapping;
  texture.wrapT = THREE.ClampToEdgeWrapping;
  // The exported PNG is already the authored 1024px lightmap tile. Do not
  // allocate another full mip chain for 378 layers: it adds ~33% GPU memory
  // without adding baked detail and is the main source of GTX970-class thrash.
  texture.generateMipmaps = false;
  texture.minFilter = THREE.LinearFilter;
  texture.magFilter = THREE.LinearFilter;
  texture.anisotropy = Math.min(2, renderer.capabilities.getMaxAnisotropy());
  texture.needsUpdate = true;
  return texture;
}

async function loadTextureRecord(record) {
  if (texturePairs.has(record.texture_key)) return;
  const layer0File = record.files.find((file) => file.layer_index === 0);
  const layer1File = record.files.find((file) => file.layer_index === 1);
  if (!layer0File || !layer1File) throw new Error(`${record.texture_key} 缺少 HQ 双层贴图`);
  const [layer0, layer1] = await Promise.all([
    textureLoader.loadAsync(`${ASSET_ROOT}${layer0File.relative_path}`),
    textureLoader.loadAsync(`${ASSET_ROOT}${layer1File.relative_path}`),
  ]);
  texturePairs.set(record.texture_key, {
    layer0: configureLightmapTexture(layer0),
    layer1: configureLightmapTexture(layer1),
  });
  runtime.residentLightmapAtlases = texturePairs.size;
  elements.lightmaps.textContent = `${texturePairs.size} / ${lightmapManifest.unique_texture_count}`;
}

function nearestTextureKeys(position, budget) {
  return [...textureRecords.keys()]
    .map((key) => ({
      key,
      distance: Math.min(...(textureSpatialPoints.get(key) ?? []).map((point) => point.distanceToSquared(position))),
    }))
    .sort((left, right) => left.distance - right.distance)
    .slice(0, Math.min(budget, textureRecords.size))
    .map((item) => item.key);
}

function disposeLightmapPair(key) {
  const pair = texturePairs.get(key);
  if (!pair) return;
  pair.layer0.dispose();
  pair.layer1.dispose();
  texturePairs.delete(key);
}

async function refreshLightmapResidency(position = camera.position, force = false) {
  if (!lightmapManifest || lightmapRefreshPromise) return lightmapRefreshPromise;
  const now = performance.now();
  if (!force
    && now - lastLightmapRefreshAt < LIGHTMAP_REFRESH_INTERVAL_MS
    && position.distanceTo(lastLightmapRefreshPosition) < LIGHTMAP_MOVE_THRESHOLD_METERS) return null;

  lastLightmapRefreshAt = now;
  lastLightmapRefreshPosition.copy(position);
  desiredLightmapKeys = new Set(nearestTextureKeys(position, runtime.requestedLightmapBudget));
  const missing = [...desiredLightmapKeys].filter((key) => !texturePairs.has(key) && !pendingLightmapKeys.has(key));
  elements.moreLightmaps.disabled = true;
  lightmapRefreshPromise = (async () => {
    let cursor = 0;
    const widestLightmap = Math.max(0, ...missing.map((key) => Number(textureRecords.get(key)?.files?.[0]?.width ?? 0)));
    // Lossless 4096 superatlases contain the same pixels as sixteen 1024
    // tiles. Decode them serially to avoid a half-gigabyte transient burst on
    // GTX970/X300-class systems; steady-state GPU content is unchanged.
    const lightmapWorkers = widestLightmap >= 4096 ? 1 : 4;
    document.body.dataset.lightmapLoadWorkers = String(lightmapWorkers);
    const workers = Array.from({ length: Math.min(lightmapWorkers, missing.length) }, async () => {
      while (cursor < missing.length) {
        const key = missing[cursor];
        cursor += 1;
        pendingLightmapKeys.add(key);
        try {
          await loadTextureRecord(textureRecords.get(key));
        } finally {
          pendingLightmapKeys.delete(key);
        }
      }
    });
    await Promise.all(workers);
    // In full mode keep every uploaded atlas resident. Evicting an atlas while
    // a mesh is visible makes the shader fall back to a dark constant and is
    // the source of the abrupt per-block lighting changes seen in QA.
    if (LIGHTMAP_MODE !== "full") {
      for (const key of [...texturePairs.keys()]) {
        if (!desiredLightmapKeys.has(key)) disposeLightmapPair(key);
      }
    }
    runtime.residentLightmapAtlases = texturePairs.size;
    elements.lightmaps.textContent = `${texturePairs.size} / ${lightmapManifest.unique_texture_count}${LIGHTMAP_MODE === "full" ? "（全量）" : "（自动）"}`;
    document.body.dataset.lightmapsResident = String(texturePairs.size);
    document.body.dataset.lightmapsBudget = String(runtime.requestedLightmapBudget);
    setStatus(LIGHTMAP_MODE === "full"
      ? `场景已运行；全量 ${texturePairs.size} 个烘焙 Lightmap atlas 已驻留`
      : `场景已运行；相机附近 ${texturePairs.size} 个 Lightmap atlas 自动驻留`, 100);
  })().finally(() => {
    lightmapRefreshPromise = null;
    elements.moreLightmaps.disabled = false;
  });
  return lightmapRefreshPromise;
}

function uePointToThree([x, y, z]) {
  return new THREE.Vector3(x * 0.01, z * 0.01, y * 0.01);
}

async function loadReflectionCapture(capture) {
  const response = await fetch(`${ASSET_ROOT}${capture.relative_path}`, { cache: "no-store" });
  if (!response.ok) throw new Error(`反射捕获读取失败：HTTP ${response.status}`);
  const buffer = await response.arrayBuffer();
  const firstMip = capture.mips[0];
  const images = firstMip.faces.map((face) => {
    const texture = new THREE.DataTexture(
      new Uint16Array(buffer, face.byte_offset, face.byte_length / 2),
      firstMip.size,
      firstMip.size,
      THREE.RGBAFormat,
      THREE.HalfFloatType,
    );
    texture.colorSpace = THREE.NoColorSpace;
    texture.flipY = false;
    texture.needsUpdate = true;
    return texture;
  });
  const cube = new THREE.CubeTexture(images);
  cube.name = capture.actor_label;
  cube.type = THREE.HalfFloatType;
  cube.format = THREE.RGBAFormat;
  cube.colorSpace = THREE.NoColorSpace;
  cube.generateMipmaps = true;
  cube.minFilter = THREE.LinearMipmapLinearFilter;
  cube.magFilter = THREE.LinearFilter;
  cube.needsUpdate = true;
  return {
    ...capture,
    position: uePointToThree(capture.capture_position_cm),
    radiusMeters: (capture.influence_radius_cm ?? Infinity) * 0.01,
    cube,
  };
}

function updateNearestReflection(position = camera.position, force = false) {
  if (!reflectionCaptures.length) return;
  // Keep the initial authored probe for continuous camera motion. A hard
  // environment swap at a volume boundary is visible as a sudden exposure
  // jump; preset changes may still request an explicit forced update.
  if (!force && activeReflectionCapture) return;
  const capture = [...reflectionCaptures].sort((left, right) => {
    const leftDistance = left.position.distanceTo(position) / Math.max(left.radiusMeters, 1);
    const rightDistance = right.position.distanceTo(position) / Math.max(right.radiusMeters, 1);
    return leftDistance - rightDistance;
  })[0];
  if (!force && capture === activeReflectionCapture) return;
  activeReflectionCapture = capture;
  scene.environment = capture.cube;
  scene.environmentIntensity = capture.brightness ?? 1;
  runtime.reflectionCapture = capture.stable_capture_key;
  runtime.reflectionSwitchCount += 1;
  elements.reflection.textContent = `${capture.actor_label}（18 探针就近切换）`;
  document.body.dataset.reflectionActive = capture.actor_label;
}

async function loadAllReflectionCaptures(manifest) {
  let cursor = 0;
  const captures = manifest.captures ?? [];
  const results = new Array(captures.length);
  const workers = Array.from({ length: Math.min(3, captures.length) }, async () => {
    while (cursor < captures.length) {
      const index = cursor;
      cursor += 1;
      results[index] = await loadReflectionCapture(captures[index]);
    }
  });
  await Promise.all(workers);
  reflectionCaptures = results.filter(Boolean);
  runtime.reflectionCaptureCount = reflectionCaptures.length;
  document.body.dataset.reflectionCaptures = String(reflectionCaptures.length);
  updateNearestReflection(startupPosition, true);
}

async function loadHdriBackground() {
  try {
    const texture = await new HDRLoader().loadAsync(HDRI_URL);
    texture.mapping = THREE.EquirectangularReflectionMapping;
    texture.colorSpace = THREE.LinearSRGBColorSpace;
    scene.background = texture;
    scene.backgroundIntensity = 0.4;
    if (scene.backgroundRotation) scene.backgroundRotation.y = THREE.MathUtils.degToRad(-90);
    runtime.hdriLoaded = true;
  } catch (error) {
    runtime.warnings.push(`HDRI 背景加载失败：${error.message}`);
    console.warn("[ZHHZ_NEW] HDRI background", error);
  }
}

function fitOverview() {
  if (!overviewBox || overviewBox.isEmpty()) return;
  const sphere = overviewBox.getBoundingSphere(new THREE.Sphere());
  const direction = new THREE.Vector3(1, 0.42, 1).normalize();
  const fov = THREE.MathUtils.degToRad(camera.fov);
  const distance = sphere.radius / Math.sin(fov / 2) * 1.12;
  camera.position.copy(sphere.center).addScaledVector(direction, distance);
  controls.target.copy(sphere.center);
  camera.near = Math.max(0.04, distance / 5000);
  camera.far = Math.max(4000, distance * 8);
  camera.updateProjectionMatrix();
  controls.update();
}

function focusSceneObject(name) {
  let targetObject = null;
  sceneRoot?.traverse((object) => {
    if (!targetObject && object.name === name) targetObject = object;
  });
  if (!targetObject) return false;
  const box = new THREE.Box3().setFromObject(targetObject);
  if (box.isEmpty()) return false;
  const sphere = box.getBoundingSphere(new THREE.Sphere());
  const radius = Math.max(sphere.radius, 0.25);
  controls.target.copy(sphere.center);
  camera.position.copy(sphere.center).add(new THREE.Vector3(radius * 1.1, radius * 0.55, radius * 1.7));
  camera.near = Math.max(0.01, radius / 500);
  camera.far = Math.max(100, radius * 100);
  camera.updateProjectionMatrix();
  controls.update();
  runtime.focusedObject = name;
  document.body.dataset.focusedObject = name;
  return true;
}
runtime.focusSceneObject = focusSceneObject;

async function loadScene() {
  runtime.phase = "loading-manifests";
  setStatus("读取 Lightmap 与反射清单…", 4);
  const [lightmapResponse, reflectionResponse, coplanarResponse] = await Promise.all([
    fetch(LIGHTMAP_MANIFEST_URL, { cache: "no-store" }),
    fetch(REFLECTION_MANIFEST_URL, { cache: "no-store" }),
    fetch(COPLANAR_FIX_MANIFEST_URL, { cache: "no-store" }),
  ]);
  if (!lightmapResponse.ok || !reflectionResponse.ok || !coplanarResponse.ok) {
    throw new Error(`静态真值清单不可用：Lightmap ${lightmapResponse.status} / Reflection ${reflectionResponse.status} / Coplanar ${coplanarResponse.status}`);
  }
  [lightmapManifest] = await Promise.all([lightmapResponse.json()]);
  const reflectionManifest = await reflectionResponse.json();
  coplanarFixManifest = await coplanarResponse.json();
  runtime.lightmapManifestLoaded = true;
  elements.bindings.textContent = `0 / ${lightmapManifest.binding_count.toLocaleString()}`;
  indexLightmapSpatialData();

  runtime.phase = "loading-geometry";
  setStatus("加载 421 MiB 建筑几何与材质…", 15);
  gltfResult = await loadArchitectureWithRetry((event) => {
    if (!event.lengthComputable) return;
    const fraction = event.loaded / event.total;
    setStatus(`加载建筑几何与材质 ${(fraction * 100).toFixed(0)}%`, 15 + fraction * 45);
  });
  sceneRoot = gltfResult.scene;
  scene.add(sceneRoot);
  sceneRoot.updateMatrixWorld(true);
  applyCoplanarSurfaceFixes();
  overviewBox = new THREE.Box3().setFromObject(sceneRoot);
  // Visual-correctness baseline: preserve authored GLTF visibility exactly.
  // Three.js frustum culling remains enabled, but no camera-dependent rule
  // changes an object's authored visibility.

  let meshCount = 0;
  sceneRoot.traverse((object) => { if (object.isMesh) meshCount += 1; });
  runtime.geometryLoaded = true;
  runtime.phase = "binding-lightmaps";
  elements.nodes.textContent = gltfResult.parser.json.nodes.length.toLocaleString();
  elements.meshes.textContent = meshCount.toLocaleString();
  bindLightmapsToScene();
  const staticBatchStats = mergeStaticArchitectureBatches();
  meshCount -= staticBatchStats.reduced;
  elements.meshes.textContent = meshCount.toLocaleString();
  setStatus(`建筑已显示，绑定烘焙光照 ${runtime.matchedBindings.toLocaleString()} 项`, 70);

  runtime.phase = "preparing-twin";

  // All 18 built captures stay available; the nearest influence volume follows the camera.
  await loadAllReflectionCaptures(reflectionManifest);
  loadHdriBackground();
  await refreshLightmapResidency(startupPosition, true);
  elements.moreLightmaps.disabled = false;
  if (LIGHTMAP_MODE === "full") {
    elements.moreLightmaps.disabled = true;
    elements.moreLightmaps.textContent = "全量高清光照已启用";
  }
  try {
  await startTwinRuntime({ scene, camera, controls, renderer, runtime, startupPosition });
  } catch (error) {
    runtime.warnings.push(`第六阶段 Twin 运行时启动失败：${error.message}`);
    console.error("[ZHHZ_NEW][Twin]", error);
  }
  runtime.phase = "running";
  const requestedCamera = new URLSearchParams(location.search).get("camera");
  if (requestedCamera) useCameraPreset(requestedCamera);
  const requestedFocus = new URLSearchParams(location.search).get("focus");
  if (requestedFocus) focusSceneObject(requestedFocus);
  // Do not reveal a partial Twin scene after a failed high-resolution load.
  // The production contract is all-high: keep the gate visible until every
  // chunk is resident, so a transient network failure cannot look like a
  // quality/culling defect.
  if (document.body.dataset.twinHighReady === "true") {
    elements.loading.classList.add("is-hidden");
  } else {
    elements.loading.classList.remove("is-hidden");
    elements.loadingDetail.textContent = document.body.dataset.twinLoadError
      ? `高清 Twin 加载失败：${document.body.dataset.twinLoadError}`
      : "高清 Twin 尚未全部就绪，请重试";
  }
}

elements.startupView.addEventListener("click", useStartupView);
elements.overview.addEventListener("click", fitOverview);
elements.moreLightmaps.addEventListener("click", () => {
  if (LIGHTMAP_MODE === "full") return;
  runtime.requestedLightmapBudget = runtime.requestedLightmapBudget === INITIAL_LIGHTMAP_BUDGET
    ? Math.min(HIGH_QUALITY_LIGHTMAP_BUDGET, lightmapManifest.unique_texture_count)
    : INITIAL_LIGHTMAP_BUDGET;
  elements.moreLightmaps.textContent = runtime.requestedLightmapBudget === INITIAL_LIGHTMAP_BUDGET
    ? "扩大高清光照范围"
    : "恢复节能光照范围";
  refreshLightmapResidency(camera.position, true);
});
elements.togglePanel.addEventListener("click", () => {
  elements.panel.hidden = true;
  elements.showPanel.hidden = false;
});
elements.showPanel.addEventListener("click", () => {
  elements.panel.hidden = false;
  elements.showPanel.hidden = true;
});

window.addEventListener("resize", () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight, false);
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1));
});

const frameTimes = [];
let lastFrame = performance.now();
let lastMetricsUpdateAt = 0;
function animate(now) {
  requestAnimationFrame(animate);
  pollGpuTimer();
  // OrbitControls owns only the free/global camera.  In first-person and
  // shoulder roaming the Twin runtime writes the camera transform every
  // frame; updating the disabled controls here would reapply its stale
  // spherical state on the next frame and pull the camera away from the
  // character.
  if (controls.enabled) controls.update();
  runtime.twin?.update(now);
  if (runtime.phase === "running" && now - lastLightmapRefreshAt >= LIGHTMAP_REFRESH_INTERVAL_MS) {
    refreshLightmapResidency(camera.position);
    updateNearestReflection(camera.position);
  }
  renderer.render(scene, camera);
  freezeFullLightmapUniforms();
  if (now - lastMetricsUpdateAt >= 500) {
    lastMetricsUpdateAt = now;
    updateRenderMetrics();
  }
  if (runtime.gpuTimerSupported && runtime.gpuTimer) {
    runtime.gpuTimer.frame += 1;
    if (runtime.gpuTimer.frame % 8 === 0 && !runtime.gpuTimer.query) beginGpuTimer();
    else if (runtime.gpuTimer.query) endGpuTimer();
  }
  frameTimes.push(now - lastFrame);
  lastFrame = now;
  if (frameTimes.length > 90) frameTimes.shift();
  if (frameTimes.length > 20 && Math.floor(now / 500) !== Math.floor((now - (frameTimes.at(-1) || 0)) / 500)) {
    const average = frameTimes.reduce((sum, value) => sum + value, 0) / frameTimes.length;
    const orderedFrameTimes = [...frameTimes].sort((left, right) => left - right);
    const p95 = orderedFrameTimes[Math.min(orderedFrameTimes.length - 1, Math.floor(orderedFrameTimes.length * 0.95))];
    const worst = orderedFrameTimes.at(-1) || average;
    const fps = 1000 / Math.max(average, 0.01);
    elements.fps.textContent = `${fps.toFixed(0)} FPS`;
    document.body.dataset.renderFps = fps.toFixed(2);
    document.body.dataset.frameTimeAvgMs = average.toFixed(3);
    document.body.dataset.frameTimeP95Ms = p95.toFixed(3);
    document.body.dataset.frameTimeWorstMs = worst.toFixed(3);
    elements.drawCalls.textContent = renderer.info.render.calls.toLocaleString();
  }
}
requestAnimationFrame(animate);

loadScene().catch(recordError);

if (new URLSearchParams(location.search).get("clean") === "1") {
  document.body.classList.add("clean-view");
}
if (new URLSearchParams(location.search).get("debug") === "1") {
  document.body.classList.add("debug-view");
}
