import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { TransformControls } from "three/addons/controls/TransformControls.js";
import * as BufferGeometryUtils from "three/addons/utils/BufferGeometryUtils.js";

const SNAPSHOT_CHANGES_URL = "/twin-api/api/v2/state/snapshot_changes";
const RUNTIME_CONFIG_URL = "/twin-api/api/v2/scene-interactions/runtime";
const BASELINE_URL = "/data/twin/twin-baseline.json";
const TWIN_CHUNK_MANIFEST_URL = "/data/twin-chunks/twin-chunks.json";
const TWIN_CHUNK_ROOT = "/data/twin-chunks/";
const TWIN_COPLANAR_ROOT = "/data/twin-coplanar-fixes/";
const TWIN_COPLANAR_SUMMARY_URL = `${TWIN_COPLANAR_ROOT}summary.json`;
const TWIN_QUERY = new URLSearchParams(location.search);
const TWIN_CHUNK_BUDGET = 1;
// The final runtime is always full resolution. A stale query string cannot
// silently switch the showroom back to an HLOD presentation.
const TWIN_DETAIL_MODE = "all";
// Keep full-high batches spatially local. A single material batch spanning an
// entire chunk gets a very large bounding sphere and defeats frustum culling.
// A 50 m cell is the measured sweet spot on the full-high scene: it removes
// about 80 resident Twin render meshes versus 20 m while retaining bounded
// culling volumes and the complete high-resolution geometry.
const requestedBatchCellSize = Number(TWIN_QUERY.get("batchCell"));
const TWIN_BATCH_CELL_SIZE = Number.isFinite(requestedBatchCellSize) && requestedBatchCellSize >= 4
  ? Math.min(requestedBatchCellSize, 50)
  : 50;
// Collision remains available by default.  `?collision=0` is a diagnostic
// switch used to isolate render performance without deleting collision data.
const COLLISION_ENABLED = TWIN_QUERY.get("collision") !== "0";
const TWIN_HIGH_LOAD_WORKERS = 2;
const TWIN_HIGH_LOAD_RETRIES = 3;
const TWIN_CHUNK_REFRESH_MS = 1000;
const POLL_INTERVAL_MS = 700;
const PROJECT_HEADERS = {
  "X-OntoTwin-UE-Project-Id": "ueproj_ZHHZ_NEW",
  "X-OntoTwin-UE-Project-Name": "ZHHZ_NEW",
};
// The UE project registers a few legacy Observer/Manny catalog entries in
// the backend, but this package deliberately contains only the six
// source-native RenderPeople exports.  Do not expose an option that would
// silently fall back to the old unanimated `roamer.gltf`: selecting it was
// the source of the visibly broken T-pose/arm behaviour in the preview.
const PACKAGED_CHARACTER_IDS = new Set([
  "character.renderpeople.sophia",
  "character.renderpeople.nathan",
  "character.renderpeople.manuel",
  "character.renderpeople.eric",
  "character.renderpeople.claudia",
  "character.renderpeople.carla",
]);
const PACKAGED_CHARACTER_ORDER = new Map([...PACKAGED_CHARACTER_IDS].map((id, index) => [id, index]));
const LOCAL_CHARACTER_CATALOG = [
  ["sophia", "白色无袖女士"],
  ["nathan", "浅蓝T恤男士"],
  ["manuel", "灰T恤青年"],
  ["eric", "西装马甲男士"],
  ["claudia", "白衬衫女士"],
  ["carla", "黑西装女士"],
].map(([name, display_name]) => ({
  id: `character.renderpeople.${name}`,
  display_name,
  default_skin_id: `skin.renderpeople.${name}.default`,
  skins: [{ id: `skin.renderpeople.${name}.default`, display_name: "默认外观" }],
}));
// The UE MainScreenVideoRuntime binds these two authored materials to two
// synchronized media players. Keep the same material-level contract in the
// browser; matching by material name also survives Twin chunk batching.
const SCREEN_VIDEO_MATERIALS = new Map([
  ["MI_OP_LED屏_吊装长方形主屏幕", "horizontal"],
  ["advcdsvdfbdfvdsvsd", "vertical"],
  // Some exports preserve the UE material names instead of the generated
  // Twin label. These aliases point to the same two physical displays.
  ["MI_MainScreen_H", "horizontal"],
  ["MI_MainScreen_V", "vertical"],
]);

const ui = {
  panel: document.querySelector("#twin-panel"),
  status: document.querySelector("#twin-status"),
  count: document.querySelector("#twin-count"),
  parts: document.querySelector("#twin-parts"),
  types: document.querySelector("#twin-types"),
  source: document.querySelector("#twin-source"),
  search: document.querySelector("#twin-search"),
  list: document.querySelector("#twin-list"),
  detail: document.querySelector("#twin-detail"),
  detailName: document.querySelector("#detail-name"),
  detailPath: document.querySelector("#detail-path"),
  detailId: document.querySelector("#detail-id"),
  detailType: document.querySelector("#detail-type"),
  detailState: document.querySelector("#detail-state"),
  detailAnimation: document.querySelector("#detail-animation"),
  detailClose: document.querySelector("#detail-close"),
  detailFocus: document.querySelector("#detail-focus"),
  detailEdit: document.querySelector("#detail-edit"),
  worldDetail: document.querySelector("#twin-world-detail"),
  worldDetailName: document.querySelector("#world-detail-name"),
  worldDetailType: document.querySelector("#world-detail-type"),
  worldDetailCopy: document.querySelector("#world-detail-copy"),
  worldDetailId: document.querySelector("#world-detail-id"),
  worldDetailPath: document.querySelector("#world-detail-path"),
  worldDetailClose: document.querySelector("#world-detail-close"),
  worldDetailFocus: document.querySelector("#world-detail-focus"),
  worldDetailEditor: document.querySelector("#world-detail-editor"),
  minimap: document.querySelector("#minimap"),
  runtimeMinimap: document.querySelector("#runtime-minimap"),
  runtimeMinimapPanel: document.querySelector("#ue-minimap"),
  runtimeMinimapToggle: document.querySelector("#ue-minimap-toggle"),
  runtimeMinimapLocate: document.querySelector("#runtime-minimap-locate"),
  runtimeMinimapFollow: document.querySelector("#runtime-minimap-follow"),
  runtimeMinimapUndo: document.querySelector("#runtime-minimap-undo"),
  runtimeMinimapStatus: document.querySelector("#runtime-minimap-status"),
  runtimeDockHandle: document.querySelector("#ue-dock-handle"),
  runtimeSceneEdit: document.querySelector("#runtime-scene-edit"),
  runtimeEditor: document.querySelector("#runtime-editor"),
  runtimeEditorState: document.querySelector("#runtime-editor-state"),
  runtimeEditorUndo: document.querySelector("#runtime-editor-undo"),
  runtimeEditorRedo: document.querySelector("#runtime-editor-redo"),
  runtimeEditorClose: document.querySelector("#runtime-editor-close"),
  runtimeEditorTabs: [...document.querySelectorAll("[data-editor-tab]")],
  runtimeEditorPanels: [...document.querySelectorAll("[data-editor-panel]")],
  runtimeEditorName: document.querySelector("#runtime-editor-name"),
  runtimeEditorType: document.querySelector("#runtime-editor-type"),
  runtimeEditorId: document.querySelector("#runtime-editor-id"),
  runtimeEditorX: document.querySelector("#runtime-editor-x"),
  runtimeEditorY: document.querySelector("#runtime-editor-y"),
  runtimeEditorZ: document.querySelector("#runtime-editor-z"),
  runtimeEditorYaw: document.querySelector("#runtime-editor-yaw"),
  runtimeEditorRemove: document.querySelector("#runtime-editor-remove"),
  runtimeEditorTranslate: document.querySelector("#runtime-editor-translate"),
  runtimeEditorRotate: document.querySelector("#runtime-editor-rotate"),
  runtimeEditorWallSnap: document.querySelector("#runtime-editor-wall-snap"),
  runtimeEditorGridSnap: document.querySelector("#runtime-editor-grid-snap"),
  runtimeEditorPending: document.querySelector("#runtime-editor-pending"),
  runtimeEditorBusinessSelection: document.querySelector("#runtime-editor-business-selection"),
  runtimeEditorBusinessSelector: document.querySelector("#runtime-editor-business-selector"),
  runtimeEditorBusinessName: document.querySelector("#runtime-editor-business-name"),
  runtimeEditorBusinessCreate: document.querySelector("#runtime-editor-business-create"),
  runtimeEditorCancel: document.querySelector("#runtime-editor-cancel"),
  runtimeEditorSave: document.querySelector("#runtime-editor-save"),
  runtimeEditorStatus: document.querySelector("#runtime-editor-status"),
  runtimeCrosshair: document.querySelector("#runtime-crosshair"),
  runtimeStatusText: document.querySelector("#ue-roaming-status-text"),
  shortcuts: document.querySelector("#ue-shortcuts"),
  roamToggle: document.querySelector("#roam-toggle"),
  controlHelp: document.querySelector("#control-help"),
  runtimeControls: document.querySelector("#ue-runtime-controls"),
  characterSelector: document.querySelector("#runtime-character-selector"),
  routeSelector: document.querySelector("#runtime-route-selector"),
  routeState: document.querySelector("#runtime-route-state"),
  cycleSkin: document.querySelector("#runtime-cycle-skin"),
  resumeRoute: document.querySelector("#runtime-resume-route"),
  restartRoute: document.querySelector("#runtime-restart-route"),
  reloadCharacter: document.querySelector("#runtime-reload-character"),
  narration: document.querySelector("#ue-narration"),
  narrationText: document.querySelector("#ue-narration-text"),
  narrationSkip: document.querySelector("#ue-narration-skip"),
  cameraButtons: [...document.querySelectorAll("[data-camera-mode]")],
  runtimeTabs: [...document.querySelectorAll("[data-runtime-tab]")],
  runtimeTabPanels: [...document.querySelectorAll("[data-runtime-panel]")],
  spaceBreadcrumb: document.querySelector("#runtime-space-breadcrumb"),
  spaceColumns: document.querySelector("#runtime-space-columns"),
  enterSpace: document.querySelector("#runtime-enter-space"),
  scopeAll: document.querySelector("#runtime-scope-all"),
  scopeCurrent: document.querySelector("#runtime-scope-current"),
  businessScope: document.querySelector("#runtime-business-scope"),
  businessRows: document.querySelector("#runtime-business-rows"),
};

function spatialOf(snapshot) {
  return snapshot?.interfaces?.I3D_Spatial ?? snapshot?.raw_state ?? {};
}

function representableOf(snapshot) {
  return snapshot?.interfaces?.I3D_Representable ?? {};
}

function positionOf(snapshot) {
  const spatial = spatialOf(snapshot);
  return new THREE.Vector3(
    Number(spatial.translation_x ?? 0) * 0.01,
    Number(spatial.translation_z ?? 0) * 0.01,
    Number(spatial.translation_y ?? 0) * 0.01,
  );
}

function quaternionOf(snapshot) {
  const spatial = spatialOf(snapshot);
  const roll = THREE.MathUtils.degToRad(Number(spatial.rotation_x ?? 0));
  const pitch = THREE.MathUtils.degToRad(Number(spatial.rotation_y ?? 0));
  const yaw = THREE.MathUtils.degToRad(Number(spatial.rotation_z ?? 0));
  const qYaw = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), -yaw);
  const qPitch = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 0, 1), pitch);
  const qRoll = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1, 0, 0), -roll);
  return qYaw.multiply(qPitch).multiply(qRoll);
}

function scaleOf(snapshot) {
  const spatial = spatialOf(snapshot);
  return new THREE.Vector3(
    Number(spatial.scale_x ?? 1),
    Number(spatial.scale_z ?? 1),
    Number(spatial.scale_y ?? 1),
  );
}

// Twin high-resolution chunks are authored by batch_twin_chunks.mjs with
// each render-part's original UE world transform already baked into the
// vertex positions and every exported instance root reset to identity. Keep
// the authored snapshot transform separately so live deltas can be applied
// once to that world-space geometry without baking the absolute transform a
// second time.
function matrixOfSnapshot(snapshot) {
  return new THREE.Matrix4().compose(positionOf(snapshot), quaternionOf(snapshot), scaleOf(snapshot));
}

function totalRenderParts(snapshots) {
  return snapshots.reduce((sum, snapshot) => sum + (representableOf(snapshot).render_parts?.length ?? 0), 0);
}

function disposeObject(root) {
  root?.traverse((object) => {
    object.geometry?.dispose?.();
    if (Array.isArray(object.material)) object.material.forEach((material) => material?.dispose?.());
    else object.material?.dispose?.();
  });
}

