export function createDebugHud(THREE) {
  const canvas = document.createElement("canvas");
  canvas.width = 768;
  canvas.height = 256;
  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  const mesh = new THREE.Mesh(
    new THREE.PlaneGeometry(1.2, 0.4),
    new THREE.MeshBasicMaterial({ map: texture, transparent: true, depthTest: false }),
  );
  mesh.position.set(0, 1.45, -1.8);
  mesh.name = "XrDebugHud";
  mesh.frustumCulled = false;
  return { mesh, canvas, texture };
}

export function updateDebugHud(hud, lines) {
  const context = hud.canvas.getContext("2d");
  context.fillStyle = "rgba(8, 12, 20, 0.88)";
  context.fillRect(0, 0, hud.canvas.width, hud.canvas.height);
  context.fillStyle = "#d7ecff";
  context.font = "28px Consolas, monospace";
  let y = 48;
  for (const line of lines) {
    context.fillText(line, 24, y, hud.canvas.width - 48);
    y += 40;
  }
  hud.texture.needsUpdate = true;
}
