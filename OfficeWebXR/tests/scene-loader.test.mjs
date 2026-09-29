import test from "node:test";
import assert from "node:assert/strict";
import {
  applyGltfUp,
  ueCmToThreeMeters,
  worldOffsetToPutSpawnOnFloor,
  isEnvironmentName,
  pruneEnvironment,
  makeUnlitAlbedo,
  classifySurfaceKind,
} from "../src/scene-loader.js";

test("ueCmToThreeMeters matches ZHHZ UE(X,Y,Z) cm to Three(X,Z,Y) m", () => {
  const p = ueCmToThreeMeters(-1265.340117, -165.270805, 339.106533);
  assert.ok(Math.abs(p.x - -12.65340117) < 1e-6);
  assert.ok(Math.abs(p.y - 3.39106533) < 1e-6);
  assert.ok(Math.abs(p.z - -1.65270805) < 1e-6);
});

test("worldOffsetToPutSpawnOnFloor puts office PlayerStart on XR local-floor", () => {
  const off = worldOffsetToPutSpawnOnFloor({
    playerStart: { x: 2500, y: -1150, z: 4485 },
    eyeHeightFromFloor: 164,
  });
  assert.ok(Math.abs(off.x - -25) < 1e-6);
  assert.ok(Math.abs(off.y - -43.21) < 1e-6);
  assert.ok(Math.abs(off.z - 11.5) < 1e-6);
});

test("applyGltfUp does not rotate Y-up scenes from UE GLTFExporter", () => {
  const root = { rotation: { x: 0 } };
  applyGltfUp(root, "y");
  assert.equal(root.rotation.x, 0);
});

test("applyGltfUp rotates only explicit Z-up sources", () => {
  const root = { rotation: { x: 0 } };
  applyGltfUp(root, "z");
  assert.ok(Math.abs(root.rotation.x + Math.PI / 2) < 1e-9);
});

test("isEnvironmentName drops ZeroBay city and sky, keeps office furniture", () => {
  assert.equal(isEnvironmentName("ZeroBay Buildings"), true);
  assert.equal(isEnvironmentName("ZeroBay Roads"), true);
  assert.equal(isEnvironmentName("SkyAtmosphere"), true);
  assert.equal(isEnvironmentName("PDF_13_会议椅_小会议室_黄色"), false);
  assert.equal(isEnvironmentName("Floor"), false);
  assert.equal(isEnvironmentName("Walls"), false);
});

test("pruneEnvironment hides environment meshes and clears collision", () => {
  const city = {
    name: "ZeroBay Buildings",
    isMesh: true,
    visible: true,
    userData: { collision: true },
  };
  const desk = {
    name: "PDF_07_会议桌_大会议室",
    isMesh: true,
    visible: true,
    userData: {},
  };
  const root = {
    traverse(fn) {
      fn(city);
      fn(desk);
    },
  };
  pruneEnvironment(root);
  assert.equal(city.visible, false);
  assert.equal(city.userData.collision, false);
  assert.equal(desk.visible, true);
});

test("makeUnlitAlbedo keeps thin glass alpha and does not write depth", () => {
  const mesh = {
    isMesh: true,
    name: "InteriorGlass",
    material: {
      name: "M_Glass",
      color: { clone() { return { r: 0.98, g: 0.98, b: 0.98 }; } },
      opacity: 0.07,
      transparent: true,
    },
  };
  makeUnlitAlbedo({ traverse: (fn) => fn(mesh) });
  assert.equal(mesh.material.transparent, true);
  assert.ok(mesh.material.opacity < 0.2);
  assert.equal(mesh.material.depthWrite, false);
  assert.ok(mesh.material.envMapIntensity < 0.2);
});

test("makeUnlitAlbedo makes opaque-exported clear glass see-through", () => {
  const mesh = {
    isMesh: true,
    name: "PhoneBooth",
    material: {
      name: "M_PB26_ClearGlass",
      color: { clone() { return { r: 1, g: 1, b: 1 }; } },
      opacity: 1,
      transparent: true,
    },
  };
  makeUnlitAlbedo({ traverse: (fn) => fn(mesh) });
  assert.equal(mesh.material.transparent, true);
  assert.ok(mesh.material.opacity < 0.35);
});

test("makeUnlitAlbedo keeps PBR maps instead of flattening to unlit grey", () => {
  const map = { id: "wood" };
  const color = {
    r: 0.45,
    g: 0.32,
    b: 0.2,
    clone() { return this; },
    copy() { return this; },
  };
  const mesh = {
    isMesh: true,
    name: "PDF_07_会议桌_大会议室",
    material: {
      isMeshStandardMaterial: true,
      name: "浅色木纹",
      color,
      map,
      opacity: 1,
      roughness: 0.48,
      metalness: 0.02,
    },
  };
  makeUnlitAlbedo({ traverse: (fn) => fn(mesh) });
  assert.equal(mesh.material.isMeshStandardMaterial, true);
  assert.equal(mesh.material.map, map);
  assert.equal(mesh.material.roughness, 0.48);
});

test("classifySurfaceKind separates floor ceiling wood and fabric", () => {
  assert.equal(classifySurfaceKind("SM_Floor_Main"), "floor");
  assert.equal(classifySurfaceKind("SM_Ceiling_Office"), "ceiling");
  assert.equal(classifySurfaceKind("PDF_07_会议桌_大会议室 浅色木纹"), "wood");
  assert.equal(classifySurfaceKind("PDF_13_会议椅 座垫"), "fabric");
});
