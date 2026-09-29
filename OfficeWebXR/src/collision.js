import { isValidFloorNormal } from "./locomotion.js";

export function classifyCollisionKind(name) {
  const hay = String(name || "");
  if (!hay.trim() || /zerobay|skyatmosphere|landscape/i.test(hay)) {
    return null;
  }
  const isFloor = /(floor|ceiling|roof|cove|skirting)/i.test(hay)
    && !/(wall|glass|door)/i.test(hay);
  if (isFloor) {
    return "floor";
  }
  if (/(wall|column|glass|door|frame|facade|partition|perimeter|core)/i.test(hay)) {
    return "wall";
  }
  return null;
}

export function collectCollisionMeshes(root) {
  const meshes = [];
  root.traverse?.((node) => {
    if (node.isMesh && node.visible !== false && node.userData?.collision) {
      meshes.push(node);
    }
  });
  return meshes;
}

export function collectWallMeshes(meshes) {
  return (meshes || []).filter((mesh) => mesh.userData?.wall);
}

export function collectInspectableMeshes(root) {
  const meshes = [];
  root.traverse?.((node) => {
    if (!node.isMesh || node.visible === false) {
      return;
    }
    let current = node;
    while (current) {
      if (current.userData?.inspectable && current.userData?.item) {
        meshes.push(node);
        return;
      }
      current = current.parent;
    }
  });
  return meshes;
}

export function pickFirst(raycaster, origin, direction, objects, maxDistance) {
  if (!objects.length) {
    return null;
  }
  raycaster.far = maxDistance;
  raycaster.set(origin, direction);
  const hits = raycaster.intersectObjects(objects, true);
  return hits[0] || null;
}

export function pickTeleport(raycaster, origin, direction, collisionMeshes, maxDistance, THREE) {
  const hit = pickFirst(raycaster, origin, direction, collisionMeshes, maxDistance);
  if (!hit) {
    return { valid: false, point: null, distance: maxDistance };
  }
  const normal = hit.face?.normal
    ? hit.face.normal.clone().transformDirection(hit.object.matrixWorld)
    : new THREE.Vector3(0, 1, 0);
  const up = normal.y;
  if (!isValidFloorNormal(up) || hit.object.userData.wall) {
    return { valid: false, point: hit.point, distance: hit.distance };
  }
  return { valid: true, point: hit.point, distance: hit.distance, normal };
}

export function snapToFloor(raycaster, position, collisionMeshes, THREE, probeHeight = 0.8, maxDrop = 4) {
  const origin = new THREE.Vector3(position.x, position.y + probeHeight, position.z);
  const down = new THREE.Vector3(0, -1, 0);
  const hit = pickFirst(raycaster, origin, down, collisionMeshes, probeHeight + maxDrop);
  if (!hit) {
    return false;
  }
  const normal = hit.face?.normal
    ? hit.face.normal.clone().transformDirection(hit.object.matrixWorld)
    : new THREE.Vector3(0, 1, 0);
  if (!isValidFloorNormal(normal.y) || hit.object.userData.wall) {
    return false;
  }
  position.y = hit.point.y;
  return true;
}

export function resolveHorizontalMove(raycaster, position, dx, dz, collisionMeshes, radius, THREE) {
  const walls = collectWallMeshes(collisionMeshes);
  const next = { x: position.x, y: position.y, z: position.z };
  const height = (position.y || 0) + 0.9;
  const origin = new THREE.Vector3(position.x, height, position.z);

  function blocked(fromX, fromZ, dirX, dirZ, travel) {
    if (travel <= 1e-6 || !walls.length) {
      return false;
    }
    const length = Math.hypot(dirX, dirZ) || 1;
    const direction = new THREE.Vector3(dirX / length, 0, dirZ / length);
    origin.set(fromX, height, fromZ);
    const hit = pickFirst(raycaster, origin, direction, walls, travel + radius);
    return !!(hit && hit.distance < travel + radius);
  }

  const dist = Math.hypot(dx, dz);
  if (dist <= 1e-6) {
    return next;
  }
  if (!blocked(position.x, position.z, dx, dz, dist)) {
    next.x += dx;
    next.z += dz;
    return next;
  }
  if (!blocked(position.x, position.z, dx, 0, Math.abs(dx))) {
    next.x += dx;
  }
  if (!blocked(position.x, position.z, 0, dz, Math.abs(dz))) {
    next.z += dz;
  }
  return next;
}
