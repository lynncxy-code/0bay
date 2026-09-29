import * as THREE from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { applyBindings, bindUnmatchedFurniture, unifyInspectableParts } from "./catalog.js";
import { classifyCollisionKind } from "./collision.js";

export const DEFAULT_SPAWN = {
  gltfUp: "y",
  units: "cm",
  playerStart: { x: 0, y: 0, z: 92 },
  capsuleRadius: 34,
  capsuleHalfHeight: 92,
  eyeHeightFromFloor: 172,
  walkSpeed: 220,
  snapTurnDegrees: 45,
  laserMaxDistance: 1200,
};

const CM_TO_M = 0.01;
const ENVIRONMENT_NAME_RE =
  /zerobay|skyatmosphere|skylight|exponentialheightfog|volumetriccloud|atmosphericfog|landscape|brush0\b|postprocessvolume|recastnavmesh|navmeshbounds/i;

export function ueCmToThreeMeters(x, y, z) {
  return { x: x * CM_TO_M, y: z * CM_TO_M, z: y * CM_TO_M };
}

export function worldOffsetToPutSpawnOnFloor(spawn) {
  const start = spawn?.playerStart || { x: 0, y: 0, z: 0 };
  const eye = ueCmToThreeMeters(start.x || 0, start.y || 0, start.z || 0);
  const eyeHeight = (spawn?.eyeHeightFromFloor ?? 164) * CM_TO_M;
  return {
    x: -eye.x,
    y: -(eye.y - eyeHeight),
    z: -eye.z,
  };
}

export function isEnvironmentName(name) {
  return ENVIRONMENT_NAME_RE.test(String(name || ""));
}

export function pruneEnvironment(root) {
  root?.traverse?.((node) => {
    const name = `${node.name || ""} ${node.parent?.name || ""}`;
    if (!isEnvironmentName(name)) {
      return;
    }
    node.visible = false;
    if (node.userData) {
      node.userData.collision = false;
    }
  });
}

export async function loadSpawn(dataRoot = "/data") {
  try {
    const response = await fetch(`${dataRoot}/spawn.json`, { cache: "no-store" });
    if (!response.ok) {
      return { ...DEFAULT_SPAWN };
    }
    return { ...DEFAULT_SPAWN, ...(await response.json()) };
  } catch {
    return { ...DEFAULT_SPAWN };
  }
}

export function applyGltfUp(root, gltfUp) {
  if (gltfUp === "z") {
    root.rotation.x = -Math.PI / 2;
  }
}

export function classifySurfaceKind(name) {
  const hay = String(name || "");
  if (/glass/i.test(hay) && !/whiteboard/i.test(hay)) {
    return "glass";
  }
  if (/(floor|skirting)/i.test(hay) && !/(wall|glass|door)/i.test(hay)) {
    return "floor";
  }
  if (/(ceiling|roof|cove)/i.test(hay)) {
    return "ceiling";
  }
  if (/(wall|column|partition|facade|perimeter)/i.test(hay)) {
    return "wall";
  }
  if (/plant|pothos|绿萝|叶/i.test(hay)) {
    return "plant";
  }
  if (/metal|steel|aluminum|不锈钢|金属|chrome/i.test(hay)) {
    return "metal";
  }
  if (/wood|木纹|木|桌|柜|茶几|吧台/i.test(hay)) {
    return "wood";
  }
  if (/椅|沙发|fabric|布|垫|cushion|leather|皮/i.test(hay)) {
    return "fabric";
  }
  return "object";
}

function applySurfaceKind(patched, kind) {
  if (kind === "glass") {
    patched.envMapIntensity = 0.08;
    patched.roughness = Math.min(patched.roughness ?? 1, 0.08);
    patched.metalness = Math.min(patched.metalness ?? 0, 0.04);
    return;
  }
  if (kind === "floor") {
    patched.roughness = Math.min(Math.max(patched.roughness ?? 0.55, 0.42), 0.62);
    patched.metalness = Math.min(patched.metalness ?? 0, 0.04);
    patched.envMapIntensity = 0.55;
    return;
  }
  if (kind === "ceiling") {
    patched.roughness = Math.max(patched.roughness ?? 0.78, 0.72);
    patched.metalness = 0;
    patched.envMapIntensity = 0.18;
    return;
  }
  if (kind === "wall") {
    patched.roughness = Math.max(patched.roughness ?? 0.7, 0.58);
    patched.metalness = Math.min(patched.metalness ?? 0, 0.03);
    patched.envMapIntensity = 0.28;
    return;
  }
  if (kind === "metal") {
    patched.metalness = Math.max(patched.metalness ?? 0, 0.55);
    patched.roughness = Math.min(patched.roughness ?? 1, 0.32);
    patched.envMapIntensity = 0.95;
    return;
  }
  if (kind === "wood") {
    patched.metalness = Math.min(patched.metalness ?? 0, 0.06);
    patched.roughness = Math.min(Math.max(patched.roughness ?? 0.48, 0.36), 0.58);
    patched.envMapIntensity = 0.72;
    return;
  }
  if (kind === "fabric") {
    patched.metalness = 0;
    patched.roughness = Math.max(patched.roughness ?? 0.8, 0.72);
    patched.envMapIntensity = 0.22;
    return;
  }
  if (kind === "plant") {
    patched.metalness = 0;
    patched.roughness = 0.7;
    patched.envMapIntensity = 0.35;
    return;
  }
  patched.envMapIntensity = patched.envMapIntensity ?? 0.58;
  if (patched.roughness == null) {
    patched.roughness = 0.55;
  }
}

