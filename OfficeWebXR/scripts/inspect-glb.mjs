import fs from "node:fs";

const path = process.argv[2] || "F:/Office_VR/OfficeWebXR/data/architecture.glb";
const b = fs.readFileSync(path);
const jsonLen = b.readUInt32LE(12);
const json = JSON.parse(b.slice(20, 20 + jsonLen).toString());

const nodes = json.nodes || [];
const meshes = json.meshes || [];
const accessors = json.accessors || [];
const materials = json.materials || [];

function accessorRange(index) {
  const acc = accessors[index];
  if (!acc || !acc.min || !acc.max) return null;
  return { min: acc.min, max: acc.max };
}

const meshBounds = [];
for (const mesh of meshes) {
  for (const prim of mesh.primitives || []) {
    const range = accessorRange(prim.attributes?.POSITION);
    if (range) meshBounds.push({ name: mesh.name, ...range });
  }
}

function unionBounds(list) {
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (const item of list) {
    for (let i = 0; i < 3; i++) {
      min[i] = Math.min(min[i], item.min[i]);
      max[i] = Math.max(max[i], item.max[i]);
    }
  }
  return { min, max, size: max.map((v, i) => v - min[i]) };
}

const world = unionBounds(meshBounds);
const translations = nodes
  .filter((n) => n.translation)
  .slice(0, 20)
  .map((n) => ({ name: n.name, t: n.translation, r: n.rotation }));

const names = nodes.map((n) => n.name).filter(Boolean);
const sampleNames = names.filter((n) => /ZeroBay|Floor|Wall|椅|桌|sofa|SM_|Player/i.test(n)).slice(0, 40);

const colors = materials.map((m) => m.pbrMetallicRoughness?.baseColorFactor || [1, 1, 1, 1]);
function isGrey(c) {
  const [r, g, b] = c;
  const avg = (r + g + b) / 3;
  return Math.abs(r - avg) < 0.04 && Math.abs(g - avg) < 0.04 && Math.abs(b - avg) < 0.04;
}
function isBlueish(c) {
  return c[2] > c[0] + 0.08 && c[2] > c[1];
}

const hasColor0 = (prim) => prim.attributes && prim.attributes.COLOR_0 != null;
let prims = 0;
let colorPrims = 0;
for (const mesh of meshes) {
  for (const prim of mesh.primitives || []) {
    prims += 1;
    if (hasColor0(prim)) colorPrims += 1;
  }
}

console.log(JSON.stringify({
  generator: json.asset?.generator,
  extras: json.asset?.extras,
  nodeCount: nodes.length,
  meshCount: meshes.length,
  materialCount: materials.length,
  imageCount: (json.images || []).length,
  worldBoundsCmGuess: world,
  sampleTranslations: translations,
  sampleNames,
  greyMaterials: colors.filter(isGrey).length,
  blueMaterials: colors.filter(isBlueish).length,
  texturedMaterials: materials.filter((m) => m.pbrMetallicRoughness?.baseColorTexture).length,
  vertexColorPrims: `${colorPrims}/${prims}`,
  firstNodes: nodes.slice(0, 8).map((n) => ({ name: n.name, t: n.translation, r: n.rotation, s: n.scale, mesh: n.mesh })),
}, null, 2));
