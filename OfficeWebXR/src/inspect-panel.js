import { toPanelText } from "./catalog.js";

export function createInspectPanel(THREE) {
  const canvas = document.createElement("canvas");
  canvas.width = 512;
  canvas.height = 768;
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  const material = new THREE.MeshBasicMaterial({
    map: texture,
    transparent: true,
    depthTest: false,
  });
  const mesh = new THREE.Mesh(new THREE.PlaneGeometry(0.48, 0.72), material);
  mesh.position.set(0.18, -0.08, -0.9);
  mesh.visible = false;
  mesh.name = "InspectPanel";
  mesh.frustumCulled = false;
  mesh.renderOrder = 30;
  return { mesh, canvas, texture, item: null };
}

export function showPanel(panel, item) {
  panel.item = item;
  const context = panel.canvas.getContext("2d");
  context.fillStyle = "rgba(8, 16, 28, 0.92)";
  context.fillRect(0, 0, panel.canvas.width, panel.canvas.height);
  context.strokeStyle = "rgba(180, 210, 255, 0.7)";
  context.lineWidth = 4;
  context.strokeRect(8, 8, panel.canvas.width - 16, panel.canvas.height - 16);
  context.fillStyle = "#e6f0ff";
  context.font = "28px Consolas, 'Courier New', monospace";
  const lines = toPanelText(item).split("\n");
  let y = 56;
  for (const line of lines) {
    context.fillText(line, 36, y, panel.canvas.width - 72);
    y += 34;
  }
  panel.texture.needsUpdate = true;
  panel.mesh.visible = true;
}

export function hidePanel(panel) {
  panel.item = null;
  panel.mesh.visible = false;
}