export function makeUnlitAlbedo(root) {
  root.traverse((node) => {
    if (!node.isMesh || !node.material) {
      return;
    }
    const list = Array.isArray(node.material) ? node.material : [node.material];
    const next = list.map((material) => {
      const matName = `${material.name || ""} ${node.name || ""} ${node.parent?.name || ""}`;
      const namedGlass = /glass/i.test(matName) && !/whiteboard/i.test(matName);
      const patched = material.isMeshStandardMaterial || material.isMeshPhysicalMaterial
        ? material
        : new THREE.MeshStandardMaterial({
          color: material.color ? material.color.clone() : new THREE.Color(0xffffff),
          map: material.map || material.emissiveMap || null,
          transparent: !!material.transparent,
          opacity: material.opacity ?? 1,
        });
      if (material.color && patched.color) {
        patched.color.copy(material.color);
      }
      if (material.map) {
        patched.map = material.map;
      }
      let opacity = material.opacity ?? patched.opacity ?? 1;
      if (namedGlass && opacity > 0.34) {
        opacity = 0.12;
      }
      const transparent = !!material.transparent || namedGlass || opacity < 0.999;
      patched.opacity = opacity;
      patched.transparent = transparent;
      patched.depthWrite = !transparent;
      patched.side = transparent ? THREE.DoubleSide : THREE.FrontSide;
      applySurfaceKind(patched, classifySurfaceKind(matName));
      patched.toneMapped = true;
      patched.needsUpdate = true;
      return patched;
    });
    node.material = Array.isArray(node.material) ? next : next[0];
  });
}

export function applyLightmaps(root) {
  const loader = new THREE.TextureLoader();
  root.traverse((node) => {
    if (!node.isMesh || !node.material) {
      return;
    }
    const extras = node.userData || {};
    const lightmapUrl = extras.lightmap || extras.officeLightmap;
    if (!lightmapUrl) {
      return;
    }
    const texture = loader.load(lightmapUrl);
    texture.colorSpace = THREE.LinearSRGBColorSpace;
    texture.channel = 1;
    const materials = Array.isArray(node.material) ? node.material : [node.material];
    for (const material of materials) {
      material.lightMap = texture;
      material.lightMapIntensity = 1;
      material.needsUpdate = true;
    }
  });
}

function markCollisionFromName(root) {
  root.traverse((node) => {
    if (!node.isMesh) {
      return;
    }
    const name = `${node.name} ${node.parent?.name || ""}`;
    const kind = classifyCollisionKind(name);
    if (kind === "floor") {
      node.userData.collision = true;
      node.userData.floor = true;
    } else     if (kind === "wall") {
      node.userData.collision = true;
      node.userData.wall = true;
    }
    let current = node;
    while (current) {
      if (current.userData?.inspectable) {
        node.userData.collision = true;
        node.userData.wall = true;
        break;
      }
      current = current.parent;
    }
  });
}

export async function tryLoadGltf(url, onProgress) {
  const loader = new GLTFLoader();
  return new Promise((resolveLoad, reject) => {
    loader.load(url, resolveLoad, (event) => {
      if (!onProgress || !event.total) {
        return;
      }
      onProgress(Math.min(1, event.loaded / event.total));
    }, reject);
  });
}

export async function tryLoadOfficeScene(spawn, items, bindings, dataRoot = "/data", onProgress) {
  try {
    const gltf = await tryLoadGltf(`${dataRoot}/architecture.glb`, onProgress);
    const root = gltf.scene;
    applyGltfUp(root, spawn.gltfUp || "y");
    applyLightmaps(root);
    makeUnlitAlbedo(root);
    applyBindings(root, bindings, items);
    bindUnmatchedFurniture(root, items);
    unifyInspectableParts(root);
    markCollisionFromName(root);
    pruneEnvironment(root);
    let hasCollision = false;
    root.traverse((node) => {
      if (node.isMesh && node.visible !== false && node.userData?.collision) {
        hasCollision = true;
      }
    });
    if (!hasCollision) {
      try {
        const collisionGltf = await tryLoadGltf(`${dataRoot}/collision.glb`);
        const collisionRoot = collisionGltf.scene;
        applyGltfUp(collisionRoot, spawn.gltfUp || "y");
        pruneEnvironment(collisionRoot);
        collisionRoot.traverse((node) => {
          if (node.isMesh && node.visible !== false) {
            node.userData.collision = true;
            node.visible = false;
          }
        });
        root.add(collisionRoot);
      } catch {
        // architecture meshes tagged as collision fallback
      }
    }
    return { root, source: "glb" };
  } catch (error) {
    console.error("OfficeWebXR architecture.glb failed", error);
    return null;
  }
}
