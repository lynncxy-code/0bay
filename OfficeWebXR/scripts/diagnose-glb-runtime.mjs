import fs from "node:fs";

const b = fs.readFileSync("F:/Office_VR/OfficeWebXR/data/architecture.glb");
const jsonLen = b.readUInt32LE(12);
const json = JSON.parse(b.slice(20, 20 + jsonLen).toString());
const nodes = json.nodes || [];
const childToParent = new Map();
nodes.forEach((node, index) => {
  for (const child of node.children || []) {
    childToParent.set(child, index);
  }
});

function parentName(index) {
  const parent = childToParent.get(index);
  return parent == null ? "" : nodes[parent]?.name || "";
}

const ARCH = [
  "SM_Wall", "SM_Floor", "SM_Ceiling", "SM_Column", "SM_Facade", "SM_Roof",
  "SM_Cove", "SM_Skirting", "SM_Core", "SM_Door", "SM_Interior", "SM_Exterior",
  "SM_Partition", "SM_Feature", "Sky", "Landscape", "BP_OfficeShell",
  "WorldDataLayers", "Brush", "Light", "Camera", "PlayerStart", "SkyAtmosphere",
];
function isArch(name) {
  return ARCH.some((token) => name.includes(token));
}

let collision = 0;
let walls = 0;
let floors = 0;
let meshes = 0;
const samples = [];
const wallNames = [];
nodes.forEach((node, index) => {
  if (node.mesh == null) {
    return;
  }
  meshes += 1;
  const name = `${node.name || ""} ${parentName(index)}`;
  const arch = isArch(name);
  if (arch) {
    collision += 1;
    if (name.includes("Wall") || name.includes("Column")) {
      walls += 1;
      if (wallNames.length < 20) wallNames.push(name);
    } else {
      floors += 1;
    }
  }
  if (samples.length < 20) {
    samples.push({ name: node.name, parent: parentName(index), arch });
  }
});

const bindings = JSON.parse(
  fs.readFileSync("F:/Office_VR/OfficeWebXR/data/inspect-bindings.json", "utf8"),
).bindings;
const nodeNames = nodes.map((node) => node.name).filter(Boolean);
let bound = 0;
const missing = [];
for (const entry of bindings) {
  const hit = nodeNames.some((name) => name === entry.nodeName || name.includes(entry.nodeName));
  if (hit) bound += 1;
  else if (missing.length < 15) missing.push(entry.nodeName);
}

const glassMats = (json.materials || []).filter((material) => {
  const name = material.name || "";
  return /glass|Glass|透明|窗/i.test(name)
    || material.alphaMode === "BLEND"
    || material.alphaMode === "MASK"
    || material.extensions?.KHR_materials_transmission;
});

console.log(JSON.stringify({
  meshes,
  collision,
  walls,
  floors,
  bound,
  bindings: bindings.length,
  missing,
  glass: glassMats.slice(0, 12).map((material) => ({
    name: material.name,
    alpha: material.alphaMode,
    color: material.pbrMetallicRoughness?.baseColorFactor,
    trans: !!material.extensions?.KHR_materials_transmission,
  })),
  samples,
  wallNames,
}, null, 2));
