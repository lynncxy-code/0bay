export function createLaser(THREE) {
  const geometry = new THREE.CylinderGeometry(0.005, 0.005, 1, 8);
  geometry.translate(0, 0.5, 0);
  geometry.rotateX(-Math.PI / 2);
  const material = new THREE.MeshBasicMaterial({
    color: 0x6ec8ff,
    depthTest: false,
    transparent: true,
    opacity: 0.9,
  });
  const mesh = new THREE.Mesh(geometry, material);
  mesh.frustumCulled = false;
  mesh.renderOrder = 20;
  mesh.name = "OfficeLaser";
  return mesh;
}

export function updateLaser(mesh, origin, direction, distance, hovering, THREE) {
  mesh.position.copy(origin);
  mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, -1), direction);
  mesh.scale.set(1, 1, Math.max(0.05, distance));
  mesh.material.color.setHex(hovering ? 0x66ff88 : 0x6ec8ff);
}

export function createTeleportMarker(THREE) {
  const geometry = new THREE.CylinderGeometry(0.18, 0.18, 0.03, 24);
  const material = new THREE.MeshBasicMaterial({
    color: 0x7cffb0,
    transparent: true,
    opacity: 0.7,
    depthTest: false,
  });
  const mesh = new THREE.Mesh(geometry, material);
  mesh.visible = false;
  mesh.name = "TeleportMarker";
  return mesh;
}
