export function pointerForwardFromMatrix(elements, out = { x: 0, y: 0, z: 0 }) {
  out.x = -(elements[8] || 0);
  out.y = -(elements[9] || 0);
  out.z = -(elements[10] || 0);
  const length = Math.hypot(out.x, out.y, out.z) || 1;
  out.x /= length;
  out.y /= length;
  out.z /= length;
  return out;
}

export function pointerAim(object, origin, direction) {
  object.updateWorldMatrix?.(true, false);
  object.getWorldPosition(origin);
  pointerForwardFromMatrix(object.matrixWorld.elements, direction);
  return { origin, direction };
}

export function createPicoControllerBody(THREE) {
  const root = new THREE.Group();
  const handle = new THREE.Mesh(
    new THREE.CylinderGeometry(0.017, 0.02, 0.1, 12),
    new THREE.MeshBasicMaterial({ color: 0x2c3036 }),
  );
  handle.rotation.x = Math.PI / 2;
  handle.position.z = 0.035;
  const head = new THREE.Mesh(
    new THREE.BoxGeometry(0.048, 0.028, 0.07),
    new THREE.MeshBasicMaterial({ color: 0x1a1d22 }),
  );
  head.position.z = -0.012;
  root.add(handle);
  root.add(head);
  return root;
}

export function createPicoPointer(THREE, color = 0xffffff) {
  const root = new THREE.Group();
  const beamGeom = new THREE.CylinderGeometry(0.0011, 0.0011, 1, 6);
  beamGeom.rotateX(-Math.PI / 2);
  beamGeom.translate(0, 0, -0.5);
  const beam = new THREE.Mesh(
    beamGeom,
    new THREE.MeshBasicMaterial({
      color,
      depthTest: false,
      transparent: true,
      opacity: 0.7,
    }),
  );
  beam.name = "beam";
  beam.frustumCulled = false;
  beam.renderOrder = 30;
  const cursor = new THREE.Mesh(
    new THREE.RingGeometry(0.01, 0.016, 24),
    new THREE.MeshBasicMaterial({
      color,
      depthTest: false,
      side: THREE.DoubleSide,
      transparent: true,
      opacity: 0.95,
    }),
  );
  cursor.name = "cursor";
  cursor.frustumCulled = false;
  cursor.renderOrder = 31;
  root.add(beam);
  root.add(cursor);
  root.userData.beam = beam;
  root.userData.cursor = cursor;
  return root;
}

export function createControllerVisual(THREE, color) {
  return createPicoPointer(THREE, color);
}

export function setPicoRay(visual, distance, hovering, hasHit) {
  const beam = visual?.userData?.beam;
  const cursor = visual?.userData?.cursor;
  if (!beam) {
    return;
  }
  const len = Math.max(0.08, distance);
  beam.scale.set(1, 1, len);
  const color = hovering ? 0x66ff88 : 0xffffff;
  beam.material.color.setHex(color);
  beam.material.opacity = hovering ? 0.95 : 0.55;
  if (cursor) {
    cursor.visible = !!hasHit;
    cursor.position.set(0, 0, -Math.max(0.04, distance));
    cursor.material.color.setHex(color);
  }
}

export function setBeamLength(visual, distance, hovering) {
  setPicoRay(visual, distance, hovering, distance < 8);
}

export function attachXrControllers(renderer, parent, THREE, onSelect) {
  const attached = [];
  for (let index = 0; index < 2; index += 1) {
    const controller = renderer.xr.getController(index);
    const grip = renderer.xr.getControllerGrip(index);
    const visual = createPicoPointer(THREE, 0xffffff);
    const body = createPicoControllerBody(THREE);
    controller.add(visual);
    grip.add(body);
    parent.add(controller);
    parent.add(grip);
    controller.addEventListener("connected", (event) => {
      controller.userData.inputSource = event.data;
      grip.userData.inputSource = event.data;
    });
    controller.addEventListener("disconnected", () => {
      controller.userData.inputSource = null;
      grip.userData.inputSource = null;
    });
    controller.addEventListener("selectstart", () => {
      onSelect(controller);
    });
    attached.push({ controller, grip, visual });
  }
  return attached;
}