export async function startTwinRuntime({ scene, camera, controls, renderer, runtime, startupPosition }) {
  const roamingDebug = new URLSearchParams(location.search).get("debug") === "1";
  // Capture the authored startup camera before the roaming runtime changes it.
  // UE's Home tab restores this exact pose and exits the roaming session; it
  // must not leave the user at the last first/third-person camera location.
  const startupCameraPosition = camera.position.clone();
  const startupCameraTarget = controls.target.clone();
  const startupCameraFov = camera.fov;
  const startupCameraNear = camera.near;
  const startupCameraFar = camera.far;
  const state = {
    snapshots: new Map(),
    visualById: new Map(),
    cursor: null,
    source: "loading",
    selectedId: null,
    selectedIds: new Set(),
    selectionBox: null,
    chunkManifest: null,
    chunkByInstanceId: new Map(),
    hlodManifest: null,
    hlodChunks: new Map(),
    loadedChunks: new Map(),
    pendingChunks: new Set(),
    desiredChunks: new Set(),
    chunkBatchTimers: new Map(),
    twinDetailMode: TWIN_DETAIL_MODE,
    chunkRefreshPromise: null,
    lastChunkRefreshAt: 0,
    lastChunkCameraPosition: new THREE.Vector3(Infinity, Infinity, Infinity),
    detailRecords: [],
    pollTimer: null,
    stopped: false,
    editMode: false,
    editDirty: false,
    runtimeEditorHistory: [],
    runtimeEditorRedo: [],
    runtimeEditorPending: new Map(),
    runtimeEditorTab: "scene",
    runtimeEditorWallSnap: false,
    runtimeEditorGridSnap: false,
    runtimeEditorGizmo: null,
    runtimeEditorGizmoHelper: null,
    runtimeEditorPivot: null,
    runtimeEditorGizmoMode: "translate",
    runtimeEditorDragBefore: null,
    runtimeEditorLastPivot: null,
    runtimeEditorLastYaw: 0,
    roaming: false,
    cameraMode: "first_person",
    availableCharacters: [],
    availableRoutes: [],
    runtimeRevision: null,
    activeCharacterId: null,
    activeSkinId: null,
    activeRouteId: null,
    routeWaypoints: [],
    routeWaypointIndex: 0,
    routePlaying: false,
    routePausedByUser: false,
    routeCompleted: false,
    routeNarrationActive: false,
    routeNarrationSegments: [],
    routeNarrationSegmentIndex: 0,
    routePauseAfterNarration: false,
    routeNarrationEnabled: true,
    crosshairEnabled: false,
    minimapFollow: true,
    minimapPreview: null,
    minimapHistory: [],
    minimapTransforms: new WeakMap(),
    cameraCollisionRaycaster: new THREE.Raycaster(),
    cameraCollisionMeshes: [],
    collisionEntries: [],
    collisionGrid: new Map(),
    collisionLargeEntries: [],
    collisionCandidateScratch: [],
    collisionCatalogAt: 0,
    collisionCatalogDirty: true,
    collisionGroundRaycaster: new THREE.Raycaster(),
    collisionSweepRaycaster: new THREE.Raycaster(),
    collisionLastGroundY: null,
    collisionLastProbeAt: 0,
    collisionCandidateCount: 0,
    collisionBlocked: false,
    collisionRadius: 0.30,
    collisionHeight: 1.76,
    collisionStepHeight: 0.42,
    collisionGroundProbeHeight: 3.0,
    collisionGroundProbeDistance: 5.0,
    collisionCellSize: 8,
    cameraCollisionCatalogAt: 0,
    cameraCollisionCheckedAt: 0,
    cameraCollisionDistance: Infinity,
    spacePath: [],
    businessScopeCurrent: false,
    businessType: null,
    narrationAudio: null,
    narrationAudioUnlocked: false,
    narrationPendingSegment: null,
    narrationPlayedSegments: 0,
    narrationAudioFailures: 0,
    narrationTimer: null,
    narrationWaypointKey: null,
    characterMixer: null,
    characterActions: new Map(),
    characterActionKey: "",
    characterAnimationMode: "gltf-animation-retargeted",
    characterLoadToken: 0,
    eyeHeight: 1.65,
    firstPersonFov: 85,
    lookSensitivity: 1,
    yaw: 0,
    pitch: 0,
    shoulderYawOffset: 0,
    shoulderPitch: 0,
    shoulderLookActive: false,
    characterCapsuleHalfHeight: 0.88,
    nearLookSensitivity: 0.8,
    jumpVelocity: 0,
    jumpGroundY: 0,
    runtimeConfig: null,
    keys: new Set(),
    character: null,
    characterVisual: null,
    characterPose: null,
    walkPhase: 0,
    startupCameraPosition,
    startupCameraTarget,
    startupCameraFov,
    startupCameraNear,
    startupCameraFar,
    mainScreenVideo: null,
    mainScreenTexture: null,
    mainScreenVideos: { horizontal: null, vertical: null },
    mainScreenTextures: { horizontal: null, vertical: null },
    mainScreenVideoStarted: false,
    mainScreenVideoStarting: false,
    mainScreenVideoSyncTimer: null,
    screenVideoMaterialCache: new Map(),
    videoScreenMeshCount: 0,
    twinPickRaycaster: new THREE.Raycaster(),
    twinPickPointer: new THREE.Vector2(),
    twinPickMeshes: [],
    twinMergeStats: { before: 0, after: 0, merged: 0, materialGroups: 0, mergeAttempts: 0, mergeFailures: 0 },
    twinBatchStats: { source: 0, render: 0, reduced: 0 },
    lastUpdateAt: performance.now(),
  };

  function initializeRuntimeGizmo() {
    try {
      const gizmo = new TransformControls(camera, renderer.domElement);
      const helper = gizmo.getHelper();
      const pivot = new THREE.Group();
      pivot.name = "RuntimeEditorGizmoPivot";
      pivot.visible = false;
      helper.visible = false;
      scene.add(pivot);
      scene.add(helper);
      gizmo.setMode("translate");
      gizmo.setSpace("world");
      gizmo.setSize(0.72);
      state.runtimeEditorGizmo = gizmo;
      state.runtimeEditorGizmoHelper = helper;
      state.runtimeEditorPivot = pivot;
      gizmo.addEventListener("dragging-changed", (event) => {
        controls.enabled = !event.value && (!state.roaming || state.cameraMode === "god");
        if (event.value) beginRuntimeGizmoDrag();
        else endRuntimeGizmoDrag();
      });
      gizmo.addEventListener("mouseDown", beginRuntimeGizmoDrag);
      gizmo.addEventListener("objectChange", updateRuntimeGizmoDrag);
    } catch (error) {
      runtime.warnings.push(`运行时 Gizmo 不可用：${error.message}`);
    }
  }

  initializeRuntimeGizmo();

  function setConnection(source, detail) {
    state.source = source;
    const live = source === "live";
    ui.panel.classList.toggle("is-offline", !live);
    ui.status.textContent = live ? "Twin 实时" : "Twin 本地基线";
    ui.status.classList.toggle("badge-green", live);
    ui.source.textContent = detail;
    document.body.dataset.twinSource = source;
  }

  function updateMetrics() {
    const snapshots = [...state.snapshots.values()];
    const partCount = totalRenderParts(snapshots);
    const typeCount = new Set(snapshots.map((snapshot) => snapshot.objectTypeRid || snapshot.objectTypeName)).size;
    ui.count.textContent = snapshots.length.toLocaleString();
    ui.parts.textContent = partCount.toLocaleString();
    ui.types.textContent = typeCount.toLocaleString();
    runtime.twinInstances = snapshots.length;
    runtime.twinRenderParts = partCount;
    document.body.dataset.twinInstances = String(snapshots.length);
    document.body.dataset.twinParts = String(partCount);
  }

  function selectedSnapshot() {
    return state.selectedId ? state.snapshots.get(state.selectedId) : null;
  }

  function showDetail(snapshot) {
    if (!snapshot) {
      ui.detail.hidden = true;
      return;
    }
    ui.detail.hidden = false;
    ui.detailName.textContent = snapshot.displayName || snapshot.instanceId;
    ui.detailPath.textContent = (snapshot.hierarchyPath ?? []).join(" / ") || "未分组";
    ui.detailId.textContent = snapshot.instanceId;
    ui.detailType.textContent = snapshot.objectTypeName || snapshot.objectTypeRid || "--";
    ui.detailState.textContent = snapshot.raw_state?.status || (snapshot.online ? "online" : "normal");
    ui.detailAnimation.textContent = snapshot.raw_state?.animation_state || "idle";
  }

  function twinIntroOf(snapshot) {
    const representable = representableOf(snapshot);
    const raw = snapshot?.raw_state ?? {};
    const candidates = [
      snapshot?.ui_label_content,
      snapshot?.description,
      snapshot?.intro,
      snapshot?.introduction,
      representable.ui_label_content,
      representable.description,
      representable.intro,
      raw.ui_label_content,
      raw.description,
      raw.introduction,
    ];
    const value = candidates.find((item) => typeof item === "string" && item.trim());
    return value?.trim() || `${snapshot?.displayName || "该 Twin 实例"}暂无已配置的详细介绍信息。`;
  }

  function showWorldDetail(snapshot) {
    if (!ui.worldDetail) return;
    if (!snapshot) {
      ui.worldDetail.hidden = true;
      return;
    }
    ui.worldDetail.hidden = false;
    if (ui.worldDetailName) ui.worldDetailName.textContent = snapshot.displayName || snapshot.instanceId;
    if (ui.worldDetailType) ui.worldDetailType.textContent = snapshot.objectTypeName || snapshot.objectTypeRid || "未分类对象";
    if (ui.worldDetailCopy) ui.worldDetailCopy.textContent = twinIntroOf(snapshot);
    if (ui.worldDetailId) ui.worldDetailId.textContent = snapshot.instanceId || "--";
    if (ui.worldDetailPath) ui.worldDetailPath.textContent = (snapshot.hierarchyPath ?? []).join(" / ") || "未分组";
  }

  function renderList() {
    const query = ui.search.value.trim().toLowerCase();
    const snapshots = [...state.snapshots.values()]
      .filter((snapshot) => !query || [
        snapshot.displayName,
        snapshot.instanceId,
        snapshot.objectTypeName,
        snapshot.objectTypeRid,
        ...(snapshot.hierarchyPath ?? []),
      ].some((value) => String(value ?? "").toLowerCase().includes(query)))
      .sort((left, right) => String(left.displayName).localeCompare(String(right.displayName), "zh-CN"))
      .slice(0, 120);
    ui.list.replaceChildren();
    if (!snapshots.length) {
      const empty = document.createElement("p");
      empty.className = "twin-empty";
      empty.textContent = "没有匹配的业务实例";
      ui.list.append(empty);
      return;
    }
    const fragment = document.createDocumentFragment();
    for (const snapshot of snapshots) {
      const row = document.createElement("button");
      row.type = "button";
      row.className = `twin-row${snapshot.instanceId === state.selectedId ? " is-selected" : ""}`;
      row.dataset.instanceId = snapshot.instanceId;
      const name = document.createElement("strong");
      name.textContent = snapshot.displayName || snapshot.instanceId;
      const meta = document.createElement("span");
      meta.textContent = `${snapshot.objectTypeName || snapshot.objectTypeRid || "未分类"} · ${representableOf(snapshot).render_parts?.length ?? 0} 子件`;
      row.append(name, meta);
      row.addEventListener("click", (event) => {
        if (editorIsActive() && event.shiftKey && state.selectedId && state.selectedId !== snapshot.instanceId) {
          toggleEditorSelection(snapshot.instanceId);
        } else {
          selectInstance(snapshot.instanceId);
        }
      });
      fragment.append(row);
    }
    ui.list.append(fragment);
  }

  function pathStartsWith(path, prefix) {
    return prefix.every((item, index) => String(path[index] ?? "") === String(item));
  }

  function renderRuntimePanels() {
    const snapshots = [...state.snapshots.values()];
    const paths = snapshots
      .map((snapshot) => Array.isArray(snapshot.hierarchyPath) ? snapshot.hierarchyPath.map(String) : [])
      .filter((path) => path.length > 0);
    if (ui.spaceBreadcrumb) ui.spaceBreadcrumb.textContent = state.spacePath.length
      ? state.spacePath.join(" / ") : "全部空间";
    if (ui.spaceColumns) {
      ui.spaceColumns.replaceChildren();
      const maxDepth = Math.min(3, Math.max(1, ...paths.map((path) => path.length)));
      for (let depth = 0; depth < maxDepth; depth += 1) {
        const parent = state.spacePath.slice(0, depth);
        const labels = [...new Set(paths
          .filter((path) => path.length > depth && pathStartsWith(path, parent))
          .map((path) => path[depth]))].sort((a, b) => a.localeCompare(b, "zh-CN"));
        if (!labels.length) continue;
        const column = document.createElement("div");
        column.className = "ue-space-column";
        for (const label of labels) {
          const button = document.createElement("button");
          button.type = "button";
          button.className = `ue-space-node${state.spacePath[depth] === label ? " is-active" : ""}`;
          button.textContent = label;
          button.addEventListener("click", () => {
            state.spacePath = [...state.spacePath.slice(0, depth), label];
            renderRuntimePanels();
            if (ui.businessScope) ui.businessScope.textContent = `当前：${state.spacePath.join(" / ") || "全部空间"}`;
          });
          column.append(button);
        }
        ui.spaceColumns.append(column);
      }
    }
    if (ui.businessRows) {
      ui.businessRows.replaceChildren();
      const scoped = state.businessScopeCurrent && state.spacePath.length
        ? snapshots.filter((snapshot) => pathStartsWith(
          Array.isArray(snapshot.hierarchyPath) ? snapshot.hierarchyPath.map(String) : [],
          state.spacePath,
        ))
        : snapshots;
      const groups = new Map();
      for (const snapshot of scoped) {
        const key = String(snapshot.objectTypeName || snapshot.objectTypeRid || "未分类");
        groups.set(key, (groups.get(key) || 0) + 1);
      }
      for (const [label, count] of [...groups.entries()].sort((a, b) => a[0].localeCompare(b[0], "zh-CN"))) {
        const row = document.createElement("button");
        row.type = "button";
        row.className = `ue-business-row${state.businessType === label ? " is-active" : ""}`;
        const labelNode = document.createElement("span");
        labelNode.textContent = label;
        const countNode = document.createElement("small");
        countNode.textContent = `${count.toLocaleString()} 个实例`;
        row.append(labelNode, countNode);
        row.addEventListener("click", () => {
          state.businessType = label;
          updateRouteStateText(`${label} · 已筛选 ${count.toLocaleString()} 个实例`);
          renderRuntimePanels();
        });
        ui.businessRows.append(row);
      }
      if (!groups.size) {
        const empty = document.createElement("span");
        empty.textContent = "当前空间暂无业务实例";
        empty.className = "ue-control-placeholder";
        ui.businessRows.append(empty);
      }
    }
  }

  function drawMinimapCanvas(canvas) {
    if (!canvas) return;
    const context = canvas.getContext("2d");
    const width = canvas.width;
    const height = canvas.height;
    context.clearRect(0, 0, width, height);
    context.fillStyle = "#071019";
    context.fillRect(0, 0, width, height);
    context.strokeStyle = "rgba(125,220,255,.08)";
    context.lineWidth = 1;
    for (let x = 20; x < width; x += 20) { context.beginPath(); context.moveTo(x, 0); context.lineTo(x, height); context.stroke(); }
    for (let y = 20; y < height; y += 20) { context.beginPath(); context.moveTo(0, y); context.lineTo(width, y); context.stroke(); }
    const snapshots = [...state.snapshots.values()];
    const twinPoints = snapshots.map((snapshot) => positionOf(snapshot));
    const routePoints = state.routeWaypoints.map((point) => point.position);
    const playerPoint = state.character?.position ?? null;
    const allPoints = [...twinPoints, ...routePoints, ...(playerPoint ? [playerPoint] : [])];
    if (!allPoints.length) {
      state.minimapTransforms.delete(canvas);
      return;
    }
    const minX = Math.min(...allPoints.map((point) => point.x));
    const maxX = Math.max(...allPoints.map((point) => point.x));
    const minZ = Math.min(...allPoints.map((point) => point.z));
    const maxZ = Math.max(...allPoints.map((point) => point.z));
    const spanX = Math.max(maxX - minX, 1);
    const spanZ = Math.max(maxZ - minZ, 1);
    const mapPoint = (point) => ({
      x: 7 + ((point.x - minX) / spanX) * (width - 14),
      y: 7 + ((point.z - minZ) / spanZ) * (height - 14),
    });
    state.minimapTransforms.set(canvas, { minX, maxX, minZ, maxZ, spanX, spanZ, width, height });
    if (routePoints.length > 1) {
      context.strokeStyle = "rgba(140,255,193,.75)";
      context.lineWidth = 1.5;
      context.beginPath();
      routePoints.forEach((point, index) => {
        const mapped = mapPoint(point);
        if (index === 0) context.moveTo(mapped.x, mapped.y);
        else context.lineTo(mapped.x, mapped.y);
      });
      context.stroke();
    }
    for (let index = 0; index < twinPoints.length; index += 1) {
      const snapshot = snapshots[index];
      const mapped = mapPoint(twinPoints[index]);
      context.fillStyle = snapshot.instanceId === state.selectedId ? "#fff176" : "rgba(107,224,249,.72)";
      context.beginPath();
      context.arc(mapped.x, mapped.y, snapshot.instanceId === state.selectedId ? 3 : 1.35, 0, Math.PI * 2);
      context.fill();
    }
    routePoints.forEach((point, index) => {
      const mapped = mapPoint(point);
      context.fillStyle = index < state.routeWaypointIndex ? "#6e8798" : (index === state.routeWaypointIndex ? "#fff176" : "#8cffc1");
      context.beginPath();
      context.arc(mapped.x, mapped.y, index === state.routeWaypointIndex ? 3 : 1.8, 0, Math.PI * 2);
      context.fill();
    });
    if (playerPoint) {
      const mapped = mapPoint(playerPoint);
      context.fillStyle = "#ff8ba7";
      context.beginPath();
      context.arc(mapped.x, mapped.y, 3.6, 0, Math.PI * 2);
      context.fill();
    }
    if (state.minimapPreview && canvas === ui.runtimeMinimap) {
      const preview = mapPoint(state.minimapPreview);
      context.strokeStyle = "#fff176";
      context.lineWidth = 1.5;
      context.beginPath();
      context.arc(preview.x, preview.y, 6, 0, Math.PI * 2);
      context.stroke();
      context.beginPath();
      context.moveTo(preview.x - 8, preview.y);
      context.lineTo(preview.x + 8, preview.y);
      context.moveTo(preview.x, preview.y - 8);
      context.lineTo(preview.x, preview.y + 8);
      context.stroke();
    }
  }

  function drawMinimap() {
    drawMinimapCanvas(ui.minimap);
    drawMinimapCanvas(ui.runtimeMinimap);
  }

  function applySnapshotToVisual(snapshot) {
    const visual = state.visualById.get(snapshot.instanceId);
    if (!visual) return;
    visual.position.copy(positionOf(snapshot));
    visual.quaternion.copy(quaternionOf(snapshot));
    visual.scale.copy(scaleOf(snapshot));
    visual.visible = representableOf(snapshot).is_visible !== false;
    visual.userData.twinSnapshot = snapshot;
    visual.updateMatrix();
    visual.updateMatrixWorld(true);
    state.collisionCatalogDirty = true;
    scheduleChunkBatchRebuild(snapshot.instanceId);
  }

  function snapshotRenderSignature(snapshot) {
    const spatial = spatialOf(snapshot);
    const representable = representableOf(snapshot);
    return [
      spatial.translation_x, spatial.translation_y, spatial.translation_z,
      spatial.rotation_x, spatial.rotation_y, spatial.rotation_z,
      spatial.scale_x, spatial.scale_y, spatial.scale_z,
      representable.is_visible,
    ].map((value) => String(value ?? "")).join("|");
  }

  function requestChangedChunk(instanceId) {
    const chunkId = state.chunkByInstanceId.get(instanceId);
    if (!chunkId || state.loadedChunks.has(chunkId) || state.pendingChunks.has(chunkId)) return;
    const record = state.chunkManifest?.chunks?.find((item) => item.id === chunkId);
    if (!record) return;
    loadChunk(record).catch((error) => {
      runtime.warnings.push(`Twin 变化块 ${chunkId} 加载失败：${error.message}`);
    });
  }

  function applyDelta(payload) {
    if (payload.mode === "reset") state.snapshots.clear();
    const upserts = Array.isArray(payload) ? payload : (payload.upserts ?? []);
    const deleted = Array.isArray(payload) ? [] : (payload.deletedIds ?? []);
    for (const snapshot of upserts) {
      if (!snapshot?.instanceId) continue;
      const previous = state.snapshots.get(snapshot.instanceId);
      const renderChanged = previous
        && snapshotRenderSignature(previous) !== snapshotRenderSignature(snapshot);
      state.snapshots.set(snapshot.instanceId, snapshot);
      applySnapshotToVisual(snapshot);
      if (renderChanged) requestChangedChunk(snapshot.instanceId);
    }
    for (const id of deleted) {
      state.snapshots.delete(id);
      const visual = state.visualById.get(id);
      if (visual) {
        visual.visible = false;
        state.collisionCatalogDirty = true;
      }
      if (state.selectedId === id) selectInstance(null);
    }
    if (!Array.isArray(payload) && payload.cursor) state.cursor = payload.cursor;
    updateMetrics();
    renderList();
    renderRuntimePanels();
    drawMinimap();
  }

  function focusSelected() {
    const visual = state.visualById.get(state.selectedId);
    if (!visual) return;
    // Do not call Box3.setFromObject on an animated SkinnedMesh: Three.js
    // 0.18 computes a deformed bounding box through applyBoneTransform, but
    // UE's exported skeleton can legitimately omit a helper bone.  Geometry
    // bounds are stable for normalization and avoid that null matrix path.
    const box = new THREE.Box3();
    visual.traverse((object) => {
      if (!object.isMesh || !object.geometry) return;
      if (!object.geometry.boundingBox) object.geometry.computeBoundingBox();
      if (object.geometry.boundingBox) {
        box.union(object.geometry.boundingBox.clone().applyMatrix4(object.matrixWorld));
      }
    });
    if (box.isEmpty()) return;
    const sphere = box.getBoundingSphere(new THREE.Sphere());
    const radius = Math.max(sphere.radius, 0.5);
    controls.target.copy(sphere.center);
    camera.position.copy(sphere.center).add(new THREE.Vector3(radius * 1.6, radius * 0.85, radius * 1.6));
    controls.update();
  }

  function selectInstance(instanceId) {
    state.selectedId = instanceId;
    state.selectedIds = new Set(instanceId ? [instanceId] : []);
    document.body.dataset.twinSelected = instanceId ?? "";
    if (state.selectionBox) {
      scene.remove(state.selectionBox);
      state.selectionBox.geometry?.dispose?.();
      state.selectionBox.material?.dispose?.();
      state.selectionBox = null;
    }
    const snapshot = selectedSnapshot();
    const visual = instanceId ? state.visualById.get(instanceId) : null;
    if (visual) {
      state.selectionBox = new THREE.BoxHelper(visual, 0x67e6ff);
      state.selectionBox.material.depthTest = false;
      state.selectionBox.material.transparent = true;
      state.selectionBox.material.opacity = 0.9;
      state.selectionBox.renderOrder = 10000;
      scene.add(state.selectionBox);
    }
    showDetail(snapshot);
    showWorldDetail(snapshot);
    renderList();
    drawMinimap();
    renderRuntimeEditor();
    if (instanceId && !visual) refreshChunkResidency(performance.now(), true, instanceId).then(() => {
      if (state.selectedId === instanceId) selectInstance(instanceId);
    });
  }

  function twinPickOwner(mesh, hitPoint) {
    const ids = Array.isArray(mesh?.userData?.twinPickInstanceIds)
      ? mesh.userData.twinPickInstanceIds.filter((id) => state.snapshots.has(id)) : [];
    if (!ids.length) return null;
    // Merged batches can contain several Twin instances. Prefer the instance
    // whose authored bounds contain the hit, then fall back to the nearest
    // instance origin. This preserves DrawCall batching without losing click
    // selection semantics.
    let nearestId = ids[0];
    let nearestDistance = Infinity;
    for (const id of ids) {
      const visual = state.visualById.get(id);
      const bounds = new THREE.Box3();
      visual?.traverse?.((object) => {
        if (!object.isMesh || !object.geometry?.boundingBox) return;
        bounds.union(object.geometry.boundingBox.clone().applyMatrix4(object.matrixWorld));
      });
      if (!bounds.isEmpty() && bounds.containsPoint(hitPoint)) return id;
      const distance = positionOf(state.snapshots.get(id)).distanceToSquared(hitPoint);
      if (distance < nearestDistance) {
        nearestDistance = distance;
        nearestId = id;
      }
    }
    return nearestId;
  }

  function pickTwinAtClientPoint(clientX, clientY) {
    if (!renderer.domElement || (state.roaming && state.cameraMode === "first_person" && document.pointerLockElement === renderer.domElement)) return null;
    const rect = renderer.domElement.getBoundingClientRect();
    if (!rect.width || !rect.height) return null;
    state.twinPickPointer.set(
      ((clientX - rect.left) / rect.width) * 2 - 1,
      -((clientY - rect.top) / rect.height) * 2 + 1,
    );
    state.twinPickRaycaster.setFromCamera(state.twinPickPointer, camera);
    state.twinPickMeshes = state.twinPickMeshes.filter((mesh) => mesh?.parent && mesh.visible);
    const hits = state.twinPickRaycaster.intersectObjects(state.twinPickMeshes, false);
    const hit = hits.find((item) => item.object?.userData?.twinPickInstanceIds?.length);
    const instanceId = hit ? twinPickOwner(hit.object, hit.point) : null;
    if (instanceId) selectInstance(instanceId);
    return instanceId;
  }

  function toggleEditorSelection(instanceId) {
    if (!editorIsActive() || !instanceId) return;
    if (state.selectedIds.has(instanceId)) {
      state.selectedIds.delete(instanceId);
      if (state.selectedId === instanceId) state.selectedId = [...state.selectedIds][0] || null;
    } else {
      state.selectedIds.add(instanceId);
      if (!state.selectedId) state.selectedId = instanceId;
    }
    document.body.dataset.twinSelected = state.selectedId ?? "";
    if (state.selectionBox) {
      scene.remove(state.selectionBox);
      state.selectionBox.geometry?.dispose?.();
      state.selectionBox.material?.dispose?.();
      state.selectionBox = null;
    }
    const primaryVisual = state.selectedId ? state.visualById.get(state.selectedId) : null;
    if (primaryVisual) {
      state.selectionBox = new THREE.BoxHelper(primaryVisual, 0x67e6ff);
      state.selectionBox.material.depthTest = false;
      state.selectionBox.material.transparent = true;
      state.selectionBox.material.opacity = 0.9;
      state.selectionBox.renderOrder = 10000;
      scene.add(state.selectionBox);
    }
    showDetail(selectedSnapshot());
    showWorldDetail(selectedSnapshot());
    renderList();
    refreshRuntimeGizmo();
    renderRuntimeEditor();
  }

  function chunkDistance(record) {
    const center = new THREE.Vector3().fromArray(record.center);
    return Math.max(0, center.distanceTo(camera.position) - Number(record.radius ?? 0));
  }

  function updateChunkMetrics() {
    const loadedMeshCount = [...state.loadedChunks.values()].reduce((sum, chunk) => sum + chunk.meshCount, 0);
    runtime.twinVisualRoots = state.visualById.size;
    runtime.twinMeshCount = loadedMeshCount;
    document.body.dataset.twinVisualRoots = String(state.visualById.size);
    document.body.dataset.twinMeshes = String(loadedMeshCount);
    document.body.dataset.twinChunksResident = String(state.loadedChunks.size);
    document.body.dataset.twinHlodResident = String(state.hlodChunks.size);
    const detailLabel = state.twinDetailMode === "all" ? "高清块（全量）" : "高清块";
    ui.source.textContent = `实时 · HLOD ${state.hlodChunks.size}（已禁用） · ${state.loadedChunks.size} ${detailLabel}`;
  }

  async function initializePersistentHlod() {
    // The final showroom must not change authored geometry while the camera
    // moves. HLOD was part of the early X300 experiment, so do not request it
    // or add it to the scene in the production Windows preview.
    document.body.dataset.twinPersistentLayer = "disabled";
    document.body.dataset.twinHlodChunks = "0";
    document.body.dataset.twinHlodBytes = "0";
    document.body.dataset.twinHlodTriangles = "0";
    document.body.dataset.twinHlodDrawCalls = "0";
  }

  function ensureMainScreenVideos() {
    const specs = {
      horizontal: "/data/MainScreen_H.mp4",
      vertical: "/data/MainScreen_V.mp4",
    };
    for (const [type, src] of Object.entries(specs)) {
      if (state.mainScreenVideos[type]) continue;
      const video = document.createElement("video");
      video.src = src;
      video.muted = true;
      video.loop = true;
      video.playsInline = true;
      video.preload = "auto";
      video.crossOrigin = "anonymous";
      video.style.display = "none";
      video.dataset.screenVideo = type;
      document.body.append(video);
      const texture = new THREE.VideoTexture(video);
      texture.colorSpace = THREE.SRGBColorSpace;
      // Both exported screen meshes carry the same vertically mirrored video
      // upload convention. Keep the correction in the texture transform so
      // geometry, screen placement and the two synchronized media clocks stay
      // untouched.
      texture.flipY = true;
      // `flipY` controls the WebGL upload origin and is not sufficient for
      // these authored screens: their glTF UVs are vertically mirrored.
      // Apply the explicit transform to both the front V and ceiling H panel.
      texture.repeat.y = -1;
      texture.offset.y = 1;
      texture.needsUpdate = true;
      texture.minFilter = THREE.LinearFilter;
      texture.magFilter = THREE.LinearFilter;
      texture.generateMipmaps = false;
      state.mainScreenVideos[type] = video;
      state.mainScreenTextures[type] = texture;
      if (type === "horizontal") {
        state.mainScreenVideo = video;
        state.mainScreenTexture = texture;
      }
    }
    document.body.dataset.videoSources = "horizontal:MainScreen_H.mp4,vertical:MainScreen_V.mp4";
    document.body.dataset.videoScreens = String(state.videoScreenMeshCount);
    return state.mainScreenTextures;
  }

  async function startMainScreenVideos() {
    ensureMainScreenVideos();
    if (state.mainScreenVideoStarted || state.mainScreenVideoStarting) return;
    const videos = Object.values(state.mainScreenVideos).filter(Boolean);
    if (!videos.length) return;
    state.mainScreenVideoStarting = true;
    try {
      // Calling play on both elements in the same task keeps their media
      // clocks aligned, matching MainScreenVideoSubsystem::TryStartSynchronized.
      videos.forEach((video) => {
        if (video.readyState < HTMLMediaElement.HAVE_METADATA) video.load();
        if (video.readyState >= HTMLMediaElement.HAVE_METADATA) video.currentTime = 0;
      });
      await Promise.all(videos.map((video) => video.play()));
      state.mainScreenVideoStarted = true;
      document.body.dataset.videoSync = "playing";
      document.body.dataset.videoSyncDriftMs = "0";
      if (!state.mainScreenVideoSyncTimer) {
        state.mainScreenVideoSyncTimer = window.setInterval(() => {
          const horizontal = state.mainScreenVideos.horizontal;
          const vertical = state.mainScreenVideos.vertical;
          if (!horizontal || !vertical || horizontal.paused || vertical.paused) return;
          const driftMs = Math.round(Math.abs(horizontal.currentTime - vertical.currentTime) * 1000);
          document.body.dataset.videoSyncDriftMs = String(driftMs);
          if (driftMs > 120) vertical.currentTime = horizontal.currentTime;
        }, 1000);
      }
    } catch (error) {
      state.mainScreenVideoStarted = false;
      document.body.dataset.videoSync = "waiting-user-gesture";
      runtime.warnings.push("双屏视频等待首次用户交互后播放");
    } finally {
      state.mainScreenVideoStarting = false;
    }
  }

  function screenVideoTypeOf(material) {
    const name = String(material?.name || "");
    if (SCREEN_VIDEO_MATERIALS.has(name)) return SCREEN_VIDEO_MATERIALS.get(name);
    if (/Rectangle2133396228|MainScreen[_ ]?H/i.test(name)) return "horizontal";
    if (/Rectangle2133396227|MainScreen[_ ]?V/i.test(name)) return "vertical";
    return null;
  }

  function patchMainScreenVideo(object) {
    if (!object.isMesh) return;
    const sourceMaterials = Array.isArray(object.material) ? object.material : [object.material];
    let changed = false;
    const patched = sourceMaterials.map((material) => {
      const type = screenVideoTypeOf(material);
      if (!type) return material;
      ensureMainScreenVideos();
      const cacheKey = `${material.uuid}:${type}`;
      let clone = state.screenVideoMaterialCache.get(cacheKey);
      if (!clone) {
        clone = material.clone();
        const texture = state.mainScreenTextures[type];
        clone.map = texture;
        if (clone.emissive) {
          clone.emissive.set(0xffffff);
          clone.emissiveMap = texture;
          clone.emissiveIntensity = 0.8;
        }
        clone.userData = { ...clone.userData, zhhzScreenVideoType: type };
        clone.needsUpdate = true;
        state.screenVideoMaterialCache.set(cacheKey, clone);
      }
      changed = true;
      state.videoScreenMeshCount += 1;
      return clone;
    });
    if (changed) {
      object.material = Array.isArray(object.material) ? patched : patched[0];
      document.body.dataset.videoScreens = String(state.videoScreenMeshCount);
      void startMainScreenVideos();
    }
  }

  const twinTextureAnisotropy = Math.min(8, renderer.capabilities.getMaxAnisotropy());

  function patchTwinTextureQuality(object) {
    if (!object.isMesh) return;
    const materials = Array.isArray(object.material) ? object.material : [object.material];
    for (const material of materials) {
      if (!material) continue;
      const textures = new Set([
        material.map,
        material.emissiveMap,
        material.normalMap,
        material.roughnessMap,
        material.metalnessMap,
        material.aoMap,
      ]);
      for (const texture of textures) {
        // The main hall screen is replaced by a VideoTexture below. Video
        // textures cannot use mipmaps; the exported still-image screens can.
        if (!texture || texture.isVideoTexture || !texture.image) continue;
        texture.anisotropy = twinTextureAnisotropy;
        texture.minFilter = THREE.LinearMipmapLinearFilter;
        texture.magFilter = THREE.LinearFilter;
        texture.generateMipmaps = true;
      }
    }
  }

  // The packaged GLTF contains render geometry rather than UE's physics
  // bodies.  Rebuild a very small CPU-side collision index from the same
  // authored meshes.  It never adds a render object or a draw call: only the
  // roaming controller consults the cached world-space AABBs and a handful of
  // ray tests around the character capsule.
  function isCollisionExcluded(object) {
    for (let parent = object; parent; parent = parent.parent) {
      if (parent === state.character) return true;
      const name = String(parent.name || "");
      if (/^(WebRoamingCharacter|RoamerFallback|RuntimeEditor|TransformControls|OntoTwin_RenderBatch_.*_helper)/i.test(name)) return true;
    }
    return !object.geometry?.attributes?.position;
  }

  function isCollisionObjectVisible(object) {
    for (let parent = object; parent; parent = parent.parent) {
      if (!parent.visible) return false;
    }
    return true;
  }

  function collisionCellKey(x, z) {
    return `${x}:${z}`;
  }

  function rebuildCollisionCatalog(now = performance.now(), force = false) {
    if (!COLLISION_ENABLED) {
      state.collisionEntries = [];
      state.collisionGrid = new Map();
      state.collisionLargeEntries = [];
      state.cameraCollisionMeshes = [];
      state.collisionCatalogAt = now;
      state.collisionCatalogDirty = false;
      document.body.dataset.collisionMode = "disabled-diagnostic";
      document.body.dataset.collisionMeshes = "0";
      document.body.dataset.collisionGridCells = "0";
      return;
    }
    if (!force && !state.collisionCatalogDirty && now - state.collisionCatalogAt < 2000) return;
    const entries = [];
    const grid = new Map();
    const largeEntries = [];
    const cellSize = state.collisionCellSize;
    scene.updateMatrixWorld(true);
    scene.traverse((object) => {
      if (!object.isMesh || !object.visible || isCollisionExcluded(object)) return;
      const geometry = object.geometry;
      if (!geometry.boundingBox) geometry.computeBoundingBox();
      if (!geometry.boundingBox) return;
      const box = geometry.boundingBox.clone().applyMatrix4(object.matrixWorld);
      if (box.isEmpty() || !Number.isFinite(box.min.x) || !Number.isFinite(box.max.x)) return;
      const entry = { object, box };
      entries.push(entry);
      const minX = Math.floor(box.min.x / cellSize);
      const maxX = Math.floor(box.max.x / cellSize);
      const minZ = Math.floor(box.min.z / cellSize);
      const maxZ = Math.floor(box.max.z / cellSize);
      const covered = (maxX - minX + 1) * (maxZ - minZ + 1);
      // Huge slabs (the floor/ceiling) are kept in a separate small list so
      // they do not populate hundreds of hash buckets.
      if (covered > 256) {
        largeEntries.push(entry);
        return;
      }
      for (let x = minX; x <= maxX; x += 1) {
        for (let z = minZ; z <= maxZ; z += 1) {
          const key = collisionCellKey(x, z);
          let bucket = grid.get(key);
          if (!bucket) { bucket = []; grid.set(key, bucket); }
          bucket.push(entry);
        }
      }
    });
    state.collisionEntries = entries;
    state.collisionGrid = grid;
    state.collisionLargeEntries = largeEntries;
    state.cameraCollisionMeshes = entries.map((entry) => entry.object);
    state.collisionCatalogAt = now;
    state.collisionCatalogDirty = false;
    document.body.dataset.collisionMode = "geometry-aabb-ray-sweep";
    document.body.dataset.collisionMeshes = String(entries.length);
    document.body.dataset.collisionGridCells = String(grid.size);
  }

  function collisionCandidatesAt(x, z, margin = 1.5) {
    const cellSize = state.collisionCellSize;
    const minX = Math.floor((x - margin) / cellSize);
    const maxX = Math.floor((x + margin) / cellSize);
    const minZ = Math.floor((z - margin) / cellSize);
    const maxZ = Math.floor((z + margin) / cellSize);
    const seen = new Set();
    const candidates = [];
    for (let cx = minX; cx <= maxX; cx += 1) {
      for (let cz = minZ; cz <= maxZ; cz += 1) {
        for (const entry of state.collisionGrid.get(collisionCellKey(cx, cz)) || []) {
          if (seen.has(entry.object.id)) continue;
          if (!isCollisionObjectVisible(entry.object)) continue;
          seen.add(entry.object.id);
          candidates.push(entry.object);
        }
      }
    }
    // Include large slabs only when their AABB overlaps the query. This is
    // still a small list and makes the floor reliable without a full-scene
    // raycast on every frame.
    const query = new THREE.Box3(
      new THREE.Vector3(x - margin, -Infinity, z - margin),
      new THREE.Vector3(x + margin, Infinity, z + margin),
    );
    for (const entry of state.collisionLargeEntries) {
      if (!entry.box.intersectsBox(query) || seen.has(entry.object.id)) continue;
      if (!isCollisionObjectVisible(entry.object)) continue;
      seen.add(entry.object.id);
      candidates.push(entry.object);
    }
    state.collisionCandidateCount = candidates.length;
    return candidates;
  }

  function worldHitNormal(hit) {
    if (!hit?.face?.normal) return null;
    return hit.face.normal.clone().transformDirection(hit.object.matrixWorld);
  }

  function probeGroundAt(x, z, referenceY) {
    const candidates = collisionCandidatesAt(x, z, state.collisionRadius + 0.16);
    if (!candidates.length) return null;
    const raycaster = state.collisionGroundRaycaster;
    const origin = new THREE.Vector3(x, referenceY + state.collisionGroundProbeHeight, z);
    raycaster.set(origin, new THREE.Vector3(0, -1, 0));
    raycaster.near = 0;
    raycaster.far = state.collisionGroundProbeDistance + state.collisionStepHeight;
    const hits = raycaster.intersectObjects(candidates, false);
    let best = null;
    const minY = referenceY - 1.25;
    const maxY = referenceY + state.collisionStepHeight + 0.08;
    for (const hit of hits) {
      const y = hit.point.y;
      if (y < minY || y > maxY) continue;
      const normal = worldHitNormal(hit);
      if (normal && normal.y < 0.42) continue;
      if (!best || y > best) best = y;
    }
    return best;
  }

  function horizontalSweepBlocked(start, delta) {
    const length = Math.hypot(delta.x, delta.z);
    if (length < 0.0001) return false;
    const direction = new THREE.Vector3(delta.x / length, 0, delta.z / length);
    const perpendicular = new THREE.Vector3(-direction.z, 0, direction.x);
    const candidates = collisionCandidatesAt(
      start.x + delta.x * 0.5,
      start.z + delta.z * 0.5,
      length + state.collisionRadius + 0.2,
    );
    if (!candidates.length) return false;
    const raycaster = state.collisionSweepRaycaster;
    const offsets = [-state.collisionRadius * 0.82, state.collisionRadius * 0.82];
    const heights = [0.36, 1.15, Math.max(1.35, state.collisionHeight - 0.2)];
    for (const offset of offsets) {
      for (const height of heights) {
        const origin = start.clone().addScaledVector(perpendicular, offset);
        origin.y += height;
        raycaster.set(origin, direction);
        raycaster.near = 0.025;
        raycaster.far = length + state.collisionRadius;
        const hit = raycaster.intersectObjects(candidates, false)[0];
        if (hit && hit.distance < length + state.collisionRadius - 0.015) return true;
      }
    }
    return false;
  }

  function moveCharacterWithCollision(delta) {
    const character = state.character;
    if (!character || delta.lengthSq() < 1e-10) return false;
    if (!COLLISION_ENABLED) {
      character.position.add(delta);
      return true;
    }
    rebuildCollisionCatalog();
    const start = character.position.clone();
    const horizontal = new THREE.Vector3(delta.x, 0, delta.z);
    const attempts = [
      horizontal,
      new THREE.Vector3(horizontal.x, 0, 0),
      new THREE.Vector3(0, 0, horizontal.z),
    ].filter((attempt, index, all) => attempt.lengthSq() > 1e-10
      && all.findIndex((item) => item.distanceToSquared(attempt) < 1e-10) === index);
    const isAirborne = Math.abs(state.jumpVelocity) > 0.01 || start.y > state.jumpGroundY + 0.035;
    let fullSweepBlocked = false;
    let blocked = true;
    for (const [attemptIndex, attempt] of attempts.entries()) {
      const targetX = start.x + attempt.x;
      const targetZ = start.z + attempt.z;
      const floorY = isAirborne ? null : probeGroundAt(targetX, targetZ, start.y);
      const floorDelta = floorY === null ? null : floorY - start.y;
      const stepAllowed = floorDelta !== null
        && floorDelta <= state.collisionStepHeight + 0.045
        && floorDelta >= -1.25;
      const sweepBlocked = horizontalSweepBlocked(start, attempt);
      if (attemptIndex === 0) fullSweepBlocked = sweepBlocked;
      // A low vertical riser is a stair step, not a wall. Permit it only when
      // the target floor is within the UE capsule step height. A wall has no
      // positive floor change and remains blocked by the sweep.
      if (sweepBlocked && !stepAllowed) continue;
      if (!sweepBlocked && floorY === null && !state.collisionEntries.length) {
        // Keep the offline/fallback spawn usable before the first geometry
        // catalog is ready. Once geometry exists, an edge with no floor is
        // correctly treated as non-walkable.
        character.position.x = targetX;
        character.position.z = targetZ;
        blocked = false;
        break;
      }
      if (floorY === null && !isAirborne) continue;
      character.position.x = targetX;
      character.position.z = targetZ;
      if (floorY !== null && !isAirborne) {
        character.position.y = floorY;
        state.jumpGroundY = floorY;
        state.collisionLastGroundY = floorY;
      }
      blocked = false;
      break;
    }
    const actualHorizontal = new THREE.Vector3(character.position.x - start.x, 0, character.position.z - start.z);
    // Report a block when the requested vector was obstructed, even if the
    // controller successfully slid along one wall axis. This is useful for
    // QA and mirrors the UE capsule's hit/slide result.
    const reportedBlocked = fullSweepBlocked && actualHorizontal.distanceTo(horizontal) > 0.02;
    state.collisionBlocked = reportedBlocked;
    document.body.dataset.collisionBlocked = String(reportedBlocked);
    document.body.dataset.collisionGroundY = Number.isFinite(state.collisionLastGroundY)
      ? state.collisionLastGroundY.toFixed(3) : "none";
    return !blocked;
  }

  function findChunkNodeIndex(object, root, parser) {
    let cursor = object;
    while (cursor && cursor !== root.parent) {
      const association = parser?.associations?.get(cursor);
      if (association?.nodes !== undefined) return association.nodes;
      cursor = cursor.parent;
    }
    return null;
  }

  function applyChunkCoplanarFixes(gltf, root, manifest, chunkId) {
    const targetByKey = new Map((manifest?.targets ?? []).map((target) => [`${target.node_index}:${target.primitive_index}`, target]));
    let applied = 0;
    let authoredTargets = 0;
    root.traverse((object) => {
      if (!object.isMesh) return;
      // Batched Twin GLBs carry the original polygon-offset decision on the
      // generated node. This keeps the near-surface fix valid after node and
      // primitive indices have been collapsed for draw-call reduction.
      const nodeIndex = findChunkNodeIndex(object, root, gltf.parser);
      const extras = nodeIndex === null ? null : gltf.parser?.json?.nodes?.[nodeIndex]?.extras;
      const authoredOffset = object.userData?.zhhz_coplanar
        ?? object.userData?.extras?.zhhz_coplanar
        ?? object.userData?.gltfExtensions?.zhhz_coplanar
        ?? extras?.zhhz_coplanar
        ?? null;
      if (nodeIndex === null && !authoredOffset) return;
      const association = gltf.parser?.associations?.get(object);
      const primitiveIndex = association?.primitives ?? association?.primitive ?? 0;
      const target = authoredOffset ?? targetByKey.get(`${nodeIndex}:${primitiveIndex}`);
      if (!target) return;
      const patchMaterial = (material) => {
        if (!material) return material;
        const clone = material.clone();
        clone.polygonOffset = true;
        clone.polygonOffsetFactor = target.polygon_offset_factor;
        clone.polygonOffsetUnits = target.polygon_offset_units;
        clone.userData = {
          ...clone.userData,
          zhhzTwinCoplanarChunk: chunkId,
          zhhzTwinCoplanarTarget: authoredOffset ? `batched:${object.name}` : `${target.node_index}:${target.primitive_index}`,
          zhhzCoplanarLayerRank: target.layer_rank,
        };
        clone.needsUpdate = true;
        return clone;
      };
      object.material = Array.isArray(object.material)
        ? object.material.map(patchMaterial)
        : patchMaterial(object.material);
      object.renderOrder = Math.max(object.renderOrder, target.layer_rank ?? 1);
      applied += 1;
      if (authoredOffset) authoredTargets += 1;
    });
    const expectedTargets = authoredTargets > 0 ? authoredTargets : (manifest?.offset_target_count ?? 0);
    if (applied !== expectedTargets) {
      runtime.warnings.push(`Twin ${chunkId} 共面修复命中 ${applied}/${expectedTargets}`);
    }
    return { applied, targets: expectedTargets, conflicts: manifest?.conflict_pair_count ?? 0 };
  }

  function updateTwinCoplanarMetrics() {
    let applied = 0;
    let targets = 0;
    let conflicts = 0;
    for (const loaded of state.loadedChunks.values()) {
      applied += loaded.coplanar?.applied ?? 0;
      targets += loaded.coplanar?.targets ?? 0;
      conflicts += loaded.coplanar?.conflicts ?? 0;
    }
    document.body.dataset.twinCoplanarApplied = String(applied);
    document.body.dataset.twinCoplanarTargets = String(targets);
    document.body.dataset.twinCoplanarConflicts = String(conflicts);
  }

  function geometryCompatibilityKey(geometry) {
    const attributes = Object.entries(geometry.attributes ?? {})
      .sort(([left], [right]) => left.localeCompare(right))
      .map(([name, attribute]) => `${name}:${attribute.itemSize}:${attribute.array?.constructor?.name ?? "array"}:${attribute.normalized ? 1 : 0}`)
      .join(",");
    // mergeGeometries promotes indices to a fresh array, so Uint16/Uint32
    // differences do not prevent a safe merge; only indexed vs non-indexed
    // layouts must remain separate.
    const indexMode = geometry.index ? "indexed" : "none";
    return `${attributes}|index:${indexMode}|morph:${geometry.morphTargetsRelative ? 1 : 0}`;
  }

  function textureCompatibilityKey(texture) {
    if (!texture) return "none";
    const imageKey = texture.name || texture.image?.src || texture.image?.currentSrc || "image";
    const offset = texture.offset ? `${texture.offset.x},${texture.offset.y}` : "0,0";
    const repeat = texture.repeat ? `${texture.repeat.x},${texture.repeat.y}` : "1,1";
    return [imageKey, texture.channel ?? 0, texture.colorSpace, texture.wrapS, texture.wrapT, offset, repeat, texture.rotation ?? 0].join(":");
  }

  function materialCompatibilityKey(material) {
    const color = material.color?.getHex?.() ?? "";
    const emissive = material.emissive?.getHex?.() ?? "";
    const userData = material.userData ?? {};
    // Material names are exporter-local labels (many otherwise identical
    // UE materials arrive as Material__<id>).  They must not prevent a safe
    // merge.  Include the complete shader/render-state and texture identity
    // instead, so batching can join only materials that render identically.
    const values = [
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
      material.vertexAlphas ? 1 : 0,
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
      textureCompatibilityKey(material.map),
      textureCompatibilityKey(material.normalMap),
      textureCompatibilityKey(material.roughnessMap),
      textureCompatibilityKey(material.metalnessMap),
      textureCompatibilityKey(material.aoMap),
      textureCompatibilityKey(material.emissiveMap),
      textureCompatibilityKey(material.lightMap),
      userData.zhhzTwinCoplanarLayerRank ?? "",
    ];
    return values.join("|");
  }

  function mergeTwinMeshesByInstance(root) {
    // Exported Twin roots preserve one root per instance. Merge only sibling
    // render parts within that root when their material and vertex layout are
    // identical. The root stays addressable by instanceId, so live transforms,
    // visibility and editor operations remain intact.
    root.updateMatrixWorld(true);
    const instanceRoots = root.children.filter((object) => /^ue_[0-9a-f-]+$/i.test(object.name ?? ""));
    const candidates = instanceRoots.length ? instanceRoots : [root];
    let before = 0;
    let after = 0;
    let merged = 0;
    for (const instanceRoot of candidates) {
      const groups = new Map();
      const meshes = [];
      instanceRoot.traverse((object) => {
        if (!object.isMesh || !object.geometry) return;
        meshes.push(object);
        before += 1;
        if (!object.visible || Array.isArray(object.material)) return;
        const key = `${materialCompatibilityKey(object.material)}|${object.renderOrder}|${geometryCompatibilityKey(object.geometry)}`;
        if (!groups.has(key)) groups.set(key, []);
        groups.get(key).push(object);
      });
      after += meshes.length;
      for (const group of groups.values()) {
        if (group.length < 2) continue;
        state.twinMergeStats.materialGroups += 1;
        state.twinMergeStats.mergeAttempts += 1;
        const inverseRoot = new THREE.Matrix4().copy(instanceRoot.matrixWorld).invert();
        const clones = group.map((object) => {
          const geometry = object.geometry.clone();
          const relative = inverseRoot.clone().multiply(object.matrixWorld);
          geometry.applyMatrix4(relative);
          return geometry;
        });
        let mergedGeometry = null;
        try {
          mergedGeometry = BufferGeometryUtils.mergeGeometries(clones, false);
        } catch (error) {
          runtime.warnings.push(`Twin 合批跳过 ${instanceRoot.name}：${error.message}`);
        }
        clones.forEach((geometry) => geometry.dispose());
        if (!mergedGeometry) {
          state.twinMergeStats.mergeFailures += 1;
          continue;
        }
        mergedGeometry.computeBoundingBox();
        mergedGeometry.computeBoundingSphere();
        const mergedMesh = new THREE.Mesh(mergedGeometry, group[0].material);
        mergedMesh.name = `${instanceRoot.name}_merged_${merged}`;
        mergedMesh.frustumCulled = true;
        mergedMesh.castShadow = false;
        mergedMesh.receiveShadow = false;
        mergedMesh.renderOrder = group[0].renderOrder;
        instanceRoot.add(mergedMesh);
        for (const object of group) {
          object.parent?.remove(object);
          // Each exported mesh index is unique, so releasing the old geometry
          // is safe and keeps all-high mode from doubling GPU memory.
          object.geometry.dispose();
        }
        after -= group.length - 1;
        merged += group.length - 1;
      }
    }
    return { before, after, merged };
  }

  function disposeBatchGeometry(batchRoot) {
    batchRoot?.traverse((object) => object.geometry?.dispose?.());
  }

  function mergeChunkRenderBatch(root, chunkId, { initialBuild = false } = {}) {
    const previousBatch = root.userData.twinRenderBatchRoot;
    if (previousBatch) {
      root.remove(previousBatch);
      disposeBatchGeometry(previousBatch);
    }
    root.updateMatrixWorld(true);
    const inverseRoot = new THREE.Matrix4().copy(root.matrixWorld).invert();
    const instanceRoots = root.children.filter((object) => /^ue_[0-9a-f-]+$/i.test(object.name ?? ""));
    const groups = new Map();
    const sourceObjects = [];
    const worldCenter = new THREE.Vector3();
    const localCenter = new THREE.Vector3();
    const localSphere = new THREE.Sphere();
    const worldSpaceGeometry = root.userData.twinChunkVertexSpace === "world";
    for (const instanceRoot of instanceRoots) {
      const authoredInverse = instanceRoot.userData.twinAuthoredWorldMatrixInverse;
      const deltaRoot = worldSpaceGeometry
        ? (initialBuild
          ? new THREE.Matrix4()
          : new THREE.Matrix4().copy(instanceRoot.matrixWorld).multiply(authoredInverse || new THREE.Matrix4()))
        : null;
      instanceRoot.traverse((object) => {
        if (!object.isMesh || !object.geometry || object.userData.twinRenderBatch) return;
        const authoredVisible = object.userData.twinBatchSourceVisible ?? object.visible;
        object.userData.twinBatchSourceVisible = authoredVisible;
        if (Array.isArray(object.material)) return;
        sourceObjects.push(object);
        if (!authoredVisible || !instanceRoot.visible) return;
        const geometry = object.geometry.clone();
        // World-space chunk vertices must receive only the delta from the
        // authored snapshot pose. Legacy/local-space assets retain the old
        // object-world-matrix path.
        const relative = worldSpaceGeometry
          ? deltaRoot.clone()
          : inverseRoot.clone().multiply(object.matrixWorld);
        geometry.applyMatrix4(relative);
        // Partition by chunk-local spatial cell so each merged mesh keeps a
        // tight bounds volume. This is critical for views that only see part
        // of a large exhibition hall.
        if (!object.geometry.boundingSphere) object.geometry.computeBoundingSphere();
        localSphere.copy(object.geometry.boundingSphere);
        worldCenter.copy(localSphere.center).applyMatrix4(relative);
        localCenter.copy(worldCenter).applyMatrix4(inverseRoot);
        const cellX = Math.floor(localCenter.x / TWIN_BATCH_CELL_SIZE);
        const cellZ = Math.floor(localCenter.z / TWIN_BATCH_CELL_SIZE);
        const cellKey = `${cellX}:${cellZ}`;
        const key = `${cellKey}|${materialCompatibilityKey(object.material)}|${object.renderOrder}|${geometryCompatibilityKey(object.geometry)}`;
        if (!groups.has(key)) groups.set(key, []);
        groups.get(key).push({
          object,
          geometry,
          material: object.material,
          renderOrder: object.renderOrder,
          instanceId: instanceRoot.name,
        });
      });
    }
    const batchRoot = new THREE.Group();
    batchRoot.name = `OntoTwin_RenderBatch_${chunkId}`;
    batchRoot.userData.twinRenderBatch = true;
    let meshCount = 0;
    for (const entries of groups.values()) {
      let outputGeometries = entries.map((entry) => entry.geometry);
      if (entries.length > 1) {
        let mergedGeometry = null;
        try {
          mergedGeometry = BufferGeometryUtils.mergeGeometries(outputGeometries, false);
        } catch (error) {
          runtime.warnings.push(`Twin 全高清合批跳过 ${chunkId}：${error.message}`);
        }
        if (mergedGeometry) {
          outputGeometries.forEach((geometry) => geometry.dispose());
          outputGeometries = [mergedGeometry];
        }
      }
      for (const geometry of outputGeometries) {
        geometry.computeBoundingBox();
        geometry.computeBoundingSphere();
        const mesh = new THREE.Mesh(geometry, entries[0].material);
        mesh.name = `${batchRoot.name}_${meshCount}`;
        mesh.renderOrder = entries[0].renderOrder;
        mesh.frustumCulled = true;
        mesh.castShadow = false;
        mesh.receiveShadow = false;
        mesh.userData.twinRenderBatch = true;
        mesh.userData.twinPickInstanceIds = [...new Set(entries.map((entry) => entry.instanceId).filter(Boolean))];
        batchRoot.add(mesh);
        meshCount += 1;
      }
    }
    for (const object of sourceObjects) {
      object.visible = false;
      // Keep CPU-side attributes for a future live-delta rebuild, but release
      // the old GPU allocation so full-high mode does not double memory.
      object.geometry.dispose();
    }
    root.add(batchRoot);
    root.userData.twinRenderBatchRoot = batchRoot;
    return { meshCount, sourceCount: sourceObjects.length };
  }

  function scheduleChunkBatchRebuild(instanceId) {
    const chunkId = state.chunkByInstanceId.get(instanceId);
    const loaded = chunkId ? state.loadedChunks.get(chunkId) : null;
    if (!loaded?.root) return;
    if (state.chunkBatchTimers?.has(chunkId)) clearTimeout(state.chunkBatchTimers.get(chunkId));
    if (!state.chunkBatchTimers) state.chunkBatchTimers = new Map();
    const timer = window.setTimeout(() => {
      state.chunkBatchTimers.delete(chunkId);
      const rebuilt = mergeChunkRenderBatch(loaded.root, chunkId);
      state.collisionCatalogDirty = true;
      loaded.meshCount = rebuilt.meshCount;
      updateChunkMetrics();
    }, 80);
    state.chunkBatchTimers.set(chunkId, timer);
  }

  async function loadChunk(record) {
    if (state.loadedChunks.has(record.id) || state.pendingChunks.has(record.id)) return;
    state.pendingChunks.add(record.id);
    try {
      let gltf;
      let lastError;
      for (let attempt = 1; attempt <= TWIN_HIGH_LOAD_RETRIES; attempt += 1) {
        try {
          // A large .bin response can be interrupted on a busy local/X300
          // connection. Retry the same high-resolution asset before failing
          // the all-high gate; no low-resolution fallback is permitted.
          const suffix = attempt === 1 ? "" : `?retry=${attempt}`;
          gltf = await new GLTFLoader().loadAsync(`${TWIN_CHUNK_ROOT}${record.gltf}${suffix}`);
          break;
        } catch (error) {
          lastError = error;
          if (attempt < TWIN_HIGH_LOAD_RETRIES) await new Promise((resolve) => setTimeout(resolve, 180 * attempt));
        }
      }
      if (!gltf) throw lastError ?? new Error(`高清 Twin ${record.id} 加载失败`);
      const coplanarManifest = await fetch(`${TWIN_COPLANAR_ROOT}${record.id}.json`, { cache: "force-cache" })
        .then((response) => response.ok ? response.json() : null)
        .catch(() => null);
      const root = gltf.scene;
      root.name = `OntoTwin_${record.id}`;
      // batch_twin_chunks.mjs writes world-space vertices and identity roots.
      // This marker selects the single-transform path in mergeChunkRenderBatch.
      root.userData.twinChunkVertexSpace = "world";
      const coplanar = applyChunkCoplanarFixes(gltf, root, coplanarManifest, record.id);
      root.traverse((object) => {
        patchTwinTextureQuality(object);
        patchMainScreenVideo(object);
      });
      const mergeStats = mergeTwinMeshesByInstance(root);
      state.twinMergeStats.before += mergeStats.before;
      state.twinMergeStats.after += mergeStats.after;
      state.twinMergeStats.merged += mergeStats.merged;
      scene.add(root);
      state.collisionCatalogDirty = true;
      root.updateMatrixWorld(true);
      root.traverse((object) => {
        if (state.snapshots.has(object.name)) state.visualById.set(object.name, object);
      });
      for (const instanceId of record.instance_ids) {
        const snapshot = state.snapshots.get(instanceId);
        const visual = state.visualById.get(instanceId);
        if (snapshot && visual) {
          visual.userData.twinAuthoredWorldMatrix = matrixOfSnapshot(snapshot);
          visual.userData.twinAuthoredWorldMatrixInverse = visual.userData.twinAuthoredWorldMatrix.clone().invert();
        }
      }
      // Build the initial visible batch while exported roots are still at
      // identity. The vertex positions already contain their UE world pose.
      const batchStats = mergeChunkRenderBatch(root, record.id, { initialBuild: true });
      // Keep the source roots addressable for Twin updates; they are hidden
      // by the batch path, so this no longer moves the rendered batch.
      for (const instanceId of record.instance_ids) {
        const snapshot = state.snapshots.get(instanceId);
        if (snapshot) applySnapshotToVisual(snapshot);
      }
      state.twinBatchStats.source += batchStats.sourceCount;
      state.twinBatchStats.render += batchStats.meshCount;
      state.twinBatchStats.reduced += Math.max(0, batchStats.sourceCount - batchStats.meshCount);
      let meshCount = batchStats.meshCount;
      root.traverse((object) => {
        if (state.snapshots.has(object.name)) state.visualById.set(object.name, object);
        if (!object.isMesh || object.userData.twinBatchSourceVisible !== undefined || object.userData.twinRenderBatch) return;
        meshCount += 1;
        object.frustumCulled = true;
        object.castShadow = false;
        object.receiveShadow = false;
        if (!object.geometry.boundingSphere) object.geometry.computeBoundingSphere();
        const localSphere = object.geometry.boundingSphere?.clone();
        if (localSphere) state.detailRecords.push({ chunkId: record.id, object, localSphere, worldSphere: new THREE.Sphere(), baseVisible: object.visible });
      });
      state.loadedChunks.set(record.id, { root, meshCount, instanceIds: record.instance_ids, coplanar, batchStats });
      root.traverse((object) => {
        if (object.isMesh && object.userData?.twinRenderBatch && object.userData?.twinPickInstanceIds?.length) {
          state.twinPickMeshes.push(object);
        }
      });
      const totalChunks = state.chunkManifest?.chunks?.length ?? 0;
      if (totalChunks > 0) {
        const completedChunks = state.loadedChunks.size;
        const overallProgress = Math.round(70 + (completedChunks / totalChunks) * 30);
        const loadingDetail = document.querySelector("#loading-detail");
        const loadingProgressBar = document.querySelector("#loading-progress-bar");
        const loadingPercent = document.querySelector("#loading-percent");
        if (loadingDetail) loadingDetail.textContent = "模型加载完成后将启动当前项目配置的场景交互。";
        if (loadingProgressBar) loadingProgressBar.style.width = `${overallProgress}%`;
        if (loadingPercent) loadingPercent.textContent = `已处理 ${completedChunks} / ${totalChunks}    总体 ${overallProgress}%`;
      }
      const hlod = state.hlodChunks.get(record.id);
      if (hlod) hlod.root.visible = false;
      document.body.dataset.twinGeometry = "spatial-streaming";
      document.body.dataset.twinMeshBeforeMerge = String(state.twinMergeStats.before);
      document.body.dataset.twinMeshAfterMerge = String(state.twinMergeStats.after);
      document.body.dataset.twinDrawsReducedByMerge = String(state.twinMergeStats.merged);
      document.body.dataset.twinMergeMaterialGroups = String(state.twinMergeStats.materialGroups);
      document.body.dataset.twinMergeFailures = String(state.twinMergeStats.mergeFailures);
      document.body.dataset.twinBatchSourceMeshes = String(state.twinBatchStats.source);
      document.body.dataset.twinBatchRenderMeshes = String(state.twinBatchStats.render);
      document.body.dataset.twinBatchDrawsReduced = String(state.twinBatchStats.reduced);
      document.body.dataset.twinBatchCellSize = String(TWIN_BATCH_CELL_SIZE);
      updateChunkMetrics();
      updateTwinCoplanarMetrics();
    } finally {
      state.pendingChunks.delete(record.id);
    }
  }

  function unloadChunk(chunkId) {
    const loaded = state.loadedChunks.get(chunkId);
    if (!loaded) return;
    scene.remove(loaded.root);
    disposeObject(loaded.root);
    state.twinPickMeshes = state.twinPickMeshes.filter((mesh) => mesh?.parent && !loaded.root.getObjectById(mesh.id));
    state.collisionCatalogDirty = true;
    for (const instanceId of loaded.instanceIds) {
      const visual = state.visualById.get(instanceId);
      if (visual && loaded.root.getObjectById(visual.id)) state.visualById.delete(instanceId);
    }
    state.detailRecords = state.detailRecords.filter((record) => record.chunkId !== chunkId);
    state.loadedChunks.delete(chunkId);
    const hlod = state.hlodChunks.get(chunkId);
    if (hlod) hlod.root.visible = true;
    updateChunkMetrics();
    updateTwinCoplanarMetrics();
  }

  async function refreshChunkResidency(now, force = false, priorityInstanceId = null) {
    if (!state.chunkManifest || state.chunkRefreshPromise) return state.chunkRefreshPromise;
    if (!force && now - state.lastChunkRefreshAt < TWIN_CHUNK_REFRESH_MS
      && camera.position.distanceTo(state.lastChunkCameraPosition) < 5) return null;
    state.lastChunkRefreshAt = now;
    state.lastChunkCameraPosition.copy(camera.position);
    const ordered = [...state.chunkManifest.chunks].sort((left, right) => chunkDistance(left) - chunkDistance(right));
    const detailBudget = state.twinDetailMode === "all"
      ? ordered.length
      : TWIN_CHUNK_BUDGET;
    document.body.dataset.twinDetailBudget = String(detailBudget);
    const desired = ordered.slice(0, detailBudget).map((record) => record.id);
    const priorityChunk = priorityInstanceId ? state.chunkByInstanceId.get(priorityInstanceId) : null;
    if (priorityChunk && !desired.includes(priorityChunk)) desired[desired.length - 1] = priorityChunk;
    state.desiredChunks = new Set(desired);
    state.chunkRefreshPromise = (async () => {
      const records = desired
        .map((chunkId) => state.chunkManifest.chunks.find((item) => item.id === chunkId))
        .filter(Boolean);
      if (state.twinDetailMode === "all") {
        // Keep the full-resolution load moving without issuing twenty
        // simultaneous multi-hundred-megabyte requests. The loading mask keeps
        // the unfinished scene out of view until every chunk has arrived.
        let cursor = 0;
        const workers = Array.from({ length: Math.min(TWIN_HIGH_LOAD_WORKERS, records.length) }, async () => {
          while (cursor < records.length) {
            const record = records[cursor];
            cursor += 1;
            await loadChunk(record);
          }
        });
        await Promise.all(workers);
      } else {
        for (const record of records) await loadChunk(record);
      }
      // Never evict a model merely because the camera moved. All detailed
      // chunks remain resident, so moving the camera cannot cause a quality
      // transition or a low-resolution replacement.
      updateChunkMetrics();
    })().catch((error) => {
      const message = error?.message ?? String(error);
      runtime.warnings.push(`Twin 空间块加载失败：${message}`);
      document.body.dataset.twinLoadError = message;
      document.body.dataset.twinGeometry = "error";
      document.body.dataset.twinHighReady = "false";
    }).finally(() => {
      state.chunkRefreshPromise = null;
    });
    return state.chunkRefreshPromise;
  }

  function restoreAuthoredTwinVisibility() {
    let visible = 0;
    for (const record of state.detailRecords) {
      record.object.visible = record.baseVisible;
      if (record.baseVisible) visible += 1;
    }
    document.body.dataset.twinDetailVisible = String(visible);
  }

  async function initializeChunkStreaming() {
    ui.source.textContent = state.twinDetailMode === "all"
      ? "读取 Twin 空间块（准备全量高清）"
      : "读取 Twin 空间块";
    try {
      const [response, coplanarSummaryResponse] = await Promise.all([
        fetch(TWIN_CHUNK_MANIFEST_URL, { cache: "no-store" }),
        fetch(TWIN_COPLANAR_SUMMARY_URL, { cache: "force-cache" }),
      ]);
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      state.chunkManifest = await response.json();
      if (coplanarSummaryResponse.ok) {
        const summary = await coplanarSummaryResponse.json();
        document.body.dataset.twinCoplanarCatalogTargets = String(summary.offset_target_count);
        document.body.dataset.twinCoplanarCatalogConflicts = String(summary.conflict_pair_count);
        document.body.dataset.twinCoplanarScannedTriangles = String(summary.scanned_triangles);
      }
      for (const record of state.chunkManifest.chunks) {
        for (const instanceId of record.instance_ids) state.chunkByInstanceId.set(instanceId, record.id);
      }
      document.body.dataset.twinChunkCount = String(state.chunkManifest.chunk_count);
      document.body.dataset.twinDetailMode = state.twinDetailMode;
      THREE.Cache.enabled = true;
      const requestedTwinFocus = new URLSearchParams(location.search).get("twinFocus");
      await refreshChunkResidency(performance.now(), true, requestedTwinFocus);
      document.body.dataset.twinHighReady = String(state.loadedChunks.size === state.chunkManifest.chunks.length);
      const loadingDetail = document.querySelector("#loading-detail");
      const loadingPercent = document.querySelector("#loading-percent");
      if (loadingDetail) loadingDetail.textContent = "数据模型已就绪，正在进入场景。";
      if (loadingPercent) loadingPercent.textContent = `已加载 ${state.loadedChunks.size} / ${state.chunkManifest.chunks.length}    100%`;
      if (requestedTwinFocus && state.snapshots.has(requestedTwinFocus)) {
        selectInstance(requestedTwinFocus);
        focusSelected();
      }
    } catch (error) {
      document.body.dataset.twinLoadError = error?.message ?? String(error);
      document.body.dataset.twinGeometry = "error";
      document.body.dataset.twinHighReady = "false";
      runtime.warnings.push(`Twin 高清空间块清单未加载：${error.message}`);
      ui.source.textContent = "高清 Twin 清单不可用";
    }
  }

  async function fetchLive(cursor = null) {
    const url = cursor ? `${SNAPSHOT_CHANGES_URL}?cursor=${encodeURIComponent(cursor)}` : SNAPSHOT_CHANGES_URL;
    const response = await fetch(url, { headers: PROJECT_HEADERS, cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  }

  async function loadInitialSnapshots() {
    try {
      const payload = await fetchLive();
      applyDelta(payload);
      setConnection("live", `实时 · rev ${payload.revision ?? "-"}`);
    } catch (liveError) {
      const response = await fetch(BASELINE_URL, { cache: "no-store" });
      if (!response.ok) throw new Error(`Twin 实时接口和本地基线都不可用：${liveError.message} / HTTP ${response.status}`);
      const baseline = await response.json();
      applyDelta(Array.isArray(baseline) ? baseline : (baseline.snapshots ?? baseline.upserts ?? []));
      setConnection("baseline", "离线基线");
    }
  }

  async function poll() {
    if (state.stopped) return;
    try {
      const payload = await fetchLive(state.cursor);
      applyDelta(payload);
      setConnection("live", `实时 · rev ${payload.revision ?? "-"}`);
    } catch {
      setConnection(state.snapshots.size ? "baseline" : "loading", "后端重连中");
    } finally {
      state.pollTimer = window.setTimeout(poll, state.cursor ? POLL_INTERVAL_MS : 3000);
    }
  }

  async function loadRoamingRuntimeConfig() {
    try {
      const response = await fetch(RUNTIME_CONFIG_URL, {
        headers: PROJECT_HEADERS,
        cache: "no-store",
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const payload = await response.json();
      const config = payload?.config ?? {};
      const cameraConfig = config.camera ?? {};
      const firstPerson = cameraConfig.first_person ?? {};
      state.runtimeConfig = config;
      state.runtimeRevision = Number.isFinite(Number(payload?.revision)) ? Number(payload.revision) : null;
      const backendCharacters = payload?.resources?.available_characters ?? [];
      state.availableCharacters = backendCharacters
        .filter((item) => PACKAGED_CHARACTER_IDS.has(item?.id))
        .sort((left, right) => PACKAGED_CHARACTER_ORDER.get(left.id) - PACKAGED_CHARACTER_ORDER.get(right.id));
      document.body.dataset.roamingCharacterCatalog = `${state.availableCharacters.length}/${backendCharacters.length}`;
      if (!state.availableCharacters.length) {
        throw new Error("后端没有与当前 Windows 包匹配的 RenderPeople 人物资源");
      }
      state.availableRoutes = payload?.available_routes ?? [];
      state.activeCharacterId = config.character_id ?? payload?.resources?.character?.id ?? null;
      if (!PACKAGED_CHARACTER_IDS.has(state.activeCharacterId)) {
        state.activeCharacterId = state.availableCharacters[0].id;
      }
      state.activeSkinId = config.default_skin_id ?? payload?.resources?.skins?.[0]?.id ?? null;
      state.activeRouteId = config.route?.route_id ?? payload?.runtime_route?.route_id ?? null;
      state.cameraMode = ["god", "near_follow", "first_person"].includes(cameraConfig.default_mode)
        ? cameraConfig.default_mode : "first_person";
      state.eyeHeight = Number(firstPerson.eye_height_cm ?? 165) * 0.01;
      state.firstPersonFov = Number(firstPerson.fov_deg ?? 85);
      state.lookSensitivity = Number(firstPerson.look_sensitivity ?? 1);
      state.nearLookSensitivity = Number(cameraConfig.near_follow?.look_sensitivity ?? 0.8);
      state.characterCapsuleHalfHeight = Number(payload?.resources?.character?.capsule?.half_height_cm ?? 88) * 0.01;
      state.routeNarrationEnabled = config.route?.enabled !== false;
      const runtimeRoute = payload?.runtime_route;
      if (runtimeRoute?.route_id && runtimeRoute.route_id === state.activeRouteId) setActiveRoute(runtimeRoute);
      else setActiveRoute(state.availableRoutes.find((route) => route.route_id === state.activeRouteId));
      populateRuntimeControls();
      document.body.dataset.roamingConfig = "live";
      document.body.dataset.roamingDefaultMode = state.cameraMode;
      document.body.dataset.roamingAutoEnter = String(config.auto_enter === true);
      return config;
    } catch (error) {
      runtime.warnings.push(`漫游运行配置未连接，使用第一人称离线默认值：${error.message}`);
      state.runtimeConfig = {
        auto_enter: true,
        camera: { default_mode: "first_person" },
      };
      // Keep the offline path on the same six packaged, textured characters;
      // a missing backend must not resurrect the old procedural roamer.
      state.availableCharacters = LOCAL_CHARACTER_CATALOG.map((item) => ({
        ...item,
        skins: item.skins.map((skin) => ({ ...skin })),
      }));
      state.activeCharacterId = PACKAGED_CHARACTER_IDS.has(state.activeCharacterId)
        ? state.activeCharacterId
        : state.availableCharacters[0].id;
      state.activeSkinId = state.availableCharacters.find((item) => item.id === state.activeCharacterId)?.default_skin_id
        ?? state.availableCharacters[0].default_skin_id;
      state.availableRoutes = [];
      state.activeRouteId = null;
      document.body.dataset.roamingCharacterCatalog = `${state.availableCharacters.length}/local`;
      document.body.dataset.roamingConfig = "offline-first-person";
      populateRuntimeControls();
      return state.runtimeConfig;
    }
  }

  function routePointToThree(waypoint) {
    const rawPoint = waypoint?.position_ue_cm ?? waypoint?.position;
    // The UE endpoint has emitted both JSON arrays and legacy whitespace
    // strings over its lifetime.  Accept both so a route switch never turns
    // into an empty route just because the backend serializer changed shape.
    const point = Array.isArray(rawPoint)
      ? rawPoint
      : typeof rawPoint === "string"
        ? rawPoint.trim().split(/[\s,]+/).map(Number)
        : rawPoint && typeof rawPoint === "object"
          ? [rawPoint.x ?? rawPoint.X, rawPoint.y ?? rawPoint.Y, rawPoint.z ?? rawPoint.Z]
          : null;
    if (!point || point.length < 3 || point.some((value) => !Number.isFinite(Number(value)))) return null;
    return {
      id: waypoint.waypoint_id ?? waypoint.id ?? `wp-${state.routeWaypoints.length}`,
      position: new THREE.Vector3(Number(point[0]) * 0.01, Number(point[2] ?? 0) * 0.01, Number(point[1]) * 0.01),
      narration: waypoint.narration ?? null,
    };
  }

  function setActiveRoute(route) {
    state.routeCompleted = false;
    if (!route) {
      state.routeWaypoints = [];
      state.activeRouteId = null;
      state.routeWaypointIndex = 0;
      return;
    }
    state.activeRouteId = route.route_id ?? route.id ?? state.activeRouteId;
    const rawWaypoints = route.waypoints ?? (route.waypoints_ue_cm ?? []).map((position_ue_cm, index) => ({ position_ue_cm, waypoint_id: `wp-${index + 1}` }));
    state.routeWaypoints = rawWaypoints.map(routePointToThree).filter(Boolean);
    state.routeWaypointIndex = Math.min(state.routeWaypointIndex, Math.max(0, state.routeWaypoints.length - 1));
    drawMinimap();
  }

  function populateRuntimeControls() {
    const selectOptions = (select, items, getValue, getLabel, active) => {
      if (!select) return;
      select.replaceChildren();
      for (const item of items) {
        const option = document.createElement("option");
        option.value = getValue(item);
        option.textContent = getLabel(item);
        option.selected = option.value === active;
        select.append(option);
      }
      select.disabled = !items.length;
    };
    selectOptions(ui.characterSelector, state.availableCharacters,
      (item) => item.id, (item) => item.display_name || item.id, state.activeCharacterId);
    selectOptions(ui.routeSelector, state.availableRoutes.filter((item) => item.enabled !== false),
      (item) => item.route_id || item.id, (item) => `${item.display_name || item.name || item.id} · ${item.waypoint_count ?? item.waypoints?.length ?? 0} 点`, state.activeRouteId);
    ui.cameraButtons.forEach((button) => button.classList.toggle("is-active", button.dataset.cameraMode === state.cameraMode));
    updateRouteStateText();
  }

  function updateRouteStateText(message = null) {
    updateHudStatus();
    if (!ui.routeState) return;
    const route = state.availableRoutes.find((item) => (item.route_id || item.id) === state.activeRouteId);
    const label = route?.display_name || route?.name || state.activeRouteId || "未选择路线";
    const stateLabel = state.routeNarrationActive
      ? `解说中（${state.routeNarrationSegmentIndex + 1}/${Math.max(1, state.routeNarrationSegments.length)}）`
      : state.routePlaying
        ? `前往第 ${Math.min(state.routeWaypointIndex + 1, state.routeWaypoints.length)}/${state.routeWaypoints.length} 点`
        : state.routePausedByUser
          ? "已暂停 · WASD 接管"
          : "已暂停";
    ui.routeState.textContent = message || `${label} · ${stateLabel}`;
    document.body.dataset.routePlaying = String(state.routePlaying);
    document.body.dataset.routePausedByUser = String(state.routePausedByUser);
    document.body.dataset.routeNarrationActive = String(state.routeNarrationActive);
    document.body.dataset.routeWaypointIndex = String(state.routeWaypointIndex);
    document.body.dataset.routeWaypointCount = String(state.routeWaypoints.length);
    updateHudShortcuts();
  }

  function compactHudLabel(value, fallback, maxCharacters) {
    const text = String(value ?? "").trim() || fallback;
    const characters = Array.from(text);
    return characters.length > maxCharacters
      ? `${characters.slice(0, maxCharacters - 1).join("")}…`
      : text;
  }

  function updateHudStatus() {
    if (!ui.runtimeStatusText) return;
    const character = state.availableCharacters.find((item) => item.id === state.activeCharacterId);
    const characterLabel = compactHudLabel(character?.display_name, "漫游人物", 16);
    const viewLabel = state.cameraMode === "god"
      ? "上帝视角"
      : state.cameraMode === "first_person" ? "第一人称" : "过肩视角";
    const route = state.availableRoutes.find((item) => (item.route_id || item.id) === state.activeRouteId);
    const routeLabel = compactHudLabel(route?.display_name || route?.name, "当前线路", 24);
    const routeStatus = state.routeNarrationActive
      ? `路线解说 - ${routeLabel}`
      : state.routePlaying
        ? `线路漫游 - ${routeLabel}`
        : state.routeCompleted
          ? `线路已完成 - ${routeLabel}`
          : "自由漫游";
    ui.runtimeStatusText.textContent = `${characterLabel}   ·   ${viewLabel}   ·   ${routeStatus}`;
  }

  // Keep the compact HUD hints in lock-step with the native plugin's
  // GetHudShortcutItems() state machine.  The old web page showed one static
  // list, which was misleading in God view and omitted the E/Space actions.
  function updateHudShortcuts() {
    if (!ui.shortcuts) return;
    const items = [];
    if (editorIsActive() || state.editMode) {
      items.push(["方向键", "移动"], ["PageUp/Down", "升降"], ["F10", "退出编辑"]);
    } else if (!state.roaming) {
      items.push(["左键", "选择并打开网页详情"], ["Tab", "Web 业务"], ["F7", "进入漫游"]);
    } else if (state.cameraMode === "god") {
      items.push(["WASD", "平移"], ["右键", "按住观察"], ["Q / E", "升降"], ["滚轮", "调整速度"]);
      if (state.routePlaying) items.push(["P", "暂停路线"]);
      else if (state.routePausedByUser) items.push(["P", "继续路线"]);
      else if (state.routeNarrationActive) items.push(["P", state.routePauseAfterNarration ? "解说后继续" : "解说后暂停"]);
      items.push(["左键", "选择并打开网页详情"], ["V", "切换视角"], ["Tab", "更多操作"], ["F7", "退出漫游"]);
    } else if (state.cameraMode === "near_follow") {
      items.push(["WASD", "移动"], ["右键", "按住观察"], ["Q / E", "视角升降"], ["左键", "选择详情"], ["Space", "跳跃"]);
      if (state.routePlaying) items.push(["P", "暂停路线"]);
      else if (state.routePausedByUser) items.push(["P", "继续路线"], ["R", "返回线路"]);
      else if (state.routeNarrationActive) items.push(["P", state.routePauseAfterNarration ? "解说后继续" : "解说后暂停"]);
      items.push(["V", "切换视角"], ["Tab", "更多操作"], ["F7", "退出漫游"]);
    } else {
      items.push(["WASD", "移动"], ["右键", "按住观察"], ["左键", "选择详情"], ["E", "准星选择"], ["Space", "跳跃"]);
      if (state.routePlaying) items.push(["P", "暂停路线"]);
      else if (state.routePausedByUser) items.push(["P", "继续路线"], ["R", "返回线路"]);
      else if (state.routeNarrationActive) items.push(["P", state.routePauseAfterNarration ? "解说后继续" : "解说后暂停"]);
      items.push(["V", "切换视角"], ["Tab", "更多操作"], ["F7", "退出漫游"]);
    }
    ui.shortcuts.replaceChildren(...items.map(([key, description]) => {
      const row = document.createElement("span");
      const chip = document.createElement("kbd");
      chip.textContent = key;
      row.append(chip, ` ${description}`);
      return row;
    }));
  }

  async function persistRuntimeConfig(changes) {
    const current = state.runtimeConfig ?? {};
    const next = JSON.parse(JSON.stringify({
      enabled: current.enabled !== false,
      auto_enter: current.auto_enter === true,
      character_id: state.activeCharacterId ?? current.character_id,
      default_skin_id: state.activeSkinId ?? current.default_skin_id,
      allowed_skin_ids: current.allowed_skin_ids ?? (state.activeSkinId ? [state.activeSkinId] : []),
      camera: current.camera ?? { default_mode: state.cameraMode },
      movement: current.movement ?? {},
      minimap: current.minimap ?? { enabled: true },
      route: current.route ?? { enabled: true, auto_start: false, takeover_enabled: true, completion_mode: "stop" },
      ...changes,
    }));
    if (changes.camera) next.camera = { ...(current.camera ?? {}), ...changes.camera };
    if (changes.route) next.route = { ...(current.route ?? {}), ...changes.route };
    try {
      const response = await fetch("/twin-api/api/v2/scene-interactions/roaming", {
        method: "PUT",
        headers: { ...PROJECT_HEADERS, "Content-Type": "application/json" },
        body: JSON.stringify({ config: next, expected_revision: state.runtimeRevision }),
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const result = await response.json();
      state.runtimeRevision = Number(result.revision ?? state.runtimeRevision);
      state.runtimeConfig = result.config ?? next;
      return true;
    } catch (error) {
      runtime.warnings.push(`漫游配置未写回（本地继续运行）：${error.message}`);
      return false;
    }
  }

  function makeCharacter() {
    const character = new THREE.Group();
    character.name = "WebRoamingCharacter";
    const fallback = new THREE.Group();
    fallback.name = "RoamerFallback";
    const bodyMaterial = new THREE.MeshBasicMaterial({ color: 0x52d9f5 });
    const trimMaterial = new THREE.MeshBasicMaterial({ color: 0x0a2530 });
    const body = new THREE.Mesh(new THREE.CapsuleGeometry(0.28, 0.72, 4, 8), bodyMaterial);
    body.position.y = 0.8;
    const head = new THREE.Mesh(new THREE.SphereGeometry(0.2, 10, 8), trimMaterial);
    head.position.y = 1.55;
    const direction = new THREE.Mesh(new THREE.ConeGeometry(0.13, 0.34, 8), bodyMaterial);
    direction.rotation.x = Math.PI / 2;
    direction.position.set(0, 1.1, -0.35);
    fallback.add(body, head, direction);
    character.add(fallback);
    const spawn = state.runtimeConfig?.spawn_ue;
    if (spawn) {
      const spawnYaw = THREE.MathUtils.degToRad(Number(spawn.yaw_deg ?? 0));
      character.position.set(
        Number(spawn.x_cm ?? 0) * 0.01,
        Number(spawn.z_hint_cm ?? 0) * 0.01,
        Number(spawn.y_cm ?? 0) * 0.01,
      );
      // UE yaw is measured from +X in the XY plane.  The converted scene
      // uses +Z as the character's authored forward axis, so the actor root
      // is π/2 - UEYaw and a Three.js camera yaw is -(UEYaw + π/2).
      character.rotation.y = Math.PI / 2 - spawnYaw;
      state.yaw = -spawnYaw - Math.PI / 2;
    } else {
      character.position.set(startupPosition.x, Math.max(0, startupPosition.y - 1.65), startupPosition.z);
      const initialDirection = new THREE.Vector3();
      camera.getWorldDirection(initialDirection);
      state.yaw = Math.atan2(-initialDirection.x, -initialDirection.z);
    }
    state.jumpGroundY = character.position.y;
    state.jumpVelocity = 0;
    character.visible = false;
    scene.add(character);
    state.character = character;
    loadCharacterVisual(state.activeCharacterId).catch((error) => {
      document.body.dataset.roamerVisual = "fallback";
      document.body.dataset.roamerError = error?.stack || error?.message || String(error);
      runtime.warnings.push(`漫游人物加载失败：${error.message}`);
    });
  }

  function retargetRenderPeopleClip(clip) {
    // RenderPeople's UE export stores locomotion in the pelvis translation.
    // The browser moves the character actor root itself, so applying that
    // track a second time makes the avatar jump hundreds of centimetres or
    // disappear from the camera. Keep bone rotations/scales for the authored
    // pose but remove root/pelvis translation and scale tracks.
    const tracks = (clip?.tracks ?? []).filter((track) => {
      const name = String(track.name || "").toLowerCase();
      const rootTrack = /(?:^|[.\[])(?:root|pelvis)(?:\]|[.])/.test(name);
      const translationOrScale = /(?:position|translation|scale)$/.test(name);
      return !(rootTrack && translationOrScale);
    });
    const normalized = new THREE.AnimationClip(clip.name, clip.duration, tracks);
    normalized.userData = { ...(clip.userData ?? {}), zhhzRetargeted: true, sourceTrackCount: clip.tracks.length };
    return normalized;
  }

  async function loadCharacterVisual(characterId = state.activeCharacterId) {
    const character = state.character;
    if (!character) return;
    const token = ++state.characterLoadToken;
    const fallback = character.getObjectByName("RoamerFallback");
    const catalogItem = state.availableCharacters.find((item) => item.id === characterId);
    const slug = String(characterId || "roamer").replace(/[^a-z0-9._-]+/gi, "-");
    // Every catalog character has its own exported mesh.  Sophia used to be
    // hard-coded to the placeholder because the first package only contained
    // roamer.gltf; keep the placeholder as a final fallback for stale packs.
    const candidates = characterId
      ? [`/data/character/${slug}.gltf?build=20260903-final-windows-characters-v5`, "/data/character/roamer.gltf"]
      : ["/data/character/roamer.gltf"];
    let gltf = null;
    let loadedUrl = candidates[candidates.length - 1];
    let lastError = null;
    for (const url of candidates) {
      try {
        gltf = await new GLTFLoader().loadAsync(url);
        loadedUrl = url;
        break;
      } catch (error) { lastError = error; }
    }
    if (!gltf) throw lastError || new Error("没有可用人物资源");
    if (token !== state.characterLoadToken) return;
    if (state.characterVisual) {
      character.remove(state.characterVisual);
      disposeObject(state.characterVisual);
      state.characterVisual = null;
    }
    state.characterMixer?.stopAllAction?.();
    state.characterMixer = null;
    state.characterActions.clear();
    state.characterActionKey = "";
    const visual = gltf.scene;
    // Avoid Box3.setFromObject on an animated SkinnedMesh: Three.js computes
    // deformed bounds through applyBoneTransform, but UE exports may omit a
    // helper bone. Geometry bounds are sufficient for character normalization.
    visual.updateMatrixWorld(true);
    const box = new THREE.Box3();
    visual.traverse((object) => {
      if (!object.isMesh || !object.geometry) return;
      if (!object.geometry.boundingBox) object.geometry.computeBoundingBox();
      if (object.geometry.boundingBox) {
        box.union(object.geometry.boundingBox.clone().applyMatrix4(object.matrixWorld));
      }
    });
    const size = box.getSize(new THREE.Vector3());
    if (size.y > 0.01) visual.scale.setScalar(1.72 / size.y);
    visual.updateMatrixWorld(true);
    const normalized = new THREE.Box3();
    visual.traverse((object) => {
      if (!object.isMesh || !object.geometry) return;
      if (!object.geometry.boundingBox) object.geometry.computeBoundingBox();
      if (object.geometry.boundingBox) {
        normalized.union(object.geometry.boundingBox.clone().applyMatrix4(object.matrixWorld));
      }
    });
    visual.userData.baseY = -normalized.min.y;
    visual.position.y = visual.userData.baseY;
    // Keep the authored UE mesh orientation.  TwinRoamingCharacter records a
    // -90° MeshYawOffsetDeg in UE's XY basis, but the browser runtime already
    // converted the scene to Three.js XZ.  Applying that value a second time
    // turns the avatar sideways.  The equivalent browser-space default is
    // therefore 0°; the URL probe above is retained for visual QA only.
    const authoredMeshYaw = Number(catalogItem?.mesh_yaw_offset_deg);
    // The UE exporter stores the RenderPeople offset in UE's XY basis while
    // the browser scene uses Three.js XZ.  Keep the catalog value as the
    // production default, but allow a URL-only probe while validating a new
    // package against the UE shoulder reference (never written to runtime
    // config and therefore harmless for the shipped default).
    const requestedMeshYaw = Number(new URLSearchParams(location.search).get("meshYaw"));
    const meshYawOffsetDeg = Number.isFinite(requestedMeshYaw)
      ? requestedMeshYaw
      : (PACKAGED_CHARACTER_IDS.has(characterId)
        ? 0
        : (Number.isFinite(authoredMeshYaw) ? authoredMeshYaw : 0));
    visual.rotation.y = THREE.MathUtils.degToRad(meshYawOffsetDeg);
    document.body.dataset.roamerMeshYawOffset = String(meshYawOffsetDeg);
    visual.traverse((object) => {
      if (!object.isMesh) return;
      object.castShadow = false;
      object.receiveShadow = false;
      // Animated SkinnedMesh bounds from the UE export do not follow every
      // retargeted pose reliably. Six tiny avatar meshes are cheaper and more
      // correct to keep uncullable than letting the shoulder camera lose the
      // character when the stale bind-pose sphere leaves the frustum.
      object.frustumCulled = false;
    });
    const poseBoneNames = [
      "pelvis", "spine_02", "spine_03",
      "upperarm_l", "lowerarm_l", "upperarm_r", "lowerarm_r",
      "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r",
    ];
    state.characterPose = new Map();
    for (const name of poseBoneNames) {
      const bone = visual.getObjectByName(name);
      if (bone) state.characterPose.set(name, { bone, rest: bone.quaternion.clone() });
    }
    document.body.dataset.roamerAnimationClipCount = String(gltf.animations?.length ?? 0);
    document.body.dataset.roamerParserAnimationCount = String(gltf.parser?.json?.animations?.length ?? -1);
    document.body.dataset.roamerLoadedCharacterId = String(characterId || "");
    // Keep the browser-side procedural locomotion as the production default.
    // The UE-exported RenderPeople quaternion tracks are component-space
    // values (not Three.js local-bone values); applying them directly causes
    // the exact collapsed/rotated limbs seen in the failed preview. The clips
    // remain available behind ?nativeAnim=1 for a future UE-side retargeted
    // export, while the shipped path is stable and keeps the authored bind
    // pose (arms/legs on their correct sides) with lightweight walk motion.
    const useNativeGltfAnimation = new URLSearchParams(location.search).get("nativeAnim") === "1";
    const normalizedClips = useNativeGltfAnimation && Array.isArray(gltf.animations)
      ? gltf.animations.map(retargetRenderPeopleClip).filter((clip) => clip.tracks.length)
      : [];
    document.body.dataset.roamerAnimationSourceTracks = String(gltf.animations?.reduce((sum, clip) => sum + clip.tracks.length, 0) ?? 0);
    document.body.dataset.roamerAnimationTracks = String(normalizedClips.reduce((sum, clip) => sum + clip.tracks.length, 0));
    if (normalizedClips.length) {
      state.characterMixer = new THREE.AnimationMixer(visual);
      for (const [index, clip] of normalizedClips.entries()) {
        const key = String(clip.name || `clip-${index}`).toLowerCase();
        state.characterActions.set(key, state.characterMixer.clipAction(clip));
      }
      state.characterAnimationMode = "gltf-animation-retargeted-debug";
      const idle = [...state.characterActions.entries()].find(([name]) => /idle|idling|stand|breath|wait|待机/.test(name));
      const action = idle?.[1] ?? state.characterActions.values().next().value;
      if (action) {
        action.reset().fadeIn(0.2).play();
        state.characterActionKey = idle?.[0] ?? state.characterActions.keys().next().value ?? "";
      }
    } else {
      state.characterAnimationMode = "procedural-walk-v2";
    }
    fallback.visible = false;
    character.add(visual);
    state.characterVisual = visual;
    document.body.dataset.roamerVisual = catalogItem?.id || "fallback";
    document.body.dataset.roamerAsset = loadedUrl;
    document.body.dataset.roamerAnimation = state.characterAnimationMode;
  }

  /* Legacy loader body retained below only as a guard for old cached bundles. */
  /*
    new GLTFLoader().loadAsync("/data/character/roamer.gltf").then((gltf) => {
      const visual = gltf.scene;
      const box = new THREE.Box3().setFromObject(visual);
      const size = box.getSize(new THREE.Vector3());
      if (size.y > 0.01) visual.scale.setScalar(1.72 / size.y);
      visual.updateMatrixWorld(true);
      const normalized = new THREE.Box3().setFromObject(visual);
      visual.userData.baseY = -normalized.min.y;
      visual.position.y = visual.userData.baseY;
      visual.traverse((object) => {
        if (!object.isMesh) return;
        object.castShadow = false;
        object.receiveShadow = false;
        object.frustumCulled = true;
      });
      const poseBoneNames = [
        "pelvis", "spine_02", "spine_03",
        "upperarm_l", "lowerarm_l", "upperarm_r", "lowerarm_r",
        "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r",
      ];
      state.characterPose = new Map();
      for (const name of poseBoneNames) {
        const bone = visual.getObjectByName(name);
        if (bone) state.characterPose.set(name, { bone, rest: bone.quaternion.clone() });
      }
      fallback.visible = false;
      character.add(visual);
      state.characterVisual = visual;
      document.body.dataset.roamerVisual = "renderpeople-sophia";
      document.body.dataset.roamerAnimation = "procedural-walk-v1";
    }).catch((error) => {
      document.body.dataset.roamerVisual = "fallback";
      runtime.warnings.push(`RenderPeople 漫游人物加载失败：${error.message}`);
    });
  */

  function setCameraMode(mode) {
    const previousMode = state.cameraMode;
    state.cameraMode = ["god", "near_follow", "first_person"].includes(mode) ? mode : "first_person";
    if (state.cameraMode === "near_follow" && previousMode !== "near_follow") {
      state.shoulderYawOffset = 0;
      state.shoulderPitch = 0;
    }
    if (state.cameraMode !== "near_follow") state.shoulderLookActive = false;
    const firstPerson = state.cameraMode === "first_person";
    state.character.visible = state.roaming && !firstPerson;
    // UE's person-camera implementation uses the configured first-person FOV
    // and a fixed 90-degree near-follow FOV. God view keeps the authored
    // camera lens rather than inheriting the shoulder-camera value.
    camera.fov = firstPerson
      ? state.firstPersonFov
      : state.cameraMode === "near_follow" ? 90 : state.startupCameraFov;
    camera.updateProjectionMatrix();
    document.body.classList.toggle("is-first-person", firstPerson);
    document.body.dataset.roamingCameraMode = state.cameraMode;
    document.body.classList.toggle("is-crosshair-enabled", state.crosshairEnabled && firstPerson);
    if (ui.runtimeCrosshair) {
      // UE disables the first-person crosshair command outside first person
      // (the toggle state is retained, but the command is not actionable).
      ui.runtimeCrosshair.disabled = !state.roaming || !firstPerson;
      ui.runtimeCrosshair.classList.toggle("is-active", state.crosshairEnabled && firstPerson);
      ui.runtimeCrosshair.setAttribute("aria-pressed", String(state.crosshairEnabled));
    }
    updateHudStatus();
    ui.cameraButtons.forEach((button) => {
      button.disabled = !state.roaming;
      button.classList.toggle("is-active", button.dataset.cameraMode === state.cameraMode);
    });
    [ui.cycleSkin, ui.resumeRoute, ui.restartRoute, ui.reloadCharacter]
      .filter(Boolean)
      .forEach((button) => { button.disabled = !state.roaming; });
    controls.enabled = state.roaming && state.cameraMode === "god";
    updateHudShortcuts();
  }

  function toggleCameraMode() {
    if (!state.roaming) return;
    const modes = ["first_person", "near_follow", "god"];
    setCameraMode(modes[(modes.indexOf(state.cameraMode) + 1) % modes.length]);
  }

  function toggleRoaming(force = null) {
    state.roaming = force === null ? !state.roaming : Boolean(force);
    // UE's F7 transition releases the HUD interaction capture before either
    // entering or leaving roaming.  A visible RuntimeDock therefore closes
    // as part of the same transition.
    if (ui.panel.classList.contains("is-open")) setRuntimeDockOpen(false);
    if (!state.roaming) {
      stopNarration();
      state.routePlaying = false;
      state.routePausedByUser = false;
      state.routeCompleted = false;
      state.minimapPreview = null;
      state.minimapHistory = [];
      setRuntimeMinimapOpen(false);
      if (ui.runtimeMinimapUndo) ui.runtimeMinimapUndo.hidden = true;
      setMinimapStatus("");
      // Match UE ActivateRuntimeHome/ExitRoaming: restore the authored
      // startup camera instead of leaving a first-person/shoulder pose behind.
      camera.position.copy(state.startupCameraPosition);
      controls.target.copy(state.startupCameraTarget);
      camera.fov = state.startupCameraFov;
      camera.near = state.startupCameraNear;
      camera.far = state.startupCameraFar;
      camera.updateProjectionMatrix();
      controls.update();
    } else if (state.runtimeConfig?.minimap?.enabled !== false) {
      // The native UOntoTwinMinimapWidget starts expanded for every roaming
      // session. Keep the 320px map visible until the user collapses it.
      setRuntimeMinimapOpen(true);
    }
    setCameraMode(state.cameraMode);
    controls.enabled = !state.roaming || state.cameraMode === "god";
    ui.roamToggle.classList.toggle("is-active", state.roaming);
    ui.roamToggle.textContent = state.roaming ? "退出人物漫游" : "进入人物漫游";
    ui.controlHelp.textContent = state.roaming
      ? state.cameraMode === "near_follow"
        ? "WASD 移动 · Q/E 视角升降 · Shift 加速 · V 切换视角 · P 暂停/继续路线 · R 返回线路"
        : "WASD 移动 · Shift 加速 · V 切换视角 · E 准星选择 · P 暂停/继续路线 · R 返回线路"
      : "左键旋转 · 右键平移 · 滚轮缩放";
    document.body.classList.toggle("is-roaming", state.roaming);
    document.body.dataset.roaming = String(state.roaming);
    if (!state.roaming && document.pointerLockElement === renderer.domElement) document.exitPointerLock?.();
    updateRouteStateText();
  }

  function toggleRoutePause() {
    if (!state.roaming || !state.routeWaypoints.length) return;
    if (state.routeNarrationActive) {
      // UE's P shortcut interrupts the current narration and leaves the
      // character paused at the next waypoint.
      stopNarration({ skipRoute: true });
      updateRouteStateText("解说已跳过 · 路线已暂停");
      return;
    }
    if (state.routePlaying) {
      state.routePlaying = false;
      state.routePausedByUser = true;
      updateRouteStateText("路线已暂停");
    } else {
      resumeRoute();
    }
    drawMinimap();
  }

  function activateRuntimeHome() {
    // Home is a real state transition in the UE plugin, not just a panel tab.
    toggleRoaming(false);
    // The UE manager exits roaming and closes the RuntimeDock when returning
    // to the authored startup view; keep the web state transition identical.
    setRuntimeDockOpen(false);
    state.routePlaying = false;
    state.routePausedByUser = false;
    stopNarration();
    state.minimapPreview = null;
    state.minimapHistory = [];
    setRuntimeMinimapOpen(false);
    if (ui.runtimeMinimapUndo) ui.runtimeMinimapUndo.hidden = true;
    setMinimapStatus("");
    state.spacePath = [];
    state.businessScopeCurrent = false;
    state.businessType = null;
    if (ui.businessScope) ui.businessScope.textContent = "当前：全部空间";
    selectInstance(null);
    document.body.dataset.runtimeHomeActivated = "true";
    updateRouteStateText("已返回主页");
    renderRuntimePanels();
  }

  const poseEuler = new THREE.Euler();
  const poseQuaternion = new THREE.Quaternion();
  function poseBone(name, x = 0, y = 0, z = 0) {
    const record = state.characterPose?.get(name);
    if (!record) return;
    poseEuler.set(x, y, z, "XYZ");
    poseQuaternion.setFromEuler(poseEuler);
    record.bone.quaternion.copy(record.rest).multiply(poseQuaternion);
  }

  function animateCharacterPose(isWalking, deltaSeconds) {
    if (state.characterAnimationMode.startsWith("gltf-animation")) return;
    if (!state.characterPose) return;
    state.walkPhase += deltaSeconds * (isWalking ? 8.5 : 2.2);
    const stride = isWalking ? Math.sin(state.walkPhase) : 0;
    const counterStride = isWalking ? Math.sin(state.walkPhase + Math.PI) : 0;
    const kneeLeft = isWalking ? Math.max(0, -stride) * 0.48 : 0.04;
    const kneeRight = isWalking ? Math.max(0, -counterStride) * 0.48 : 0.04;
    const breath = Math.sin(state.walkPhase * 0.5) * 0.025;
    poseBone("pelvis", isWalking ? Math.sin(state.walkPhase * 2) * 0.025 : 0, 0, 0);
    poseBone("spine_02", breath, 0, isWalking ? stride * 0.035 : 0);
    poseBone("spine_03", breath * 0.5, 0, isWalking ? -stride * 0.03 : 0);
    // RenderPeople's bind pose leaves the upper arms about 35° away from the
    // torso. Fold both sides a small, symmetric amount for the UE standing
    // silhouette, then add only a restrained counter-swing while walking.
    // This is a local browser-space correction; it does not mirror either
    // hand or alter the skinned mesh/materials.
    const armDrop = -0.32;
    poseBone("upperarm_l", 0, 0, armDrop + (isWalking ? -stride * 0.08 : 0));
    poseBone("lowerarm_l", 0, 0, isWalking ? -stride * 0.03 : 0);
    poseBone("upperarm_r", 0, 0, armDrop + (isWalking ? stride * 0.08 : 0));
    poseBone("lowerarm_r", 0, 0, isWalking ? stride * 0.03 : 0);
    poseBone("thigh_l", stride * 0.42, 0, 0);
    poseBone("calf_l", kneeLeft, 0, 0);
    poseBone("foot_l", -kneeLeft * 0.45, 0, 0);
    poseBone("thigh_r", counterStride * 0.42, 0, 0);
    poseBone("calf_r", kneeRight, 0, 0);
    poseBone("foot_r", -kneeRight * 0.45, 0, 0);
  }

  function stopNarration({ skipRoute = false } = {}) {
    const wasActive = state.routeNarrationActive;
    if (state.narrationAudio) {
      state.narrationAudio.pause();
      state.narrationAudio.removeAttribute("src");
      state.narrationAudio.load();
      state.narrationAudio.onended = null;
      state.narrationAudio.onerror = null;
    }
    if (state.narrationTimer) clearTimeout(state.narrationTimer);
    state.narrationTimer = null;
    state.routeNarrationSegments = [];
    state.routeNarrationSegmentIndex = 0;
    state.narrationPendingSegment = null;
    state.routeNarrationActive = false;
    state.narrationPendingSegment = null;
    if (ui.narration) ui.narration.hidden = true;
    if (skipRoute && wasActive) {
      state.routeWaypointIndex = Math.min(
        state.routeWaypointIndex + 1,
        Math.max(0, state.routeWaypoints.length - 1),
      );
      state.routePlaying = false;
      state.routePausedByUser = true;
    }
    updateRouteStateText();
  }

  function completeNarration() {
    if (!state.routeNarrationActive) return;
    const pauseAfter = state.routePauseAfterNarration;
    state.routeNarrationActive = false;
    state.routeNarrationSegments = [];
    state.routeNarrationSegmentIndex = 0;
    state.routePauseAfterNarration = false;
    const finalWaypointIndex = Math.max(0, state.routeWaypoints.length - 1);
    const completedFinalWaypoint = state.routeWaypointIndex >= finalWaypointIndex;
    state.routeWaypointIndex = Math.min(state.routeWaypointIndex + 1, finalWaypointIndex);
    state.routePausedByUser = pauseAfter;
    state.routeCompleted = !pauseAfter && completedFinalWaypoint;
    state.routePlaying = !pauseAfter && !state.routeCompleted;
    if (ui.narration) ui.narration.hidden = true;
    updateRouteStateText();
  }

  function narrationAudioUrls(assetId) {
    const encoded = encodeURIComponent(assetId);
    return {
      local: `/data/narration/${encoded}.wav`,
      backend: `/twin-api/api/v2/scene-interactions/narration-assets/${encoded}`,
    };
  }

  function playNarrationSegment() {
    const segment = state.routeNarrationSegments[state.routeNarrationSegmentIndex];
    if (!segment) {
      completeNarration();
      return;
    }
    const audio = state.narrationAudio || document.createElement("audio");
    state.narrationAudio = audio;
    if (!audio.isConnected) {
      audio.style.display = "none";
      audio.setAttribute("aria-hidden", "true");
      document.body.append(audio);
    }
    audio.pause();
    audio.onended = null;
    audio.onerror = null;
    audio.removeAttribute("src");
    audio.load();
    if (state.narrationTimer) clearTimeout(state.narrationTimer);
    state.narrationTimer = null;
    if (ui.narration && ui.narrationText) {
      ui.narration.hidden = false;
      ui.narrationText.textContent = segment.text || "";
    }
    updateRouteStateText();
    let settled = false;
    const finishSegment = () => {
      if (settled) return;
      settled = true;
      if (state.narrationTimer) clearTimeout(state.narrationTimer);
      state.narrationTimer = null;
      if (state.narrationAudio) {
        state.narrationAudio.pause();
      }
      state.narrationPendingSegment = null;
      state.narrationPlayedSegments += 1;
      document.body.dataset.narrationPlayedSegments = String(state.narrationPlayedSegments);
      if (state.routeNarrationSegmentIndex + 1 < state.routeNarrationSegments.length) {
        state.routeNarrationSegmentIndex += 1;
        playNarrationSegment();
      } else {
        completeNarration();
      }
    };
    const durationMs = Math.max(
      1500,
      Number(segment.duration_sec ?? segment.audio_duration_sec ?? 8) * 1000,
    );
    if (segment.audio_asset_id) {
      const urls = narrationAudioUrls(segment.audio_asset_id);
      audio.src = urls.local;
      audio.preload = "auto";
      audio.volume = Number((state.runtimeConfig?.narration_defaults?.volume ?? 50) / 100);
      let backendFallbackAttempted = false;
      const useBackendFallback = () => {
        if (backendFallbackAttempted || settled) return;
        backendFallbackAttempted = true;
        document.body.dataset.narrationAudioError = "local-source-error";
        state.narrationAudioFailures += 1;
        document.body.dataset.narrationAudioFailures = String(state.narrationAudioFailures);
        audio.src = urls.backend;
        audio.load();
        document.body.dataset.narrationAudioSource = "backend";
        audio.play().catch(() => {
          state.narrationPendingSegment = segment;
          document.body.dataset.narrationAutoplay = "blocked";
          document.body.dataset.narrationAudioError = "backend-play-blocked";
        });
      };
      audio.onended = finishSegment;
      audio.onerror = useBackendFallback;
      document.body.dataset.narrationAudioSource = "local";
      state.narrationPendingSegment = segment;
      audio.play().then(() => {
        state.narrationAudioUnlocked = true;
        state.narrationPendingSegment = null;
        document.body.dataset.narrationAutoplay = "playing";
      }).catch((error) => {
        document.body.dataset.narrationAutoplay = "blocked";
        document.body.dataset.narrationAudioError = String(error?.name || "play-blocked");
      });
    } else {
      state.narrationPendingSegment = null;
    }
    state.narrationTimer = window.setTimeout(finishSegment, durationMs + 120);
  }

  function triggerRouteWaypoint(index) {
    const waypoint = state.routeWaypoints[index];
    if (!waypoint || state.routeNarrationActive) return false;
    state.narrationWaypointKey = waypoint.id;
    const segments = waypoint.narration?.segments ?? [];
    const playableSegments = segments.filter((segment) => segment && (segment.text || segment.audio_asset_id));
    if (!playableSegments.length) return false;
    state.routeNarrationSegments = playableSegments;
    state.routeNarrationSegmentIndex = 0;
    state.routeNarrationActive = true;
    state.routePlaying = false;
    state.routePausedByUser = false;
    state.routePauseAfterNarration = false;
    playNarrationSegment();
    return true;
  }

  function resumeRoute() {
    if (!state.roaming) return;
    if (!state.routeWaypoints.length) {
      updateRouteStateText("当前路线没有可用路径点");
      return;
    }
    if (state.routeNarrationActive) {
      state.routePauseAfterNarration = false;
      updateRouteStateText("解说结束后继续路线");
      return;
    }
    state.routePlaying = true;
    state.routePausedByUser = false;
    state.routeCompleted = false;
    state.narrationWaypointKey = null;
    updateRouteStateText();
    drawMinimap();
  }

  function restartRoute() {
    if (!state.roaming) return;
    const first = state.routeWaypoints[0]?.position;
    if (first && state.character) {
      state.character.position.copy(first);
      state.character.position.y = 0;
      state.jumpGroundY = state.character.position.y;
      state.jumpVelocity = 0;
    }
    state.routeWaypointIndex = 0;
    state.routePausedByUser = false;
    state.routeCompleted = false;
    state.narrationWaypointKey = null;
    stopNarration();
    resumeRoute();
  }

  async function selectRuntimeRoute(routeId) {
    const route = state.availableRoutes.find((item) => (item.route_id || item.id) === routeId);
    if (!route) return;
    state.activeRouteId = routeId;
    stopNarration();
    const detail = route.waypoints?.length ? route : await fetch(`/twin-api/api/v2/scene-interactions/routes/${encodeURIComponent(routeId)}`, { cache: "no-store" }).then((response) => response.ok ? response.json() : null).then((payload) => payload?.route ?? route).catch(() => route);
    setActiveRoute(detail);
    state.routeWaypointIndex = 0;
    state.routePausedByUser = false;
    state.routeCompleted = false;
    state.routePlaying = state.routeWaypoints.length > 1;
    if (state.runtimeConfig) state.runtimeConfig.route = { ...(state.runtimeConfig.route ?? {}), route_id: routeId };
    populateRuntimeControls();
    await persistRuntimeConfig({ route: { route_id: routeId, enabled: true, auto_start: false } });
    updateRouteStateText(`${route.display_name || route.name || routeId} · 已切换`);
  }

  async function selectRuntimeCharacter(characterId) {
    const character = state.availableCharacters.find((item) => item.id === characterId);
    if (!character) return;
    state.activeCharacterId = characterId;
    state.activeSkinId = character.default_skin_id || character.skins?.[0]?.id || state.activeSkinId;
    populateRuntimeControls();
    await loadCharacterVisual(characterId);
    await persistRuntimeConfig({ character_id: characterId, default_skin_id: state.activeSkinId, allowed_skin_ids: [state.activeSkinId] });
    updateRouteStateText(`${character.display_name || characterId} · 人物已切换`);
  }

  function tryJump() {
    if (!state.roaming || state.cameraMode === "god" || !state.character) return;
    const groundY = Number.isFinite(state.jumpGroundY) ? state.jumpGroundY : state.character.position.y;
    if (state.character.position.y > groundY + 0.02 || Math.abs(state.jumpVelocity) > 0.01) return;
    const jumpHeight = Number(state.runtimeConfig?.movement?.jump_height_cm ?? 80) * 0.01;
    state.jumpGroundY = groundY;
    state.jumpVelocity = Math.sqrt(Math.max(0.1, 2 * 9.8 * jumpHeight));
    state.routePlaying = false;
    state.routePausedByUser = true;
    updateRouteStateText("跳跃 · 路线已暂停");
  }

  function updateRoaming(deltaSeconds) {
    if (!state.roaming || !state.character) return;
    const forward = state.cameraMode === "near_follow"
      ? new THREE.Vector3(
        Math.sin(state.character.rotation.y + state.shoulderYawOffset),
        0,
        Math.cos(state.character.rotation.y + state.shoulderYawOffset),
      )
      : new THREE.Vector3(-Math.sin(state.yaw), 0, -Math.cos(state.yaw));
    const right = new THREE.Vector3().crossVectors(forward, camera.up).normalize();
    const movement = new THREE.Vector3();
    if (state.keys.has("KeyW")) movement.add(forward);
    if (state.keys.has("KeyS")) movement.sub(forward);
    if (state.keys.has("KeyD")) movement.add(right);
    if (state.keys.has("KeyA")) movement.sub(right);
    const hasManualInput = movement.lengthSq() > 0;
    const verticalViewInput = (state.keys.has("KeyE") ? 1 : 0) - (state.keys.has("KeyQ") ? 1 : 0);
    if (state.cameraMode === "near_follow" && verticalViewInput) {
      // Match the UE shoulder camera's vertical boom control. Q/E are held
      // actions in third-person; E must not be consumed as the first-person
      // crosshair toggle while this mode is active.
      state.shoulderPitch = THREE.MathUtils.clamp(
        state.shoulderPitch + verticalViewInput * 1.15 * deltaSeconds,
        -0.75,
        0.75,
      );
    }

    // In UE, WASD takes control from an automatic route in first/shoulder
    // views, including stopping the active narration.  God view instead moves
    // the free camera while the character may continue its route.
    if (hasManualInput && state.cameraMode !== "god" && (state.routePlaying || state.routeNarrationActive)) {
      stopNarration({ skipRoute: state.routeNarrationActive });
      state.routePlaying = false;
      state.routePausedByUser = true;
      updateRouteStateText("WASD 已接管 · 路线暂停");
    }
    if (state.cameraMode === "god" && hasManualInput) {
      const cameraForward = camera.getWorldDirection(new THREE.Vector3()).setY(0).normalize();
      const cameraRight = new THREE.Vector3().crossVectors(cameraForward, camera.up).normalize();
      const planar = new THREE.Vector3()
        .addScaledVector(cameraForward, (state.keys.has("KeyW") ? 1 : 0) - (state.keys.has("KeyS") ? 1 : 0))
        .addScaledVector(cameraRight, (state.keys.has("KeyD") ? 1 : 0) - (state.keys.has("KeyA") ? 1 : 0));
      if (planar.lengthSq()) planar.normalize();
      const vertical = (state.keys.has("KeyE") ? 1 : 0) - (state.keys.has("KeyQ") ? 1 : 0);
      const godSpeed = Number(state.runtimeConfig?.camera?.god?.move_speed_cm_s ?? 1200) * 0.01;
      const speedScale = state.keys.has("ShiftLeft") || state.keys.has("ShiftRight") ? 1.8 : 1;
      const cameraDelta = planar.multiplyScalar(godSpeed * speedScale * deltaSeconds);
      cameraDelta.y += vertical * godSpeed * speedScale * deltaSeconds;
      camera.position.add(cameraDelta);
      controls.target.add(cameraDelta);
      movement.set(0, 0, 0);
    }

    const speed = state.keys.has("ShiftLeft") || state.keys.has("ShiftRight") ? 8 : 3.5;
    const isRouteMoving = state.routePlaying && !state.routeNarrationActive && state.routeWaypoints.length > 1 && !hasManualInput;
    if (!hasManualInput && isRouteMoving) {
      const target = state.routeWaypoints[state.routeWaypointIndex]?.position;
      if (target) movement.copy(target).sub(state.character.position).setY(0);
      if (movement.lengthSq() < 0.18 * 0.18) {
        if (!triggerRouteWaypoint(state.routeWaypointIndex)) {
          if (state.routeWaypointIndex < state.routeWaypoints.length - 1) state.routeWaypointIndex += 1;
          else if (state.availableRoutes.find((item) => (item.route_id || item.id) === state.activeRouteId)?.loop) {
            state.routeWaypointIndex = 0;
            state.narrationWaypointKey = null;
          } else {
            state.routePlaying = false;
            state.routePausedByUser = false;
            state.routeCompleted = true;
          }
          updateRouteStateText();
        }
        drawMinimap();
        movement.set(0, 0, 0);
      }
    }
    const isWalking = movement.lengthSq() > 0;
    if (isWalking) {
      movement.normalize();
      const moveSpeed = isRouteMoving
        ? Number(state.availableRoutes.find((item) => (item.route_id || item.id) === state.activeRouteId)?.speed_cm_s ?? 180) * 0.01
        : speed;
      moveCharacterWithCollision(movement.clone().multiplyScalar(moveSpeed * deltaSeconds));
      state.character.rotation.y = Math.atan2(movement.x, movement.z);
      if (state.characterVisual) state.characterVisual.position.y = state.characterVisual.userData.baseY + Math.sin(performance.now() * 0.012) * 0.025;
    } else if (state.characterVisual) {
      state.characterVisual.position.y = THREE.MathUtils.lerp(state.characterVisual.position.y, state.characterVisual.userData.baseY, 0.2);
    }
    if (COLLISION_ENABLED && state.cameraMode !== "god" && !isWalking
      && Math.abs(state.jumpVelocity) <= 0.01
      && performance.now() - state.collisionLastProbeAt > 450) {
      rebuildCollisionCatalog();
      const floorY = probeGroundAt(state.character.position.x, state.character.position.z, state.character.position.y);
      if (floorY !== null && floorY - state.character.position.y <= state.collisionStepHeight + 0.045) {
        state.character.position.y = floorY;
        state.jumpGroundY = floorY;
        state.collisionLastGroundY = floorY;
      }
      state.collisionLastProbeAt = performance.now();
    }
    if (state.cameraMode !== "god" && state.character
      && (Math.abs(state.jumpVelocity) > 0.001
        || state.character.position.y > state.jumpGroundY + 0.001)) {
      // UE's Space action is a short grounded jump; keep it separate from
      // WASD. Q/E are reserved for the God-view vertical axis and the
      // third-person shoulder boom, while the crosshair is controlled by its
      // dedicated UI button.
      state.jumpVelocity -= 9.8 * deltaSeconds;
      state.character.position.y += state.jumpVelocity * deltaSeconds;
      if (state.character.position.y <= state.jumpGroundY) {
        state.character.position.y = state.jumpGroundY;
        state.jumpVelocity = 0;
      }
    }
    animateCharacterPose(isWalking, deltaSeconds);
    if (state.characterMixer) {
      state.characterMixer.update(deltaSeconds);
      const entries = [...state.characterActions.entries()];
      const desired = isWalking
        ? entries.find(([name]) => /walk|walking|run|locomotion/.test(name))
        : entries.find(([name]) => /idle|idling|stand|breath|wait|待机/.test(name));
      const selected = desired ?? entries[0];
      if (selected && selected[0] !== state.characterActionKey) {
        const next = selected[1];
        for (const action of state.characterActions.values()) {
          if (action !== next) action.fadeOut(0.12);
        }
        next.reset().fadeIn(0.12).play();
        state.characterActionKey = selected[0];
      }
    }
    if (state.cameraMode === "first_person") {
      camera.position.copy(state.character.position).add(new THREE.Vector3(0, state.eyeHeight, 0));
      const personNear = Math.min(state.startupCameraNear, 0.03);
      if (camera.near !== personNear) {
        camera.near = personNear;
        camera.updateProjectionMatrix();
      }
      camera.rotation.order = "YXZ";
      camera.rotation.set(state.pitch, state.yaw, 0);
    } else if (state.cameraMode === "near_follow") {
      const nearConfig = state.runtimeConfig?.camera?.near_follow ?? {};
      // The backend's historical 60 cm value is a UE SpringArm probe length,
      // not a presentational third-person distance; using it in the browser
      // puts the camera inside the avatar and crops the body. Keep the same
      // configurable value when it is a real shoulder distance, but enforce
      // a 2.4 m minimum so the full high-resolution character remains visible.
      const followDistance = Math.max(Number(nearConfig.distance_cm ?? 60) * 0.01, 2.4);
      const followHeight = Number(nearConfig.height_cm ?? 90) * 0.01;
      // UE's near camera is a SpringArm attached to the character capsule,
      // so its arm follows the actor's movement-facing yaw rather than the
      // free-look yaw.  The web preview previously used state.yaw here; once
      // the route turned the actor, that put the camera in front/under the
      // character and made the entire hall appear reversed.  Character roots
      // use +Z as their authored forward axis, hence this explicit conversion.
      const actorForward = new THREE.Vector3(
        Math.sin(state.character.rotation.y),
        0,
        Math.cos(state.character.rotation.y),
      );
      const orbitForward = actorForward.applyAxisAngle(new THREE.Vector3(0, 1, 0), state.shoulderYawOffset);
      const armHorizontal = orbitForward.clone().multiplyScalar(-followDistance * Math.cos(state.shoulderPitch));
      const armVertical = new THREE.Vector3(0, followDistance * Math.sin(state.shoulderPitch), 0);
      // UE's TargetOffset.Z is measured from the capsule centre. The web
      // actor position is the feet, so add the capsule half-height first.
      const armPivotHeight = state.characterCapsuleHalfHeight + followHeight;
      const lookTarget = state.character.position.clone().add(new THREE.Vector3(0, armPivotHeight, 0));
      const desiredCamera = lookTarget.clone().add(armHorizontal).add(armVertical);
      const cameraArm = desiredCamera.clone().sub(lookTarget);
      const cameraArmLength = Math.max(0.01, cameraArm.length());
      const now = performance.now();
      if (COLLISION_ENABLED) {
        rebuildCollisionCatalog(now);
        if (now - state.cameraCollisionCheckedAt > 90) {
          const raycaster = state.cameraCollisionRaycaster;
          raycaster.set(lookTarget, cameraArm.clone().normalize());
          raycaster.near = 0.04;
          raycaster.far = cameraArmLength;
          // The old path tested every one of the ~3k high-poly render meshes
          // ten times a second.  Query only the collision hash cells crossed by
          // this short spring arm, preserving camera collision without a full
          // scene triangle walk whenever the view rotates.
          const cameraProbe = lookTarget.clone().addScaledVector(cameraArm, 0.5);
          const cameraCandidates = collisionCandidatesAt(
            cameraProbe.x,
            cameraProbe.z,
            cameraArmLength * 0.6 + 0.25,
          );
          const hit = raycaster.intersectObjects(cameraCandidates, false)[0];
          state.cameraCollisionDistance = hit
            ? Math.max(0.08, hit.distance - 0.08)
            : cameraArmLength;
          state.cameraCollisionCheckedAt = now;
          document.body.dataset.roamerCameraCollision = hit ? "blocked" : "clear";
          document.body.dataset.roamerCameraCollisionCandidates = String(cameraCandidates.length);
        }
      } else {
        state.cameraCollisionDistance = cameraArmLength;
        document.body.dataset.roamerCameraCollision = "disabled";
        document.body.dataset.roamerCameraCollisionCandidates = "0";
      }
      desiredCamera.copy(lookTarget).addScaledVector(
        cameraArm.normalize(),
        Math.min(cameraArmLength, state.cameraCollisionDistance),
      );
      const personNear = Math.min(state.startupCameraNear, 0.03);
      if (camera.near !== personNear) {
        camera.near = personNear;
        camera.updateProjectionMatrix();
      }
      camera.position.lerp(desiredCamera, 1 - Math.exp(-12 * deltaSeconds));
      camera.lookAt(lookTarget);
      // UE's spring arm shortens at nearby walls. If the camera must sit
      // inside the upper body, hide only the local avatar for that moment so
      // the view stays readable; the world character remains unchanged.
      state.character.visible = state.roaming && state.cameraCollisionDistance >= 0.26;
    } else if (state.cameraMode === "god" && state.character) {
      if (camera.near !== state.startupCameraNear) {
        camera.near = state.startupCameraNear;
        camera.updateProjectionMatrix();
      }
      controls.target.lerp(state.character.position.clone().add(new THREE.Vector3(0, 0.8, 0)), 1 - Math.exp(-3 * deltaSeconds));
    }
    if (roamingDebug) {
      const characterPosition = state.character?.position;
      const cameraPosition = camera.position;
      document.body.dataset.roamerCharacterVisible = String(Boolean(state.character?.visible));
      document.body.dataset.roamerVisualVisible = String(Boolean(state.characterVisual?.visible));
      document.body.dataset.roamerCharacterPosition = characterPosition
        ? [characterPosition.x, characterPosition.y, characterPosition.z].map((value) => Number(value).toFixed(3)).join(",") : "none";
      document.body.dataset.roamerCameraPosition = [cameraPosition.x, cameraPosition.y, cameraPosition.z]
        .map((value) => Number(value).toFixed(3)).join(",");
    }
  }

  async function commitSelectedPosition() {
    const snapshot = selectedSnapshot();
    const visual = state.visualById.get(state.selectedId);
    if (!snapshot || !visual) return;
    const original = spatialOf(snapshot);
    const body = {
      instance_id: snapshot.instanceId,
      transform: {
        tx: visual.position.x * 100,
        ty: visual.position.z * 100,
        tz: visual.position.y * 100,
        rx: Number(original.rotation_x ?? 0),
        ry: Number(original.rotation_y ?? 0),
        rz: Number(original.rotation_z ?? 0),
        sx: Number(original.scale_x ?? 1),
        sy: Number(original.scale_y ?? 1),
        sz: Number(original.scale_z ?? 1),
      },
    };
    ui.detailEdit.disabled = true;
    ui.detailEdit.textContent = "保存中…";
    try {
      const response = await fetch("/twin-api/api/v2/state/writeback", {
        method: "POST",
        headers: { ...PROJECT_HEADERS, "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      state.editMode = false;
      state.editDirty = false;
      ui.detailEdit.textContent = "编辑位置";
    } catch (error) {
      ui.detailEdit.textContent = "保存失败，重试";
      runtime.warnings.push(`Twin 位置回写失败：${error.message}`);
    } finally {
      ui.detailEdit.disabled = false;
    }
  }

  function toggleEdit() {
    if (!state.selectedId || !state.visualById.has(state.selectedId)) return;
    if (state.editMode) {
      commitSelectedPosition();
      return;
    }
    state.editMode = true;
    state.editDirty = false;
    ui.detailEdit.textContent = "保存位置";
    ui.controlHelp.textContent = "方向键移动 · PageUp/PageDown 升降 · 再点“保存位置”回写";
  }

  function editKey(event) {
    if ((!state.editMode && !editorIsActive()) || !state.selectedId) return false;
    const visual = state.visualById.get(state.selectedId);
    if (!visual) return false;
    const pendingRecord = editorIsActive() ? editorRecordFor(state.selectedId) : null;
    const before = editorPositionSnapshot(state.selectedId);
    const step = event.shiftKey ? 1 : 0.1;
    const delta = new THREE.Vector3();
    if (event.code === "ArrowUp") delta.z -= step;
    else if (event.code === "ArrowDown") delta.z += step;
    else if (event.code === "ArrowLeft") delta.x -= step;
    else if (event.code === "ArrowRight") delta.x += step;
    else if (event.code === "PageUp") delta.y += step;
    else if (event.code === "PageDown") delta.y -= step;
    else return false;
    visual.position.add(delta);
    if (editorIsActive() && state.runtimeEditorGridSnap) {
      const grid = 0.1;
      visual.position.x = Math.round(visual.position.x / grid) * grid;
      visual.position.y = Math.round(visual.position.y / grid) * grid;
      visual.position.z = Math.round(visual.position.z / grid) * grid;
    }
    if (editorIsActive() && state.runtimeEditorWallSnap) visual.position.y = Math.max(0, visual.position.y);
    visual.updateMatrixWorld(true);
    scheduleChunkBatchRebuild(state.selectedId);
    state.selectionBox?.update();
    if (editorIsActive()) {
      const record = pendingRecord || editorRecordFor(state.selectedId);
      if (record) record.after = editorPositionSnapshot(state.selectedId);
      state.runtimeEditorHistory.push({ instanceId: state.selectedId, before, after: editorPositionSnapshot(state.selectedId) });
      state.runtimeEditorRedo = [];
      state.editDirty = true;
      if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = "位置已修改 · 保存后同步到 Twin";
      renderRuntimeEditor();
    }
    state.editDirty = true;
    event.preventDefault();
    return true;
  }

  function setRuntimeDockOpen(open) {
    ui.panel.classList.toggle("is-open", open);
    document.body.classList.toggle("ue-dock-open", open);
    ui.runtimeDockHandle?.setAttribute("aria-expanded", String(open));
  }

  function toggleRuntimeDock() {
    setRuntimeDockOpen(!ui.panel.classList.contains("is-open"));
  }

  function setRuntimeMinimapOpen(open) {
    if (!ui.runtimeMinimapPanel) return;
    ui.runtimeMinimapPanel.hidden = !open;
    if (ui.runtimeMinimapToggle) {
      ui.runtimeMinimapToggle.hidden = false;
      ui.runtimeMinimapToggle.setAttribute("aria-expanded", String(open));
      ui.runtimeMinimapToggle.setAttribute("aria-label", open ? "收起小地图" : "展开小地图");
    }
    if (open) drawMinimap();
  }

  function toggleRuntimeMinimap() {
    if (!ui.runtimeMinimapPanel) return;
    setRuntimeMinimapOpen(ui.runtimeMinimapPanel.hidden);
  }

  function setMinimapStatus(message) {
    if (!ui.runtimeMinimapStatus) return;
    ui.runtimeMinimapStatus.textContent = message;
    ui.runtimeMinimapStatus.hidden = !message;
  }

  function worldPointFromMinimapEvent(event) {
    const canvas = ui.runtimeMinimap;
    if (!canvas || ui.runtimeMinimapPanel?.hidden) return null;
    const transform = state.minimapTransforms.get(canvas);
    if (!transform) return null;
    const rect = canvas.getBoundingClientRect();
    if (!rect.width || !rect.height) return null;
    const u = THREE.MathUtils.clamp((event.clientX - rect.left) / rect.width, 0, 1);
    const v = THREE.MathUtils.clamp((event.clientY - rect.top) / rect.height, 0, 1);
    return new THREE.Vector3(
      transform.minX + u * transform.spanX,
      0,
      transform.minZ + v * transform.spanZ,
    );
  }

  function previewMinimapPosition(event) {
    const point = worldPointFromMinimapEvent(event);
    if (!point) return;
    state.minimapPreview = point;
    setMinimapStatus("已预览位置 · 双击传送");
    window.setTimeout(() => {
      if (ui.runtimeMinimapStatus?.textContent === "已预览位置 · 双击传送") setMinimapStatus("");
    }, 1800);
    drawMinimap();
  }

  function teleportToMinimapPosition(event) {
    const point = worldPointFromMinimapEvent(event);
    if (!point || !state.character) return;
    state.minimapHistory.push(state.character.position.clone());
    if (state.minimapHistory.length > 4) state.minimapHistory.shift();
    state.character.position.copy(point);
    state.character.position.y = 0;
    state.routePlaying = false;
    state.minimapPreview = null;
    if (ui.runtimeMinimapUndo) ui.runtimeMinimapUndo.hidden = false;
    setMinimapStatus("已传送 · 可返回原位置");
    window.setTimeout(() => {
      if (ui.runtimeMinimapStatus?.textContent === "已传送 · 可返回原位置") setMinimapStatus("");
    }, 1800);
    updateRouteStateText("小地图传送 · 漫游已暂停");
    drawMinimap();
  }

  function undoMinimapTeleport() {
    const previous = state.minimapHistory.pop();
    if (!previous || !state.character) return;
    state.character.position.copy(previous);
    state.character.position.y = 0;
    if (ui.runtimeMinimapUndo) ui.runtimeMinimapUndo.hidden = state.minimapHistory.length === 0;
    setMinimapStatus("已返回原位置");
    window.setTimeout(() => setMinimapStatus(""), 1800);
    drawMinimap();
  }

  function toggleMinimapFollow() {
    state.minimapFollow = !state.minimapFollow;
    if (ui.runtimeMinimapFollow) {
      ui.runtimeMinimapFollow.setAttribute("aria-pressed", String(state.minimapFollow));
      ui.runtimeMinimapFollow.textContent = state.minimapFollow ? "跟随" : "固定";
    }
    setMinimapStatus(state.minimapFollow ? "小地图跟随人物" : "小地图已固定");
    drawMinimap();
  }

  function locateMinimapCharacter() {
    if (!state.character) return;
    state.minimapPreview = state.character.position.clone();
    setMinimapStatus("人物已定位");
    drawMinimap();
  }

  function refreshRuntimeGizmo() {
    const gizmo = state.runtimeEditorGizmo;
    const helper = state.runtimeEditorGizmoHelper;
    const pivot = state.runtimeEditorPivot;
    if (!gizmo || !helper || !pivot) return;
    const visuals = [...state.selectedIds]
      .map((id) => state.visualById.get(id))
      .filter(Boolean);
    if (!editorIsActive() || !visuals.length) {
      gizmo.detach();
      helper.visible = false;
      pivot.visible = false;
      return;
    }
    const center = new THREE.Vector3();
    visuals.forEach((visual) => center.add(visual.position));
    center.multiplyScalar(1 / visuals.length);
    pivot.position.copy(center);
    pivot.rotation.set(0, 0, 0);
    pivot.scale.set(1, 1, 1);
    pivot.updateMatrixWorld(true);
    gizmo.setMode(state.runtimeEditorGizmoMode);
    if (gizmo.object !== pivot) gizmo.attach(pivot);
    helper.visible = true;
    pivot.visible = false;
  }

  function beginRuntimeGizmoDrag() {
    if (!editorIsActive() || !state.runtimeEditorPivot || !state.selectedIds.size) return;
    state.runtimeEditorDragBefore = new Map();
    for (const instanceId of state.selectedIds) {
      const record = editorPositionSnapshot(instanceId);
      if (record) state.runtimeEditorDragBefore.set(instanceId, record);
    }
    state.runtimeEditorLastPivot = state.runtimeEditorPivot.position.clone();
    state.runtimeEditorLastYaw = state.runtimeEditorPivot.rotation.y;
  }

  function snapRuntimeGizmo() {
    const pivot = state.runtimeEditorPivot;
    if (!pivot) return;
    if (state.runtimeEditorGridSnap) {
      const step = 0.1;
      pivot.position.x = Math.round(pivot.position.x / step) * step;
      pivot.position.y = Math.round(pivot.position.y / step) * step;
      pivot.position.z = Math.round(pivot.position.z / step) * step;
      pivot.rotation.y = Math.round(pivot.rotation.y / THREE.MathUtils.degToRad(5)) * THREE.MathUtils.degToRad(5);
    }
    if (state.runtimeEditorWallSnap) pivot.position.y = Math.max(0, pivot.position.y);
  }

  function updateRuntimeGizmoDrag() {
    if (!state.runtimeEditorDragBefore || !state.runtimeEditorPivot) return;
    const pivot = state.runtimeEditorPivot;
    snapRuntimeGizmo();
    const currentPivot = pivot.position.clone();
    const previousPivot = state.runtimeEditorLastPivot || currentPivot.clone();
    const delta = currentPivot.clone().sub(previousPivot);
    const yawDelta = pivot.rotation.y - state.runtimeEditorLastYaw;
    const rotate = state.runtimeEditorGizmoMode === "rotate" && Math.abs(yawDelta) > 1e-7;
    for (const instanceId of state.selectedIds) {
      const visual = state.visualById.get(instanceId);
      if (!visual) continue;
      if (rotate) {
        const offset = visual.position.clone().sub(currentPivot);
        offset.applyAxisAngle(new THREE.Vector3(0, 1, 0), yawDelta);
        visual.position.copy(currentPivot).add(offset);
        visual.rotation.y += yawDelta;
      } else {
        visual.position.add(delta);
      }
      visual.updateMatrixWorld(true);
      scheduleChunkBatchRebuild(instanceId);
    }
    state.runtimeEditorLastPivot = currentPivot;
    state.runtimeEditorLastYaw = pivot.rotation.y;
    if (state.selectedId) state.selectionBox?.update();
  }

  function stageRuntimeEditorChanges(changes) {
    for (const change of changes) {
      const { instanceId, before, after } = change;
      if (!before || !after) continue;
      const existing = state.runtimeEditorPending.get(instanceId);
      const record = existing || { before, after };
      record.after = after;
      if (editorRecordsEqual(record.before, record.after)) state.runtimeEditorPending.delete(instanceId);
      else state.runtimeEditorPending.set(instanceId, record);
    }
    state.editDirty = state.runtimeEditorPending.size > 0;
  }

  function endRuntimeGizmoDrag() {
    if (!state.runtimeEditorDragBefore) return;
    const changes = [];
    for (const [instanceId, before] of state.runtimeEditorDragBefore) {
      const after = editorPositionSnapshot(instanceId);
      if (after && !editorRecordsEqual(before, after)) changes.push({ instanceId, before, after });
    }
    if (changes.length) {
      stageRuntimeEditorChanges(changes);
      state.runtimeEditorHistory.push({ changes, label: "Gizmo 变换" });
      state.runtimeEditorRedo = [];
      if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = `已变换 ${changes.length} 个实例 · 保存后同步到 Twin`;
    }
    state.runtimeEditorDragBefore = null;
    renderRuntimeEditor();
  }

  function setRuntimeEditorGizmoMode(mode) {
    state.runtimeEditorGizmoMode = mode === "rotate" ? "rotate" : "translate";
    state.runtimeEditorGizmo?.setMode(state.runtimeEditorGizmoMode);
    ui.runtimeEditorTranslate?.classList.toggle("is-active", state.runtimeEditorGizmoMode === "translate");
    ui.runtimeEditorRotate?.classList.toggle("is-active", state.runtimeEditorGizmoMode === "rotate");
    if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = state.runtimeEditorGizmoMode === "rotate" ? "旋转 Gizmo · 拖拽环形手柄" : "移动 Gizmo · 拖拽轴/平面";
  }

  function editorIsActive() {
    return document.body.classList.contains("debug-view");
  }

  function editorPositionSnapshot(instanceId) {
    const visual = state.visualById.get(instanceId);
    if (!visual) return null;
    return {
      position: visual.position.clone(),
      visible: visual.visible,
    };
  }

  function applyEditorPosition(instanceId, record) {
    const visual = state.visualById.get(instanceId);
    if (!visual || !record) return false;
    visual.position.copy(record.position);
    visual.visible = record.visible !== false;
    visual.updateMatrixWorld(true);
    scheduleChunkBatchRebuild(instanceId);
    if (state.selectedId === instanceId) state.selectionBox?.update();
    return true;
  }

  function editorRecordsEqual(left, right) {
    return Boolean(left && right)
      && left.visible === right.visible
      && left.position.distanceToSquared(right.position) < 1e-10;
  }

  function editorRecordFor(instanceId) {
    const existing = state.runtimeEditorPending.get(instanceId);
    if (existing) return existing;
    const baseline = editorPositionSnapshot(instanceId);
    if (!baseline) return null;
    const record = { before: baseline, after: { position: baseline.position.clone(), visible: baseline.visible } };
    state.runtimeEditorPending.set(instanceId, record);
    return record;
  }

  function renderRuntimeEditor() {
    if (!ui.runtimeEditor) return;
    const active = editorIsActive();
    ui.runtimeEditor.hidden = !active;
    const snapshot = selectedSnapshot();
    const visual = state.selectedId ? state.visualById.get(state.selectedId) : null;
    const pendingCount = state.runtimeEditorPending.size;
    const dirty = pendingCount > 0;
    if (ui.runtimeEditorState) ui.runtimeEditorState.textContent = dirty ? "有待保存修改" : "编辑中";
    if (ui.runtimeEditorState) ui.runtimeEditorState.style.color = dirty ? "#ffcf78" : "";
    if (ui.runtimeEditorName) ui.runtimeEditorName.textContent = state.selectedIds.size > 1
      ? `已选择 ${state.selectedIds.size} 个实例`
      : (snapshot?.displayName || snapshot?.instanceId || "未选择实例");
    if (ui.runtimeEditorType) ui.runtimeEditorType.textContent = snapshot?.objectTypeName || snapshot?.objectTypeRid || "-";
    if (ui.runtimeEditorId) ui.runtimeEditorId.textContent = snapshot?.instanceId || "-";
    const spatial = snapshot ? spatialOf(snapshot) : null;
    const values = visual ? {
      x: visual.position.x * 100,
      y: visual.position.z * 100,
      z: visual.position.y * 100,
      yaw: -THREE.MathUtils.radToDeg(visual.rotation.y),
    } : null;
    if (ui.runtimeEditorX) ui.runtimeEditorX.textContent = values ? values.x.toFixed(1) : (spatial?.translation_x ?? "-");
    if (ui.runtimeEditorY) ui.runtimeEditorY.textContent = values ? values.y.toFixed(1) : (spatial?.translation_y ?? "-");
    if (ui.runtimeEditorZ) ui.runtimeEditorZ.textContent = values ? values.z.toFixed(1) : (spatial?.translation_z ?? "-");
    if (ui.runtimeEditorYaw) ui.runtimeEditorYaw.textContent = values ? values.yaw.toFixed(1) : (spatial?.rotation_z ?? "-");
    if (ui.runtimeEditorPending) ui.runtimeEditorPending.textContent = `待保存修改（${pendingCount}）`;
    if (ui.runtimeEditorUndo) ui.runtimeEditorUndo.disabled = !state.runtimeEditorHistory.length;
    if (ui.runtimeEditorRedo) ui.runtimeEditorRedo.disabled = !state.runtimeEditorRedo.length;
    if (ui.runtimeEditorSave) ui.runtimeEditorSave.disabled = !dirty;
    if (ui.runtimeEditorCancel) ui.runtimeEditorCancel.disabled = !dirty;
    if (ui.runtimeEditorRemove) ui.runtimeEditorRemove.disabled = !visual;
    if (ui.runtimeEditorBusinessSelection) ui.runtimeEditorBusinessSelection.textContent = snapshot
      ? `${snapshot.displayName || snapshot.instanceId} · ${snapshot.objectTypeName || "未分类"}`
      : "请先在场景中选择对象";
    if (ui.runtimeEditorBusinessSelector) {
      const labels = [...new Set([...state.snapshots.values()].map((item) => String(item.objectTypeName || item.objectTypeRid || "未分类")))].sort((a, b) => a.localeCompare(b, "zh-CN"));
      const current = state.businessType || "";
      if (ui.runtimeEditorBusinessSelector.options.length !== labels.length + 1
        || [...ui.runtimeEditorBusinessSelector.options].slice(1).some((option, index) => option.value !== labels[index])) {
        ui.runtimeEditorBusinessSelector.replaceChildren();
        const empty = document.createElement("option"); empty.value = ""; empty.textContent = "选择业务归属"; ui.runtimeEditorBusinessSelector.append(empty);
        labels.forEach((label) => { const option = document.createElement("option"); option.value = label; option.textContent = label; ui.runtimeEditorBusinessSelector.append(option); });
      }
      ui.runtimeEditorBusinessSelector.value = labels.includes(current) ? current : "";
      ui.runtimeEditorBusinessSelector.disabled = !snapshot;
    }
    if (ui.runtimeEditorStatus && active && !ui.runtimeEditorStatus.textContent) {
      ui.runtimeEditorStatus.textContent = "选择实例后可用方向键移动";
    }
    refreshRuntimeGizmo();
  }

  function undoRuntimeEditor() {
    const command = state.runtimeEditorHistory.pop();
    if (!command) return;
    const changes = command.changes || [command];
    for (const change of changes) applyEditorPosition(change.instanceId, change.before);
    stageRuntimeEditorChanges(changes.map((change) => ({
      instanceId: change.instanceId,
      before: change.after,
      after: change.before,
    })));
    state.runtimeEditorRedo.push(command);
    if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = "已撤销上一步";
    renderRuntimeEditor();
  }

  function redoRuntimeEditor() {
    const command = state.runtimeEditorRedo.pop();
    if (!command) return;
    const changes = command.changes || [command];
    for (const change of changes) applyEditorPosition(change.instanceId, change.after);
    stageRuntimeEditorChanges(changes);
    state.runtimeEditorHistory.push(command);
    if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = "已重做上一步";
    renderRuntimeEditor();
  }

  async function saveRuntimeEditor() {
    const pending = [...state.runtimeEditorPending.entries()];
    if (!pending.length) return;
    if (ui.runtimeEditorSave) { ui.runtimeEditorSave.disabled = true; ui.runtimeEditorSave.textContent = "正在保存…"; }
    try {
      for (const [instanceId, record] of pending) {
        const visual = state.visualById.get(instanceId);
        if (!visual) continue;
        const body = {
          instance_id: instanceId,
          transform: {
            tx: visual.position.x * 100,
            ty: visual.position.z * 100,
            tz: visual.position.y * 100,
            rx: 0, ry: 0, rz: -THREE.MathUtils.radToDeg(visual.rotation.y),
            sx: visual.scale.x, sy: visual.scale.z, sz: visual.scale.y,
          },
          is_loaded: visual.visible,
        };
        const response = await fetch("/twin-api/api/v2/state/writeback", {
          method: "POST", headers: { ...PROJECT_HEADERS, "Content-Type": "application/json" }, body: JSON.stringify(body),
        });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
      }
      state.runtimeEditorPending.clear();
      state.runtimeEditorHistory = [];
      state.runtimeEditorRedo = [];
      state.editDirty = false;
      if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = "全部修改已保存";
      if (ui.runtimeEditorSave) ui.runtimeEditorSave.textContent = "保存全部修改";
    } catch (error) {
      runtime.warnings.push(`运行时编辑保存失败：${error.message}`);
      if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = "保存失败，请重试";
      if (ui.runtimeEditorSave) ui.runtimeEditorSave.textContent = "重试保存";
    } finally {
      renderRuntimeEditor();
    }
  }

  function cancelRuntimeEditor() {
    for (const [instanceId, record] of state.runtimeEditorPending) applyEditorPosition(instanceId, record.before);
    state.runtimeEditorPending.clear();
    state.runtimeEditorHistory = [];
    state.runtimeEditorRedo = [];
    state.editDirty = false;
    if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = "已取消全部修改";
    renderRuntimeEditor();
  }

  function removeRuntimeEditorSelection() {
    if (!state.selectedId) return;
    const visual = state.visualById.get(state.selectedId);
    if (!visual) return;
    const pendingRecord = editorRecordFor(state.selectedId);
    const before = editorPositionSnapshot(state.selectedId);
    visual.visible = false;
    const after = editorPositionSnapshot(state.selectedId);
    if (pendingRecord) pendingRecord.after = after;
    state.runtimeEditorHistory.push({ instanceId: state.selectedId, before, after });
    state.runtimeEditorRedo = [];
    state.editDirty = true;
    if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = "已从视口隐藏 · 保存后生效 · 可撤销";
    renderRuntimeEditor();
  }

  function toggleRuntimeEditor() {
    const enabled = !editorIsActive();
    if (!enabled && state.runtimeEditorPending.size) {
      if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = "存在待保存修改，请先保存或取消";
      return;
    }
    // UE's F10 runtime editor and F7 roaming are mutually exclusive.  Leave
    // roaming through the same state transition before exposing the editor so
    // the character, pointer lock, route and camera do not remain active
    // underneath the editor panel.
    if (enabled && state.roaming) toggleRoaming(false);
    document.body.classList.toggle("debug-view", enabled);
    document.body.dataset.runtimeEditor = String(enabled);
    if (ui.runtimeSceneEdit) ui.runtimeSceneEdit.textContent = enabled ? "退出编辑" : "编辑";
    if (enabled) {
      state.runtimeEditorHistory = [];
      state.runtimeEditorRedo = [];
      state.runtimeEditorPending.clear();
      state.editMode = false;
      state.editDirty = false;
      if (ui.controlHelp) ui.controlHelp.textContent = "运行时编辑 · 选择实例后方向键移动 · F10 退出";
      updateRouteStateText("运行时编辑已开启");
    } else {
      if (ui.controlHelp) ui.controlHelp.textContent = "左键旋转 · 右键平移 · 滚轮缩放";
      updateRouteStateText();
    }
    showDetail(selectedSnapshot());
    showWorldDetail(selectedSnapshot());
    renderList();
    renderRuntimeEditor();
  }

  ui.search.addEventListener("input", renderList);
  ui.detailClose.addEventListener("click", () => selectInstance(null));
  ui.detailFocus.addEventListener("click", focusSelected);
  ui.detailEdit.addEventListener("click", toggleEdit);
  ui.roamToggle.addEventListener("click", () => toggleRoaming());
  ui.characterSelector?.addEventListener("change", (event) => {
    selectRuntimeCharacter(event.target.value).catch((error) => {
      document.body.dataset.roamerVisual = "fallback";
      document.body.dataset.roamerError = error?.stack || error?.message || String(error);
      runtime.warnings.push(`人物切换失败：${error?.message || error}`);
    });
  });
  ui.routeSelector?.addEventListener("change", (event) => selectRuntimeRoute(event.target.value));
  ui.cameraButtons.forEach((button) => button.addEventListener("click", () => {
    if (!state.roaming || button.disabled) return;
    setCameraMode(button.dataset.cameraMode);
    persistRuntimeConfig({ camera: { default_mode: state.cameraMode } });
  }));
  ui.runtimeTabs.forEach((tab) => tab.addEventListener("click", () => {
    const selected = tab.dataset.runtimeTab;
    ui.runtimeTabs.forEach((item) => item.classList.toggle("is-active", item === tab));
    ui.runtimeTabPanels.forEach((panel) => { panel.hidden = panel.dataset.runtimePanel !== selected; });
    if (selected === "home") activateRuntimeHome();
  }));
  ui.cycleSkin?.addEventListener("click", async () => {
    const character = state.availableCharacters.find((item) => item.id === state.activeCharacterId);
    const skins = character?.skins ?? [];
    if (!skins.length) return;
    const index = Math.max(0, skins.findIndex((skin) => skin.id === state.activeSkinId));
    state.activeSkinId = skins[(index + 1) % skins.length].id;
    await persistRuntimeConfig({ default_skin_id: state.activeSkinId, allowed_skin_ids: [state.activeSkinId] });
    updateRouteStateText(`${character.display_name || state.activeCharacterId} · ${skins[(index + 1) % skins.length].display_name || "皮肤已切换"}`);
  });
  ui.resumeRoute?.addEventListener("click", resumeRoute);
  ui.restartRoute?.addEventListener("click", restartRoute);
  ui.reloadCharacter?.addEventListener("click", () => loadCharacterVisual(state.activeCharacterId));
  ui.narrationSkip?.addEventListener("click", () => stopNarration({ skipRoute: true }));
  ui.runtimeDockHandle?.addEventListener("click", toggleRuntimeDock);
  ui.runtimeMinimapToggle?.addEventListener("click", toggleRuntimeMinimap);
  ui.runtimeMinimapLocate?.addEventListener("click", locateMinimapCharacter);
  ui.runtimeMinimapFollow?.addEventListener("click", toggleMinimapFollow);
  ui.runtimeMinimapUndo?.addEventListener("click", undoMinimapTeleport);
  ui.runtimeMinimap?.addEventListener("click", previewMinimapPosition);
  ui.runtimeMinimap?.addEventListener("dblclick", (event) => {
    event.preventDefault();
    teleportToMinimapPosition(event);
  });
  ui.runtimeSceneEdit?.addEventListener("click", toggleRuntimeEditor);
  ui.runtimeEditorClose?.addEventListener("click", toggleRuntimeEditor);
  ui.runtimeEditorUndo?.addEventListener("click", undoRuntimeEditor);
  ui.runtimeEditorRedo?.addEventListener("click", redoRuntimeEditor);
  ui.runtimeEditorSave?.addEventListener("click", saveRuntimeEditor);
  ui.runtimeEditorCancel?.addEventListener("click", cancelRuntimeEditor);
  ui.runtimeEditorRemove?.addEventListener("click", removeRuntimeEditorSelection);
  ui.runtimeEditorTranslate?.addEventListener("click", () => setRuntimeEditorGizmoMode("translate"));
  ui.runtimeEditorRotate?.addEventListener("click", () => setRuntimeEditorGizmoMode("rotate"));
  ui.runtimeEditorTabs.forEach((tab) => tab.addEventListener("click", () => {
    state.runtimeEditorTab = tab.dataset.editorTab || "scene";
    ui.runtimeEditorTabs.forEach((item) => item.classList.toggle("is-active", item === tab));
    ui.runtimeEditorPanels.forEach((panel) => { panel.hidden = panel.dataset.editorPanel !== state.runtimeEditorTab; });
  }));
  ui.runtimeEditorWallSnap?.addEventListener("change", (event) => { state.runtimeEditorWallSnap = event.target.checked; });
  ui.runtimeEditorGridSnap?.addEventListener("change", (event) => { state.runtimeEditorGridSnap = event.target.checked; });
  ui.runtimeEditorBusinessSelector?.addEventListener("change", (event) => {
    state.businessType = event.target.value || null;
    if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = state.businessType ? `业务归属已选择：${state.businessType} · 保存后应用` : "请选择业务归属";
  });
  ui.runtimeEditorBusinessCreate?.addEventListener("click", () => {
    const name = ui.runtimeEditorBusinessName?.value.trim();
    if (!name) {
      if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = "请输入新建业务名称";
      return;
    }
    if (ui.runtimeEditorStatus) ui.runtimeEditorStatus.textContent = "业务目录由实时 Twin 数据提供；新建业务需由后端权限接口提交";
  });
  ui.scopeAll?.addEventListener("click", () => {
    state.businessScopeCurrent = false;
    if (ui.businessScope) ui.businessScope.textContent = "当前：全部空间";
    renderRuntimePanels();
  });
  ui.scopeCurrent?.addEventListener("click", () => {
    if (!state.spacePath.length) {
      updateRouteStateText("请先在空间页选择一个节点");
      return;
    }
    state.businessScopeCurrent = true;
    if (ui.businessScope) ui.businessScope.textContent = `当前：${state.spacePath.join(" / ")}`;
    renderRuntimePanels();
  });
  ui.enterSpace?.addEventListener("click", () => {
    if (!state.spacePath.length) {
      updateRouteStateText("请先选择空间节点");
      return;
    }
    state.businessScopeCurrent = true;
    if (ui.businessScope) ui.businessScope.textContent = `当前：${state.spacePath.join(" / ")}`;
    updateRouteStateText(`已进入空间 · ${state.spacePath.join(" / ")}`);
    renderRuntimePanels();
  });
  ui.runtimeCrosshair?.addEventListener("click", () => {
    if (ui.runtimeCrosshair.disabled) return;
    state.crosshairEnabled = !state.crosshairEnabled;
    setCameraMode(state.cameraMode);
  });
  ui.worldDetailClose?.addEventListener("click", () => selectInstance(null));
  ui.worldDetailFocus?.addEventListener("click", focusSelected);
  ui.worldDetailEditor?.addEventListener("click", toggleRuntimeEditor);
  // A viewport click selects the authored Twin even when its visible mesh is
  // part of a DrawCall-merged batch.
  renderer.domElement.addEventListener("click", (event) => {
    if (event.target !== renderer.domElement) return;
    if (state.roaming && state.cameraMode === "first_person") return;
    pickTwinAtClientPoint(event.clientX, event.clientY);
  }, { capture: true });
  document.addEventListener("pointerdown", () => {
    // Retry muted display playback and any narration waiting for a user
    // gesture; subsequent route waypoints then reuse the unlocked element.
    void startMainScreenVideos();
    if (state.narrationPendingSegment && state.routeNarrationActive) playNarrationSegment();
  }, { passive: true });
  renderer.domElement.addEventListener("click", () => {
    if (state.roaming && state.cameraMode === "first_person" && document.pointerLockElement !== renderer.domElement) {
      renderer.domElement.requestPointerLock?.();
    }
  });
  document.addEventListener("contextmenu", (event) => {
    if (state.roaming && state.cameraMode === "near_follow") event.preventDefault();
  });
  document.addEventListener("pointerdown", (event) => {
    if (event.button === 2 && state.roaming && state.cameraMode === "near_follow") {
      state.shoulderLookActive = true;
      event.preventDefault();
    }
  });
  document.addEventListener("pointerup", (event) => {
    if (event.button === 2) state.shoulderLookActive = false;
  });
  document.addEventListener("pointermove", (event) => {
    if (!state.roaming) return;
    if (state.cameraMode === "first_person") {
      if (document.pointerLockElement !== renderer.domElement) return;
      const scale = 0.0018 * state.lookSensitivity;
      state.yaw -= event.movementX * scale;
      state.pitch = THREE.MathUtils.clamp(state.pitch - event.movementY * scale, -Math.PI * 0.49, Math.PI * 0.49);
      return;
    }
    if (state.cameraMode === "near_follow" && (state.shoulderLookActive || (event.buttons & 2))) {
      const scale = 0.0018 * state.nearLookSensitivity;
      state.shoulderYawOffset -= event.movementX * scale;
      state.shoulderPitch = THREE.MathUtils.clamp(state.shoulderPitch - event.movementY * scale, -0.75, 0.75);
    }
  });
  window.addEventListener("keydown", (event) => {
    const typing = event.target instanceof HTMLInputElement
      || event.target instanceof HTMLTextAreaElement
      || event.target instanceof HTMLSelectElement
      || event.target?.isContentEditable;
    if (!typing && editKey(event)) return;
    if (!event.repeat && event.code === "F10") {
      toggleRuntimeEditor();
      event.preventDefault();
      return;
    }
    if (!event.repeat && event.code === "F7") {
      toggleRoaming();
      event.preventDefault();
      return;
    }
    if (!event.repeat && event.code === "Tab") {
      toggleRuntimeDock();
      event.preventDefault();
      return;
    }
    if (!event.repeat && event.code === "KeyV") {
      toggleCameraMode();
      event.preventDefault();
      return;
    }
    if (!typing && !event.repeat && event.code === "KeyE"
      && state.roaming && state.cameraMode === "first_person") {
      state.crosshairEnabled = !state.crosshairEnabled;
      setCameraMode(state.cameraMode);
      if (state.crosshairEnabled) pickTwinAtClientPoint(window.innerWidth * 0.5, window.innerHeight * 0.5);
      event.preventDefault();
      return;
    }
    if (!typing && !event.repeat && event.code === "Space"
      && state.roaming && state.cameraMode !== "god") {
      tryJump();
      event.preventDefault();
      return;
    }
    if (!typing && !event.repeat && event.code === "KeyP") {
      toggleRoutePause();
      event.preventDefault();
      return;
    }
    if (!typing && !event.repeat && event.code === "KeyR") {
      resumeRoute();
      event.preventDefault();
      return;
    }
    state.keys.add(event.code);
  });
  window.addEventListener("keyup", (event) => state.keys.delete(event.code));
  window.addEventListener("blur", () => state.keys.clear());

  await loadRoamingRuntimeConfig();
  makeCharacter();
  const query = new URLSearchParams(location.search);
  const requestedRouteId = query.get("route");
  if (requestedRouteId) await selectRuntimeRoute(requestedRouteId);
  const roamOverride = query.get("roam");
  const benchmarkViews = query.get("benchmark") === "views";
  // Fixed-camera benchmark pages must not have the roaming tick overwrite the
  // requested UE QA pose.  This is a diagnostics-only query flag; normal
  // launches continue to obey the backend auto_enter setting.
  if (benchmarkViews) setCameraMode("god");
  const shouldAutoEnter = !benchmarkViews && (roamOverride === "1"
    || (roamOverride !== "0" && state.runtimeConfig?.auto_enter === true));
  if (shouldAutoEnter) toggleRoaming(true);
  if (state.runtimeConfig?.route?.auto_start === true && state.routeWaypoints.length > 1) resumeRoute();
  if (query.get("roamtest") === "1") {
    const start = state.character.position.clone();
    state.keys.add("KeyW");
    window.setTimeout(() => {
      state.keys.delete("KeyW");
      document.body.dataset.roamTestDistance = state.character.position.distanceTo(start).toFixed(3);
      document.body.dataset.roamTest = "PASS";
    }, 1800);
  }
  await loadInitialSnapshots();
  await Promise.all([
    initializePersistentHlod(),
    initializeChunkStreaming(),
  ]);
  state.pollTimer = window.setTimeout(poll, POLL_INTERVAL_MS);

  const api = {
    state,
    selectInstance,
    focusSelected,
    toggleRoaming,
    setCameraMode,
    toggleCameraMode,
    selectRuntimeRoute,
    selectRuntimeCharacter,
    resumeRoute,
    restartRoute,
    toggleRoutePause,
    stopNarration,
    activateRuntimeHome,
    update(now) {
      const deltaSeconds = Math.min((now - state.lastUpdateAt) / 1000, 0.05);
      state.lastUpdateAt = now;
      updateRoaming(deltaSeconds);
      state.selectionBox?.update();
      refreshChunkResidency(now);
      restoreAuthoredTwinVisibility();
    },
    stop() {
      state.stopped = true;
      if (state.pollTimer) clearTimeout(state.pollTimer);
    },
  };
  runtime.twin = api;
  return api;
}
